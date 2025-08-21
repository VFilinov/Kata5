#include "../core/global.h"
#include "../core/commandloop.h"
#include "../core/config_parser.h"
#include "../core/fileutils.h"
#include "../core/timer.h"
#include "../core/datetime.h"
#include "../core/makedir.h"
#include "../core/test.h"
#include "../dataio/sgf.h"
#include "../search/searchnode.h"
#include "../search/asyncbot.h"
#include "../search/patternbonustable.h"
#include "../program/setup.h"
#include "../program/playutils.h"
#include "../program/play.h"
#include "../tests/tests.h"
#include "../command/commandline.h"
#include "../main.h"

extern bool brain;

using namespace std;

static bool tryParseLoc(const string& s, const Board& b, Loc& loc) {
  return Location::tryOfString(s,b,loc);
}

//Filter out all double newlines, since double newline terminates GTP command responses
static string filterDoubleNewlines(const string& s) {
  string filtered;
  for(int i = 0; i<s.length(); i++) {
    if(i > 0 && s[i-1] == '\n' && s[i] == '\n')
      continue;
    filtered += s[i];
  }
  return filtered;
}

static bool timeIsValid(const double& time) {
  if(isnan(time) || time < 0.0 || time > TimeControls::MAX_USER_INPUT_TIME)
    return false;
  return true;
}
static bool timeIsValidAllowNegative(const double& time) {
  if(isnan(time) || time < -TimeControls::MAX_USER_INPUT_TIME || time > TimeControls::MAX_USER_INPUT_TIME)
    return false;
  return true;
}

static double parseTime(const vector<string>& args, int argIdx, const string& description) {
  double time = 0.0;
  if(args.size() <= argIdx || !Global::tryStringToDouble(args[argIdx],time))
    throw StringError("Expected float for " + description + " as argument " + Global::intToString(argIdx));
  if(!timeIsValid(time))
    throw StringError(description + " is an invalid value: " + args[argIdx]);
  return time;
}
static double parseTimeAllowNegative(const vector<string>& args, int argIdx, const string& description) {
  double time = 0.0;
  if(args.size() <= argIdx || !Global::tryStringToDouble(args[argIdx],time))
    throw StringError("Expected float for " + description + " as argument " + Global::intToString(argIdx));
  if(!timeIsValidAllowNegative(time))
    throw StringError(description + " is an invalid value: " + args[argIdx]);
  return time;
}
static bool noWhiteStonesOnBoard(const Board& board) {
  for(int y = 0; y < board.y_size; y++) {
    for(int x = 0; x < board.x_size; x++) {
      Loc loc = Location::getLoc(x, y, board.x_size);
      if(board.colors[loc] == P_WHITE)
        return false;
    }
  }
  return true;
}
static bool getTwoRandomMove(const Board& board, Loc& whiteLoc, Loc& blackLoc, string& responseMoves) {
  if(board.numStonesOnBoard() != 3)
    return false;
  double x1, x2, x3, y1, y2, y3; 
  int c = 0;
  
  for(int x = 0; x < board.x_size; x++)
    for(int y = 0; y < board.y_size; y++) {
      Loc loc = Location::getLoc(x, y, board.x_size);
      if(board.colors[loc] != C_EMPTY) {
        c++;
        if(c == 1) {
          x1 = x;
          y1 = y;
        } else if(c == 2) {
          x2 = x;
          y2 = y;
        } else if(c == 3) {
          x3 = x;
          y3 = y;
        }
      }
    }
  if(c != 3)
    return false;

  double values[Board::MAX_LEN][Board::MAX_LEN];

  for(int x = 0; x < board.x_size; x++)
    for(int y = 0; y < board.y_size; y++) {
      Loc loc = Location::getLoc(x, y, board.x_size);
      if((x == 0 || y == 0 || x == board.x_size || y == board.y_size) && board.colors[loc] == C_EMPTY)
        values[x][y] = -1.0 / sqrt((x - x1) * (x - x1) + (y - y1) * (y - y1)) -
                       1.0 / sqrt((x - x2) * (x - x2) + (y - y2) * (y - y2)) -
                       1.0 / sqrt((x - x3) * (x - x3) + (y - y3) * (y - y3));
      else
        values[x][y] = -1e32;
    }
  double bestValue;
  int bestX, bestY;

  bestValue = -1e30;
  bestX = -1;
  bestY = -1;
  for(int x = 0; x < board.x_size; x++)
    for(int y = 0; y < board.y_size; y++) {
      if(values[x][y] > bestValue) {
        bestValue = values[x][y];
        bestX = x;
        bestY = y;
      }
    }
  values[bestX][bestY] = -1e31;
  bestValue = -1e30;
  bestX = -1;
  bestY = -1;
  for(int x = 0; x < board.x_size; x++)
    for(int y = 0; y < board.y_size; y++) {
      if(values[x][y] > bestValue) {
        bestValue = values[x][y];
        bestX = x;
        bestY = y;
      }
    }
  values[bestX][bestY] = -1e31;
  bestValue = -1e30;
  bestX = -1;
  bestY = -1;
  for(int x = 0; x < board.x_size; x++)
    for(int y = 0; y < board.y_size; y++) {
      if(values[x][y] > bestValue) {
        bestValue = values[x][y];
        bestX = x;
        bestY = y;
      }
    }
  values[bestX][bestY] = -1e31;
  whiteLoc = Location::getLoc(bestX, bestY, 15);
  responseMoves = to_string(bestX) + "," + to_string(bestY);
  bestValue = -1e30;
  bestX = -1;
  bestY = -1;
  for(int x = 0; x < board.x_size; x++)
    for(int y = 0; y < board.y_size; y++) {
      if(values[x][y] > bestValue) {
        bestValue = values[x][y];
        bestX = x;
        bestY = y;
      }
    }
  values[bestX][bestY] = -1e31;
  blackLoc = Location::getLoc(bestX, bestY, board.x_size);
  responseMoves = responseMoves + " " + to_string(bestX) + "," + to_string(bestY);
  return true;
}



struct GomEngine {
  GomEngine(const GomEngine&) = delete;
  GomEngine& operator=(const GomEngine&) = delete;

  const string nnModelFile;
  const string humanModelFile;
  const int analysisPVLen;

  const bool autoAvoidPatterns;
  double normalAvoidRepeatedPatternUtility;

  int logPVCoordinatesMode;

  NNEvaluator* nnEval;
  NNEvaluator* humanEval;
  AsyncBot* bot;
  Rules currentRules; //Should always be the same as the rules in bot, if bot is not NULL.

  // Stores the params we want to be using during genmoves or analysis
  SearchParams genmoveParams;
  SearchParams analysisParams;
  bool isGenmoveParams;

  TimeControls bTimeControls;
  TimeControls wTimeControls;

  //This move history doesn't get cleared upon consecutive moves by the same side, and is used
  //for undo, whereas the one in search does.
  Board initialBoard;
  Player initialPla;
  vector<Move> moveHistory;

  vector<double> recentWinLossValues;
  double lastSearchFactor;
  std::unique_ptr<PatternBonusTable> patternBonusTable;

  double delayMoveScale;
  double delayMoveMax;

  Player perspective;

  Rand gtpRand;

  ClockTimer genmoveTimer;
  double genmoveTimeSum;
  std::atomic<int> genmoveExpectedId;

  // Positions during this game when genmove was called
  std::vector<Sgf::PositionSample> genmoveSamples;

  GomEngine(
    const string& modelFile, const string& hModelFile,
    SearchParams initialGenmoveParams, SearchParams initialAnalysisParams,
    Rules initialRules,
    bool autoPattern,
    double normAvoidRepeatedPatternUtility,
    double delayScale, double delayMax,
    Player persp, int pvLen,
    std::unique_ptr<PatternBonusTable>&& pbTable
  )
    : nnModelFile(modelFile),
      humanModelFile(hModelFile),
      analysisPVLen(pvLen),
      autoAvoidPatterns(autoPattern),
      normalAvoidRepeatedPatternUtility(normAvoidRepeatedPatternUtility),
      nnEval(NULL),
      humanEval(NULL),
      bot(NULL),
      currentRules(initialRules),
      genmoveParams(initialGenmoveParams),
      analysisParams(initialAnalysisParams),
      isGenmoveParams(true),
      bTimeControls(),
      wTimeControls(),
      initialBoard(),
      initialPla(P_BLACK),
      moveHistory(),
      recentWinLossValues(),
      lastSearchFactor(1.0),
      patternBonusTable(std::move(pbTable)),
      delayMoveScale(delayScale),
      delayMoveMax(delayMax),
      perspective(persp),
      gtpRand(),
      genmoveTimer(),
      genmoveTimeSum(0.0),
      genmoveSamples(),
      logPVCoordinatesMode(brain ? 9 : -1)
  {
  }

  ~GomEngine() {
    stopAndWait();
    delete bot;
    delete nnEval;
  }

  void stopAndWait() {
    bot->stopAndWait();
  }

  Rules getCurrentRules() {
    return currentRules;
  }

  void clearStatsForNewGame() {
    //Currently nothing
  }

