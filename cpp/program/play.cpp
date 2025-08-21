#include "../program/play.h"

#include "../core/global.h"
#include "../core/fileutils.h"
#include "../core/timer.h"
#include "../program/playutils.h"
#include "../program/setup.h"
#include "../search/asyncbot.h"
#include "../search/searchnode.h"
#include "../dataio/files.h"
#include "../game/randomopening.h"

#include "../core/test.h"

extern bool extra_time_log;

using namespace std;
//std::mutex outMutex;

struct Opening {
  double prob;
  int movenum;
  int begin;
  int end;
  std::vector<std::pair<int, int>> moves;  // <1000,1000> = PASS
  string game;
  Player startPla;
};

struct GameOpenings {
  int rule;
  int x_size;
  int y_size;
  double prob;
  int count;
  std::vector<Opening> games;
};

std::vector<GameOpenings> openings;

static auto findOpenings(int rule, int x_size, int y_size) {
  for(auto op = openings.begin(); op != openings.end(); op++) {
    if(op->rule == rule && op->x_size == x_size && op->y_size == y_size) {
      return op;
    }
  }
  return openings.end();
};

static void loadOpenings(std::string file, bool logOpenings) {
  openings.clear();
  if(file == "")
    return;

  ifstream o(file);
  if(!o.good()) {
    if(logOpenings)
      cout << "Error of file opening " << file << endl;
    return;
  }

  auto getLoc = [&](string& move, int& x, int& y, int& movenum) {
    x = move[0] - 'a';
    y = Global::stringToInt(move.substr(1, move.length() - 1)) - 1;
    movenum++;
    move = "";
  };
  auto parseline = [&](Opening& op, string s, int& movenum, int x_size, int y_size) {
    movenum = 0;
    string move = "";
    op.game = "";
    int x, y;
    for(int j = 0; j < s.length(); j++) {
      if(s[j] >= 'a' && s[j] <= 'z') {
        switch(move.length()) {
          case 3:
            if(move == "pas" && s[j] == 's') {
              movenum++;
              op.moves.push_back(std::pair<int, int>(1000, 1000));  // pass
              op.game += "pass";
              move = "";
            }
            break;
          case 2:
            if(move == "pa" && s[j] == 's') {
              move = move + s[j];
            } else {
              return false;
            }
            break;
          case 1:
            if(move == "p" && s[j] == 'a') {
              move = move + s[j];
            } else {
              return false;
            }
            break;
          case 0:
            move = move + s[j];
            break;
          default:
            return false;
        }
      } else if(s[j] >= '0' && s[j] <= '9') {
        if(move.length() == 1 && move[0] >= 'a' && move[0] <= 'z' && s[j] != '0') {
          move = move + s[j];
          if((j == s.length() - 1) || (s[j + 1] >= 'a' && s[j + 1] <= 'z')) {
            // last symbol or next letter
            op.game += move;
            getLoc(move, x, y, movenum);
            if(x<x_size && y<y_size)
              op.moves.push_back(std::pair<int, int>(x, y));  //
            else
              return false;
          } else if(s[j + 1] >= '0' && s[j + 1] <= '9') {
          } else {
            return false;
          }
        } else if(move.length() == 2 && move[0] >= 'a' && move[0] <= 'z' && move[1] >= '0' && move[1] <= '9') {
          move = move + s[j];
          op.game += move;
          getLoc(move, x, y, movenum);
          if(x < x_size && y < y_size)
            op.moves.push_back(std::pair<int, int>(x, y));  //
          else
            return false;
        } else {
          return false;
        }
      } else {
        return false;
      }
    }
    return movenum > 0;
  };

  for(string line; getline(o, line);) {
    line = Global::trim(line);
    if(line == "")
      continue;
    line = Global::toLower(line);
    // comment
    if(line[0] == '#' || line[0] == ':')
      continue;

    stringstream ss;
    ss << line;

    // format: rule(freestyle,standard,renju) [board size](WxH) parcent(0..100) <random game len>(begin move) file

    string s;
    ss >> s;

    int rule = 0;
    try {
      rule = Rules::parseBasicRule(s);
    } catch(const IOError& e) {
      if(logOpenings)
        cout << "Error: " << e.what() << endl;
      continue;
    }

    // Optional board size [WidthxHeight]
    string sz;
    ss >> sz;

    int x_size = 0, y_size = 0;
    double prob = 0;
    try {
      if(sscanf(sz.c_str(), "[%dx%d]", &x_size, &y_size) == 2) {
        ss >> prob;
      }
      else {
        x_size = 15;
        y_size = 15;
        stringstream ss2;
        ss2 << sz;
        ss2 >> prob;
      }
      if(prob < 0 || prob > 100)
        throw IOError("Wrong percent");
      prob /= 100.0;
      if(x_size > Board::MAX_LEN || x_size < 7 || y_size > Board::MAX_LEN || y_size < 7)
        throw IOError("Wrong board size");

    } catch(exception &e) {
      if(logOpenings)
        cout << e.what() << endl;
      continue;
    }

    string fs;
    string f;
    ss >> fs;

    // Optional random len of the game <begin>
    int begin = 0;
    int end = 0;
    try {
      if(sscanf(fs.c_str(), "<%d:%d>", &begin, &end) == 2) {
        ss >> f;
      } else if(sscanf(fs.c_str(), "<%d>", &begin) == 1) {
        ss >> f;
      }
      else {
        stringstream ss2;
        ss2 << fs;
        ss2 >> f;
      }
    } catch(exception& e) {
      if(logOpenings)
        cout << e.what() << endl;
      continue;
    }

    f = Global::trim(f);
    if(f == "")
      continue;

    ifstream ifs(f);
    if(!ifs.good()) {
      if(logOpenings)
        cout << "Error of file opening " << f << endl;
      continue;
    }
    if(logOpenings) {
      cout << "File witn openings: " << f << " rule: " << Rules::writeBasicRule(rule) << " [" << x_size << "x" << y_size
           << "] percent: " << prob * 100.0;
      if(begin > 0)
        cout << " random len from " << begin;
      if(end > 0)
        cout << " to " << end;
      cout << endl;
    }

    int op_num = 0;
    std::vector<Opening> games;
    for(string s; getline(ifs, s);) {
      s = Global::trim(s);
      if(s == "")
        continue;
      if(s[0] == '#' || s[0] == ':')
        continue;
      s = Global::toLower(s);
      Opening op;
      int movenum = 0;
      if(parseline(op, s, movenum, x_size, y_size)) {
        if(movenum % 2 == 0)
          op.startPla = P_BLACK;
        else
          op.startPla = P_WHITE;
        op.movenum = movenum;
        op.begin = begin;
        op.end = end;
        games.push_back(op);
        op_num++;
      } else {
        if(logOpenings)
          cout << "Wrong notation " << s << " in file " << f << endl;
        continue;
      }
    }

    auto gops = findOpenings(rule, x_size, y_size);
    if(gops == openings.end()) {
      GameOpenings gop;
      gop.rule = rule;
      gop.x_size = x_size;
      gop.y_size = y_size;
      gop.count = 0;
      gop.prob = 0;
      openings.push_back(gop);
      gops = openings.end()-1;
    }

    for(auto& op: games) {
      op.prob = prob / double(op_num);
      gops->games.push_back(op);
    }

    gops->count += op_num;
    gops->prob += prob;

  }

  for(auto op = openings.begin(); op != openings.end(); op++) {
    if(op->prob > 1.0) {
      for(auto game = op->games.begin(); game != op->games.end(); game++) {
        game->prob /= op->prob;
      }
      op->prob = 1.0;
    }
    if(op->count > 0) {
      if(logOpenings)
        cout << "Successfully load " << op->count << " openings for " << Rules::writeBasicRule(op->rule)
             << " [" << op->x_size << "x" << op->y_size << "]" << endl;
    }
  }
}

static void getRandomLibOpening(Rand& rand, BoardHistory& hist, Board& board, Player& startPla, const PlaySettings& playSettings) {

  auto gops = findOpenings(hist.rules.basicRule, board.x_size, board.y_size);
  if(gops == openings.end()) {
    return;
  }
  if(gops->count == 0 || gops->prob <= 0) {
    return;
  }

  auto& openings = gops->games;

  double opIdxDouble = rand.nextDouble();
  int opIdx = -1;
  double probTotal = 0;
  for(int i = 0; i < openings.size(); i++) {
    probTotal += openings[i].prob;
    if(opIdxDouble < probTotal) {
      opIdx = i;
      break;
    }
  }
  if(opIdx == -1) {
    //if(logOpenings)
    //  cout << "Failed to get an opening on rand " << opIdxDouble << endl;
    return;
  }
  Opening& op = openings[opIdx];
  //if(op.begin > 0)
    startPla = P_BLACK;
  //else
  //  startPla = op.startPla;

  int maxmovenum = op.movenum;
  if(op.end > 0 && op.begin > 0 && op.end >= op.begin && maxmovenum > op.end) {
    maxmovenum = op.end;
  }
  if(op.begin > 0 && maxmovenum > op.begin) {
    maxmovenum = rand.nextInt(op.begin, maxmovenum);
  }

  for(int i = 0; i < maxmovenum; i++) {
    int x = op.moves[i].first;
    int y = op.moves[i].second;
    Loc loc;
    if(x == 1000 && y == 1000)
      loc = Board::PASS_LOC;
    else
      loc = Location::getLoc(x, y, board.x_size);
    Player pl;
    if(i % 2 == 0)
      pl = P_BLACK;
    else
      pl = P_WHITE;
    hist.makeBoardMoveAssumeLegal(board, loc, pl);
    startPla = getOpp(pl);
  }

  if(playSettings.logOpenings && hist.getMovenum() > 0) {
    cout << "   Rule:" << Rules::writeBasicRule(hist.rules.basicRule) << " [" << board.x_size << "x" << board.y_size
         << "] Opening: " << hist.getMoves(board.x_size, board.y_size) << " last move: "
         << hist.getMovenum() << "/"  << op.movenum << endl;
  }
}

const GameInitializer* GameRunner::getGameInitializer() const {
  return gameInit;
}

const PlaySettings GameRunner::getPlaySettings() const {
  return playSettings;
}

bool GameRunner::GenerateGame(
    const string& seed,
    const MatchPairer::BotSpec& bSpecB,
    const MatchPairer::BotSpec& bSpecW,
    Logger& logger,
    InitialPosition* initPosition
)
{
  if(!initPosition)
    return false;

  MatchPairer::BotSpec botSpecB = bSpecB;
  MatchPairer::BotSpec botSpecW = bSpecW;
  Rand gameRand(seed + ":" + "forGameRand");
  const InitialPosition* initialPosition = NULL;

  Board board;
  Player pla;
  BoardHistory hist;

  OtherGameProperties otherGameProps;

  if(playSettings.forSelfPlay) {
    assert(botSpecB.botIdx == botSpecW.botIdx);
    SearchParams params = botSpecB.baseParams;
    gameInit->createGame(board, pla, hist, params, initialPosition, playSettings, otherGameProps, NULL);
    botSpecB.baseParams = params;
    botSpecW.baseParams = params;
  } else {
    gameInit->createGame(board, pla, hist, initialPosition, playSettings, otherGameProps, NULL);

    bool rulesWereSupported;
    if(botSpecB.nnEval != NULL) {
      botSpecB.nnEval->getSupportedRules(hist.rules, rulesWereSupported);
      if(!rulesWereSupported)
        logger.write("WARNING: Match is running bot on rules that it does not support: " + botSpecB.botName);
    }
    if(botSpecW.nnEval != NULL) {
      botSpecW.nnEval->getSupportedRules(hist.rules, rulesWereSupported);
      if(!rulesWereSupported)
        logger.write("WARNING: Match is running bot on rules that it does not support: " + botSpecW.botName);
    }
  }

  // Generate game from openings
  if(!playSettings.fileOpenings.empty()) {
    // lock_guard<std::mutex> lock(outMutex);
    getRandomLibOpening(gameRand, hist, board, pla, playSettings);
  }

  Search* botB;
  Search* botW;
  if(botSpecB.botIdx == botSpecW.botIdx) {
    botB = new Search(botSpecB.baseParams, botSpecB.nnEval, &logger, seed);
    botW = botB;
  } else {
    botB = new Search(botSpecB.baseParams, botSpecB.nnEval, &logger, seed + "@B");
    botW = new Search(botSpecW.baseParams, botSpecW.nnEval, &logger, seed + "@W");
  }

  // Весь алгоритм генерации в одну функцию
  if(board.numStonesOnBoard() == 0)
    RandomOpening::initializeBalancedRandomOpening(botB, botW, board, hist, pla, gameRand, this, otherGameProps);

  if(botW != botB)
    delete botW;
  delete botB;

  initPosition->board = board;
  initPosition->pla = pla;
  initPosition->hist = hist;
  return true;

}

