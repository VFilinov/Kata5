#include "../core/global.h"
#include "../core/fileutils.h"
#include "../core/makedir.h"
#include "../core/config_parser.h"
#include "../core/timer.h"
#include "../dataio/sgf.h"
#include "../search/asyncbot.h"
#include "../search/patternbonustable.h"
#include "../program/setup.h"
#include "../program/play.h"
#include "../command/commandline.h"
#include "../main.h"
#include "../external/nlohmann_json/json.hpp"

#include <csignal>

using namespace std;
using nlohmann::json;

struct MatchResultOneBot {
  int win;
  int lose;
  int draw;
  int win_b;
  int lose_b;
  int draw_b;
  MatchResultOneBot() {
    win = 0;
    lose = 0;
    draw = 0;
    win_b = 0;
    lose_b = 0;
    draw_b = 0;
  }
};

std::string getCurrentTimeString() {
  auto now = std::chrono::system_clock::now();

  std::time_t now_time_t = std::chrono::system_clock::to_time_t(now);

  std::tm now_tm;
#if defined(_MSC_VER)  // MSVC (Visual Studio)
  localtime_s(&now_tm, &now_time_t);
#else  // GCC/Clang
  localtime_r(&now_time_t, &now_tm);
#endif

  auto duration = now.time_since_epoch();
  auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(duration) % 1000;

  std::ostringstream oss;
  oss << std::put_time(&now_tm, "%Y-%m-%d-%H-%M-%S");
  oss << '-' << std::setfill('0') << std::setw(3) << millis.count();
  return oss.str();
}


static std::atomic<bool> sigReceived(false);
static std::atomic<bool> shouldStop(false);
static void signalHandler(int signal)
{
  if(signal == SIGINT || signal == SIGTERM) {
    sigReceived.store(true);
    shouldStop.store(true);
  }
}