  // Specify -1 for the sizes for a default
  void setOrResetBoardSize(
    ConfigParser& cfg,
    Logger& logger,
    Rand& seedRand,
    int boardXSize,
    int boardYSize,
    bool loggingToStderr) {
    bool wasDefault = false;
    if(boardXSize == -1 || boardYSize == -1) {
      boardXSize = Board::DEFAULT_LEN;
      boardYSize = Board::DEFAULT_LEN;
      wasDefault = true;
    }

    bool defaultRequireExactNNLen = true;
    int nnXLen = boardXSize;
    int nnYLen = boardYSize;

    if(cfg.contains("gtpForceMaxNNSize") && cfg.getBool("gtpForceMaxNNSize")) {
      defaultRequireExactNNLen = false;
      nnXLen = Board::MAX_LEN;
      nnYLen = Board::MAX_LEN;
    }

    // If the neural net is wrongly sized, we need to create or recreate it
    if(nnEval == NULL || !(nnXLen == nnEval->getNNXLen() && nnYLen == nnEval->getNNYLen())) {
      if(nnEval != NULL) {
        assert(bot != NULL);
        bot->stopAndWait();
        delete bot;
        delete nnEval;
        delete humanEval;
        bot = NULL;
        nnEval = NULL;
        humanEval = NULL;
        logger.write("Cleaned up old neural net and bot");
      }

      const int expectedConcurrentEvals = std::max(genmoveParams.numThreads, analysisParams.numThreads);
      const int defaultMaxBatchSize = std::max(8, ((expectedConcurrentEvals + 3) / 4) * 4);
      const bool disableFP16 = false;
      const string expectedSha256 = "";
      nnEval = Setup::initializeNNEvaluator(
        nnModelFile,
        nnModelFile,
        expectedSha256,
        cfg,
        logger,
        seedRand,
        expectedConcurrentEvals,
        nnXLen,
        nnYLen,
        defaultMaxBatchSize,
        defaultRequireExactNNLen,
        disableFP16,
        Setup::SETUP_FOR_GTP);
      logger.write(
        "Loaded neural net with nnXLen " + Global::intToString(nnEval->getNNXLen()) + " nnYLen " +
        Global::intToString(nnEval->getNNYLen()));
      if(humanModelFile != "") {
        humanEval = Setup::initializeNNEvaluator(
          humanModelFile,
          humanModelFile,
          expectedSha256,
          cfg,
          logger,
          seedRand,
          expectedConcurrentEvals,
          nnXLen,
          nnYLen,
          defaultMaxBatchSize,
          defaultRequireExactNNLen,
          disableFP16,
          Setup::SETUP_FOR_GTP);
        logger.write(
          "Loaded human SL net with nnXLen " + Global::intToString(humanEval->getNNXLen()) + " nnYLen " +
          Global::intToString(humanEval->getNNYLen()));
        if(!humanEval->requiresSGFMetadata()) {
          string warning;
          warning +=
            "WARNING: Human model was not trained from SGF metadata to vary by rank! Did you pass the wrong model for "
            "-human-model?\n";
          logger.write(warning);
          if(!loggingToStderr)
            cerr << warning << endl;
        }
      }

      {
        bool rulesWereSupported;
        nnEval->getSupportedRules(currentRules, rulesWereSupported);
        if(!rulesWereSupported) {
          throw StringError(
            "Rules " + currentRules.toJsonStringNoKomi() + " from config file " + cfg.getFileName() +
            " are NOT supported by neural net");
        }
      }
    }

    // On default setup, also override board size to whatever the neural net was initialized with
    // So that if the net was initalized smaller, we don't fail with a big board
    if(wasDefault) {
      boardXSize = nnEval->getNNXLen();
      boardYSize = nnEval->getNNYLen();
    }

    // If the bot is wrongly sized, we need to create or recreate the bot
    if(bot == NULL || bot->getRootBoard().x_size != boardXSize || bot->getRootBoard().y_size != boardYSize) {
      if(bot != NULL) {
        assert(bot != NULL);
        bot->stopAndWait();
        delete bot;
        bot = NULL;
        logger.write("Cleaned up old bot");
      }

      logger.write(
        "Initializing board with boardXSize " + Global::intToString(boardXSize) + " boardYSize " +
        Global::intToString(boardYSize));
      if(!loggingToStderr)
        cerr << ("Initializing board with boardXSize " + Global::intToString(boardXSize) + " boardYSize " +
                 Global::intToString(boardYSize))
             << endl;

      string searchRandSeed;
      if(cfg.contains("searchRandSeed"))
        searchRandSeed = cfg.getString("searchRandSeed");
      else
        searchRandSeed = Global::uint64ToString(seedRand.nextUInt64());

      bot = new AsyncBot(genmoveParams, nnEval, humanEval, &logger, searchRandSeed);
      bot->setCopyOfExternalPatternBonusTable(patternBonusTable);

      Board board(boardXSize, boardYSize);
      Player pla = P_BLACK;
      BoardHistory hist(board, pla, currentRules);
      vector<Move> newMoveHistory;
      setPositionAndRules(pla, board, hist, board, pla, newMoveHistory);
      clearStatsForNewGame();
    }
  }
  void setPatternBonusTable(std::unique_ptr<PatternBonusTable>&& pbTable) {
    patternBonusTable = std::move(pbTable);
    if(bot != nullptr)
      bot->setCopyOfExternalPatternBonusTable(patternBonusTable);
  }
  void setPositionAndRules(Player pla, const Board& board, const BoardHistory& h, const Board& newInitialBoard, Player newInitialPla, const vector<Move> newMoveHistory) {
    BoardHistory hist(h);

    currentRules = hist.rules;
    bot->setPosition(pla,board,hist);
    initialBoard = newInitialBoard;
    initialPla = newInitialPla;
    moveHistory = newMoveHistory;
    recentWinLossValues.clear();
  }

  void clearBoard() {
    assert(bot->getRootHist().rules == currentRules);
    int newXSize = bot->getRootBoard().x_size;
    int newYSize = bot->getRootBoard().y_size;
    Board board(newXSize,newYSize);
    Player pla = P_BLACK;
    BoardHistory hist(board,pla,currentRules);
    vector<Move> newMoveHistory;
    setPositionAndRules(pla,board,hist,board,pla,newMoveHistory);
    clearStatsForNewGame();
  }

  bool setPosition(const vector<Move>& initialStones) {
    assert(bot->getRootHist().rules == currentRules);
    int newXSize = bot->getRootBoard().x_size;
    int newYSize = bot->getRootBoard().y_size;
    Board board(newXSize,newYSize);
    for(int i = 0; i<initialStones.size(); i++) {
      if(!board.isOnBoard(initialStones[i].loc) || board.colors[initialStones[i].loc] != C_EMPTY) {
        return false;
      }
      bool suc = board.setStone(initialStones[i].loc, initialStones[i].pla);
      if(!suc) {
        return false;
      }
    }

    //Make sure nothing died along the way
    for(int i = 0; i<initialStones.size(); i++) {
      if(board.colors[initialStones[i].loc] != initialStones[i].pla) {
        return false;
      }
    }
    Player pla = P_BLACK;
    BoardHistory hist(board,pla,currentRules);
    vector<Move> newMoveHistory;
    setPositionAndRules(pla,board,hist,board,pla,newMoveHistory);
    clearStatsForNewGame();
    return true;
  }

  void updateKomiIfNew(float newKomi) {
    bot->setKomiIfNew(newKomi);
    currentRules.komi = newKomi;
    setNoResultUtilityForWhite(newKomi / 10.0);
  }

  void setNoResultUtilityForWhite(double x) {
    if(x > 1)
      x = 1;
    if(x < -1)
      x = -1;
    analysisParams.noResultUtilityForWhite = x;
    bot->setParams(analysisParams);
    bot->clearSearch();
  }

  bool play(Loc loc, Player pla) {
    assert(bot->getRootHist().rules == currentRules);
    bool suc = bot->makeMove(loc,pla);
    if(suc)
      moveHistory.push_back(Move(loc,pla));
    return suc;
  }

  bool undo() {
    if(moveHistory.size() <= 0)
      return false;
    assert(bot->getRootHist().rules == currentRules);

    vector<Move> moveHistoryCopy = moveHistory;

    Board undoneBoard = initialBoard;
    BoardHistory undoneHist(undoneBoard,initialPla,currentRules);
    vector<Move> emptyMoveHistory;
    setPositionAndRules(initialPla,undoneBoard,undoneHist,initialBoard,initialPla,emptyMoveHistory);

    for(int i = 0; i<moveHistoryCopy.size()-1; i++) {
      Loc moveLoc = moveHistoryCopy[i].loc;
      Player movePla = moveHistoryCopy[i].pla;
      bool suc = play(moveLoc,movePla);
      assert(suc);
      (void)suc; //Avoid warning when asserts are off
    }
    return true;
  }

    bool setRules(Rules newRules, string& error) {
    assert(nnEval != NULL);
    assert(bot->getRootHist().rules == currentRules);

    bool rulesWereSupported;
    nnEval->getSupportedRules(newRules, rulesWereSupported);
    if(!rulesWereSupported) {
      error = "Rules " + newRules.toJsonString() + " are not supported by this neural net version";
      return false;
    }

    vector<Move> moveHistoryCopy = moveHistory;

    Board board = initialBoard;
    BoardHistory hist(board, initialPla, newRules);
    hist.setInitialTurnNumber(bot->getRootHist().initialTurnNumber);
    vector<Move> emptyMoveHistory;
    setPositionAndRules(initialPla, board, hist, initialBoard, initialPla, emptyMoveHistory);

    for(int i = 0; i < moveHistoryCopy.size(); i++) {
      Loc moveLoc = moveHistoryCopy[i].loc;
      Player movePla = moveHistoryCopy[i].pla;
      bool suc = play(moveLoc, movePla);

      // Because internally we use a highly tolerant test, we don't expect this to actually trigger
      // even if a rules change did make some earlier moves illegal. But this check simply futureproofs
      // things in case we ever do
      if(!suc) {
        error = "Could not make the rules change, some earlier moves in the game would now become illegal.";
        return false;
      }
    }
    return true;
  }