FinishedGameData* GameRunner::runGame(
  const string& seed,
  const MatchPairer::BotSpec& bSpecB,
  const MatchPairer::BotSpec& bSpecW,
  const Sgf::PositionSample* startPosSample,
  Logger& logger,
  const std::function<bool()>& shouldStop,
  const WaitableFlag* shouldPause,
  std::function<NNEvaluator*()> checkForNewNNEval,
  std::function<void(const MatchPairer::BotSpec&, Search*)> afterInitialization,
  std::function<void(const Board&, const BoardHistory&, Player, Loc, const std::vector<double>&, const std::vector<double>&,  const Search*)> onEachMove,
  const InitialPosition* initPosition
)
{

  MatchPairer::BotSpec botSpecB = bSpecB;
  MatchPairer::BotSpec botSpecW = bSpecW;

  Rand gameRand(seed + ":" + "forGameRand");

  const InitialPosition* initialPosition = NULL;

  Board board;
  Player pla;
  BoardHistory hist;
  OtherGameProperties otherGameProps;

  if(initPosition) {
    board = initPosition->board;
    pla = initPosition->pla;
    hist = initPosition->hist;
  } else {
    if(playSettings.forSelfPlay) {
      assert(botSpecB.botIdx == botSpecW.botIdx);
      SearchParams params = botSpecB.baseParams;
      gameInit->createGame(board, pla, hist, params, initialPosition, playSettings, otherGameProps, startPosSample);
      botSpecB.baseParams = params;
      botSpecW.baseParams = params;
    } else {
      gameInit->createGame(board, pla, hist, initialPosition, playSettings, otherGameProps, startPosSample);

      bool rulesWereSupported;
      if(botSpecB.nnEval != NULL) {
        botSpecB.nnEval->getSupportedRules(hist.rules, rulesWereSupported);
        if(!rulesWereSupported)
          logger.write("WARNING: Match is running bot on rules that it does not support: " + botSpecB.botName);
      }
      if(botSpecW.nnEval != NULL) {
        botSpecW.nnEval->getSupportedRules(hist.rules, rulesWereSupported);
        if(!rulesWereSupported)
          logger.write("WARNING: Match is running bot on rules that it does not support: " + botSpecW.botName);
      }
    }
  }

  bool clearBotBeforeSearchThisGame = clearBotBeforeSearch;
  if(botSpecB.botIdx == botSpecW.botIdx) {
    // Avoid interactions between the two bots since they're the same.
    // Also in self-play this makes sure root noise is effective on each new search
    clearBotBeforeSearchThisGame = true;
  }

  Search* botB;
  Search* botW;
  if(botSpecB.botIdx == botSpecW.botIdx) {
    botB = new Search(botSpecB.baseParams, botSpecB.nnEval, &logger, seed);
    botW = botB;
  } else {
    botB = new Search(botSpecB.baseParams, botSpecB.nnEval, &logger, seed + "@B");
    botW = new Search(botSpecW.baseParams, botSpecW.nnEval, &logger, seed + "@W");
  }
  if(afterInitialization != nullptr) {
    if(botSpecB.botIdx == botSpecW.botIdx) {
      afterInitialization(botSpecB, botB);
    } else {
      afterInitialization(botSpecB, botB);
      afterInitialization(botSpecW, botW);
    }
  }

  // Generate not in Play::runGame
  if(!initPosition) {
    InitialPosition initPos;
    if(GenerateGame(seed, botSpecB, botSpecW, logger, &initPos)) {
      board = initPos.board;
      hist = initPos.hist;
      pla = initPos.pla;
    }
  }

  FinishedGameData* finishedGameData = Play::runGame(
    board,
    pla,
    hist,
    botSpecB,
    botSpecW,
    botB,
    botW,
    clearBotBeforeSearchThisGame,
    logger,
    logSearchInfo,
    logMoves,
    maxMovesPerGame,
    shouldStop,
    shouldPause,
    playSettings,
    otherGameProps,
    gameRand,
    checkForNewNNEval,  // Note that if this triggers, botSpecB and botSpecW will get updated, for use in maybeForkGame
    onEachMove);

  if(initialPosition != NULL) {
    finishedGameData->usedInitialPosition = 1;
    testAssert(finishedGameData->trainingWeight == initialPosition->trainingWeight);
  } else if(startPosSample != NULL) {
    testAssert(finishedGameData->trainingWeight == startPosSample->trainingWeight);
  }

  assert(finishedGameData->trainingWeight > 0.0);
  assert(finishedGameData->trainingWeight < 5.0);

  // Make sure not to write the game if we terminated in the middle of this game!
  if(shouldStop != nullptr && shouldStop()) {
    if(botW != botB)
      delete botW;
    delete botB;
    delete finishedGameData;
    return NULL;
  }

  assert(finishedGameData != NULL);

  if(botW != botB)
    delete botW;
  delete botB;

  if(initialPosition != NULL)
    delete initialPosition;
  if(logSearchInfo)
    finishedGameData->printDebug(std::cout);

  return finishedGameData;
}

//----------------------------------------------------------------------------------------------------------

InitialPosition::InitialPosition() : board(), hist(), pla(C_EMPTY) {}
InitialPosition::InitialPosition(const Board& b, const BoardHistory& h, Player p, double tw)
  : board(b), hist(h), pla(p), trainingWeight(tw) {}

InitialPosition::~InitialPosition() {}



//------------------------------------------------------------------------------------------------

GameInitializer::GameInitializer(ConfigParser& cfg, Logger& logger)
  :createGameMutex(),rand()
{
  initShared(cfg,logger);
}

GameInitializer::GameInitializer(ConfigParser& cfg, Logger& logger, const string& randSeed)
  :createGameMutex(),rand(randSeed)
{
  initShared(cfg,logger);
}

void GameInitializer::initShared(ConfigParser& cfg, Logger& logger) {
  if(cfg.contains("basicRule"))
    allowedBasicRules.push_back(Rules::parseBasicRule(cfg.getString("basicRule")));
  else {
    allowedBasicRuleStrs = cfg.getStrings("basicRules", Rules::basicRuleStrings());
    for(size_t i = 0; i < allowedBasicRuleStrs.size(); i++)
      allowedBasicRules.push_back(Rules::parseBasicRule(allowedBasicRuleStrs[i]));
  }

  if(allowedBasicRules.size() <= 0)
    throw IOError("basicRules or basicRule must haves at least one value in " + cfg.getFileName());

  allowedVCNRuleStrs = cfg.getStrings("VCNRules", Rules::VCNRuleStrings());

  for(size_t i = 0; i < allowedVCNRuleStrs.size(); i++)
    allowedVCNRules.push_back(Rules::parseVCNRule(allowedVCNRuleStrs[i]));

  if(allowedVCNRules.size() <= 0)
    throw IOError("VCNRules must have at least one value in " + cfg.getFileName());

  allowedFirstPassWinRules = cfg.getBools("firstPassWinRules");
  if(allowedFirstPassWinRules.size() <= 0)
    throw IOError("firstPassWinRules must have at least one value in " + cfg.getFileName());

  moveLimitProb = cfg.contains("moveLimitProb") ? cfg.getDouble("moveLimitProb", 0.0, 1.0) : 0.0;

  if(cfg.contains("bSizes") == cfg.contains("bSizesXY"))
    throw IOError("Must specify exactly one of bSizes or bSizesXY");

  if(cfg.contains("bSizes")) {
    std::vector<int> allowedBEdges = cfg.getInts("bSizes", 2, Board::MAX_LEN);
    std::vector<double> allowedBEdgeRelProbs = cfg.getDoubles("bSizeRelProbs", 0.0, 1e100);
    double relProbSum = 0.0;
    for(const double p: allowedBEdgeRelProbs)
      relProbSum += p;
    if(relProbSum <= 1e-100)
      throw IOError("bSizeRelProbs must sum to a positive value");
    double allowRectangleProb =
      cfg.contains("allowRectangleProb") ? cfg.getDouble("allowRectangleProb", 0.0, 1.0) : 0.0;

    if(allowedBEdges.size() <= 0)
      throw IOError("bSizes must have at least one value in " + cfg.getFileName());
    if(allowedBEdges.size() != allowedBEdgeRelProbs.size())
      throw IOError("bSizes and bSizeRelProbs must have same number of values in " + cfg.getFileName());

    allowedBSizes.clear();
    allowedBSizeRelProbs.clear();
    for(int i = 0; i < (int)allowedBEdges.size(); i++) {
      for(int j = 0; j < (int)allowedBEdges.size(); j++) {
        int x = allowedBEdges[i];
        int y = allowedBEdges[j];
        if(x == y) {
          allowedBSizes.push_back(std::make_pair(x, y));
          allowedBSizeRelProbs.push_back(
            (1.0 - allowRectangleProb) * allowedBEdgeRelProbs[i] / relProbSum +
            allowRectangleProb * allowedBEdgeRelProbs[i] * allowedBEdgeRelProbs[j] / relProbSum / relProbSum);
        } else {
          if(allowRectangleProb > 0.0) {
            allowedBSizes.push_back(std::make_pair(x, y));
            allowedBSizeRelProbs.push_back(
              allowRectangleProb * allowedBEdgeRelProbs[i] * allowedBEdgeRelProbs[j] / relProbSum / relProbSum);
          }
        }
      }
    }
  } else if(cfg.contains("bSizesXY")) {
    if(cfg.contains("allowRectangleProb"))
      throw IOError("Cannot specify allowRectangleProb when specifying bSizesXY, please adjust the relative frequency of rectangles yourself");
    allowedBSizes = cfg.getNonNegativeIntDashedPairs("bSizesXY", 2, Board::MAX_LEN);
    allowedBSizeRelProbs = cfg.getDoubles("bSizeRelProbs", 0.0, 1e100);

    double relProbSum = 0.0;
    for(const double p: allowedBSizeRelProbs)
      relProbSum += p;
    if(relProbSum <= 1e-100)
      throw IOError("bSizeRelProbs must sum to a positive value");
  }

  auto generateCumProbs = [](const vector<Sgf::PositionSample> poses, double lambda, double& effectiveSampleSize) {
    int64_t minInitialTurnNumber = 0;
    for(size_t i = 0; i < poses.size(); i++)
      minInitialTurnNumber = std::min(minInitialTurnNumber, poses[i].initialTurnNumber);

    vector<double> cumProbs;
    cumProbs.resize(poses.size());
    // Fill with uncumulative probs
    for(size_t i = 0; i < poses.size(); i++) {
      int64_t startTurn = poses[i].getCurrentTurnNumber() - minInitialTurnNumber;
      cumProbs[i] = exp(-startTurn * lambda) * poses[i].weight;
    }
    for(size_t i = 0; i < poses.size(); i++) {
      if(!(cumProbs[i] > -1e200 && cumProbs[i] < 1e200)) {
        throw StringError("startPos found bad unnormalized probability: " + Global::doubleToString(cumProbs[i]));
      }
    }

    // Compute ESS
    double sum = 0.0;
    double sumSq = 0.0;
    for(size_t i = 0; i < poses.size(); i++) {
      sum += cumProbs[i];
      sumSq += cumProbs[i] * cumProbs[i];
    }
    effectiveSampleSize = sum * sum / (sumSq + 1e-200);

    // Make cumulative
    for(size_t i = 1; i < poses.size(); i++)
      cumProbs[i] += cumProbs[i - 1];

    return cumProbs;
  };

  startPosesProb = 0.0;
  if(cfg.contains("startPosesFromSgfDir")) {
    startPoses.clear();
    startPosCumProbs.clear();
    startPosesProb = cfg.getDouble("startPosesProb", 0.0, 1.0);

    vector<string> dirs = Global::split(cfg.getString("startPosesFromSgfDir"), ',');
    vector<string> excludes =
      Global::split(cfg.contains("startPosesSgfExcludes") ? cfg.getString("startPosesSgfExcludes") : "", ',');
    double startPosesLoadProb = cfg.getDouble("startPosesLoadProb", 0.0, 1.0);
    double startPosesTurnWeightLambda = cfg.getDouble("startPosesTurnWeightLambda", -10, 10);

    vector<string> files;
    FileHelpers::collectSgfsFromDirs(dirs, files);
    std::set<Hash128> excludeHashes = Sgf::readExcludes(excludes);
    logger.write("Found " + Global::uint64ToString(files.size()) + " sgf files");
    logger.write("Loaded " + Global::uint64ToString(excludeHashes.size()) + " excludes");
    std::set<Hash128> uniqueHashes;
    std::function<void(Sgf::PositionSample&, const BoardHistory&, const string&)> posHandler =
      [startPosesLoadProb, this](Sgf::PositionSample& posSample, const BoardHistory& hist, const string& comments) {
        (void)hist;
        (void)comments;
        if(rand.nextBool(startPosesLoadProb))
          startPoses.push_back(posSample);
      };
    int64_t numExcluded = 0;
    for(size_t i = 0; i < files.size(); i++) {
      Sgf* sgf = NULL;
      try {
        sgf = Sgf::loadFile(files[i]);
        if(contains(excludeHashes, sgf->hash))
          numExcluded += 1;
        else {
          bool hashComments = false;
          bool hashParent = false;
          bool allowGameOver = false;
          sgf->iterAllUniquePositions(uniqueHashes, hashComments, hashParent, allowGameOver, NULL, posHandler);
        }
      } catch(const StringError& e) {
        logger.write("Invalid SGF " + files[i] + ": " + e.what());
      }
      if(sgf != NULL)
        delete sgf;
    }
    logger.write("Kept " + Global::uint64ToString(startPoses.size()) + " start positions");
    logger.write(
      "Excluded " + Global::int64ToString(numExcluded) + "/" + Global::uint64ToString(files.size()) + " sgf files");

    double ess = 0.0;
    startPosCumProbs = generateCumProbs(startPoses, startPosesTurnWeightLambda, ess);

    if(startPoses.size() <= 0) {
      logger.write("No start positions loaded, disabling start position logic");
      startPosesProb = 0;
    } else {
      logger.write(
        "Cumulative unnormalized probability for start poses: " +
        Global::doubleToString(startPosCumProbs[startPoses.size() - 1]));
      logger.write("Effective sample size for start poses: " + Global::doubleToString(ess));
    }
  }

  hintPosesProb = 0.0;
  if(cfg.contains("hintPosesDir")) {
    hintPoses.clear();
    hintPosCumProbs.clear();
    hintPosesProb = cfg.getDouble("hintPosesProb", 0.0, 1.0);

    vector<string> dirs = Global::split(cfg.getString("hintPosesDir"), ',');

    vector<string> files;
    std::function<bool(const string&)> fileFilter = [](const string& fileName) {
      return Global::isSuffix(fileName, ".hintposes.txt") || Global::isSuffix(fileName, ".startposes.txt") ||
             Global::isSuffix(fileName, ".bookposes.txt");
    };
    for(int i = 0; i < dirs.size(); i++) {
      string dir = Global::trim(dirs[i]);
      if(dir.size() > 0)
        FileUtils::collectFiles(dir, fileFilter, files);
    }

    for(size_t i = 0; i < files.size(); i++) {
      vector<string> lines = FileUtils::readFileLines(files[i], '\n');
      for(size_t j = 0; j < lines.size(); j++) {
        string line = Global::trim(lines[j]);
        if(line.size() > 0) {
          try {
            Sgf::PositionSample posSample = Sgf::PositionSample::ofJsonLine(line);
            hintPoses.push_back(posSample);
          } catch(const StringError& err) {
            logger.write(string("ERROR parsing hintpos: ") + err.what());
          }
        }
      }
    }
    logger.write("Loaded " + Global::uint64ToString(hintPoses.size()) + " hint positions");

    double ess = 0.0;
    hintPosCumProbs = generateCumProbs(hintPoses, 0.0, ess);

    if(hintPoses.size() <= 0) {
      logger.write("No hint positions loaded, disabling hint position logic");
      hintPosesProb = 0;
    } else {
      logger.write(
        "Cumulative unnormalized probability for hint poses: " +
        Global::doubleToString(hintPosCumProbs[hintPoses.size() - 1]));
      logger.write("Effective sample size for hint poses: " + Global::doubleToString(ess));
    }
  }

  if(allowedBSizes.size() <= 0)
    throw IOError("bSizes or bSizesXY must have at least one value in " + cfg.getFileName());
  if(allowedBSizes.size() != allowedBSizeRelProbs.size())
    throw IOError("bSizes or bSizesXY and bSizeRelProbs must have same number of values in " + cfg.getFileName());

  minBoardXSize = allowedBSizes[0].first;
  minBoardYSize = allowedBSizes[0].second;
  maxBoardXSize = allowedBSizes[0].first;
  maxBoardYSize = allowedBSizes[0].second;
  for(const std::pair<int, int>& bSize: allowedBSizes) {
    minBoardXSize = std::min(minBoardXSize, bSize.first);
    minBoardYSize = std::min(minBoardYSize, bSize.second);
    maxBoardXSize = std::max(maxBoardXSize, bSize.first);
    maxBoardYSize = std::max(maxBoardYSize, bSize.second);
  }

  for(const Sgf::PositionSample& pos: hintPoses) {
    minBoardXSize = std::min(minBoardXSize, pos.board.x_size);
    minBoardYSize = std::min(minBoardYSize, pos.board.y_size);
    maxBoardXSize = std::max(maxBoardXSize, pos.board.x_size);
    maxBoardYSize = std::max(maxBoardYSize, pos.board.y_size);
  }

  noResultRandRadius = cfg.contains("noResultRandRadius") ? cfg.getDouble("noResultRandRadius", 0.0, 1.0) : 0.0;
  fourAttackPolicyReduceMean =
    cfg.contains("fourAttackPolicyReduceMean") ? cfg.getDouble("fourAttackPolicyReduceMean", 0.0, 3.0) : 0.0;

  randomMoveNumProb_NOVC = vector<float>{10, 30, 50, 80, 60, 40, 20, 10, 5, 1, 0, 0};
  randomMoveNumProb_VC1_B = vector<float>{0.03, 0.03, 25, 20, 15, 10, 5, 1, 0, 0, 0, 0};
  randomMoveNumProb_VC1_W = vector<float>{0.03, 0.03, 0.03, 20, 15, 10, 5, 1, 0, 0, 0, 0};
  randomMoveNumProb_VC2_B = vector<float>{0.01, 0.01, 0.03, 0.03, 35, 30, 25, 20, 15, 10, 5, 1};
  randomMoveNumProb_VC2_W = vector<float>{0.01, 0.01, 0.03, 0.03, 0.03, 30, 25, 20, 15, 10, 5, 1};

  if(cfg.contains("randomMoveNumProb_NOVC"))
    randomMoveNumProb_NOVC = cfg.getFloats("randomMoveNumProb_NOVC", 0.0, 100);
  if(cfg.contains("randomMoveNumProb_VC1_B"))
    randomMoveNumProb_VC1_B = cfg.getFloats("randomMoveNumProb_VC1_B", 0.0, 100);
  if(cfg.contains("randomMoveNumProb_VC1_W"))
    randomMoveNumProb_VC1_W = cfg.getFloats("randomMoveNumProb_VC1_W", 0.0, 100);
  if(cfg.contains("randomMoveNumProb_VC2_B"))
    randomMoveNumProb_VC2_B = cfg.getFloats("randomMoveNumProb_VC2_B", 0.0, 100);
  if(cfg.contains("randomMoveNumProb_VC2_W"))
    randomMoveNumProb_VC2_W = cfg.getFloats("randomMoveNumProb_VC2_W", 0.0, 100);
}

