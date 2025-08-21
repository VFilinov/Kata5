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
#include "../core/datetime.h"

#include <csignal>
#include <optional>

using namespace std;
using nlohmann::json;

struct MatchResultOneBot {
  int win;
  int lose;
  int draw;
  int win_b;
  int lose_b;
  int draw_b;
  double timeUsed;
  uint64_t moves;
  int count;

  MatchResultOneBot() {
    win = 0;
    lose = 0;
    draw = 0;
    win_b = 0;
    lose_b = 0;
    draw_b = 0;
    timeUsed = 0;
    moves = 0;
    count = 0;
  }
};

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
  Rand seedRand;

  ConfigParser cfg;
  string logFile;
  string sgfOutputDir;
  string resultOutputDir;
  string openingsFile;
  auto start = std::chrono::steady_clock::now();

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
    vector<bool> includeBot(numBots, true);
    if(cfg.contains("includeBots")) {
      std::fill(includeBot.begin(), includeBot.end(), false);
      vector<int> includeBotIdxs = cfg.getInts("includeBots",0,Setup::MAX_BOT_PARAMS_FROM_CFG);
      for(int idx : includeBotIdxs)
        if(idx >= 0 && idx < numBots)
          includeBot[idx] = true;
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
          matchupsPerRound.emplace_back(i, j);
          matchupsPerRound.emplace_back(j, i);
        }
      }
    }

    if(cfg.contains("extraPairs")) {
      std::vector<std::pair<int, int>> pairs = cfg.getNonNegativeIntDashedPairs("extraPairs", 0, numBots - 1);
      for(const std::pair<int, int>& pair: pairs) {
        int p0 = pair.first;
        int p1 = pair.second;
        if(cfg.contains("extraPairsAreOneSidedBW") && cfg.getBool("extraPairsAreOneSidedBW")) {
          matchupsPerRound.emplace_back(p0, p1);
        }
        else {
          matchupsPerRound.emplace_back(p0, p1);
          matchupsPerRound.emplace_back(p1, p0);
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
  const bool logOpenings = cfg.contains("logOpenings") ? cfg.getBool("logOpenings") : false;
  const int maxTryTimes = cfg.contains("maxTryTimes") ? cfg.getInt("maxTryTimes", 1, 1000) : 20;
  const int logThreadsEvery = cfg.contains("logThreadsEvery") ? cfg.getInt("logThreadsEvery", 1, 10000) : 20;
  const int64_t logGamesEvery = cfg.getInt64("logGamesEvery", 1, 1000000);

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
  playSettings.logOpenings = logOpenings;
  playSettings.maxTryTimes = maxTryTimes;

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
  std::map<string, MatchResultOneBot> resultsByBotMap;

  Rand hashRand;
  std::optional<std::ofstream> sgfOut;
  if(!sgfOutputDir.empty()) {
    sgfOut.emplace();
    FileUtils::open(*sgfOut, sgfOutputDir + "/" + Global::uint64ToHexString(hashRand.nextUInt64()) + ".sgfs");
  }

  auto runMatchLoop = [
    &gameRunner,&matchPairer,&sgfOut,&logger,&gameSeedBase,&patternBonusTables,
    &statsMutex, &gameCount, &resultsByBotMap, &logGamesEvery, &logThreadsEvery](int threadIdx)
  {
    auto shouldStopFunc = []() noexcept {
      return shouldStop.load();
    };
    WaitableFlag* shouldPause = nullptr;

    //Rand thisLoopSeedRand;

    if(threadIdx % logThreadsEvery == 0)
      logger.write("Match loop thread " + Global::intToString(threadIdx) + " starting");

    while(true) {
      if(shouldStop.load())
        break;

      FinishedGameData* gameData = nullptr;
      InitialPosition* initialPosition = nullptr;

      MatchPairer::BotSpec botSpecB;
      MatchPairer::BotSpec botSpecW;

      //string seed = gameSeedBase + ":" + Global::uint64ToHexString(thisLoopSeedRand.nextUInt64());
      string seed = gameSeedBase;
      if(matchPairer->getMatchup(botSpecB, botSpecW, logger, seed, &initialPosition)) {
        std::function<void(const MatchPairer::BotSpec&, Search*)> afterInitialization = [&patternBonusTables](const MatchPairer::BotSpec& spec, Search* search) {
          assert(spec.botIdx < patternBonusTables.size());
          search->setCopyOfExternalPatternBonusTable(patternBonusTables[spec.botIdx]);
        };
        gameData = gameRunner->runGame(
            seed, botSpecB, botSpecW, NULL, logger, shouldStopFunc, shouldPause, nullptr, afterInitialization, nullptr, initialPosition);
      }

      bool shouldContinue = gameData;
      if(gameData) {
        if(sgfOut) {
          std::lock_guard<std::mutex> lock(statsMutex);
          WriteSgf::writeSgf(*sgfOut, gameData->bName, gameData->wName, gameData->endHist, gameData, false, true);
          (*sgfOut) << endl;
        }

        {
          std::lock_guard<std::mutex> lock(statsMutex);
          gameCount += 1;

          resultsByBotMap.try_emplace(gameData->bName, MatchResultOneBot());
          resultsByBotMap.try_emplace(gameData->wName, MatchResultOneBot());

          auto& br = resultsByBotMap[gameData->bName];
          auto& wr = resultsByBotMap[gameData->wName];

          br.count += 1;
          wr.count += 1;
          br.timeUsed += gameData->bTimeUsed;
          wr.timeUsed += gameData->wTimeUsed;
          br.moves += gameData->bMoveCount;
          wr.moves += gameData->wMoveCount;

          Color winner = gameData->endHist.winner;

          if(winner == C_BLACK) {
            br.win += 1;
            br.win_b += 1;
            wr.lose += 1;
          } else if(winner == C_WHITE) {
            br.lose += 1;
            br.lose_b += 1;
            wr.win += 1;
          } else {
            br.draw += 1;
            br.draw_b += 1;
            wr.draw += 1;
          }

          if(gameCount % logGamesEvery == 0) {
            for(auto& [name, r] : resultsByBotMap) {
              double avgtime = r.timeUsed;
              if(r.moves != 0)
                avgtime /= r.moves;
              avgtime = round(avgtime * 1000.0) / 1000.0;
              logger.write(
                Global::strprintf("%s played %d games (wins %d, draws %d, moves %d, avg time %.3f)", name, r.count, r.win, r.draw, r.moves, avgtime));
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
    if(threadIdx % logThreadsEvery == 0)
      logger.write("Match loop thread " + Global::intToString(threadIdx) + " terminating");
  };
  auto runMatchLoopProtected = [&logger, &runMatchLoop](int threadIdx) {
    Logger::logThreadUncaught("match loop", &logger, [&]() { runMatchLoop(threadIdx); });
  };

  // Rand hashRand;
  vector<std::thread> threads;
  for(int i = 0; i<numGameThreads; i++) {
    threads.push_back(std::thread(runMatchLoopProtected, i ));
  }
  for(auto& t : threads)
    t.join();

  delete matchPairer;
  delete gameRunner;

  if(sgfOut)
    sgfOut->close();

  if(numBots == 2) {
    std::lock_guard<std::mutex> lock(statsMutex);
    resultsByBotMap.try_emplace(botNames[0], MatchResultOneBot());
    resultsByBotMap.try_emplace(botNames[1], MatchResultOneBot());
    auto& r0 = resultsByBotMap[botNames[0]];
    auto& r1 = resultsByBotMap[botNames[1]];

    json j;
    j["bot0name"] = botNames[0];
    j["bot1name"] = botNames[1];
    j["bot0model"] = nnModelFilesByBot[0];
    j["bot1model"] = nnModelFilesByBot[1];
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

  if(sigReceived.load())
    logger.write("Exited cleanly after signal");

  auto finish = std::chrono::steady_clock::now();
  auto time = std::chrono::duration_cast<std::chrono::seconds>(finish - start);
  auto time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(finish - start);

  auto avgtime = time_ms.count();

  uint64_t cnt_moves = 0;
  for(auto& pair: resultsByBotMap)
    cnt_moves += pair.second.moves;

  if(cnt_moves != 0)
    avgtime /= cnt_moves;
  avgtime = round(avgtime * 1000.0) / 1000.0;

  ostringstream out;
  out << "All cleaned up, quitting. Moves: " << cnt_moves << ", time : " << time.count()
      << " sec., average time : " << avgtime << " ms/move";
  logger.write(out.str());

  return 0;
}