  bool setRulesNotIncludingKomi(Rules newRules, string& error) {
    assert(nnEval != NULL);
    assert(bot->getRootHist().rules == currentRules);
    newRules.komi = currentRules.komi;

    bool rulesWereSupported;
    nnEval->getSupportedRules(newRules, rulesWereSupported);
    if(!rulesWereSupported) {
      error = "Rules " + newRules.toJsonStringNoKomi() + " are not supported by this neural net version";
      return false;
    }

    vector<Move> moveHistoryCopy = moveHistory;

    Board board = initialBoard;
    BoardHistory hist(board, initialPla, newRules);
    hist.setInitialTurnNumber(bot->getRootHist().initialTurnNumber);
    vector<Move> emptyMoveHistory;
    setPositionAndRules(initialPla, board, hist, initialBoard, initialPla, emptyMoveHistory);

    for(int i = 0; i < moveHistoryCopy.size(); i++) {
      Loc moveLoc = moveHistoryCopy[i].loc;
      Player movePla = moveHistoryCopy[i].pla;
      bool suc = play(moveLoc, movePla);

      // Because internally we use a highly tolerant test, we don't expect this to actually trigger
      // even if a rules change did make some earlier moves illegal. But this check simply futureproofs
      // things in case we ever do
      if(!suc) {
        error = "Could not make the rules change, some earlier moves in the game would now become illegal.";
        return false;
      }
    }
    return true;
  }

  void ponder() {
    bot->ponder(lastSearchFactor);
  }

  struct GenmoveArgs {
    double searchFactorWhenWinningThreshold;
    double searchFactorWhenWinning;
    enabled_t cleanupBeforePass;
    enabled_t friendlyPass;
    bool ogsChatToStderr;
    bool allowResignation;
    double resignThreshold;
    int resignConsecTurns;
    double resignMinScoreDifference;
    double resignMinMovesPerBoardArea;
    bool logSearchInfo;
    bool logSearchInfoForChosenMove;
    bool debug;
  };

  struct AnalyzeArgs {
    bool analyzing = false;
    bool lz = false;
    bool kata = false;
    int minMoves = 0;
    int maxMoves = 10000000;
    bool showRootInfo = false;
    bool showPVVisits = false;
    bool showPVEdgeVisits = false;
    double secondsPerReport = TimeControls::UNLIMITED_TIME_DEFAULT;
    vector<int> avoidMoveUntilByLocBlack;
    vector<int> avoidMoveUntilByLocWhite;
  };

  void filterZeroVisitMoves(const AnalyzeArgs& args, vector<AnalysisData> buf) {
    // Avoid printing moves that have 0 visits, unless we need them
    // These should already be sorted so that 0-visit moves only appear at the end.
    int keptMoves = 0;
    for(int i = 0; i < buf.size(); i++) {
      if(buf[i].childVisits > 0 || keptMoves < args.minMoves)
        buf[keptMoves++] = buf[i];
    }
    buf.resize(keptMoves);
  }

  std::function<void(const Search* search)> getAnalyzeCallback(Player pla, AnalyzeArgs args) {
    std::function<void(const Search* search)> callback;
    // lz-analyze
    if(args.lz && !args.kata) {
      // Avoid capturing anything by reference except [this], since this will potentially be used
      // asynchronously and called after we return
      callback = [args, pla, this](const Search* search) {
        vector<AnalysisData> buf;
        bool duplicateForSymmetries = true;
        search->getAnalysisData(buf, args.minMoves, false, analysisPVLen, duplicateForSymmetries);
        filterZeroVisitMoves(args, buf);
        if(buf.size() > args.maxMoves)
          buf.resize(args.maxMoves);
        if(buf.size() <= 0)
          return;

        const Board board = search->getRootBoard();
        for(int i = 0; i < buf.size(); i++) {
          if(i > 0)
            cout << " ";
          const AnalysisData& data = buf[i];
          double winrate = 0.5 * (1.0 + data.winLossValue);
          double lcb = PlayUtils::getHackedLCBForWinrate(search, data, pla);
          if(perspective == P_BLACK || (perspective != P_BLACK && perspective != P_WHITE && pla == P_BLACK)) {
            winrate = 1.0 - winrate;
            lcb = 1.0 - lcb;
          }
          cout << "info";
          cout << " move " << Location::toString(data.move, board);
          cout << " visits " << data.childVisits;
          cout << " winrate " << round(winrate * 10000.0);
          cout << " prior " << round(data.policyPrior * 10000.0);
          cout << " lcb " << round(lcb * 10000.0);
          cout << " order " << data.order;
          cout << " pv ";
          data.writePV(cout, board);
          if(args.showPVVisits) {
            cout << " pvVisits ";
            data.writePVVisits(cout);
          }
          if(args.showPVEdgeVisits) {
            cout << " pvEdgeVisits ";
            data.writePVEdgeVisits(cout);
          }
        }
        cout << endl;
      };
    }
    // kata-analyze, analyze (sabaki)
    else {
      callback = [args, pla, this](const Search* search) {
        vector<AnalysisData> buf;
        bool duplicateForSymmetries = true;
        search->getAnalysisData(buf, args.minMoves, false, analysisPVLen, duplicateForSymmetries);
        ReportedSearchValues rootVals;
        bool suc = search->getPrunedRootValues(rootVals);
        if(!suc)
          return;
        filterZeroVisitMoves(args, buf);
        if(buf.size() > args.maxMoves)
          buf.resize(args.maxMoves);
        if(buf.size() <= 0)
          return;
        const SearchNode* rootNode = search->getRootNode();

        ostringstream out;
        if(!args.kata) {
          // Hack for sabaki - ensure always showing decimal point. Also causes output to be more verbose with trailing
          // zeros, unfortunately, despite doing not improving the precision of the values.
          out << std::showpoint;
        }

        const Board board = search->getRootBoard();
        for(int i = 0; i < buf.size(); i++) {
          if(i > 0)
            out << " ";
          const AnalysisData& data = buf[i];
          double winrate = 0.5 * (1.0 + data.winLossValue);
          double drawrate = 100.0 * data.noResultValue;
          double utility = data.utility;
          // We still hack the LCB for consistency with LZ-analyze
          double lcb = PlayUtils::getHackedLCBForWinrate(search, data, pla);
          /// But now we also offer the proper LCB that KataGo actually uses.
          double utilityLcb = data.lcb;
          if(perspective == P_BLACK || (perspective != P_BLACK && perspective != P_WHITE && pla == P_BLACK)) {
            winrate = 1.0 - winrate;
            lcb = 1.0 - lcb;
            utility = -utility;
            utilityLcb = -utilityLcb;
          }
          out << "info";
          out << " move " << Location::toString(data.move, board);
          out << " visits " << data.childVisits;
          out << " edgeVisits " << data.numVisits;
          out << " utility " << utility;
          out << " winrate " << winrate;
          out << " scoreMean " << drawrate;
          out << " scoreStdev " << 0.0f;
          out << " scoreLead " << drawrate;
          out << " prior " << data.policyPrior;
          out << " lcb " << lcb;
          out << " utilityLcb " << utilityLcb;
          out << " weight " << data.childWeightSum;
          if(data.isSymmetryOf != Board::NULL_LOC)
            out << " isSymmetryOf " << Location::toString(data.isSymmetryOf, board);
          out << " order " << data.order;
          out << " pv ";
          data.writePV(out, board);
          if(args.showPVVisits) {
            out << " pvVisits ";
            data.writePVVisits(out);
          }
          if(args.showPVEdgeVisits) {
            out << " pvEdgeVisits ";
            data.writePVEdgeVisits(out);
          }
        }
        if(args.showRootInfo) {
          out << " rootInfo";
          double winrate = 0.5 * (1.0 + rootVals.winLossValue);
          double drawrate = 100.0 * rootVals.noResultValue;
          double utility = rootVals.utility;
          if(perspective == P_BLACK || (perspective != P_BLACK && perspective != P_WHITE && pla == P_BLACK)) {
            winrate = 1.0 - winrate;
            utility = -utility;
          }
          out << " visits " << rootVals.visits;
          out << " utility " << utility;
          out << " winrate " << winrate;
          out << " scoreMean " << drawrate;
          out << " scoreStdev " << 0.0f;
          out << " scoreLead " << drawrate;
          out << " weight " << rootVals.weight;
          if(rootNode != NULL) {
            const NNOutput* nnOutput = rootNode->getNNOutput();
            if(nnOutput != NULL) {
              out << " rawStWrError " << nnOutput->shorttermWinlossError;
              out << " rawVarTimeLeft " << nnOutput->varTimeLeft;
            }
          }
        }

        cout << out.str() << endl;
      };
    }
    return callback;
  }