GameInitializer::~GameInitializer()
{}

void GameInitializer::createGame(
  Board& board, Player& pla, BoardHistory& hist,
  const InitialPosition* initialPosition,
  const PlaySettings& playSettings,
  OtherGameProperties& otherGameProps,
  const Sgf::PositionSample* startPosSample
) {
  //Multiple threads will be calling this, and we have some mutable state such as rand.
  lock_guard<std::mutex> lock(createGameMutex);
  createGameSharedUnsynchronized(board,pla,hist,initialPosition,playSettings,otherGameProps,startPosSample);
  if(noResultRandRadius != 0.0)
    throw StringError("GameInitializer::createGame called in a mode that doesn't support specifying noResultRandRadius");
}

void GameInitializer::createGame(
  Board& board, Player& pla, BoardHistory& hist,
  SearchParams& params,
  const InitialPosition* initialPosition,
  const PlaySettings& playSettings,
  OtherGameProperties& otherGameProps,
  const Sgf::PositionSample* startPosSample
) {
  //Multiple threads will be calling this, and we have some mutable state such as rand.
  lock_guard<std::mutex> lock(createGameMutex);
  createGameSharedUnsynchronized(board,pla,hist,initialPosition,playSettings,otherGameProps,startPosSample);

  params.fourAttackPolicyReduce = 0.0;
  if(fourAttackPolicyReduceMean > 1e-30)
  {
    do {
      params.fourAttackPolicyReduce = rand.nextExponential() * fourAttackPolicyReduceMean;
    } while(params.fourAttackPolicyReduce > 6.0);
  }

  if(noResultRandRadius > 1e-30) {
    double mean = params.noResultUtilityForWhite;
    if(mean < -1.0 || mean > 1.0)
      throw StringError("GameInitializer: params.noResultUtilityForWhite not within [-1,1]: " + Global::doubleToString(mean));
    params.noResultUtilityForWhite = mean + noResultRandRadius * (rand.nextDouble() * 2 - 1);
    while(params.noResultUtilityForWhite < -1.0 || params.noResultUtilityForWhite > 1.0)
      params.noResultUtilityForWhite = mean + noResultRandRadius * (rand.nextDouble() * 2 - 1);
  }
}

bool GameInitializer::isAllowedBSize(int xSize, int ySize) {
  if(!contains(allowedBSizes, std::make_pair(xSize, ySize)))
    return false;
  return true;
}

std::vector<std::pair<int, int>> GameInitializer::getAllowedBSizes() const {
  return allowedBSizes;
}

std::vector<float> GameInitializer::getRandomMoveNumProb(int VCNRule) const {
  if(VCNRule == Rules::VCNRULE_NOVC)
    return randomMoveNumProb_NOVC;
  if(VCNRule == Rules::VCNRULE_VC1_B)
    return randomMoveNumProb_VC1_B;
  if(VCNRule == Rules::VCNRULE_VC1_W)
    return randomMoveNumProb_VC1_W;
  if(VCNRule == Rules::VCNRULE_VC2_B)
    return randomMoveNumProb_VC2_B;
  if(VCNRule == Rules::VCNRULE_VC2_W)
    return randomMoveNumProb_VC2_W;
  return std::vector<float>{};
}

int GameInitializer::getMinBoardXSize() const {
  return minBoardXSize;
}
int GameInitializer::getMinBoardYSize() const {
  return minBoardYSize;
}
int GameInitializer::getMaxBoardXSize() const {
  return maxBoardXSize;
}
int GameInitializer::getMaxBoardYSize() const {
  return maxBoardYSize;
}

Rules GameInitializer::createRules() {
  lock_guard<std::mutex> lock(createGameMutex);
  return createRulesUnsynchronized();
}

Rules GameInitializer::createRulesUnsynchronized() {
  Rules rules;
  rules.basicRule = allowedBasicRules[rand.nextUInt(allowedBasicRules.size())];
  rules.VCNRule = allowedVCNRules[rand.nextUInt(allowedVCNRules.size())];
  if(rules.VCNRule == Rules::VCNRULE_NOVC)
    rules.firstPassWin = allowedFirstPassWinRules[rand.nextUInt(allowedFirstPassWinRules.size())];
  else
    rules.firstPassWin = false;
  return rules;
}