int MainCmds::match(const vector<string>& args) {
  Board::initHash();
  //ScoreValue::initTables();
  Rand seedRand;

  ConfigParser cfg;
  string logFile;
  string sgfOutputDir;
  string resultOutputDir;
  string openingsFile;
  auto start = std::chrono::steady_clock::now();
  uint64_t cnt_moves = 0;

  try {
    KataGoCommandLine cmd("Play different nets against each other with different search settings in a match or tournament.");
    cmd.addConfigFileArg("","match_example.cfg");

    TCLAP::ValueArg<string> logFileArg("","log-file","Log file to output to",false,string(),"FILE");
    TCLAP::ValueArg<string> sgfOutputDirArg("","sgf-output-dir","Dir to output sgf files",false,string(),"DIR");
    TCLAP::ValueArg<string> openingsFileArg("", "openings", "File with description of openings files", false, string(), "FILE");
    TCLAP::ValueArg<string> resultOutputDirArg("", "result-output-dir", "Dir to output json result files", false, string(), "DIR");

    cmd.add(logFileArg);
    cmd.add(sgfOutputDirArg);
    cmd.add(openingsFileArg);
    cmd.add(resultOutputDirArg);

    cmd.setShortUsageArgLimit();
    cmd.addOverrideConfigArg();

    cmd.parseArgs(args);

    logFile = logFileArg.getValue();
    sgfOutputDir = sgfOutputDirArg.getValue();
    openingsFile = openingsFileArg.getValue();
    resultOutputDir = resultOutputDirArg.getValue();

    cmd.getConfig(cfg);
  }
  catch (TCLAP::ArgException &e) {
    cerr << "Error: " << e.error() << " for argument " << e.argId() << endl;
    return 1;
  }

  Logger logger(&cfg);
  logger.addFile(logFile);

  logger.write("Match Engine starting...");
#ifndef NO_GIT_REVISION
  logger.write(string("Git revision: ") + Version::getGitRevision());
#endif
  //Load per-bot search config, first, which also tells us how many bots we're running
  vector<SearchParams> paramss = Setup::loadParams(cfg,Setup::SETUP_FOR_MATCH);
  assert(paramss.size() > 0);
  int numBots = (int)paramss.size();

  //Figure out all pairs of bots that will be playing.
  std::vector<std::pair<int,int>> matchupsPerRound;
  {
    //Load a filter on what bots we actually want to run. By default, include everything.
    vector<bool> includeBot(numBots);
    if(cfg.contains("includeBots")) {
      vector<int> includeBotIdxs = cfg.getInts("includeBots",0,Setup::MAX_BOT_PARAMS_FROM_CFG);
      for(int i = 0; i<numBots; i++) {
        if(contains(includeBotIdxs,i))
          includeBot[i] = true;
      }
    }
    else {
      for(int i = 0; i<numBots; i++) {
        includeBot[i] = true;
      }
    }

    std::vector<int> secondaryBotIdxs;
    if(cfg.contains("secondaryBots"))
      secondaryBotIdxs = cfg.getInts("secondaryBots",0,Setup::MAX_BOT_PARAMS_FROM_CFG);
    for(int i = 0; i<secondaryBotIdxs.size(); i++)
      assert(secondaryBotIdxs[i] >= 0 && secondaryBotIdxs[i] < numBots);

    for(int i = 0; i<numBots; i++) {
      if(!includeBot[i])
        continue;
      for(int j = 0; j<numBots; j++) {
        if(!includeBot[j])
          continue;
        if(i < j && !(contains(secondaryBotIdxs,i) && contains(secondaryBotIdxs,j))) {
          matchupsPerRound.push_back(make_pair(i,j));
          matchupsPerRound.push_back(make_pair(j,i));
        }
      }
    }

    if(cfg.contains("extraPairs")) {
      std::vector<std::pair<int, int>> pairs = cfg.getNonNegativeIntDashedPairs("extraPairs", 0, numBots - 1);
      for(const std::pair<int, int>& pair: pairs) {
        int p0 = pair.first;
        int p1 = pair.second;
        if(cfg.contains("extraPairsAreOneSidedBW") && cfg.getBool("extraPairsAreOneSidedBW")) {
          matchupsPerRound.push_back(std::make_pair(p0,p1));
        }
        else {
          matchupsPerRound.push_back(std::make_pair(p0,p1));
          matchupsPerRound.push_back(std::make_pair(p1,p0));
        }
      }
    }
  }

  //Load the names of the bots and which model each bot is using
  vector<string> nnModelFilesByBot(numBots);
  vector<string> botNames(numBots);
  for(int i = 0; i<numBots; i++) {
    string idxStr = Global::intToString(i);

    if(cfg.contains("botName"+idxStr))
      botNames[i] = cfg.getString("botName"+idxStr);
    else if(numBots == 1)
      botNames[i] = cfg.getString("botName");
    else
      throw StringError("If more than one bot, must specify botName0, botName1,... individually");

    if(cfg.contains("nnModelFile"+idxStr))
      nnModelFilesByBot[i] = cfg.getString("nnModelFile"+idxStr);
    else
      nnModelFilesByBot[i] = cfg.getString("nnModelFile");
  }

  vector<bool> botIsUsed(numBots);
  for(const std::pair<int, int>& pair: matchupsPerRound) {
    botIsUsed[pair.first] = true;
    botIsUsed[pair.second] = true;
  }

  //Dedup and load each necessary model exactly once
  vector<string> nnModelFiles;
  vector<int> whichNNModel(numBots);
  for(int i = 0; i<numBots; i++) {
    if(!botIsUsed[i])
      continue;

    const string& desiredFile = nnModelFilesByBot[i];
    int alreadyFoundIdx = -1;
    for(int j = 0; j<nnModelFiles.size(); j++) {
      if(nnModelFiles[j] == desiredFile) {
        alreadyFoundIdx = j;
        break;
      }
    }
    if(alreadyFoundIdx != -1)
      whichNNModel[i] = alreadyFoundIdx;
    else {
      whichNNModel[i] = (int)nnModelFiles.size();
      nnModelFiles.push_back(desiredFile);
    }
  }

  //Load match runner settings
  int numGameThreads = cfg.getInt("numGameThreads",1,16384);
  const string gameSeedBase = Global::uint64ToHexString(seedRand.nextUInt64());

  //Work out an upper bound on how many concurrent nneval requests we could end up making.
  int expectedConcurrentEvals;
  {
    //Work out the max threads any one bot uses
    int maxBotThreads = 0;
    for(int i = 0; i<numBots; i++)
      if(paramss[i].numThreads > maxBotThreads)
        maxBotThreads = paramss[i].numThreads;
    //Mutiply by the number of concurrent games we could have
    expectedConcurrentEvals = maxBotThreads * numGameThreads;
  }

  //Initialize object for randomizing game settings and running games
  PlaySettings playSettings = PlaySettings::loadForMatch(cfg);
  playSettings.fileOpenings = openingsFile;

  GameRunner* gameRunner = new GameRunner(cfg, playSettings, logger);
  const int minBoardXSizeUsed = gameRunner->getGameInitializer()->getMinBoardXSize();
  const int minBoardYSizeUsed = gameRunner->getGameInitializer()->getMinBoardYSize();
  const int maxBoardXSizeUsed = gameRunner->getGameInitializer()->getMaxBoardXSize();
  const int maxBoardYSizeUsed = gameRunner->getGameInitializer()->getMaxBoardYSize();

  //Initialize neural net inference engine globals, and load models
  Setup::initializeSession(cfg);
  const vector<string>& nnModelNames = nnModelFiles;
  const int defaultMaxBatchSize = -1;
  const bool defaultRequireExactNNLen = minBoardXSizeUsed == maxBoardXSizeUsed && minBoardYSizeUsed == maxBoardYSizeUsed;
  const bool disableFP16 = false;
  const vector<string> expectedSha256s;
  vector<NNEvaluator*> nnEvals = Setup::initializeNNEvaluators(
    nnModelNames,nnModelFiles,expectedSha256s,cfg,logger,seedRand,expectedConcurrentEvals,
    maxBoardXSizeUsed,maxBoardYSizeUsed,defaultMaxBatchSize,defaultRequireExactNNLen,disableFP16,
    Setup::SETUP_FOR_MATCH
  );
  logger.write("Loaded neural net");

  vector<NNEvaluator*> nnEvalsByBot(numBots);
  for(int i = 0; i<numBots; i++) {
    if(!botIsUsed[i])
      continue;
    nnEvalsByBot[i] = nnEvals[whichNNModel[i]];
  }

  std::vector<std::unique_ptr<PatternBonusTable>> patternBonusTables = Setup::loadAvoidSgfPatternBonusTables(cfg,logger);
  assert(patternBonusTables.size() == numBots);

  //Initialize object for randomly pairing bots
  int64_t numGamesTotal = cfg.getInt64("numGamesTotal",1,((int64_t)1) << 62);
  MatchPairer* matchPairer = new MatchPairer(cfg,numBots,botNames,nnEvalsByBot,paramss,matchupsPerRound,numGamesTotal,gameRunner);

  //Check for unused config keys
  cfg.warnUnusedKeys(cerr,&logger);
  for(int i = 0; i<numBots; i++) {
    if(!botIsUsed[i])
      continue;
    Setup::maybeWarnHumanSLParams(paramss[i],nnEvalsByBot[i],NULL,cerr,&logger);
  }

  //Done loading!
  //------------------------------------------------------------------------------------
  logger.write("Loaded all config stuff, starting matches");
  if(!logger.isLoggingToStdout())
    cout << "Loaded all config stuff, starting matches" << endl;

  // string resultOutputDir = "./matchresult";
  if(sgfOutputDir != string()) {
    MakeDir::make(sgfOutputDir);
  }
  if(resultOutputDir == string()) 
      resultOutputDir = "./matchresult";
  MakeDir::make(resultOutputDir);

  if(!std::atomic_is_lock_free(&shouldStop))
    throw StringError("shouldStop is not lock free, signal-quitting mechanism for terminating matches will NOT work!");
  std::signal(SIGINT, signalHandler);
  std::signal(SIGTERM, signalHandler);


  std::mutex statsMutex;
  int64_t gameCount = 0;
  std::map<string,double> timeUsedByBotMap;
  std::map<string,double> movesByBotMap;
  std::map<string, MatchResultOneBot> resultsByBotMap;

  Rand hashRand;
  ofstream* sgfOut = NULL;
  if(sgfOutputDir.length() > 0) {
    sgfOut = new ofstream();
    FileUtils::open(*sgfOut, sgfOutputDir + "/" + Global::uint64ToHexString(hashRand.nextUInt64()) + ".sgfs");
  }

  auto runMatchLoop = [
    &gameRunner,&matchPairer,&sgfOut/*&sgfOutputDir*/,&logger,&gameSeedBase,&patternBonusTables,
    &statsMutex, &gameCount, &timeUsedByBotMap, &movesByBotMap, &resultsByBotMap, &cnt_moves
  ](
    /* uint64_t threadHash*/
  ) {
    /*
    ofstream* sgfOut = NULL;
    if(sgfOutputDir.length() > 0) {
      sgfOut = new ofstream();
      FileUtils::open(*sgfOut, sgfOutputDir + "/" + Global::uint64ToHexString(threadHash) + ".sgfs");
    }
    */
    auto shouldStopFunc = []() {
      return shouldStop.load();
    };
    WaitableFlag* shouldPause = nullptr;

    Rand thisLoopSeedRand;

    while(true) {
      if(shouldStop.load())
        break;

      FinishedGameData* gameData = NULL;
      InitialPosition* initialPosition = NULL;

      MatchPairer::BotSpec botSpecB;
      MatchPairer::BotSpec botSpecW;

      string seed = gameSeedBase + ":" + Global::uint64ToHexString(thisLoopSeedRand.nextUInt64());
      if(matchPairer->getMatchup(botSpecB, botSpecW, logger, seed, &initialPosition)) {
        std::function<void(const MatchPairer::BotSpec&, Search*)> afterInitialization = [&patternBonusTables](const MatchPairer::BotSpec& spec, Search* search) {
          assert(spec.botIdx < patternBonusTables.size());
          search->setCopyOfExternalPatternBonusTable(patternBonusTables[spec.botIdx]);
        };
        gameData = gameRunner->runGame(
            seed, botSpecB, botSpecW, NULL, logger, shouldStopFunc, shouldPause, nullptr, afterInitialization, nullptr, initialPosition);
      }

      bool shouldContinue = gameData != NULL;
      if(gameData != NULL) {
        if(sgfOut != NULL) {
          std::lock_guard<std::mutex> lock(statsMutex);
          WriteSgf::writeSgf(*sgfOut, gameData->bName, gameData->wName, gameData->endHist, gameData, false, true);
          (*sgfOut) << endl;
        }

        {
          std::lock_guard<std::mutex> lock(statsMutex);
          gameCount += 1;
          timeUsedByBotMap[gameData->bName] += gameData->bTimeUsed;
          timeUsedByBotMap[gameData->wName] += gameData->wTimeUsed;
          movesByBotMap[gameData->bName] += (double)gameData->bMoveCount;
          movesByBotMap[gameData->wName] += (double)gameData->wMoveCount;
          cnt_moves += gameData->bMoveCount;
          cnt_moves += gameData->wMoveCount;

          if(!resultsByBotMap.count(gameData->bName))
            resultsByBotMap[gameData->bName] = MatchResultOneBot();
          if(!resultsByBotMap.count(gameData->wName))
            resultsByBotMap[gameData->wName] = MatchResultOneBot();

          Color winner = gameData->endHist.winner;
          auto& br = resultsByBotMap[gameData->bName];
          auto& wr = resultsByBotMap[gameData->wName];

          if(winner == C_EMPTY) {
            br.draw += 1;
            br.draw_b += 1;
            wr.draw += 1;
          } else if(winner == C_BLACK) {
            br.win += 1;
            br.win_b += 1;
            wr.lose += 1;
          } else if(winner == C_WHITE) {
            br.lose += 1;
            br.lose_b += 1;
            wr.win += 1;
          } else
            throw StringError("Unknown match game result");

          int64_t x = gameCount;
          while(x % 2 == 0 && x > 1) x /= 2;
          if(x == 1 || x == 3 || x == 5) {
            for(auto& pair : timeUsedByBotMap) {
              logger.write(
                "Avg move time used by " + pair.first + " " +
                Global::doubleToString(pair.second / movesByBotMap[pair.first]) + " " +
                Global::doubleToString(movesByBotMap[pair.first]) + " moves"
              );
            }
          }
        }

        delete gameData;
      }

      if(shouldStop.load())
        break;
      if(!shouldContinue)
        break;
    }
    /*if(sgfOut != NULL) {
      sgfOut->close();
      delete sgfOut;
    }*/
    logger.write("Match loop thread terminating");
  };
  auto runMatchLoopProtected = [&logger, &runMatchLoop](/* uint64_t threadHash*/) {
    Logger::logThreadUncaught("match loop", &logger, [&]() { runMatchLoop(/* threadHash*/); });
  };

  // Rand hashRand;
  vector<std::thread> threads;
  for(int i = 0; i<numGameThreads; i++) {
    threads.push_back(std::thread(runMatchLoopProtected /*, hashRand.nextUInt64()*/));
  }
  for(int i = 0; i<threads.size(); i++)
    threads[i].join();

  delete matchPairer;
  delete gameRunner;

  if(sgfOut != NULL) {
    sgfOut->close();
    delete sgfOut;
  }

  if(numBots == 2) {
    json j;
    j["bot0name"] = botNames[0];
    j["bot1name"] = botNames[1];
    j["bot0model"] = nnModelFiles[0];
    j["bot1model"] = nnModelFiles[1];
    auto& r0 = resultsByBotMap[botNames[0]];
    auto& r1 = resultsByBotMap[botNames[1]];
    if(r0.win != r1.lose || r0.draw != r1.draw || r0.lose != r1.win)
      throw StringError("result of two bots not match");
    j["total"] = r0.win + r0.draw + r0.lose;
    j["win0"] = r0.win;
    j["lose0"] = r0.lose;
    j["draw"] = r0.draw;
    j["win0_b"] = r0.win_b;
    j["draw0_b"] = r0.draw_b;
    j["lose0_b"] = r0.lose_b;
    // bot 1 can be calculated by this, so no need to store

    string jsonOutPath = resultOutputDir + "/" + getCurrentTimeString() + ".json";

    std::ofstream file(jsonOutPath);
    if(file.is_open()) {
      file << j.dump(4);
      file.close();
    } else {
      std::cerr << "failed to write json file:" << jsonOutPath << std::endl;
    }
  } 


  nnEvalsByBot.clear();
  for(int i = 0; i<nnEvals.size(); i++) {
    if(nnEvals[i] != NULL) {
      logger.write(nnEvals[i]->getModelFileName());
      logger.write("NN rows: " + Global::int64ToString(nnEvals[i]->numRowsProcessed()));
      logger.write("NN batches: " + Global::int64ToString(nnEvals[i]->numBatchesProcessed()));
      logger.write("NN avg batch size: " + Global::doubleToString(nnEvals[i]->averageProcessedBatchSize()));
      delete nnEvals[i];
    }
  }
  NeuralNet::globalCleanup();
  //ScoreValue::freeTables();

  if(sigReceived.load())
    logger.write("Exited cleanly after signal");

  auto finish = std::chrono::steady_clock::now();
  auto time = std::chrono::duration_cast<std::chrono::seconds>(finish - start);
  auto time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(finish - start);

  auto avgtime = time_ms.count();
  if(cnt_moves != 0)
    avgtime /= cnt_moves;

  ostringstream out;
  out << "All cleaned up, quitting. Moves: " << cnt_moves << ", time : " << time.count()
      << " sec., average time : " << avgtime << " ms/move";
  logger.write(out.str());

  // logger.write("All cleaned up, quitting");
  return 0;
}