  void genMove(
    Player pla,
    Logger& logger,
    const GenmoveArgs& gargs,
    const AnalyzeArgs& args,
    bool playChosenMove,
    std::function<void(const string&, bool)> printGTPResponse,
    bool& maybeStartPondering) {
    bool onMoveWasCalled = false;
    Loc genmoveMoveLoc = Board::NULL_LOC;
    auto onMove = [&genmoveMoveLoc, &onMoveWasCalled, this](Loc moveLoc, int searchId, Search* search) noexcept {
      (void)searchId;
      (void)search;
      onMoveWasCalled = true;
      genmoveMoveLoc = moveLoc;
    };
    launchGenMove(pla, gargs, args, onMove);
    bot->waitForSearchToEnd();
    testAssert(onMoveWasCalled);
    string response;
    bool responseIsError = false;
    Loc moveLocToPlay = Board::NULL_LOC;
    handleGenMoveResult(pla, bot->getSearchStopAndWait(), logger, gargs, args, genmoveMoveLoc, response, responseIsError, moveLocToPlay);
    printGTPResponse(response, responseIsError);
    if(moveLocToPlay != Board::NULL_LOC && playChosenMove) {
      bool suc = bot->makeMove(moveLocToPlay, pla);
      if(suc)
        moveHistory.push_back(Move(moveLocToPlay, pla));
      assert(suc);
      (void)suc;  // Avoid warning when asserts are off

      maybeStartPondering = true;
    }
  }

  void launchGenMove(Player pla, GenmoveArgs gargs, AnalyzeArgs args, std::function<void(Loc, int, Search*)> onMove) {
    genmoveTimer.reset();

    nnEval->clearStats();
    if(humanEval != NULL)
      humanEval->clearStats();
    TimeControls tc = pla == P_BLACK ? bTimeControls : wTimeControls;

    if(!isGenmoveParams) {
      bot->setParams(genmoveParams);
      isGenmoveParams = true;
    }

    SearchParams paramsToUse = genmoveParams;
    // Make sure we have the right parameters, in case someone updated params in the meantime.

    {
      double avoidRepeatedPatternUtility = normalAvoidRepeatedPatternUtility;
      paramsToUse.avoidRepeatedPatternUtility = avoidRepeatedPatternUtility;
    }

    if(paramsToUse != bot->getParams())
      bot->setParams(paramsToUse);

    // Play faster when winning
    double searchFactor = PlayUtils::getSearchFactor(
      gargs.searchFactorWhenWinningThreshold, gargs.searchFactorWhenWinning, paramsToUse, recentWinLossValues, pla);
    lastSearchFactor = searchFactor;

    bot->setAvoidMoveUntilByLoc(args.avoidMoveUntilByLocBlack, args.avoidMoveUntilByLocWhite);

    // So that we can tell by the end of the search whether we still care for the result.
    int expectedSearchId = (genmoveExpectedId.load() + 1) & 0x3FFFFFFF;
    genmoveExpectedId.store(expectedSearchId);

    if(args.analyzing) {
      std::function<void(const Search* search)> callback = getAnalyzeCallback(pla, args);

      // Make sure callback happens at least once
      auto onMoveWrapped = [onMove, callback](Loc moveLoc, int searchId, Search* search) {
        callback(search);
        onMove(moveLoc, searchId, search);
      };
      bot->genMoveAsyncAnalyze(
        pla, expectedSearchId, tc, searchFactor, onMoveWrapped, args.secondsPerReport, args.secondsPerReport, callback);
    } else {
      bot->genMoveAsync(pla, expectedSearchId, tc, searchFactor, onMove);
    }
  }

  void handleGenMoveResult(
    Player pla,
    Search* searchBot,
    Logger& logger,
    const GenmoveArgs& gargs,
    const AnalyzeArgs& args,
    Loc moveLoc,
    string& response,
    bool& responseIsError,
    Loc& moveLocToPlay) {
    response = "";
    responseIsError = false;
    moveLocToPlay = Board::NULL_LOC;

    const Search* search = searchBot;

    bool isLegal = search->isLegalStrict(moveLoc, pla);
    if(moveLoc == Board::NULL_LOC || !isLegal) {
      responseIsError = true;
      response = "genmove returned null location or illegal move";
      ostringstream sout;
      sout << "genmove null location or illegal move!?!"  << endl;
      sout << search->getRootBoard() << endl;
      sout << "Pla: " << PlayerIO::playerToString(pla) << endl;
      sout << "MoveLoc: " << Location::toString(moveLoc, search->getRootBoard()) << endl;
      logger.write(sout.str());
      genmoveTimeSum += genmoveTimer.getSeconds();
      return;
    }

    SearchNode* rootNode = search->rootNode;
    if(rootNode != NULL && delayMoveScale > 0.0 && delayMoveMax > 0.0) {
      int pos = search->getPos(moveLoc);
      const NNOutput* nnOutput = rootNode->getHumanOutput();
      nnOutput = nnOutput != NULL ? nnOutput : rootNode->getNNOutput();
      if(nnOutput != NULL) {
#ifdef QUANTIZED_OUTPUT
        double policyProb = nnOutput->getPolicyProbMaybeNoised(pos);
#else
        const float* policyProbs = nnOutput->getPolicyProbsMaybeNoised();
        double policyProb = policyProbs[pos];
#endif
        double prob = std::max(0.0, policyProb);
        double meanWait = 0.5 * delayMoveScale / (prob + 0.10);
        double waitTime = gtpRand.nextGamma(2.0) * meanWait / 2.0;
        waitTime = std::min(waitTime, delayMoveMax);
        waitTime = std::max(waitTime, 0.0001);
        std::this_thread::sleep_for(std::chrono::duration<double>(waitTime));
      }
    }

    ReportedSearchValues values;
    double winLossValue;
    {
      values = search->getRootValuesRequireSuccess();
      winLossValue = values.winLossValue;
    }

    // Record data for resignation or adjusting handicap behavior ------------------------
    recentWinLossValues.push_back(winLossValue);

    // Decide whether we should resign---------------------
    bool resigned = false;

    // Snapshot the time NOW - all meaningful play-related computation time is done, the rest is just
    // output of various things.
    double timeTaken = genmoveTimer.getSeconds();
    genmoveTimeSum += timeTaken;

    // Chatting and logging ----------------------------
    const SearchParams& params = search->searchParams;

    int64_t visits = bot->getSearch()->getRootVisits();
    double winrate = 0.5 * (1.0 + (values.winValue - values.lossValue));
    // Print winrate from desired perspective
    if(perspective == P_BLACK || (perspective != P_BLACK && perspective != P_WHITE && pla == P_BLACK)) {
      winrate = 1.0 - winrate;
    }
    cout << "MESSAGE "
         << "Visits " << visits << " Winrate " << Global::strprintf("%.2f%%", winrate * 100.0) << " Drawrate "
         << Global::strprintf("%.2f%%", values.noResultValue * 100.0) << " Time "
         << Global::strprintf("%.3f", timeTaken);
    if(params.playoutDoublingAdvantage != 0.0) {
      cout << Global::strprintf(
        " (PDA %.2f)",
        bot->getSearch()->getRootPla() == getOpp(params.playoutDoublingAdvantagePla) ? -params.playoutDoublingAdvantage
                                                                                     : params.playoutDoublingAdvantage);
    }
    cout << " PV ";
    bot->getSearch()->printPVForMove(cout, bot->getSearch()->rootNode, moveLoc, analysisPVLen);
    cout << endl;

    if(gargs.logSearchInfo) {
      ostringstream sout;
      PlayUtils::printGenmoveLog(
        sout, search, nnEval, moveLoc, timeTaken, perspective, gargs.logSearchInfoForChosenMove);
      logger.write(sout.str());
    }

    if(gargs.debug) {
      PlayUtils::printGenmoveLog(
        cerr, search, nnEval, moveLoc, timeTaken, perspective, gargs.logSearchInfoForChosenMove);
    }

    // Actual reporting of chosen move---------------------
    int x = Location::getX(moveLoc, bot->getRootBoard().x_size);
    int y = Location::getY(moveLoc, bot->getRootBoard().x_size);
    response = to_string(x) + "," + to_string(y);
    
    if(autoAvoidPatterns) {
      // Auto pattern expects moveless records using hintloc to contain the move.
      Sgf::PositionSample posSample;
      const BoardHistory& hist = search->getRootHist();
      posSample.board = search->getRootBoard();
      posSample.nextPla = pla;
      posSample.initialTurnNumber = hist.getCurrentTurnNumber();
      posSample.hintLoc = moveLoc;
      posSample.weight = 1.0;
      genmoveSamples.push_back(posSample);
    }

    if(!resigned) {
      moveLocToPlay = moveLoc;
    }

    /*
    if(args.analyzing) {
      response = "play " + response;
    }
    */

    return;
  }