void GameInitializer::createGameSharedUnsynchronized(
  Board& board, Player& pla, BoardHistory& hist,
  const InitialPosition* initialPosition,
  const PlaySettings& playSettings,
  OtherGameProperties& otherGameProps,
  const Sgf::PositionSample* startPosSample
) {
  if(initialPosition != NULL) {
    board = initialPosition->board;
    hist = initialPosition->hist;
    pla = initialPosition->pla;

    otherGameProps.isSgfPos = false;
    otherGameProps.isHintPos = false;
    otherGameProps.allowPolicyInit = false; //On fork positions, don't play extra moves at start
    otherGameProps.hintLoc = Board::NULL_LOC;
    otherGameProps.hintTurn = -1;
    otherGameProps.trainingWeight = initialPosition->trainingWeight;
    return;
  }

  int bSizeIdx = rand.nextUInt(allowedBSizeRelProbs.data(), allowedBSizeRelProbs.size());
  Rules rules = createRulesUnsynchronized();

  const Sgf::PositionSample* posSample = NULL;
  if(startPosSample != NULL) 
    posSample = startPosSample;

  if(posSample == NULL) {
    if(startPosesProb > 0 && rand.nextBool(startPosesProb)) {
      assert(startPoses.size() > 0);
      size_t r = rand.nextIndexCumulative(startPosCumProbs.data(),startPosCumProbs.size());
      assert(r < startPosCumProbs.size());
      posSample = &(startPoses[r]);
    }
    else if(hintPosesProb > 0 && rand.nextBool(hintPosesProb)) {
      assert(hintPoses.size() > 0);
      size_t r = rand.nextIndexCumulative(hintPosCumProbs.data(),hintPosCumProbs.size());
      assert(r < hintPosCumProbs.size());
      posSample = &(hintPoses[r]);
    }
  }

  if(posSample != NULL) {
    const Sgf::PositionSample& startPos = *posSample;
    board = startPos.board;
    pla = startPos.nextPla;
    hist.clear(board,pla,rules);
    hist.setInitialTurnNumber(startPos.initialTurnNumber);
    Loc hintLoc = startPos.hintLoc;
    testAssert(startPos.moves.size() < 0xFFFFFF);
    for(size_t i = 0; i<startPos.moves.size(); i++) {
      bool isLegal = hist.isLegal(board,startPos.moves[i].loc,startPos.moves[i].pla);
      // Makes a best effort to still use the position, stopping if we hit an illegal move. It's possible
      // we hit this because of rules differences making a superko move or self-capture illegal, for example.
      if(!isLegal) {
        //If we stop due to illegality, it doesn't make sense to still use the hintLoc
        hintLoc = Board::NULL_LOC;
        break;
      }
      hist.makeBoardMoveAssumeLegal(board,startPos.moves[i].loc,startPos.moves[i].pla);
      pla = getOpp(startPos.moves[i].pla);
    }

    otherGameProps.isSgfPos = hintLoc == Board::NULL_LOC;
    otherGameProps.isHintPos = hintLoc != Board::NULL_LOC;
    otherGameProps.allowPolicyInit = hintLoc == Board::NULL_LOC; //On sgf positions, do allow extra moves at start
    otherGameProps.hintLoc = hintLoc;
    otherGameProps.hintTurn = (int)hist.moveHistory.size();
    otherGameProps.hintPosHash = board.pos_hash;
    otherGameProps.trainingWeight = startPos.trainingWeight;
  }
  else {
    int xSize = allowedBSizes[bSizeIdx].first;
    int ySize = allowedBSizes[bSizeIdx].second;
    if(!rules.firstPassWin && rules.VCNRule == Rules::VCNRULE_NOVC && rand.nextBool(moveLimitProb)) {
      int maxMoves = rand.nextExponential() * 30 + 30 - rand.nextExponential() * 5;
      if(maxMoves > xSize * ySize - 5)
        maxMoves = 0;
      if(maxMoves < 10)
        maxMoves = 0;
      rules.maxMoves = maxMoves;
    }
    board = Board(xSize, ySize);
    pla = P_BLACK;
    hist.clear(board,pla,rules);

    otherGameProps.isSgfPos = false;
    otherGameProps.isHintPos = false;
    otherGameProps.allowPolicyInit = true; //Handicap and regular games do allow policy init
    otherGameProps.hintLoc = Board::NULL_LOC;
    otherGameProps.hintTurn = -1;
    otherGameProps.trainingWeight = 1.0;
  }

  double asymmetricProb = playSettings.normalAsymmetricPlayoutProb;
  if(asymmetricProb > 0 && rand.nextBool(asymmetricProb)) {
    assert(playSettings.maxAsymmetricRatio >= 1.0);
    double maxNumDoublings = log(playSettings.maxAsymmetricRatio) / log(2.0);
    double numDoublings = rand.nextDouble(maxNumDoublings);
    if(rand.nextBool(0.5)) {
      otherGameProps.playoutDoublingAdvantagePla = C_WHITE;
      otherGameProps.playoutDoublingAdvantage = numDoublings;
    }
    else {
      otherGameProps.playoutDoublingAdvantagePla = C_BLACK;
      otherGameProps.playoutDoublingAdvantage = numDoublings;
    }
  }
}

//----------------------------------------------------------------------------------------------------------


MatchPairer::MatchPairer(
  ConfigParser& cfg,
  int nBots,
  const vector<string>& bNames,
  const vector<NNEvaluator*>& nEvals,
  const vector<SearchParams>& bParamss,
  const std::vector<std::pair<int, int>>& matchups,
  int64_t numGames,
  GameRunner *run)
  :numBots(nBots),
   botNames(bNames),
   nnEvals(nEvals),
   baseParamss(bParamss),
   matchupsPerRound(matchups),
   nextMatchups(),
   rand(),
   numGamesStartedSoFar(0),
   numGamesTotal(numGames),
   runner(run),
   initialPosition(NULL),
   generate(false),
   logMatch(false),
   logGamesEvery(),
   getMatchupMutex()
{
  assert(botNames.size() == numBots);
  assert(nnEvals.size() == numBots);
  assert(baseParamss.size() == numBots);

  if(matchupsPerRound.size() <= 0)
    throw StringError("MatchPairer: no matchups specified");
  if(matchupsPerRound.size() > 0xFFFFFF)
    throw StringError("MatchPairer: too many matchups");

  logGamesEvery = cfg.getInt64("logGamesEvery",1,1000000);
  logMatch = cfg.contains("logMatch") ? cfg.getBool("logMatch") : false;
  if(runner)
    initialPosition = new InitialPosition();
}

MatchPairer::~MatchPairer()
{
  delete initialPosition;
}

int64_t MatchPairer::getNumGamesTotalToGenerate() const {
  return numGamesTotal;
}

bool MatchPairer::getMatchup(BotSpec& botSpecB, BotSpec& botSpecW, Logger& logger, const string seed, InitialPosition** initPosition)
{
  std::lock_guard<std::mutex> lock(getMatchupMutex);

  if(numGamesStartedSoFar >= numGamesTotal)
    return false;

  numGamesStartedSoFar += 1;

  if(numGamesStartedSoFar % logGamesEvery == 0)
    logger.write("Started " + Global::int64ToString(numGamesStartedSoFar) + " games");
  int64_t logNNEvery = logGamesEvery*100 > 1000 ? logGamesEvery*100 : 1000;
  if(numGamesStartedSoFar % logNNEvery == 0) {
    for(int i = 0; i<nnEvals.size(); i++) {
      if(nnEvals[i] != NULL) {
        logger.write(nnEvals[i]->getModelFileName());
        logger.write("NN rows: " + Global::int64ToString(nnEvals[i]->numRowsProcessed()));
        logger.write("NN batches: " + Global::int64ToString(nnEvals[i]->numBatchesProcessed()));
        logger.write("NN avg batch size: " + Global::doubleToString(nnEvals[i]->averageProcessedBatchSize()));
      }
    }
  }
  pair<int,int> matchup = getMatchupPairUnsynchronized();

  botSpecB.botIdx = matchup.first;
  botSpecB.botName = botNames[matchup.first];
  botSpecB.nnEval = nnEvals[matchup.first];
  botSpecB.baseParams = baseParamss[matchup.first];

  botSpecW.botIdx = matchup.second;
  botSpecW.botName = botNames[matchup.second];
  botSpecW.nnEval = nnEvals[matchup.second];
  botSpecW.baseParams = baseParamss[matchup.second];
  if(logMatch)
  {
    //lock_guard<std::mutex> lock(outMutex);
    cout << botSpecB.botName << " - " << botSpecW.botName << endl;
  }

  if(runner) {
    if(generate) {
      runner->GenerateGame(seed, botSpecB, botSpecW, logger, initialPosition);
      if(logMatch) {
        //lock_guard<std::mutex> lock(outMutex);
        cout << "Rule:" << Rules::writeBasicRule(initialPosition->hist.rules.basicRule) << " ["
             << initialPosition->board.x_size << "x" << initialPosition->board.y_size << "] Match game: "
             << initialPosition->hist.getMoves(initialPosition->board.x_size, initialPosition->board.y_size)
             << " last move: " << initialPosition->hist.getMovenum() << endl;
      }
    }
    else {
      if(logMatch) {
        //lock_guard<std::mutex> lock(outMutex);
        cout << "Rule:" << Rules::writeBasicRule(initialPosition->hist.rules.basicRule) << " ["
             << initialPosition->board.x_size << "x" << initialPosition->board.y_size << "] Next match game: "
             << initialPosition->hist.getMoves(initialPosition->board.x_size, initialPosition->board.y_size)
             << " last move: " << initialPosition->hist.getMovenum() << endl;
      }
    }
    if(initPosition)
      *initPosition = initialPosition;
  }



  return true;
}

pair<int,int> MatchPairer::getMatchupPairUnsynchronized() {
  generate = false;
  if(nextMatchups.size() <= 0) {
    if(numBots == 0)
      throw StringError("MatchPairer::getMatchupPairUnsynchronized: no bots to match up");
    // Append all matches for the next round
    generate = true;
    nextMatchups.clear();
    nextMatchups.insert(nextMatchups.begin(), matchupsPerRound.begin(), matchupsPerRound.end());

    //Shuffle
    for(int i = (int)nextMatchups.size() - 1; i >= 1; i--) {
      int j = (int)rand.nextUInt(i + 1);
      pair<int, int> tmp = nextMatchups[i];
      nextMatchups[i] = nextMatchups[j];
      nextMatchups[j] = tmp;
    }
  }

  pair<int,int> matchup = nextMatchups.back();
  nextMatchups.pop_back();

  return matchup;
}

//----------------------------------------------------------------------------------------------------------

static void failIllegalMove(Search* bot, Logger& logger, Board board, Loc loc) {
  ostringstream sout;
  sout << "Bot returned null location or illegal move!?!" << "\n";
  sout << board << "\n";
  sout << bot->getRootBoard() << "\n";
  sout << "Pla: " << PlayerIO::playerToString(bot->getRootPla()) << "\n";
  sout << "Loc: " << Location::toString(loc,bot->getRootBoard()) << "\n";
  logger.write(sout.str());
  bot->getRootBoard().checkConsistency();
  ASSERT_UNREACHABLE;
}

static void logSearch(Search* bot, Logger& logger, Loc loc, OtherGameProperties otherGameProps) {
  ostringstream sout;
  Board::printBoard(sout, bot->getRootBoard(), loc, &(bot->getRootHist().moveHistory));
  sout << "\n";
  sout << "Rules: " << bot->getRootHist().rules << "\n";
  sout << "Root visits: " << bot->getRootVisits() << "\n";
  if(otherGameProps.hintLoc != Board::NULL_LOC &&
     otherGameProps.hintTurn == bot->getRootHist().moveHistory.size() &&
     otherGameProps.hintPosHash == bot->getRootBoard().pos_hash) {
    sout << "HintLoc " << Location::toString(otherGameProps.hintLoc,bot->getRootBoard()) << "\n";
  }
  sout << "Policy surprise " << bot->getPolicySurprise() << "\n";
  sout << "Raw WL " << bot->getRootRawNNValuesRequireSuccess().winLossValue << "\n";
  sout << "PV: ";
  bot->printPV(sout, bot->rootNode, 25);
  sout << "\n";
  sout << "Tree:\n";
  bot->printTree(sout, bot->rootNode, PrintTreeOptions().maxDepth(1).maxChildrenToShow(10),P_WHITE);

  logger.write(sout.str());
}

static Loc chooseRandomForkingMove(const NNOutput* nnOutput, const Board& board, const BoardHistory& hist, Player pla, Rand& gameRand, Loc banMove) {
  double r = gameRand.nextDouble();

  bool allowPass = true;

  //70% of the time, do a random temperature 1 policy move
  if(r < 0.70)
    return PlayUtils::chooseRandomPolicyMove(nnOutput, board, hist, pla, gameRand, 1.0, allowPass, banMove);
  //25% of the time, do a random temperature 2 policy move
  else if(r < 0.95)
    return PlayUtils::chooseRandomPolicyMove(nnOutput, board, hist, pla, gameRand, 2.0, allowPass, banMove);
  //5% of the time, do a random legal move
  else
    return PlayUtils::chooseRandomLegalMove(board, hist, pla, gameRand, banMove);
}

void Play::extractPolicyTarget(
  vector<PolicyTargetMove>& buf,
  const Search* toMoveBot,
  const SearchNode* node,
  vector<Loc>& locsBuf,
  vector<double>& playSelectionValuesBuf
) {
  double scaleMaxToAtLeast = 10.0;

  assert(node != NULL);
  assert(!toMoveBot->searchParams.rootSymmetryPruning);
  bool allowDirectPolicyMoves = false;
  bool success = toMoveBot->getPlaySelectionValues(*node,locsBuf,playSelectionValuesBuf,NULL,scaleMaxToAtLeast,allowDirectPolicyMoves);
  testAssert(success);
  (void)success; //Avoid warning when asserts are disabled

  assert(locsBuf.size() == playSelectionValuesBuf.size());
  assert(locsBuf.size() <= toMoveBot->rootBoard.x_size * toMoveBot->rootBoard.y_size + 1);

  //Make sure we don't overflow int16
  double maxValue = 0.0;
  for(int moveIdx = 0; moveIdx<locsBuf.size(); moveIdx++) {
    double value = playSelectionValuesBuf[moveIdx];
    testAssert(value >= 0.0);
    if(value > maxValue)
      maxValue = value;
  }

  double factor = 1.0;
  if(maxValue > 30000.0)
    factor = 30000.0 / maxValue;

  for(int moveIdx = 0; moveIdx<locsBuf.size(); moveIdx++) {
    double value = playSelectionValuesBuf[moveIdx] * factor;
    assert(value <= 30001.0);
    buf.push_back(PolicyTargetMove(locsBuf[moveIdx],(int16_t)round(value)));
  }
}