  double searchAndGetValue(
    Player pla,
    Logger& logger,
    double searchTime,
    bool logSearchInfo,
    bool logSearchInfoForChosenMove,
    string& response,
    bool& responseIsError,
    AnalyzeArgs args) {
    ClockTimer timer;

    response = "";
    responseIsError = false;

    nnEval->clearStats();
    TimeControls tc;
    tc.maxTimePerMove = searchTime;

    /*
    // Make sure we have the right parameters, in case someone ran analysis in the meantime.
    if(params.playoutDoublingAdvantage != staticPlayoutDoublingAdvantage) {
      params.playoutDoublingAdvantage = staticPlayoutDoublingAdvantage;
      bot->setParams(params);
    }

    if(params.wideRootNoise != genmoveWideRootNoise) {
      params.wideRootNoise = genmoveWideRootNoise;
      bot->setParams(params);
    }
    */

    // Play faster when winning
    double searchFactor = 1.0;
    lastSearchFactor = searchFactor;
    Loc moveLoc;
    bot->setAvoidMoveUntilByLoc(args.avoidMoveUntilByLocBlack, args.avoidMoveUntilByLocWhite);
    moveLoc = bot->genMoveSynchronous(pla, tc, searchFactor);

    bool isLegal = bot->isLegalStrict(moveLoc, pla);
    if(moveLoc == Board::NULL_LOC || !isLegal) {
      responseIsError = true;
      response = "genmove returned null location or illegal move";
      // cout<< "genmove returned null location or illegal move";
      ostringstream sout;
      sout << "genmove null location or illegal move!?!"
           << "\n";
      sout << bot->getRootBoard() << "\n";
      sout << "Pla: " << PlayerIO::playerToString(pla) << "\n";
      sout << "MoveLoc: " << Location::toString(moveLoc, bot->getRootBoard()) << "\n";
      logger.write(sout.str());
      genmoveTimeSum += timer.getSeconds();
      return 0;
    }

    ReportedSearchValues values;
    double winLossValue;
    {
      values = bot->getSearch()->getRootValuesRequireSuccess();
      winLossValue = values.winLossValue;
    }

    // Snapshot the time NOW - all meaningful play-related computation time is done, the rest is just
    // output of various things.
    double timeTaken = timer.getSeconds();
    genmoveTimeSum += timeTaken;

    // Chatting and logging ----------------------------

    int64_t visits = bot->getSearch()->getRootVisits();
    double winrate = 0.5 * (1.0 + (values.winValue - values.lossValue));
    // Print winrate from desired perspective
    if(perspective == P_BLACK || (perspective != P_BLACK && perspective != P_WHITE && pla == P_BLACK)) {
      winrate = 1.0 - winrate;
    }
    cout << "MESSAGE "
         << "Visits " << visits << " Winrate " << Global::strprintf("%.2f%%", winrate * 100.0) << " Drawrate "
         << Global::strprintf("%.2f%%", values.noResultValue * 100.0) << " Time "
         << Global::strprintf("%.3f", timeTaken);
    /*
    if(params.playoutDoublingAdvantage != 0.0) {
      cout << Global::strprintf(
        " (PDA %.2f)",
        bot->getSearch()->getRootPla() == getOpp(params.playoutDoublingAdvantagePla) ? -params.playoutDoublingAdvantage
                                                                                     : params.playoutDoublingAdvantage);
    }
    */
    cout << " PV ";
    bot->getSearch()->printPVForMove(cout, bot->getSearch()->rootNode, moveLoc, analysisPVLen);
    cout << endl;

    // Actual reporting of chosen move---------------------
    int x = Location::getX(moveLoc, bot->getRootBoard().x_size);
    int y = Location::getY(moveLoc, bot->getRootBoard().x_size);
    response = to_string(x) + "," + to_string(y);

    if(logSearchInfo) {
      ostringstream sout;
      PlayUtils::printGenmoveLog(
        sout, bot->getSearch(), nnEval, moveLoc, timeTaken, perspective, logSearchInfoForChosenMove);
      logger.write(sout.str());
    }

    return values.winValue - values.lossValue;
  }

  void clearCache() {
    bot->clearSearch();
    nnEval->clearCache();
  }


  void analyze(Player pla, AnalyzeArgs args) {
    assert(args.analyzing);
    if(isGenmoveParams) {
      bot->setParams(analysisParams);
      isGenmoveParams = false;
    }

    std::function<void(const Search* search)> callback = getAnalyzeCallback(pla, args);
    bot->setAvoidMoveUntilByLoc(args.avoidMoveUntilByLocBlack, args.avoidMoveUntilByLocWhite);

    double searchFactor = 1e40;  // go basically forever
    bot->analyzeAsync(pla, searchFactor, args.secondsPerReport, args.secondsPerReport, callback);
  }

  void computeAnticipatedWinner(Player& winner) {
    stopAndWait();

    // No playoutDoublingAdvantage to avoid bias
    // Also never assume the game will end abruptly due to pass
    {
      SearchParams tmpParams = genmoveParams;
      tmpParams.playoutDoublingAdvantage = 0.0;
      tmpParams.humanSLChosenMoveProp = 0.0;
      tmpParams.humanSLRootExploreProbWeightful = 0.0;
      tmpParams.humanSLRootExploreProbWeightless = 0.0;
      tmpParams.humanSLPlaExploreProbWeightful = 0.0;
      tmpParams.humanSLPlaExploreProbWeightless = 0.0;
      tmpParams.humanSLOppExploreProbWeightful = 0.0;
      tmpParams.humanSLOppExploreProbWeightless = 0.0;
      tmpParams.avoidRepeatedPatternUtility = 0;
      bot->setParams(tmpParams);
    }

    // Make absolutely sure we can restore the bot's old state
    const Player oldPla = bot->getRootPla();
    const Board oldBoard = bot->getRootBoard();
    const BoardHistory oldHist = bot->getRootHist();

    Board board = bot->getRootBoard();
    BoardHistory hist = bot->getRootHist();
    Player pla = bot->getRootPla();

    // Restore
    bot->setPosition(oldPla, oldBoard, oldHist);
    bot->setParams(genmoveParams);
    isGenmoveParams = true;
  }

  vector<bool> computeAnticipatedStatuses() {
    stopAndWait();

    // No playoutDoublingAdvantage to avoid bias
    // Also never assume the game will end abruptly due to pass
    {
      SearchParams tmpParams = genmoveParams;
      tmpParams.playoutDoublingAdvantage = 0.0;
      tmpParams.humanSLChosenMoveProp = 0.0;
      tmpParams.humanSLRootExploreProbWeightful = 0.0;
      tmpParams.humanSLRootExploreProbWeightless = 0.0;
      tmpParams.humanSLPlaExploreProbWeightful = 0.0;
      tmpParams.humanSLPlaExploreProbWeightless = 0.0;
      tmpParams.humanSLOppExploreProbWeightful = 0.0;
      tmpParams.humanSLOppExploreProbWeightless = 0.0;
      tmpParams.avoidRepeatedPatternUtility = 0;
      bot->setParams(tmpParams);
    }

    // Make absolutely sure we can restore the bot's old state
    const Player oldPla = bot->getRootPla();
    const Board oldBoard = bot->getRootBoard();
    const BoardHistory oldHist = bot->getRootHist();

    Board board = bot->getRootBoard();
    BoardHistory hist = bot->getRootHist();
    Player pla = bot->getRootPla();

    int64_t numVisits = std::max(100, genmoveParams.numThreads * 20);
    vector<bool> isAlive = PlayUtils::computeAnticipatedStatusesSimple(board, hist);

    // Restore
    bot->setPosition(oldPla, oldBoard, oldHist);
    bot->setParams(genmoveParams);
    isGenmoveParams = true;

    return isAlive;
  }

  const SearchParams& getGenmoveParams() { return genmoveParams; }

  void setGenmoveParamsIfChanged(const SearchParams& p) {
    if(genmoveParams != p) {
      genmoveParams = p;
      if(isGenmoveParams)
        bot->setParams(genmoveParams);
    }
  }

  const SearchParams& getAnalysisParams() { return analysisParams; }

  void setAnalysisParamsIfChanged(const SearchParams& p) {
    if(analysisParams != p) {
      analysisParams = p;
      if(!isGenmoveParams)
        bot->setParams(analysisParams);
    }
  }
};