static void extractValueTargets(ValueTargets& buf, const Search* toMoveBot, const SearchNode* node) {
  ReportedSearchValues values;
  bool success = toMoveBot->getNodeValues(node,values);
  testAssert(success);
  (void)success; //Avoid warning when asserts are disabled

  buf.win = (float)values.winValue;
  buf.loss = (float)values.lossValue;
  buf.noResult = (float)values.noResultValue;
}

static void extractQValueTargets(vector<QValueTargetMove>& buf, const Search* toMoveBot, const SearchNode* node) {
  ConstSearchNodeChildrenReference children = node->getChildren();
  int numChildren = children.iterateAndCountChildren();
  for(int childIdx = 0; childIdx < numChildren; childIdx++) {
    const SearchChildPointer& childPtr = children[childIdx];
    const SearchNode* child = childPtr.getIfAllocated();

    if(child == NULL)
      continue;

    ReportedSearchValues values;
    bool success = toMoveBot->getNodeValues(child, values);
    if(!success)
      continue;
    if(values.visits <= 0)
      continue;

    Loc moveLoc = childPtr.getMoveLoc();
    buf.push_back(QValueTargetMove(moveLoc, (float)values.winLossValue, 0.0, values.visits));
  }
}

static NNRawStats computeNNRawStats(const Search* bot, const Board& board, const BoardHistory& hist, Player pla) {
  NNResultBuf buf;
  MiscNNInputParams nnInputParams;
  nnInputParams.noResultUtilityForWhite = bot->searchParams.noResultUtilityForWhite;
  Board b = board;
  bot->nnEvaluator->evaluate(b,hist,pla,nnInputParams,buf,false);
  NNOutput& nnOutput = *(buf.result);

  NNRawStats nnRawStats;
  nnRawStats.whiteWinLoss = nnOutput.whiteWinProb - nnOutput.whiteLossProb;
  {
    double entropy = 0.0;
    int policySize = NNPos::getPolicySize(nnOutput.nnXLen,nnOutput.nnYLen);
    for(int pos = 0; pos<policySize; pos++) {
#ifdef QUANTIZED_OUTPUT
      double prob = nnOutput.getPolicyProb(pos);
#else
      double prob = nnOutput.policyProbs[pos];
#endif
      if(prob >= 1e-30)
        entropy += -prob * log(prob);
    }
    nnRawStats.policyEntropy = entropy;
  }
  return nnRawStats;
}

//Recursively walk non-root-node subtree under node recording positions that have enough visits
//We also only record positions where the player to move made best moves along the tree so far.
//Does NOT walk down branches of excludeLoc0 and excludeLoc1 - these are used to avoid writing
//subtree positions for branches that we are about to actually play or do a forked sideposition search on.
static void recordTreePositionsRec(
  FinishedGameData* gameData,
  const Board& board, const BoardHistory& hist, Player pla,
  const Search* toMoveBot,
  const SearchNode* node, int depth, int maxDepth, bool plaAlwaysBest, bool oppAlwaysBest,
  int64_t minVisitsAtNode, float recordTreeTargetWeight,
  int numNeuralNetChangesSoFar,
  vector<Loc>& locsBuf, vector<double>& playSelectionValuesBuf,
  Loc excludeLoc0, Loc excludeLoc1
) {
  ConstSearchNodeChildrenReference children = node->getChildren();
  int numChildren = children.iterateAndCountChildren();

  if(numChildren <= 0)
    return;

  if(plaAlwaysBest && node != toMoveBot->rootNode) {
    SidePosition* sp = new SidePosition(board,hist,pla,numNeuralNetChangesSoFar);
    Play::extractPolicyTarget(sp->policyTarget, toMoveBot, node, locsBuf, playSelectionValuesBuf);
    extractValueTargets(sp->whiteValueTargets, toMoveBot, node);
    extractQValueTargets(sp->whiteQValueTargets.targets, toMoveBot, node);

    double policySurprise = 0.0, policyEntropy = 0.0, searchEntropy = 0.0;
    bool success = toMoveBot->getPolicySurpriseAndEntropy(policySurprise, searchEntropy, policyEntropy, node);
    testAssert(success);
    (void)success; //Avoid warning when asserts are disabled
    sp->policySurprise = policySurprise;
    sp->policyEntropy = policyEntropy;
    sp->searchEntropy = searchEntropy;

    sp->nnRawStats = computeNNRawStats(toMoveBot, board, hist, pla);
    sp->targetWeight = recordTreeTargetWeight;
    sp->unreducedNumVisits = toMoveBot->getRootVisits();
    gameData->sidePositions.push_back(sp);
  }

  if(depth >= maxDepth)
    return;

  //Best child is the one with the largest number of visits, find it
  int bestChildIdx = 0;
  int64_t bestChildVisits = 0;
  for(int i = 1; i<numChildren; i++) {
    const SearchNode* child = children[i].getIfAllocated();
    while(child->statsLock.test_and_set(std::memory_order_acquire));
    int64_t numVisits = child->stats.visits;
    child->statsLock.clear(std::memory_order_release);
    Loc moveLoc = children[i].getMoveLoc();
    if(moveLoc == Board::PASS_LOC && toMoveBot->searchParams.suppressPass)
      continue;
    if(numVisits > bestChildVisits) {
      bestChildVisits = numVisits;
      bestChildIdx = i;
    }
  }

  for(int i = 0; i<numChildren; i++) {
    bool newPlaAlwaysBest = oppAlwaysBest;
    bool newOppAlwaysBest = plaAlwaysBest && i == bestChildIdx;

    if(!newPlaAlwaysBest && !newOppAlwaysBest)
      continue;

    const SearchNode* child = children[i].getIfAllocated();
    Loc moveLoc = children[i].getMoveLoc();
    if(moveLoc == excludeLoc0 || moveLoc == excludeLoc1)
      continue;
    if(moveLoc == Board::PASS_LOC && toMoveBot->searchParams.suppressPass)
      continue;

    int64_t numVisits = child->stats.visits.load(std::memory_order_acquire);
    if(numVisits < minVisitsAtNode)
      continue;

    if(hist.isLegal(board, moveLoc, pla)) {
      Board copy = board;
      BoardHistory histCopy = hist;
      histCopy.makeBoardMoveAssumeLegal(copy, moveLoc, pla);
      Player nextPla = getOpp(pla);
      recordTreePositionsRec(
        gameData,
        copy,histCopy,nextPla,
        toMoveBot,
        child,depth+1,maxDepth,newPlaAlwaysBest,newOppAlwaysBest,
        minVisitsAtNode,recordTreeTargetWeight,
        numNeuralNetChangesSoFar,
        locsBuf,playSelectionValuesBuf,
        Board::NULL_LOC,Board::NULL_LOC
      );
    }
  }
}

//Top-level caller for recursive func
static void recordTreePositions(
  FinishedGameData* gameData,
  const Board& board, const BoardHistory& hist, Player pla,
  const Search* toMoveBot,
  int64_t minVisitsAtNode, float recordTreeTargetWeight,
  int numNeuralNetChangesSoFar,
  vector<Loc>& locsBuf, vector<double>& playSelectionValuesBuf,
  Loc excludeLoc0, Loc excludeLoc1
) {
  assert(toMoveBot->rootBoard.pos_hash == board.pos_hash);
  assert(toMoveBot->rootHistory.moveHistory.size() == hist.moveHistory.size());
  assert(toMoveBot->rootPla == pla);
  assert(toMoveBot->rootNode != NULL);
  //Don't go too deep recording extra positions
  int maxDepth = 5;
  recordTreePositionsRec(
    gameData,
    board,hist,pla,
    toMoveBot,
    toMoveBot->rootNode, 0, maxDepth, true, true,
    minVisitsAtNode, recordTreeTargetWeight,
    numNeuralNetChangesSoFar,
    locsBuf,playSelectionValuesBuf,
    excludeLoc0,excludeLoc1
  );
}


struct SearchLimitsThisMove {
  bool doAlterVisitsPlayouts;
  int64_t numAlterVisits;
  int64_t numAlterPlayouts;
  bool clearBotBeforeSearchThisMove;
  bool removeRootNoise;
  float targetWeight;

  //Note: these two behave slightly differently than the ones in searchParams - derived from OtherGameProperties
  //game, they make the playouts *actually* vary instead of only making the neural net think they do.
  double playoutDoublingAdvantage;
  Player playoutDoublingAdvantagePla;

  Loc hintLoc;
};

static SearchLimitsThisMove getSearchLimitsThisMove(
  const Search* toMoveBot, Player pla, const PlaySettings& playSettings, Rand& gameRand,
  const vector<double>& historicalMctsWinLossValues,
  const vector<double>& historicalMctsDrawValues,
  bool clearBotBeforeSearch,
  const OtherGameProperties& otherGameProps
) {
  bool doAlterVisitsPlayouts = false;
  int64_t numAlterVisits = toMoveBot->searchParams.maxVisits;
  int64_t numAlterPlayouts = toMoveBot->searchParams.maxPlayouts;
  bool clearBotBeforeSearchThisMove = clearBotBeforeSearch;
  bool removeRootNoise = false;
  float targetWeight = 1.0f;
  double playoutDoublingAdvantage = 0.0;
  Player playoutDoublingAdvantagePla = C_EMPTY;
  Loc hintLoc = Board::NULL_LOC;
  double cheapSearchProb = playSettings.cheapSearchProb;

  const BoardHistory& hist = toMoveBot->getRootHist();
  if(otherGameProps.hintLoc != Board::NULL_LOC) {
    if(otherGameProps.hintTurn == hist.moveHistory.size() &&
       otherGameProps.hintPosHash == toMoveBot->getRootBoard().pos_hash) {
      hintLoc = otherGameProps.hintLoc;
      doAlterVisitsPlayouts = true;
      double cap = (double)((int64_t)1L << 50);
      numAlterVisits = (int64_t)ceil(std::min(cap, numAlterVisits * 4.0));
      numAlterPlayouts = (int64_t)ceil(std::min(cap, numAlterPlayouts * 4.0));
    }
  }
  //For the first few turns after a hint move or fork, reduce the probability of cheap search
  if((otherGameProps.hintLoc != Board::NULL_LOC ) && otherGameProps.hintTurn + 6 > hist.moveHistory.size()) {
    cheapSearchProb *= 0.5;
  }


  if(hintLoc == Board::NULL_LOC && cheapSearchProb > 0.0 && gameRand.nextBool(cheapSearchProb)) {
    if(playSettings.cheapSearchVisits <= 0)
      throw StringError("playSettings.cheapSearchVisits <= 0");
    if(playSettings.cheapSearchVisits > toMoveBot->searchParams.maxVisits ||
       playSettings.cheapSearchVisits > toMoveBot->searchParams.maxPlayouts)
      throw StringError("playSettings.cheapSearchVisits > maxVisits and/or maxPlayouts");

    doAlterVisitsPlayouts = true;
    numAlterVisits = std::min(numAlterVisits,(int64_t)playSettings.cheapSearchVisits);
    numAlterPlayouts = std::min(numAlterPlayouts,(int64_t)playSettings.cheapSearchVisits);
    targetWeight *= playSettings.cheapSearchTargetWeight;

    //If not recording cheap searches, do a few more things
    if(playSettings.cheapSearchTargetWeight <= 0.0) {
      clearBotBeforeSearchThisMove = false;
      removeRootNoise = true;
    }
  }
  else if(hintLoc == Board::NULL_LOC && playSettings.reduceVisits) {
    if(playSettings.reducedVisitsMin <= 0)
      throw StringError("playSettings.reducedVisitsMin <= 0");
    if(playSettings.reducedVisitsMin > toMoveBot->searchParams.maxVisits ||
       playSettings.reducedVisitsMin > toMoveBot->searchParams.maxPlayouts)
      throw StringError("playSettings.reducedVisitsMin > maxVisits and/or maxPlayouts");

    assert(historicalMctsWinLossValues.size() == historicalMctsDrawValues.size());
    if(historicalMctsWinLossValues.size() >= playSettings.reduceVisitsThresholdLookback) {
      double minWinLossValue = 1e20;
      double maxWinLossValue = -1e20;
      double minDrawValue = 1e20;
      for(int j = 0; j < playSettings.reduceVisitsThresholdLookback; j++) {
        double winLossValue = historicalMctsWinLossValues[historicalMctsWinLossValues.size() - 1 - j];
        if(winLossValue < minWinLossValue)
          minWinLossValue = winLossValue;
        if(winLossValue > maxWinLossValue)
          maxWinLossValue = winLossValue;

        double drawValue = historicalMctsDrawValues[historicalMctsWinLossValues.size() - 1 - j];
        if(drawValue < minDrawValue)
          minDrawValue = drawValue;
      }
      assert(playSettings.reduceVisitsThreshold >= 0.0);
      double signedMostExtreme = std::max(minWinLossValue,-maxWinLossValue);
      signedMostExtreme = std::max(signedMostExtreme, minDrawValue);
      assert(signedMostExtreme <= 1.000001);
      if(signedMostExtreme > 1.0)
        signedMostExtreme = 1.0;
      double amountThrough = signedMostExtreme - playSettings.reduceVisitsThreshold;
      if(amountThrough > 0) {
        double proportionThrough = amountThrough / (1.0 - playSettings.reduceVisitsThreshold);
        assert(proportionThrough >= 0.0 && proportionThrough <= 1.0);
        double visitReductionProp = proportionThrough * proportionThrough;
        doAlterVisitsPlayouts = true;
        numAlterVisits = (int64_t)round(numAlterVisits + visitReductionProp * ((double)playSettings.reducedVisitsMin - (double)numAlterVisits));
        numAlterPlayouts = (int64_t)round(numAlterPlayouts + visitReductionProp * ((double)playSettings.reducedVisitsMin - (double)numAlterPlayouts));
        targetWeight = (float)(targetWeight + visitReductionProp * (playSettings.reducedVisitsWeight - targetWeight));
        numAlterVisits = std::max(numAlterVisits,(int64_t)playSettings.reducedVisitsMin);
        numAlterPlayouts = std::max(numAlterPlayouts,(int64_t)playSettings.reducedVisitsMin);
      }
    }
  }

  if(otherGameProps.playoutDoublingAdvantage != 0.0 && otherGameProps.playoutDoublingAdvantagePla != C_EMPTY) {
    assert(pla == otherGameProps.playoutDoublingAdvantagePla || getOpp(pla) == otherGameProps.playoutDoublingAdvantagePla);

    playoutDoublingAdvantage = otherGameProps.playoutDoublingAdvantage;
    playoutDoublingAdvantagePla = otherGameProps.playoutDoublingAdvantagePla;

    double factor = pow(2.0, otherGameProps.playoutDoublingAdvantage);
    if(pla == otherGameProps.playoutDoublingAdvantagePla)
      factor = 2.0 * (factor / (factor + 1.0));
    else
      factor = 2.0 * (1.0 / (factor + 1.0));


    doAlterVisitsPlayouts = true;
    //Set this back to true - we need to always clear the search if we are doing asymmetric playouts
    clearBotBeforeSearchThisMove = true;
    numAlterVisits = (int64_t)round(numAlterVisits * factor);
    numAlterPlayouts = (int64_t)round(numAlterPlayouts * factor);

    //Hardcoded limit here to ensure sanity
    if(numAlterVisits < 5)
      throw StringError("ERROR: asymmetric playout doubling resulted in fewer than 5 visits");
    if(numAlterPlayouts < 5)
      throw StringError("ERROR: asymmetric playout doubling resulted in fewer than 5 playouts");
  }

  SearchLimitsThisMove limits;
  limits.doAlterVisitsPlayouts = doAlterVisitsPlayouts;
  limits.numAlterVisits = numAlterVisits;
  limits.numAlterPlayouts = numAlterPlayouts;
  limits.clearBotBeforeSearchThisMove = clearBotBeforeSearchThisMove;
  limits.removeRootNoise = removeRootNoise;
  limits.targetWeight = targetWeight;
  limits.playoutDoublingAdvantage = playoutDoublingAdvantage;
  limits.playoutDoublingAdvantagePla = playoutDoublingAdvantagePla;
  limits.hintLoc = hintLoc;
  return limits;
}