int MainCmds::gomprotocol(const std::vector<std::string>& args) {
  Board::initHash();
   
  Rand seedRand;

  ConfigParser cfg;
  string nnModelFile;
  string humanModelFile;
  string overrideVersion;

  KataGoCommandLine cmd("Run KataGo main GTP engine for piskvork.");
  try {
    cmd.addConfigFileArg("config.cfg", "gtp_example.cfg");
    cmd.addModelFileArg();
    cmd.addHumanModelFileArg();
    cmd.setShortUsageArgLimit();
    cmd.addOverrideConfigArg();

    TCLAP::ValueArg<string> overrideVersionArg("","override-version","Force KataGo to say a certain value in response to gtp version command",false,string(),"VERSION");
    cmd.add(overrideVersionArg);
    cmd.parseArgs(args);
    nnModelFile = cmd.getModelFile();
    humanModelFile = cmd.getHumanModelFile();
    overrideVersion = overrideVersionArg.getValue();

    cmd.getConfig(cfg);
  }
  catch (TCLAP::ArgException &e) {
    cerr << "ERROR " << e.error() << " for argument " << e.argId() << endl;
    return 1;
  }

  Logger logger(&cfg);

  const bool logAllGTPCommunication = cfg.getBool("logAllGTPCommunication");
  const bool logSearchInfo = cfg.getBool("logSearchInfo");
  const bool logSearchInfoForChosenMove = cfg.contains("logSearchInfoForChosenMove") ? cfg.getBool("logSearchInfoForChosenMove") : false;

  bool logTimeStamp = cfg.contains("logTimeStamp") ? cfg.getBool("logTimeStamp") : true;
  if(!logTimeStamp)
    logger.setLogTime(false);

  bool startupPrintMessageToStderr = true;
  if(cfg.contains("startupPrintMessageToStderr"))
    startupPrintMessageToStderr = cfg.getBool("startupPrintMessageToStderr");

  //if(cfg.contains("logToStderr") && cfg.getBool("logToStderr")) {
    logger.setLogToStderr(true);
  //}

  logger.setDisabled(true);

  logger.write("GTP Engine starting...");
  logger.write(Version::getKataGoVersionForHelp());
  //Also check loggingToStderr so that we don't duplicate the message from the log file

  Rules initialRules = Setup::loadSingleRules(cfg);
  logger.write("Using " + initialRules.toString() + " rules initially, unless GTP/GUI overrides this");
  if(startupPrintMessageToStderr && !logger.isLoggingToStderr()) {
    cerr << "MESSAGE Using " << initialRules.toString() << " rules initially, unless GTP/GUI overrides this" << endl;
  }

  const bool hasHumanModel = humanModelFile != "";

  auto loadParams = [&hasHumanModel](ConfigParser& config, SearchParams& genmoveOut, SearchParams& analysisOut) {
    SearchParams params = Setup::loadSingleParams(config, Setup::SETUP_FOR_GTP, hasHumanModel);

    const double analysisWideRootNoise = config.contains("analysisWideRootNoise")
                                           ? config.getDouble("analysisWideRootNoise", 0.0, 5.0)
                                           : Setup::DEFAULT_ANALYSIS_WIDE_ROOT_NOISE;
    genmoveOut = params;
    analysisOut = params;
    analysisOut.wideRootNoise = analysisWideRootNoise;
  };

  SearchParams initialGenmoveParams;
  SearchParams initialAnalysisParams;
  loadParams(cfg, initialGenmoveParams, initialAnalysisParams);
  logger.write("Using " + Global::intToString(initialGenmoveParams.numThreads) + " CPU thread(s) for search");

  const bool ponderingEnabled = cfg.getBool("ponderingEnabled");
  const bool allowResignation = cfg.contains("allowResignation") ? cfg.getBool("allowResignation") : false;
  const double resignThreshold = cfg.contains("allowResignation") ? cfg.getDouble("resignThreshold",-1.0,0.0) : -1.0; //Threshold on [-1,1], regardless of winLossUtilityFactor
  const int resignConsecTurns = cfg.contains("resignConsecTurns") ? cfg.getInt("resignConsecTurns",1,100) : 3;
  const double resignMinMovesPerBoardArea = cfg.contains("resignMinMovesPerBoardArea") ? cfg.getDouble("resignMinMovesPerBoardArea",0.0,1.0) : 0.0;

  Setup::initializeSession(cfg);

  const double searchFactorWhenWinning = cfg.contains("searchFactorWhenWinning") ? cfg.getDouble("searchFactorWhenWinning",0.01,1.0) : 1.0;
  const double searchFactorWhenWinningThreshold = cfg.contains("searchFactorWhenWinningThreshold") ? cfg.getDouble("searchFactorWhenWinningThreshold",0.0,1.0) : 1.0;
  const bool ogsChatToStderr = cfg.contains("ogsChatToStderr") ? cfg.getBool("ogsChatToStderr") : false;
  const int analysisPVLen = cfg.contains("analysisPVLen") ? cfg.getInt("analysisPVLen",1,1000) : 13;
  const double normalAvoidRepeatedPatternUtility = initialGenmoveParams.avoidRepeatedPatternUtility;
  const double initialDelayMoveScale = cfg.contains("delayMoveScale") ? cfg.getDouble("delayMoveScale",0.0,10000.0) : 0.0;
  const double initialDelayMoveMax = cfg.contains("delayMoveMax") ? cfg.getDouble("delayMoveMax",0.0,1000000.0) : 1000000.0;

  int defaultBoardXSize = -1;
  int defaultBoardYSize = -1;
  Setup::loadDefaultBoardXYSize(cfg, logger, defaultBoardXSize, defaultBoardYSize);

  const bool forDeterministicTesting = cfg.contains("forDeterministicTesting") ? cfg.getBool("forDeterministicTesting") : false;

  if(forDeterministicTesting)
    seedRand.init("forDeterministicTesting");
  std::unique_ptr<PatternBonusTable> patternBonusTable = nullptr;
  {
    std::vector<std::unique_ptr<PatternBonusTable>> tables = Setup::loadAvoidSgfPatternBonusTables(cfg, logger);
    assert(tables.size() == 1);
    patternBonusTable = std::move(tables[0]);
  }
  bool autoAvoidPatterns = false;
  {
    std::unique_ptr<PatternBonusTable> autoTable = Setup::loadAndPruneAutoPatternBonusTables(cfg, logger);
    if(autoTable != nullptr && patternBonusTable != nullptr)
      throw StringError("Providing both sgf avoid patterns and auto avoid patterns is not implemented right now");
    if(autoTable != nullptr) {
      autoAvoidPatterns = true;
      patternBonusTable = std::move(autoTable);
    }
  }

  double swap2time = 5400;//swap2
  
  // Toggled to true every time we save, toggled back to false once we do load.
  bool shouldReloadAutoAvoidPatterns = false;
  Player perspective = Setup::parseReportAnalysisWinrates(cfg, C_EMPTY);

  GomEngine* engine = new GomEngine(
    nnModelFile,
    humanModelFile,
    initialGenmoveParams,
    initialAnalysisParams,
    initialRules,
    autoAvoidPatterns,
    normalAvoidRepeatedPatternUtility,
    initialDelayMoveScale,
    initialDelayMoveMax,
    perspective,
    analysisPVLen,
    std::move(patternBonusTable));
  engine->setOrResetBoardSize(cfg, logger, seedRand, defaultBoardXSize, defaultBoardYSize, startupPrintMessageToStderr);

  auto maybeSaveAvoidPatterns = [&](bool forceSave) {
    if(engine != NULL && autoAvoidPatterns) {
      int samplesPerSave = 200;
      if(cfg.contains("autoAvoidRepeatSaveChunkSize"))
        samplesPerSave = cfg.getInt("autoAvoidRepeatSaveChunkSize", 1, 10000);

      if(forceSave || engine->genmoveSamples.size() >= samplesPerSave) {
        bool suc = Setup::saveAutoPatternBonusData(engine->genmoveSamples, cfg, logger, seedRand);
        if(suc) {
          engine->genmoveSamples.clear();
          shouldReloadAutoAvoidPatterns = true;
        }
      }
    }
  };

  logger.write("GTP Engine for piskvork starting...");
  logger.write(Version::getKataGoVersionForHelp());

  cerr << "MESSAGE GTP Engine for piskvork starting..." << endl;
  cerr << "MESSAGE " << Version::getKataGoVersionForHelp() << endl;

  //Check for unused config keys
  cfg.warnUnusedKeys(cerr,&logger);
  Setup::maybeWarnHumanSLParams(initialGenmoveParams, engine->nnEval, engine->humanEval, cerr, &logger);

  logger.write("Loaded config " + cfg.getFileName());
  logger.write("Loaded model "+ nnModelFile);
  if(humanModelFile != "")
    logger.write("Loaded human SL model " + humanModelFile);
  cmd.logOverrides(logger);
  logger.write("Model name: " + (engine->nnEval == NULL ? string() : engine->nnEval->getInternalModelName()));
  if(engine->humanEval != NULL)
    logger.write("Human SL model name: " + (engine->humanEval->getInternalModelName()));
  logger.write("GTP ready, beginning main protocol loop");
  if(humanModelFile != "" && !cfg.contains("humanSLProfile") && engine->humanEval->requiresSGFMetadata()) {
    logger.write(
      "WARNING: Provided -human-model but humanSLProfile was not set in the config or overrides. The human SL model "
      "will not be used until it is set in the config or at runtime via kata-set-param.");
  }

  //cerr << "MESSAGE Engine Rule: " << initialRules.toString() << endl;
  //cerr << "MESSAGE Board Size: " << Board::DEFAULT_LEN << endl;
  cerr << "MESSAGE Loaded config " << cfg.getFileName() << endl;
  cerr << "MESSAGE Loaded model " << nnModelFile << endl;
  if(humanModelFile != "")
    cerr << "MESSAGE Loaded human SL model " << humanModelFile << endl;
  cerr << "MESSAGE Model name: " << (engine->nnEval == NULL ? string() : engine->nnEval->getInternalModelName()) << endl;
  if(engine->humanEval != NULL)
    cerr << "MESSAGE Human SL model name: " << engine->humanEval->getInternalModelName() << endl;
  cerr << "MESSAGE GTP ready, beginning main protocol loop" << endl;

  bool currentlyGenmoving = false;
  bool currentlyAnalyzing = false;

  string line;

  //istringstream input("START 15\nBEGIN\n");
  //while(getline(input, line)) {

  while(getline(cin, line)) {
    // Parse command, extracting out the command itself, the arguments, and any GTP id number for the command.
    string command;
    vector<string> pieces;
    bool hasId = false;
    int id = 0;
    {
      line = CommandLoop::processSingleCommandLine(line);

      // Upon any input line at all, stop any analysis and output a newline
      // Only difference between analysis and genmove is that genmove handles its own
      // double newline in its onmove callback.
      if(currentlyAnalyzing) {
        currentlyAnalyzing = false;
        engine->stopAndWait();
        cout << endl;
      }
      if(currentlyGenmoving) {
        currentlyGenmoving = false;
        engine->stopAndWait();
      }

      if(line.length() == 0)
        continue;

      if(logAllGTPCommunication)
        logger.write("Controller: " + line);

      //Parse id number of command, if present
      size_t digitPrefixLen = 0;
      while(digitPrefixLen < line.length() && Global::isDigit(line[digitPrefixLen]))
        digitPrefixLen++;
      if(digitPrefixLen > 0) {
        hasId = true;
        try {
          id = Global::parseDigits(line,0,digitPrefixLen);
        }
        catch(const IOError& e) {
          cerr << "MESSAGE ? GTP id '" << id << "' could not be parsed: " << e.what() << endl;
          continue;
        }
        line = line.substr(digitPrefixLen);
      }

      line = Global::trim(line);
      if(line.length() <= 0) {
        cerr << "MESSAGE ? empty command" << endl;
        continue;
      }

      pieces = Global::split(line,' ');
      for(size_t i = 0; i<pieces.size(); i++)
        pieces[i] = Global::trim(pieces[i]);
      assert(pieces.size() > 0);

      command = pieces[0];
      pieces.erase(pieces.begin());
    }

    auto printGTPResponse = [hasId, id, &logger, logAllGTPCommunication](const string& response, bool responseIsError) {
      string postProcessed = response;
      //if(hasId)
      //  postProcessed = Global::intToString(id) + " " + postProcessed;
      //else
      //  postProcessed = " " + postProcessed;

      if(responseIsError)
        postProcessed = "ERROR " + postProcessed;

      cout << postProcessed << endl;

      if(logAllGTPCommunication)
        logger.write(postProcessed);
    };

    auto getCoordinates = [&](const string coorditates, char delim, int& x, int& y) {
      vector<string> subpieces = Global::split(coorditates, delim);
      return (subpieces.size() == 2 && Global::tryStringToInt(subpieces[0], x) && Global::tryStringToInt(subpieces[1], y));
    };

    bool responseIsError = false;
    bool suppressResponse = false;
    bool shouldQuitAfterResponse = false;
    bool maybeStartPondering = false;
    string response;

    if(command == "ABOUT") {
      response = "name=" + Version::getKataGoVersionForHelp();
    }
    else if(command == "END") {
      maybeSaveAvoidPatterns(true);
      shouldQuitAfterResponse = true;
      logger.write("Quit requested by controller");
    }
    else if(command == "START" || command == "RECTSTART") {
      maybeSaveAvoidPatterns(false);
      int newXSize = 0;
      int newYSize = 0;
      bool suc = false;
      if(pieces.size() == 1) {
        if(command == "START") {
          if(Global::tryStringToInt(pieces[0], newXSize)) {
            newYSize = newXSize;
            suc = true;
          }
        } else if(command == "RECTSTART") {
          if(getCoordinates(pieces[0], ',', newXSize, newYSize))
            suc = true;
        }
      } 

      if(!suc) {
        responseIsError = true;
        response = "Expected int argument for boardsize or pair of ints but got '" + Global::concat(pieces," ") + "'";
      } else if(newXSize < 2 || newYSize < 2 || newXSize > Board::MAX_LEN || newYSize > Board::MAX_LEN) {
        responseIsError = true;
        response = "this version not support " + to_string(newXSize) + "x" + to_string(newYSize) + " board";
      }
      else {
        engine->setOrResetBoardSize(cfg, logger, seedRand, newXSize, newYSize, logger.isLoggingToStderr());
        response = "OK";
      }
    }
    else if(command == "RESTART") {
      maybeSaveAvoidPatterns(false);
      engine->clearBoard();
      response = "OK";
    }
    else if (command == "INFO")
    {
      if (pieces.size() == 0) {}
      else
      {
        string subcommand = pieces[0];
        if (subcommand == "time_left")
        {
          // ќставшеес€ врем€ на матч (синхронизаци€ с пискворком)
          double time = 0;
          if (pieces.size() != 2 || !Global::tryStringToDouble(pieces[1], time)) {
            responseIsError = true;
            response = "Expected 1 arguments for info:time_left but got '" + Global::concat(pieces, " ") + "'";
          }
          else
          {
            engine->bTimeControls.mainTimeLeft = time / 1000.0;
            engine->wTimeControls.mainTimeLeft = time / 1000.0;
          }
        }
        else if(subcommand == "timeout_match") {
          // 1 команда временного контрол€ - врем€ на матч
          double time = 0;
          if(pieces.size() != 2 || !Global::tryStringToDouble(pieces[1], time)) {
            responseIsError = true;
            response = "Expected 1 arguments for info:timeout_match but got '" + Global::concat(pieces, " ") + "'";
          } else {
            engine->bTimeControls.originalMainTime = time / 1000.0;
            engine->wTimeControls.originalMainTime = time / 1000.0;
            engine->bTimeControls.mainTimeLeft = engine->bTimeControls.originalMainTime;
            engine->wTimeControls.mainTimeLeft = engine->wTimeControls.originalMainTime;
          }
        }
        else if(subcommand == "timeout_turn")
        {
          // 2 команда временного контрол€ - врем€ на ход
          double time = 0;
          if (pieces.size() != 2 || !Global::tryStringToDouble(pieces[1], time)) {
            responseIsError = true;
            response = "Expected 1 arguments for info:timeout_turn but got '" + Global::concat(pieces, " ") + "'";
          }
          else
          {
            engine->bTimeControls.maxTimePerMove = time / 1000.0;
            engine->wTimeControls.maxTimePerMove = time / 1000.0;
            // Ёкспериментальный режим без учЄта времени на партию - не рекомендуетс€
            //engine->bTimeControls.inTimePerMove = true;
            //engine->wTimeControls.inTimePerMove = true;
          }
        }
        else if(subcommand == "rule") {
          int rule = 0;
          if(pieces.size() != 2 || !Global::tryStringToInt(pieces[1], rule)) {
            responseIsError = true;
            response = "Expected 1 arguments for info:rule but got '" + Global::concat(pieces, " ") + "'";
          } else {
            if(rule !=0 && rule !=1 && rule != 4) {
              responseIsError = true;
              response = "Unsupported arguments for info:rule '" + Global::concat(pieces, " ") + "'";
            }
            else
            {
              if(rule == 0)
                engine->currentRules.basicRule = Rules::BASICRULE_FREESTYLE;
              if(rule == 1)
                engine->currentRules.basicRule = Rules::BASICRULE_STANDARD;
              if(rule == 4)
                engine->currentRules.basicRule = Rules::BASICRULE_RENJU;
            }
          }
        } else if(subcommand == "coordinates") {
          int coordinates = -1;
          if(pieces.size() != 2 || !Global::tryStringToInt(pieces[1], coordinates)) {
            responseIsError = true;
            response = "Expected 1 arguments for info:coordinates but got '" + Global::concat(pieces, " ") + "'";
          } else {
            if(coordinates < 0 || coordinates > 31) {
              responseIsError = true;
              response = "Unsupported arguments for info:coordinates '" + Global::concat(pieces, " ") + "'";
            }
            else {
              engine->logPVCoordinatesMode = coordinates;
            }
          }
        }
      }
    }
    else if (command == "BOARD") {
      engine->clearCache();
      engine->clearBoard();
      maybeSaveAvoidPatterns(false);

      string moveline;
      vector<Move> initialStones;
      Player p = P_BLACK;
      while (getline(cin, moveline))
      {
        //Convert , to spaces
        for (size_t i = 0; i < moveline.length(); i++)
          if (moveline[i] == ',')
            moveline[i] = ' ';

        moveline = Global::trim(moveline);
        //cout << moveline;
        if (moveline == "DONE")
        {
          bool debug = false;
          bool playChosenMove = true;
          engine->setPosition(initialStones);
          GomEngine::GenmoveArgs gargs;
          gargs.searchFactorWhenWinningThreshold = searchFactorWhenWinningThreshold;
          gargs.searchFactorWhenWinning = searchFactorWhenWinning;
          gargs.ogsChatToStderr = ogsChatToStderr;
          gargs.allowResignation = allowResignation;
          gargs.resignThreshold = resignThreshold;
          gargs.resignConsecTurns = resignConsecTurns;
          gargs.resignMinMovesPerBoardArea = resignMinMovesPerBoardArea;
          gargs.logSearchInfo = logSearchInfo;
          gargs.logSearchInfoForChosenMove = logSearchInfoForChosenMove;
          gargs.debug = debug;

          engine->genMove(
            p, logger, gargs, GomEngine::AnalyzeArgs(), playChosenMove, printGTPResponse, maybeStartPondering);
          suppressResponse = true;  // genmove handles it manually by calling printGTPResponse
          break;
        }
        else {
          stringstream ss(moveline);
          int x, y;
          ss >> x >> y;
          if(x < 0 || x >= engine->bot->getRootBoard().x_size || y < 0 || y >= engine->bot->getRootBoard().y_size)
          {
            responseIsError = true;
            response = "Move Outside Board";
          }
          else
          {
            Loc loc = Location::getLoc(x, y, engine->bot->getRootBoard().x_size);
            initialStones.push_back(Move(loc, p));
            p = getOpp(p);
          }
        }
      }
    } 
    else if(command == "SWAP2BOARD") {
      if(engine->currentRules.basicRule != Rules::BASICRULE_STANDARD)
        throw StringError("SWAP2 is only for STANDARD rule");
      engine->clearCache();
      engine->clearBoard();
      maybeSaveAvoidPatterns(false);

      string moveline;
      vector<Move> initialStones;
      Player p = P_BLACK;
      while(getline(cin, moveline)) {
        // Convert , to spaces
        for(size_t i = 0; i < moveline.length(); i++)
          if(moveline[i] == ',')
            moveline[i] = ' ';

        moveline = Global::trim(moveline);
        // cout << moveline;
        if(moveline == "DONE") {
          bool debug = false;
          bool playChosenMove = false;
          int swap2num = initialStones.size();
          if(swap2num == 0) {
            static const string openings[10] = {
              "5,0 6,0 6,1", 
              "3,14 12,14 4,14", 
              "7,0 11,0 5,0", 
              "7,0 9,0 7,1", 
              "1,1 2,1 3,2",
              "7,0 5,0 6,1",
              "1,2 0,0 2,1",
              "0,0 0,3 3,2",
              "8,1 9,0 10,0",
              "7,0 6,0 8,1",
            };
            int choice = seedRand.nextUInt(10);
            response = openings[choice];
          } 
          else if(swap2num == 3) {
            engine->setPosition(initialStones);
            double value = engine->searchAndGetValue(
              p, logger, swap2time / 10, logSearchInfo, logSearchInfoForChosenMove, response, responseIsError, GomEngine::AnalyzeArgs());

            string response1 = response;
            if(value < -0.15) {
              response = "SWAP";  
              cerr << "MESSAGE White winrate = " << 50 * (value + 1) << "%, So engine plays black" << endl;
            } 
            else if(value > 0.15) {
              cerr << "MESSAGE White winrate = " << 50 * (value + 1) << "%, So engine plays white" << endl;
            }  
            else {
              cerr << "MESSAGE White winrate = " << 50 * (value + 1) << "%, So randomly plays 2 moves" << endl;
              Loc blackLoc, whiteLoc;
              string random2response;
              getTwoRandomMove(engine->bot->getRootBoard(), whiteLoc, blackLoc, random2response);

              bool suc1 = engine->play(whiteLoc, C_WHITE);
              bool suc2 = engine->play(blackLoc, C_BLACK);
              if(!suc1 || !suc2) {
                cerr << "DEBUG unknown error" << endl;
                response = "SWAP";
              }
              double value2 = engine->searchAndGetValue(
                p, logger, swap2time / 20, logSearchInfo, logSearchInfoForChosenMove, response, responseIsError, GomEngine::AnalyzeArgs());
              if(value2 > -0.25 && value2 < 0.25) {
                cerr << "MESSAGE After these two moves, white winrate = " << 50 * (value2 + 1)
                     << "%, So engine plays these two moves" << endl;
                response = random2response;
              } else {
                if(value < 0)
                  response = "SWAP"; 
                else {
                  cerr << "MESSAGE After these two moves, white winrate = " << 50 * (value + 1)
                       << "%, So not play these two moves" << endl;
                  response = response1;
                }  
              }
            }
          }

          else if(swap2num == 5) {
            engine->setPosition(initialStones);
            double value = engine->searchAndGetValue(
              p, logger, swap2time / 10, logSearchInfo, logSearchInfoForChosenMove, response, responseIsError, GomEngine::AnalyzeArgs());
            if(value < 0) {
              cerr << "MESSAGE White winrate = " << 50 * (value + 1) << "%, So engine plays black" << endl;
              response = "SWAP";
            } else {
              cerr << "MESSAGE White winrate = " << 50 * (value + 1) << "%, So engine plays white" << endl;
            }
          }
          break;

        } 
        else {
          stringstream ss(moveline);
          int x, y;
          ss >> x >> y;
          if(x < 0 || x >= engine->bot->getRootBoard().x_size || y < 0 || y >= engine->bot->getRootBoard().y_size) {
            responseIsError = true;
            response = "Move Outside Board";
          } else {
            Loc loc = Location::getLoc(x, y, engine->bot->getRootBoard().x_size);
            initialStones.push_back(Move(loc, p));
            p = getOpp(p);
          }
        }
      }
    } else if(command == "TAKEBACK") {
      int x, y;
      if(pieces.size() != 1) {
        responseIsError = true;
        response = "Expected one argument for takeback but got '" + Global::concat(pieces, " ") + "'";
      } else if(getCoordinates(pieces[0], ',', x, y)) {
        engine->undo();
        response = "OK";
      } else {
        responseIsError = true;
        response = "Invalid coordinates for takeback '" + pieces[0] + "'";
      }
    } /* else if(command == "PLAY") {
      Player pla;
      Loc loc;
      if(pieces.size() != 1) {
        responseIsError = true;
        response = "Expected one argument for play but got '" + Global::concat(pieces," ") + "'";
      }
      else if(!PlayerIO::tryParsePlayer(pieces[0],pla)) {
        responseIsError = true;
        response = "Could not parse color: '" + pieces[0] + "'";
      }
      else if(!tryParseLoc(pieces[1],engine->bot->getRootBoard(),loc)) {
        responseIsError = true;
        response = "Could not parse vertex: '" + pieces[1] + "'";
      }
      else {
        bool suc = engine->play(loc,pla);
        if(!suc) {
          responseIsError = true;
          response = "illegal move";
        }
        maybeStartPondering = true;
      }
    }*/
    else if(command == "BEGIN") {
      const Board& b = engine->bot->getRootBoard();
      if(b.numStonesOnBoard() != 0) {
        responseIsError = true;
        response = "Board is not empty";
      }
      if(!responseIsError) {
        bool debug = false;
        bool playChosenMove = true;
        Player nextPla = P_BLACK;
        GomEngine::GenmoveArgs gargs;
        gargs.searchFactorWhenWinningThreshold = searchFactorWhenWinningThreshold;
        gargs.searchFactorWhenWinning = searchFactorWhenWinning;
        gargs.ogsChatToStderr = ogsChatToStderr;
        gargs.allowResignation = allowResignation;
        gargs.resignThreshold = resignThreshold;
        gargs.resignConsecTurns = resignConsecTurns;
        gargs.resignMinMovesPerBoardArea = resignMinMovesPerBoardArea;
        gargs.logSearchInfo = logSearchInfo;
        gargs.logSearchInfoForChosenMove = logSearchInfoForChosenMove;
        gargs.debug = debug;
        engine->genMove(
          nextPla, logger, gargs, GomEngine::AnalyzeArgs(), playChosenMove, printGTPResponse, maybeStartPondering);
      }
    }
    else if (command == "TURN") {
      const Board& b = engine->bot->getRootBoard();
      Player nextPla = b.numStonesOnBoard() % 2 ? P_WHITE : P_BLACK;
      Loc loc;
      if (pieces.size() != 1) {
        responseIsError = true;
        response = "Expected one argument for turn but got '" + Global::concat(pieces, " ") + "'";
      }
      else {
        int x, y;
        if(getCoordinates(pieces[0], ',', x, y)) {
          loc = Location::getLoc(x, y, engine->bot->getRootBoard().x_size);
          if(!engine->play(loc, nextPla)) {
            responseIsError = true;
            response = "illegal move for turn '" + pieces[0] + "'";
          }
        } else {
          responseIsError = true;
          response = "Invalid coordinates for turn '" + pieces[0] + "'";
        }
      }
      if (!responseIsError)  {
        bool debug = false;
        bool playChosenMove = true;
        nextPla = getOpp(nextPla);
        GomEngine::GenmoveArgs gargs;
        gargs.searchFactorWhenWinningThreshold = searchFactorWhenWinningThreshold;
        gargs.searchFactorWhenWinning = searchFactorWhenWinning;
        gargs.ogsChatToStderr = ogsChatToStderr;
        gargs.allowResignation = allowResignation;
        gargs.resignThreshold = resignThreshold;
        gargs.resignConsecTurns = resignConsecTurns;
        gargs.resignMinMovesPerBoardArea = resignMinMovesPerBoardArea;
        gargs.logSearchInfo = logSearchInfo;
        gargs.logSearchInfoForChosenMove = logSearchInfoForChosenMove;
        gargs.debug = debug;
        engine->genMove(
          nextPla, logger, gargs, GomEngine::AnalyzeArgs(), playChosenMove, printGTPResponse, maybeStartPondering);
      }
    }
    else if(command == "setswap2time") {
      float newSwap2Time = 5400;
      if(pieces.size() != 1 || !Global::tryStringToFloat(pieces[0], newSwap2Time) || newSwap2Time <= 1) {
        responseIsError = true;
        response = "Expected single float argument for setSwap2Time but got '" + Global::concat(pieces, " ") + "'";
      } else {
        swap2time = newSwap2Time;
      }
    }
    else if(command == "clear_cache") {
      engine->clearCache();
    }
    else if(command == "showboard") {
      ostringstream sout;
      engine->bot->getRootHist().printBasicInfo(sout, engine->bot->getRootBoard());
      response = Global::trim(filterDoubleNewlines(sout.str()));
    }
    else {
      responseIsError = true;
      response = "unknown command";
    }

    if(!suppressResponse)
      printGTPResponse(response, responseIsError);

    if(logAllGTPCommunication)
      logger.write(response);

    if(shouldQuitAfterResponse)
      break;

    if(maybeStartPondering && ponderingEnabled)
      engine->ponder();

  } //Close read loop

  // Interrupt stuff if we close stdout
  if(currentlyAnalyzing) {
    currentlyAnalyzing = false;
    engine->stopAndWait();
    cout << endl;
  }
  if(currentlyGenmoving) {
    currentlyGenmoving = false;
    engine->stopAndWait();
  }

  maybeSaveAvoidPatterns(true);
  delete engine;
  engine = NULL;
  NeuralNet::globalCleanup();

  logger.write("All cleaned up, quitting");
  return 0;
}