//Returns the move chosen
static Loc runBotWithLimits(
  Search* toMoveBot, Player pla, const PlaySettings& playSettings,
  const SearchLimitsThisMove& limits
) {
  if(limits.clearBotBeforeSearchThisMove)
    toMoveBot->clearSearch();

  Loc loc;

  //HACK - Disable LCB for making the move (it will still affect the policy target gen)
  bool lcb = toMoveBot->searchParams.useLcbForSelection;
  if(playSettings.forSelfPlay) {
    toMoveBot->searchParams.useLcbForSelection = false;
  }

  if(limits.doAlterVisitsPlayouts) {
    assert(limits.numAlterVisits > 0);
    assert(limits.numAlterPlayouts > 0);
    SearchParams oldParams = toMoveBot->searchParams;

    toMoveBot->searchParams.maxVisits = limits.numAlterVisits;
    toMoveBot->searchParams.maxPlayouts = limits.numAlterPlayouts;
    if(limits.removeRootNoise) {
      //Note - this is slightly sketchy to set the params directly. This works because
      //some of the parameters like FPU are basically stateless and will just affect future playouts
      //and because even stateful effects like rootNoiseEnabled and rootPolicyTemperature only affect
      //the root so when we step down in the tree we get a fresh start.
      toMoveBot->searchParams.rootNoiseEnabled = false;
      toMoveBot->searchParams.rootPolicyTemperature = 1.0;
      toMoveBot->searchParams.rootPolicyTemperatureEarly = 1.0;
      toMoveBot->searchParams.rootFpuLossProp = toMoveBot->searchParams.fpuLossProp;
      toMoveBot->searchParams.rootFpuReductionMax = toMoveBot->searchParams.fpuReductionMax;
      toMoveBot->searchParams.rootDesiredPerChildVisitsCoeff = 0.0;
      toMoveBot->searchParams.rootNumSymmetriesToSample = 1;
    }
    if(limits.playoutDoublingAdvantagePla != C_EMPTY) {
      toMoveBot->searchParams.playoutDoublingAdvantagePla = limits.playoutDoublingAdvantagePla;
      toMoveBot->searchParams.playoutDoublingAdvantage = limits.playoutDoublingAdvantage;
    }

    //If we cleared the search, do a very short search first to get a good dynamic score utility center
    if(limits.clearBotBeforeSearchThisMove && toMoveBot->searchParams.maxVisits > 10 && toMoveBot->searchParams.maxPlayouts > 10) {
      int64_t oldMaxVisits = toMoveBot->searchParams.maxVisits;
      toMoveBot->searchParams.maxVisits = 10;
      toMoveBot->runWholeSearchAndGetMove(pla);
      toMoveBot->searchParams.maxVisits = oldMaxVisits;
    }

    if(limits.hintLoc != Board::NULL_LOC) {
      assert(limits.clearBotBeforeSearchThisMove);
      //This will actually forcibly clear the search
      toMoveBot->setRootHintLoc(limits.hintLoc);
    }

    loc = toMoveBot->runWholeSearchAndGetMove(pla);

    if(limits.hintLoc != Board::NULL_LOC)
      toMoveBot->setRootHintLoc(Board::NULL_LOC);

    toMoveBot->searchParams = oldParams;
  }
  else {
    assert(!limits.removeRootNoise);
    loc = toMoveBot->runWholeSearchAndGetMove(pla);
  }

  //HACK - restore LCB so that it affects policy target gen
  if(playSettings.forSelfPlay) {
    toMoveBot->searchParams.useLcbForSelection = lcb;
  }

  return loc;
}


//Run a game between two bots. It is OK if both bots are the same bot.
FinishedGameData* Play::runGame(
  const Board& startBoard, Player pla, const BoardHistory& startHist, 
  MatchPairer::BotSpec& botSpecB, MatchPairer::BotSpec& botSpecW,
  const string& searchRandSeed,
  bool clearBotBeforeSearch,
  Logger& logger, bool logSearchInfo, bool logMoves,
  int maxMovesPerGame, const std::function<bool()>& shouldStop,
  const WaitableFlag* shouldPause,
  const PlaySettings& playSettings, const OtherGameProperties& otherGameProps,
  Rand& gameRand,
  std::function<NNEvaluator*()> checkForNewNNEval,
  std::function<void(const Board&, const BoardHistory&, Player, Loc, const std::vector<double>&,  const std::vector<double>&, const Search*)> onEachMove
) {
  Search* botB;
  Search* botW;
  if(botSpecB.botIdx == botSpecW.botIdx) {
    botB = new Search(botSpecB.baseParams, botSpecB.nnEval, &logger, searchRandSeed);
    botW = botB;
  }
  else {
    botB = new Search(botSpecB.baseParams, botSpecB.nnEval, &logger, searchRandSeed + "@B");
    botW = new Search(botSpecW.baseParams, botSpecW.nnEval, &logger, searchRandSeed + "@W");
  }

  FinishedGameData* gameData = runGame(
    startBoard, pla, startHist,
    botSpecB, botSpecW,
    botB, botW,
    clearBotBeforeSearch,
    logger, logSearchInfo, logMoves,
    maxMovesPerGame, shouldStop,
    shouldPause,
    playSettings, otherGameProps,
    gameRand,
    checkForNewNNEval,
    onEachMove
  );

  if(botW != botB)
    delete botW;
  delete botB;

  return gameData;
}

FinishedGameData* Play::runGame(
  const Board& startBoard, Player startPla, const BoardHistory& startHist,
  MatchPairer::BotSpec& botSpecB, MatchPairer::BotSpec& botSpecW,
  Search* botB, Search* botW,
  bool clearBotBeforeSearch,
  Logger& logger, bool logSearchInfo, bool logMoves,
  int maxMovesPerGame, const std::function<bool()>& shouldStop,
  const WaitableFlag* shouldPause,
  const PlaySettings& playSettings, const OtherGameProperties& otherGameProps,
  Rand& gameRand,
  std::function<NNEvaluator*()> checkForNewNNEval,
  std::function<void(const Board&, const BoardHistory&, Player, Loc, const std::vector<double>&, const std::vector<double>&, const Search*)> onEachMove
) {
  FinishedGameData* gameData = new FinishedGameData();

  Board board(startBoard);
  BoardHistory hist(startHist);
  Player pla = startPla;
  assert(!(playSettings.forSelfPlay && !clearBotBeforeSearch));

  gameData->bName = botSpecB.botName;
  gameData->wName = botSpecW.botName;
  gameData->bIdx = botSpecB.botIdx;
  gameData->wIdx = botSpecW.botIdx;

  gameData->gameHash.hash0 = gameRand.nextUInt64();
  gameData->gameHash.hash1 = gameRand.nextUInt64();

  gameData->noResultUtilityForWhite = botSpecB.baseParams.noResultUtilityForWhite;
  gameData->playoutDoublingAdvantagePla = otherGameProps.playoutDoublingAdvantagePla;
  gameData->playoutDoublingAdvantage = otherGameProps.playoutDoublingAdvantage;

  gameData->mode = FinishedGameData::MODE_NORMAL;
  gameData->usedInitialPosition = 0;

  //Might get overwritten next as we also play sgfposes and such with asym mode!
  //So this is just a best efforts to make it more prominent for most of the asymmetric games.
  if(gameData->playoutDoublingAdvantage != 0)
    gameData->mode = FinishedGameData::MODE_ASYM;

  if(otherGameProps.isSgfPos)
    gameData->mode = FinishedGameData::MODE_SGFPOS;
  if(otherGameProps.isHintPos)
    gameData->mode = FinishedGameData::MODE_HINTPOS;

  //In selfplay, record all the policy maps and evals and such as well for training data
  bool recordFullData = playSettings.forSelfPlay;

  //NOTE: that checkForNewNNEval might also cause the old nnEval to be invalidated and freed. This is okay since the only
  //references we both hold on to and use are the ones inside the bots here, and we replace the ones in the botSpecs.
  //We should NOT ever store an nnEval separately from these.
  auto maybeCheckForNewNNEval = [&botB,&botW,&botSpecB,&botSpecW,&checkForNewNNEval,&gameRand,&gameData](int nextTurnIdx) {
    //Check if we got a new nnEval, with some probability.
    //Randomized and low-probability so as to reduce contention in checking, while still probably happening in a timely manner.
    if(checkForNewNNEval != nullptr && gameRand.nextBool(0.1)) {
      NNEvaluator* newNNEval = checkForNewNNEval();
      if(newNNEval != NULL) {
        botB->setNNEval(newNNEval);
        if(botW != botB)
          botW->setNNEval(newNNEval);
        botSpecB.nnEval = newNNEval;
        botSpecW.nnEval = newNNEval;
        gameData->changedNeuralNets.push_back(new ChangedNeuralNet(newNNEval->getModelName(),nextTurnIdx));
      }
    }
  };
    
  //Set in the starting board and history to gameData and both bots
  gameData->startBoard = board;
  gameData->startHist = hist;
  gameData->startPla = pla;

  botB->setPosition(pla,board,hist);
  if(botB != botW)
    botW->setPosition(pla,board,hist);

  vector<Loc> locsBuf;
  vector<double> playSelectionValuesBuf;

  vector<SidePosition*> sidePositionsToSearch;

  vector<double> historicalMctsWinLossValues;
  vector<double> historicalMctsDrawValues;
  vector<ReportedSearchValues> rawNNValues;

  ClockTimer timer;

  bool drawEarlyEndGame = playSettings.allowEarlyDraw && (!playSettings.forSelfPlay || gameRand.nextBool(playSettings.earlyDrawProbSelfplay));

  //Main play loop

  auto start = std::chrono::steady_clock::now();
  int start_num = hist.getCurrentTurnNumber();
  //for(int i = 0; i < maxMovesPerGame; i++) {
  for(int i = start_num; i < maxMovesPerGame; i++) {
    if(hist.isGameFinished)
      break;
    if(shouldPause != nullptr)
      shouldPause->waitUntilFalse();
    if(shouldStop != nullptr && shouldStop())
      break;

    Search* toMoveBot = pla == P_BLACK ? botB : botW;
 
    SearchLimitsThisMove limits = getSearchLimitsThisMove(
      toMoveBot,
      pla,
      playSettings,
      gameRand,
      historicalMctsWinLossValues,
      historicalMctsDrawValues,
      clearBotBeforeSearch,
      otherGameProps
    );
    Loc loc;
    if(playSettings.recordTimePerMove) {
      double t0 = timer.getSeconds();
      loc = runBotWithLimits(toMoveBot, pla, playSettings, limits);
      double t1 = timer.getSeconds();
      if(pla == P_BLACK)
        gameData->bTimeUsed += t1-t0;
      else
        gameData->wTimeUsed += t1-t0;
    }
    else {
      loc = runBotWithLimits(toMoveBot, pla, playSettings, limits);
    }

    if(pla == P_BLACK)
      gameData->bMoveCount += 1;
    else
      gameData->wMoveCount += 1;

    if(loc == Board::NULL_LOC || !toMoveBot->isLegalStrict(loc,pla))
      failIllegalMove(toMoveBot,logger,board,loc);
    if(logSearchInfo)
      logSearch(toMoveBot,logger,loc,otherGameProps);
    if(logMoves)
      logger.write("Move " + Global::uint64ToString(hist.moveHistory.size()) + " made: " + Location::toString(loc,board));

    ValueTargets whiteValueTargets;
    extractValueTargets(whiteValueTargets, toMoveBot, toMoveBot->rootNode);
    gameData->whiteValueTargetsByTurn.push_back(whiteValueTargets);
    QValueTargets whiteQValueTargets;
    extractQValueTargets(whiteQValueTargets.targets, toMoveBot, toMoveBot->rootNode);
    gameData->whiteQValueTargetsByTurn.push_back(whiteQValueTargets);

    if(!recordFullData) {
      //Go ahead and record this anyways with just the visits, as a bit of a hack so that the sgf output can also write the number of visits.
      int64_t unreducedNumVisits = toMoveBot->getRootVisits();
      gameData->policyTargetsByTurn.push_back(PolicyTarget(NULL,unreducedNumVisits));
    }
    else {
      vector<PolicyTargetMove>* policyTarget = new vector<PolicyTargetMove>();
      int64_t unreducedNumVisits = toMoveBot->getRootVisits();
      Play::extractPolicyTarget(*policyTarget, toMoveBot, toMoveBot->rootNode, locsBuf, playSelectionValuesBuf);
      gameData->policyTargetsByTurn.push_back(PolicyTarget(policyTarget,unreducedNumVisits));
      gameData->nnRawStatsByTurn.push_back(computeNNRawStats(toMoveBot, board, hist, pla));

      gameData->targetWeightByTurn.push_back(limits.targetWeight);

      double policySurprise = 0.0, policyEntropy = 0.0, searchEntropy = 0.0;
      bool success = toMoveBot->getPolicySurpriseAndEntropy(policySurprise, searchEntropy, policyEntropy);
      testAssert(success);
      (void)success; //Avoid warning when asserts are disabled
      gameData->policySurpriseByTurn.push_back(policySurprise);
      gameData->policyEntropyByTurn.push_back(policyEntropy);
      gameData->searchEntropyByTurn.push_back(searchEntropy);

      rawNNValues.push_back(toMoveBot->getRootRawNNValuesRequireSuccess());

      //Occasionally fork off some positions to evaluate
      Loc sidePositionForkLoc = Board::NULL_LOC;
      if(playSettings.sidePositionProb > 0.0 && gameRand.nextBool(playSettings.sidePositionProb)) {
        assert(toMoveBot->rootNode != NULL);
        const NNOutput* nnOutput = toMoveBot->rootNode->getNNOutput();
        assert(nnOutput != NULL);
        Loc banMove = loc;
        sidePositionForkLoc = chooseRandomForkingMove(nnOutput, board, hist, pla, gameRand, banMove);
        if(sidePositionForkLoc != Board::NULL_LOC) {
          SidePosition* sp = new SidePosition(board,hist,pla,(int)gameData->changedNeuralNets.size());
          sp->hist.makeBoardMoveAssumeLegal(sp->board,sidePositionForkLoc,sp->pla);
          sp->pla = getOpp(sp->pla);
          if(sp->hist.isGameFinished) delete sp;
          else sidePositionsToSearch.push_back(sp);
        }
      }

      //If enabled, also record subtree positions from the search as training positions
      if(playSettings.recordTreePositions && playSettings.recordTreeTargetWeight > 0.0f) {
        if(playSettings.recordTreeTargetWeight > 1.0f)
          throw StringError("playSettings.recordTreeTargetWeight > 1.0f");

        recordTreePositions(
          gameData,
          board,hist,pla,
          toMoveBot,
          playSettings.recordTreeThreshold,playSettings.recordTreeTargetWeight,
          (int)gameData->changedNeuralNets.size(),
          locsBuf,playSelectionValuesBuf,
          loc,sidePositionForkLoc
        );
      }
    }

    if(playSettings.allowResignation || playSettings.reduceVisits || playSettings.allowEarlyDraw) {
      ReportedSearchValues values = toMoveBot->getRootValuesRequireSuccess();
      historicalMctsWinLossValues.push_back(values.winLossValue);
      historicalMctsDrawValues.push_back(values.noResultValue);
    }

    if(onEachMove != nullptr)
      onEachMove(board, hist, pla, loc, historicalMctsWinLossValues, historicalMctsDrawValues, toMoveBot);
      //onEachMove(board, hist, pla, loc, historicalMctsWinLossValues, historicalMctsWinLossValues, toMoveBot);

    //Finally, make the move on the bots
    bool suc;
    suc = botB->makeMove(loc,pla);
    testAssert(suc);
    if(botB != botW) {
      suc = botW->makeMove(loc,pla);
      testAssert(suc);
    }
    (void)suc; //Avoid warning when asserts disabled

    //And make the move on our copy of the board
    testAssert(hist.isLegal(board,loc,pla));
    hist.makeBoardMoveAssumeLegal(board,loc,pla);

    //Check for resignation
    if(playSettings.allowResignation && historicalMctsWinLossValues.size() >= playSettings.resignConsecTurns) {
      //Play at least some moves no matter what
      int minTurnForResignation = 1 + board.x_size * board.y_size / 5;
      if(i >= minTurnForResignation) {
        if(playSettings.resignThreshold > 0 || std::isnan(playSettings.resignThreshold))
          throw StringError("playSettings.resignThreshold > 0 || std::isnan(playSettings.resignThreshold)");

        bool shouldResign = true;
        for(int j = 0; j<playSettings.resignConsecTurns; j++) {
          double winLossValue = historicalMctsWinLossValues[historicalMctsWinLossValues.size()-j-1];
          Player resignPlayerThisTurn = C_EMPTY;
          if(winLossValue < playSettings.resignThreshold)
            resignPlayerThisTurn = P_WHITE;
          else if(winLossValue > -playSettings.resignThreshold)
            resignPlayerThisTurn = P_BLACK;

          if(resignPlayerThisTurn != pla) {
            shouldResign = false;
            break;
          }
        }

        if(shouldResign)
          hist.setWinnerByResignation(getOpp(pla));
      }
    }

    //Check for drawEarlyEndGame
    if(drawEarlyEndGame && historicalMctsDrawValues.size() >= playSettings.earlyDrawConsecTurns) {
      bool shouldEndGameDraw = true;
      for(int j = 0; j < playSettings.earlyDrawConsecTurns; j++) {
        if(historicalMctsDrawValues[historicalMctsDrawValues.size() - j - 1] < playSettings.earlyDrawThreshold)
          shouldEndGameDraw = false;
      }
      if(shouldEndGameDraw)
        hist.setWinner(C_EMPTY);
    }

    testAssert(hist.moveHistory.size() < 0x1FFFffff);
    int nextTurnIdx = (int)hist.moveHistory.size();
    maybeCheckForNewNNEval(nextTurnIdx);

    pla = getOpp(pla);
  }
  if(extra_time_log) {
    auto finish = std::chrono::steady_clock::now();
    auto time = std::chrono::duration_cast<std::chrono::milliseconds>(finish - start);
    std::cout << "Play::runGame : " << hist.getMoves(board.x_size, board.y_size) << " add "
              << (hist.getCurrentTurnNumber() - start_num) << " moves for " << time.count() << " ms"
              << ", " << time.count() / (hist.getCurrentTurnNumber() - start_num) << " ms/move" << std::endl;
  }

  gameData->endHist = hist;
  if(hist.isGameFinished)
    gameData->hitTurnLimit = false;
  else
    gameData->hitTurnLimit = true;


  if(recordFullData) {
    if(hist.isResignation)
      throw StringError("Recording full data currently incompatible with resignation");

    ValueTargets finalValueTargets;

    if(hist.isGameFinished) {
      
      finalValueTargets.win = 0.0f;
      finalValueTargets.loss = 0.0f;
      finalValueTargets.noResult = 0.0f;

      if(hist.winner == C_WHITE)
        finalValueTargets.win = 1.0f;
      else if(hist.winner == C_BLACK)
        finalValueTargets.loss = 1.0f;
      else 
        finalValueTargets.noResult = 1.0f;
    }
    gameData->whiteValueTargetsByTurn.push_back(finalValueTargets);

    //If we had a hintloc, then don't trust the first value, it will be corrupted a bit by the forced playouts.
    //Just copy the next turn's value.
    if(otherGameProps.hintLoc != Board::NULL_LOC) {
      gameData->whiteValueTargetsByTurn[0] = gameData->whiteValueTargetsByTurn[std::min((size_t)1,gameData->whiteValueTargetsByTurn.size()-1)];
    }


    gameData->hasFullData = true;

    vector<double> valueSurpriseByTurn;
    {
      const vector<ValueTargets>& whiteValueTargetsByTurn = gameData->whiteValueTargetsByTurn;
      assert(whiteValueTargetsByTurn.size() == gameData->targetWeightByTurn.size() + 1);
      assert(rawNNValues.size() == gameData->targetWeightByTurn.size());
      valueSurpriseByTurn.resize(rawNNValues.size());

      int boardArea = board.x_size * board.y_size;
      double nowFactor = 1.0/(1.0 + boardArea * 0.016);

      double winValue = whiteValueTargetsByTurn[whiteValueTargetsByTurn.size()-1].win;
      double lossValue = whiteValueTargetsByTurn[whiteValueTargetsByTurn.size()-1].loss;
      double noResultValue = whiteValueTargetsByTurn[whiteValueTargetsByTurn.size()-1].noResult;
      for(int i = (int)rawNNValues.size()-1; i >= 0; i--) {
        winValue = winValue + nowFactor * (whiteValueTargetsByTurn[i].win - winValue);
        lossValue = lossValue + nowFactor * (whiteValueTargetsByTurn[i].loss - lossValue);
        noResultValue = noResultValue + nowFactor * (whiteValueTargetsByTurn[i].noResult - noResultValue);

        double valueSurprise = 0.0;
        if(winValue > 1e-100) valueSurprise += winValue * (log(winValue) - log(std::max((double)rawNNValues[i].winValue,1e-100)));
        if(lossValue > 1e-100) valueSurprise += lossValue * (log(lossValue) - log(std::max((double)rawNNValues[i].lossValue,1e-100)));
        if(noResultValue > 1e-100) valueSurprise += noResultValue * (log(noResultValue) - log(std::max((double)rawNNValues[i].noResultValue,1e-100)));

        //Just in case, guard against float imprecision
        if(valueSurprise < 0.0)
          valueSurprise = 0.0;
        //Cap value surprise at extreme value, to reduce the chance of a ridiculous weight on a move.
        valueSurpriseByTurn[i] = std::min(valueSurprise,1.0);
      }
    }

    //Compute desired expectation with which to write main game rows
    if(playSettings.policySurpriseDataWeight > 0 || playSettings.valueSurpriseDataWeight > 0) {
      size_t numWeights = gameData->targetWeightByTurn.size();
      assert(numWeights == gameData->policySurpriseByTurn.size());

      double sumWeights = 0.0;
      double sumPolicySurpriseWeighted = 0.0;
      double sumValueSurpriseWeighted = 0.0;
      for(size_t i = 0; i < numWeights; i++) {
        float targetWeight = gameData->targetWeightByTurn[i];
        assert(targetWeight >= 0.0 && targetWeight <= 1.0);
        sumWeights += targetWeight;
        double policySurprise = gameData->policySurpriseByTurn[i];
        assert(policySurprise >= 0.0);
        double valueSurprise = valueSurpriseByTurn[i];
        assert(valueSurprise >= 0.0);
        sumPolicySurpriseWeighted += policySurprise * targetWeight;
        sumValueSurpriseWeighted += valueSurprise * targetWeight;
      }

      if(sumWeights >= 1) {
        double averagePolicySurpriseWeighted = sumPolicySurpriseWeighted / sumWeights;
        double averageValueSurpriseWeighted = sumValueSurpriseWeighted / sumWeights;

        //It's possible that we have very little value surprise, such as if the game was initialized lopsided and never again changed
        //from that and the expected player won. So if the total value surprise on targetWeighted turns is too small, then also don't
        //do much valueSurpriseDataWeight, since it would be basically dividing by almost zero, in potentially weird ways.
        double valueSurpriseDataWeight = playSettings.valueSurpriseDataWeight;
        if(averageValueSurpriseWeighted < 0.010) { //0.010 logits on average, pretty arbitrary, mainly just intended limit to extreme cases.
          valueSurpriseDataWeight *= averageValueSurpriseWeighted / 0.010;
        }

        //We also include some rows from non-full searches, if despite the shallow search
        //they were quite surprising to the policy.
        double thresholdToIncludeReduced = averagePolicySurpriseWeighted * 1.5;

        //Part of the weight will be proportional to surprisePropValue which is just policySurprise on normal rows
        //and the excess policySurprise beyond threshold on shallow searches.
        //First pass - we sum up the surpriseValue.
        double sumPolicySurprisePropValue = 0.0;
        double sumValueSurprisePropValue = 0.0;
        for(int i = 0; i<numWeights; i++) {
          float targetWeight = gameData->targetWeightByTurn[i];
          double policySurprise = gameData->policySurpriseByTurn[i];
          double valueSurprise = valueSurpriseByTurn[i];
          double policySurprisePropValue =
            targetWeight * policySurprise + (1-targetWeight) * std::max(0.0,policySurprise-thresholdToIncludeReduced);
          double valueSurprisePropValue =
            targetWeight * valueSurprise;
          sumPolicySurprisePropValue += policySurprisePropValue;
          sumValueSurprisePropValue += valueSurprisePropValue;
        }

        //Just in case, avoid div by 0
        sumPolicySurprisePropValue = std::max(sumPolicySurprisePropValue,1e-10);
        sumValueSurprisePropValue = std::max(sumValueSurprisePropValue,1e-10);

        for(int i = 0; i<numWeights; i++) {
          float targetWeight = gameData->targetWeightByTurn[i];
          double policySurprise = gameData->policySurpriseByTurn[i];
          double valueSurprise = valueSurpriseByTurn[i];
          double policySurprisePropValue =
            targetWeight * policySurprise + (1-targetWeight) * std::max(0.0,policySurprise-thresholdToIncludeReduced);
          double valueSurprisePropValue =
            targetWeight * valueSurprise;
          double newValue =
            (1.0-playSettings.policySurpriseDataWeight-valueSurpriseDataWeight) * targetWeight
            + playSettings.policySurpriseDataWeight * policySurprisePropValue * sumWeights / sumPolicySurprisePropValue
            + valueSurpriseDataWeight * valueSurprisePropValue * sumWeights / sumValueSurprisePropValue;
          gameData->targetWeightByTurn[i] = (float)(newValue);
        }
      }
    }

    //Also evaluate all the side positions as well that we queued up to be searched
    NNResultBuf nnResultBuf;
    for(int i = 0; i<sidePositionsToSearch.size(); i++) {
      SidePosition* sp = sidePositionsToSearch[i];

      if(shouldPause != nullptr)
        shouldPause->waitUntilFalse();
      if(shouldStop != nullptr && shouldStop()) {
        delete sp;
        continue;
      }

      Search* toMoveBot = sp->pla == P_BLACK ? botB : botW;
      toMoveBot->setPosition(sp->pla,sp->board,sp->hist);
      //We do NOT apply playoutDoublingAdvantage here. If changing this, note that it is coordinated with train data writing
      //not using playoutDoublingAdvantage for these rows too.
      Loc responseLoc = toMoveBot->runWholeSearchAndGetMove(sp->pla);

      Play::extractPolicyTarget(sp->policyTarget, toMoveBot, toMoveBot->rootNode, locsBuf, playSelectionValuesBuf);
      extractValueTargets(sp->whiteValueTargets, toMoveBot, toMoveBot->rootNode);
      extractQValueTargets(sp->whiteQValueTargets.targets, toMoveBot, toMoveBot->rootNode);

      double policySurprise = 0.0, policyEntropy = 0.0, searchEntropy = 0.0;
      bool success = toMoveBot->getPolicySurpriseAndEntropy(policySurprise, searchEntropy, policyEntropy);
      testAssert(success);
      (void)success; //Avoid warning when asserts are disabled
      sp->policySurprise = policySurprise;
      sp->policyEntropy = policyEntropy;
      sp->searchEntropy = searchEntropy;

      sp->nnRawStats = computeNNRawStats(toMoveBot, sp->board, sp->hist, sp->pla);
      sp->targetWeight = 1.0f;
      sp->unreducedNumVisits = toMoveBot->getRootVisits();
      sp->numNeuralNetChangesSoFar = (int)gameData->changedNeuralNets.size();

      gameData->sidePositions.push_back(sp);

      //If enabled, also record subtree positions from the search as training positions
      if(playSettings.recordTreePositions && playSettings.recordTreeTargetWeight > 0.0f) {
        if(playSettings.recordTreeTargetWeight > 1.0f)
          throw StringError("playSettings.recordTreeTargetWeight > 1.0f");
        recordTreePositions(
          gameData,
          sp->board,sp->hist,sp->pla,
          toMoveBot,
          playSettings.recordTreeThreshold,playSettings.recordTreeTargetWeight,
          (int)gameData->changedNeuralNets.size(),
          locsBuf,playSelectionValuesBuf,
          Board::NULL_LOC, Board::NULL_LOC
        );
      }

      //Occasionally continue the fork a second move or more, to provide some situations where the opponent has played "weird" moves not
      //only on the most immediate turn, but rather the turns before.
      if(gameRand.nextBool(0.25)) {
        if(responseLoc == Board::NULL_LOC || !sp->hist.isLegal(sp->board,responseLoc,sp->pla))
          failIllegalMove(toMoveBot,logger,sp->board,responseLoc);

        SidePosition* sp2 = new SidePosition(sp->board,sp->hist,sp->pla,(int)gameData->changedNeuralNets.size());
        sp2->hist.makeBoardMoveAssumeLegal(sp2->board,responseLoc,sp2->pla);
        sp2->pla = getOpp(sp2->pla);
        if(sp2->hist.isGameFinished)
          delete sp2;
        else {
          Search* toMoveBot2 = sp2->pla == P_BLACK ? botB : botW;
          MiscNNInputParams nnInputParams;
          nnInputParams.noResultUtilityForWhite = toMoveBot2->searchParams.noResultUtilityForWhite;
          toMoveBot2->nnEvaluator->evaluate(
            sp2->board,sp2->hist,sp2->pla,nnInputParams,
            nnResultBuf,false
          );
          Loc banMove = Board::NULL_LOC;
          Loc forkLoc = chooseRandomForkingMove(nnResultBuf.result.get(), sp2->board, sp2->hist, sp2->pla, gameRand, banMove);
          if(forkLoc != Board::NULL_LOC) {
            sp2->hist.makeBoardMoveAssumeLegal(sp2->board,forkLoc,sp2->pla);
            sp2->pla = getOpp(sp2->pla);
            if(sp2->hist.isGameFinished) delete sp2;
            else sidePositionsToSearch.push_back(sp2);
          }
        }
      }

      testAssert(gameData->endHist.moveHistory.size() < 0x1FFFffff);
      maybeCheckForNewNNEval((int)gameData->endHist.moveHistory.size());
    }

    if(playSettings.scaleDataWeight != 1.0) {
      for(int i = 0; i<gameData->targetWeightByTurn.size(); i++)
        gameData->targetWeightByTurn[i] = (float)(playSettings.scaleDataWeight * gameData->targetWeightByTurn[i]);
      for(int i = 0; i<gameData->sidePositions.size(); i++)
        gameData->sidePositions[i]->targetWeight = (float)(playSettings.scaleDataWeight * gameData->sidePositions[i]->targetWeight);
    }

    //Record weights before we possibly probabilistically resolve them
    {
      gameData->targetWeightByTurnUnrounded.resize(gameData->targetWeightByTurn.size());
      for(int i = 0; i<gameData->targetWeightByTurn.size(); i++)
        gameData->targetWeightByTurnUnrounded[i] = gameData->targetWeightByTurn[i];
      for(int i = 0; i<gameData->sidePositions.size(); i++)
        gameData->sidePositions[i]->targetWeightUnrounded = gameData->sidePositions[i]->targetWeight;
    }

    //Resolve probabilistic weights of things
    //Do this right now so that if something isn't included at all, we can skip some work, like lead estmation.
    if(!playSettings.noResolveTargetWeights) {
      auto resolveWeight = [&gameRand](float weight){
        if(weight <= 0) weight = 0;
        float floored = floor(weight);
        float excess = weight - floored;
        weight = gameRand.nextBool(excess) ? floored+1 : floored;
        return weight;
      };

      for(int i = 0; i<gameData->targetWeightByTurn.size(); i++)
        gameData->targetWeightByTurn[i] = resolveWeight(gameData->targetWeightByTurn[i]);
      for(int i = 0; i<gameData->sidePositions.size(); i++)
        gameData->sidePositions[i]->targetWeight = resolveWeight(gameData->sidePositions[i]->targetWeight);
    }
  }
  
  gameData->trainingWeight = otherGameProps.trainingWeight;
  return gameData;
}

static void replayGameUpToMove(const FinishedGameData* finishedGameData, int moveIdx, Rules rules, Board& board, BoardHistory& hist, Player& pla) {
  board = finishedGameData->startHist.initialBoard;
  pla = finishedGameData->startHist.initialPla;

  hist.clear(board,pla,rules);
  //Make sure it's prior to the last move
  if(finishedGameData->endHist.moveHistory.size() <= 0)
    return;
  moveIdx = std::min(moveIdx,(int)(finishedGameData->endHist.moveHistory.size()-1));

  //Replay all those moves
  for(int i = 0; i<moveIdx; i++) {
    Loc loc = finishedGameData->endHist.moveHistory[i].loc;
    if(!hist.isLegal(board,loc,pla)) {
      //We have a bug of some sort if we got an illegal move on replay, unless
      //we are in encore phase (pass for ko may change) or the rules are different
      if(rules == finishedGameData->startHist.rules) {
        cout << board << endl;
        cout << PlayerIO::colorToChar(pla) << endl;
        cout << Location::toString(loc,board) << endl;
        hist.printDebugInfo(cout,board);
        cout << endl;
        throw StringError("Illegal move when replaying to fork game?");
      }
      //Just break out due to the illegal move and stop the replay here
      return;
    }
    assert(finishedGameData->endHist.moveHistory[i].pla == pla);
    hist.makeBoardMoveAssumeLegal(board,loc,pla);
    pla = getOpp(pla);

    if(hist.isGameFinished)
      return;
  }
}


GameRunner::GameRunner(ConfigParser& cfg, PlaySettings pSettings, Logger& logger)
  :logSearchInfo(),logMoves(),maxMovesPerGame(),clearBotBeforeSearch(),
   playSettings(pSettings),
   gameInit(NULL)
{
  logSearchInfo = cfg.getBool("logSearchInfo");
  logMoves = cfg.getBool("logMoves");
  maxMovesPerGame = cfg.getInt("maxMovesPerGame",0,1 << 30);
  clearBotBeforeSearch = cfg.contains("clearBotBeforeSearch") ? cfg.getBool("clearBotBeforeSearch") : false;

  //Initialize object for randomizing game settings
  gameInit = new GameInitializer(cfg,logger);
  loadOpenings(playSettings.fileOpenings, playSettings.logOpenings);
}

GameRunner::GameRunner(ConfigParser& cfg, const string& gameInitRandSeed, PlaySettings pSettings, Logger& logger)
  :logSearchInfo(),logMoves(),maxMovesPerGame(),clearBotBeforeSearch(),
   playSettings(pSettings),
   gameInit(NULL)
{
  logSearchInfo = cfg.getBool("logSearchInfo");
  logMoves = cfg.getBool("logMoves");
  maxMovesPerGame = cfg.getInt("maxMovesPerGame",0,1 << 30);
  clearBotBeforeSearch = cfg.contains("clearBotBeforeSearch") ? cfg.getBool("clearBotBeforeSearch") : false;

  //Initialize object for randomizing game settings
  gameInit = new GameInitializer(cfg,logger,gameInitRandSeed);
  loadOpenings(playSettings.fileOpenings, playSettings.logOpenings);
}

GameRunner::~GameRunner() {
  delete gameInit;
}

