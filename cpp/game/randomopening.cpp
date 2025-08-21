#include "../game/randomopening.h"
#include "../game/gamelogic.h"
#include "../core/rand.h"
#include "../program/playutils.h"

extern bool extra_time_log;
//std::mutex outMutex2;

using namespace RandomOpening;
using namespace std;
//std::atomic<int64_t> triedCount(0);
//std::atomic<int64_t> succeedCount(0);
//std::atomic<int64_t> evalCount(0);

static Loc getRandomNearbyMove(Board& board, Rand& gameRand, double avgDist) {
  int xsize = board.x_size, ysize = board.y_size;
  const double middleBonusFactor = 1.5;

  if(board.isEmpty()) {
    double xd = gameRand.nextGaussianTruncated(middleBonusFactor * 0.999) / (2 * middleBonusFactor);
    double yd = gameRand.nextGaussianTruncated(middleBonusFactor * 0.999) / (2 * middleBonusFactor);
    int x = round(xd * xsize + 0.5 * (xsize - 1)), y = round(xd * xsize + 0.5 * (xsize - 1));
    if(x < 0 || x >= xsize || y < 0 || y >= ysize)
      ASSERT_UNREACHABLE;
    Loc loc = Location::getLoc(x, y, xsize);
    return loc;
  }

  const double halfBoardLen = std::max(0.5 * (xsize - 1), 0.5 * (ysize - 1));
  const double avgDist2 = avgDist*avgDist;
  std::vector<double> prob(xsize * ysize, 0);
  std::vector<double> middleBonus(xsize * ysize, 0);

  for(int x = 0; x < xsize; x++)
    for(int y = 0; y < ysize; y++) {
      Loc loc = Location::getLoc(x, y, xsize);
      if(board.colors[loc] != C_EMPTY)
        continue;
      double distFromCenter = std::max(std::abs(x - 0.5 * (xsize - 1)), std::abs(y - 0.5 * (ysize - 1)));
      middleBonus[y*xsize + x] = 1 + middleBonusFactor * (halfBoardLen - distFromCenter) / halfBoardLen;
    }

  for(int x1 = 0; x1 < xsize; x1++)
    for(int y1 = 0; y1 < ysize; y1++) {
      Loc loc = Location::getLoc(x1, y1, xsize);
      if(board.colors[loc] == C_EMPTY)
        continue;
      for(int x2 = 0; x2 < xsize; x2++)
        for(int y2 = 0; y2 < ysize; y2++) {
          if(middleBonus[y2 * xsize + x2]==0)
            continue;
          double prob_increase = middleBonus[y2 * xsize + x2] * pow((x2-x1)*(x2-x1) + (y2-y1)*(y2-y1) + avgDist2, -2);
          prob[y2 * xsize + x2] += prob_increase;
        }
    }

  double totalProb = 0;
  for(int x = 0; x < xsize; x++)
    for(int y = 0; y < ysize; y++) {
      totalProb += prob[y * xsize + x];
    }

  for(int x = 0; x < xsize; x++)
    for(int y = 0; y < ysize; y++) {
      prob[y * xsize + x] /= totalProb;
    }

  double randomDouble = gameRand.nextDouble() - 1e-8;

  double probSum = 0;
  for(int x = 0; x < xsize; x++)
    for(int y = 0; y < ysize; y++) {
      probSum += prob[y * xsize + x];
      if(probSum >= randomDouble) {
        return Location::getLoc(x, y, xsize);
      }
    }

  ASSERT_UNREACHABLE;
}

static double getBoardValue(Search* bot, Board& board, const BoardHistory& hist, Player nextPlayer) {
  // evalCount++;
  NNEvaluator* nnEval = bot->nnEvaluator;

  MiscNNInputParams nnInputParams;

  //nnInputParams.noResultUtilityForWhite = bot->searchParams.noResultUtilityForWhite;
  nnInputParams.useVCFInput = false;
  nnInputParams.suppressPass = false;
  
  NNResultBuf buf;

  auto start = std::chrono::steady_clock::now();

  nnEval->evaluate(board, hist, nextPlayer, nnInputParams, buf, false);

  std::shared_ptr<NNOutput> nnOutput = std::move(buf.result);
  double value = nnOutput->whiteWinProb - nnOutput->whiteLossProb;

  if(nextPlayer == P_BLACK)
    value = -value;

  if(extra_time_log) {
    auto finish = std::chrono::steady_clock::now();
    auto time = std::chrono::duration_cast<std::chrono::milliseconds>(finish - start);
    //lock_guard<std::mutex> lock(outMutex2);
    cout << "getBoardValue : value " << value << " for " << (nextPlayer == P_BLACK ? "black " : "white ")
         << hist.getMoves(board.x_size, board.y_size) << " rule = " << hist.rules.toString() << " " << time.count()
         << " ms " << std::endl;
  }

  return value;
}

static Loc getBalanceMove(
  Search* botB,
  Search* botW,
  Board& board,
  const BoardHistory& hist,
  Player nextPlayer,
  Rand& gameRand,
  bool forSelfplay,
  double rejectProb,
  bool logGenerate) {
  int xsize = board.x_size, ysize = board.y_size;

  // Search* bot = gameRand.nextBool(0.5) ? botB : botW;
  Search* bot;
  if(nextPlayer == P_BLACK)
    bot = botB;
  else
    bot = botW;

  double maxProb = 0;
  Player oppPlayer = getOpp(nextPlayer);

  double rootValuePla = getBoardValue(bot, board, hist, nextPlayer);

  if(rootValuePla < 0) {  // probably all moves are losing
    double rejectFactor = 1 - exp(-3 * rootValuePla * rootValuePla);
    if(gameRand.nextBool(rejectFactor) && gameRand.nextBool(rejectProb)) {
      return Board::NULL_LOC;
    }
  }

  // 13.04.25 - for version 1.16
  Board boardCopy(board);
  BoardHistory histCopy(hist);
  histCopy.makeBoardMoveAssumeLegal(boardCopy, Board::PASS_LOC, nextPlayer);
  if(oppPlayer == P_BLACK)
    bot = botB;
  else
    bot = botW;

  //double rootValueOpp = getBoardValue(bot, board, hist, oppPlayer);
  double rootValueOpp = getBoardValue(bot, boardCopy, histCopy, oppPlayer);
  if(rootValueOpp < 0) {  // probably all moves are winning, even pass
    double rejectFactor = 1 - exp(-3 * rootValueOpp * rootValueOpp);
    if(gameRand.nextBool(rejectFactor) && gameRand.nextBool(rejectProb)) {
      return Board::NULL_LOC;
    }
  }

  bool shouldCheckNearbyStones = rootValueOpp > 0 && board.stonenum > 0;

  std::vector<double> prob(xsize * ysize, 0);

  for(int x = 0; x < xsize; x++)
    for(int y = 0; y < ysize; y++) {
      Loc loc = Location::getLoc(x, y, xsize);

      if(!board.isLegal(loc, nextPlayer))
        continue;

      // check whether it nears some stones
      if(shouldCheckNearbyStones) {
        bool nearExistingStone = false;
        for(int x1 = x - 3; x1 <= x + 3; x1++) {
          for(int y1 = y - 3; y1 <= y + 3; y1++) {
            if(x1 < 0 || x1 >= xsize || y1 < 0 || y1 >= ysize)
              continue;
            Loc loc1 = Location::getLoc(x1, y1, xsize);
            assert(board.isOnBoard(loc1));
            if(board.colors[loc1] != C_EMPTY)
              nearExistingStone = true;
            if(nearExistingStone)
              break;
          }
          if(nearExistingStone)
            break;
        }
        if(!nearExistingStone)
          continue;
      }

      Board boardCopy(board);
      BoardHistory histCopy(hist);

      histCopy.makeBoardMoveAssumeLegal(boardCopy, loc, nextPlayer);
      if(histCopy.isGameFinished)
        continue;

      if(oppPlayer == P_BLACK)
        bot = botB;
      else
        bot = botW;

      double value = getBoardValue(bot, boardCopy, histCopy, oppPlayer);

      double p = forSelfplay ? pow(1 - value * value, 4) : pow(1 - value * value, 10);
      maxProb = std::max(maxProb, p);
      prob[y * xsize + x] = p;
    }

  if(gameRand.nextBool(1 - maxProb) && gameRand.nextBool(rejectProb)) {
    return Board::NULL_LOC;
  }

  double totalProb = 0;
  for(int x = 0; x < xsize; x++)
    for(int y = 0; y < ysize; y++) {
      totalProb += prob[y * xsize + x];
    }
  for(int x = 0; x < xsize; x++)
    for(int y = 0; y < ysize; y++) {
      prob[y * xsize + x] /= totalProb;
    }

  double randomDouble = gameRand.nextDouble() - 1e-8;

  double probSum = 0;
  for(int x = 0; x < xsize; x++)
    for(int y = 0; y < ysize; y++) {
      probSum += prob[y * xsize + x];
      if(probSum >= randomDouble) {
        return Location::getLoc(x, y, xsize);
      }
    }

  // some rare conditions, return NULL_LOC.
  if(logGenerate) {
    //lock_guard<std::mutex> lock(outMutex2);
    cout << "totalProb=" << totalProb << ", probSum=" << probSum
              << " in getBalanceMove(), rule=" << hist.rules.toString() << std::endl;
  }
  return Board::NULL_LOC;
  /*while (1)
  {
    int x = gameRand.nextUInt(xsize);
    int y = gameRand.nextUInt(ysize);
    Loc loc=Location::getLoc(x, y, xsize);
    if (board.isLegal(loc, nextPlayer, true))
      return loc;
  }
  ASSERT_UNREACHABLE;*/
}

static int tryInitializeBalancedRandomOpening(
  Search* botB,
  Search* botW,
  Board& board,
  BoardHistory& hist,
  Player& nextPlayer,
  Rand& gameRand,
  double rejectProb,
  const GameRunner* gameRunner,
  const OtherGameProperties& otherGameProps,
  const GenerationProperties& genProps)
{
  Board boardCopy(board);
  BoardHistory histCopy(hist);
  Player nextPlayerCopy = nextPlayer;

  // for generate and searcn balance
  // histCopy.rules.VCNRule = Rules::VCNRULE_NOVC;

  bool firstPassWin = hist.rules.firstPassWin;
  int maxMoves = hist.rules.maxMoves; 
  float komi = hist.rules.komi;

  histCopy.rules.firstPassWin = false;
  histCopy.rules.maxMoves = 0;
  histCopy.rules.komi = 0.0;

  PlaySettings playSettings = gameRunner->getPlaySettings();
  
  auto start = std::chrono::steady_clock::now();

  // triedCount++;
  if(genProps.genPolicy) {
    // Place moves with policy
    double avgPolicyInitMoveNum = otherGameProps.isSgfPos ? playSettings.startPosesPolicyInitAvgMoveNum : playSettings.policyInitAvgMoveNum;
    if(avgPolicyInitMoveNum > 0) {
      double temperature = playSettings.policyInitAreaTemperature;
      assert(temperature > 0.0 && temperature < 10.0);
      PlayUtils::initializeGameUsingPolicy(
        botB, botW, boardCopy, histCopy, nextPlayerCopy, gameRand, avgPolicyInitMoveNum, temperature, playSettings.policyInitGaussMoveNum);
      if(histCopy.isGameFinished)
        return 0;
    } else
      return -1;
  } 
  if(genProps.genAvgDist) {
    std::vector<float> randomMoveNumProb = gameRunner->getGameInitializer()->getRandomMoveNumProb(hist.rules.VCNRule);

    int maxRandomMoveNum = randomMoveNumProb.size();

    if(maxRandomMoveNum == 0) {
      //if(playSettings.logGenerate) {
        //lock_guard<std::mutex> lock(outMutex2);
        cout << Rules::writeVCNRule(hist.rules.VCNRule) << " does not support balanced openings init" << endl;
      //}
      return -1;
    }

    static const double avgRandomDistFactor = 0.8;
    //int randomMoveNum = gameRand.nextUInt((double*)randomMoveNumProb.data(), randomMoveNumProb.size());

    double randomMoveNumProbTotal = 0;
    for(int i = 0; i < maxRandomMoveNum; i++)
      randomMoveNumProbTotal += randomMoveNumProb[i];
    double randomMoveNumProbSum = 0;
    double randomMoveNumProbRandomDouble = gameRand.nextDouble() * randomMoveNumProbTotal - 1e-7;
    int randomMoveNum = -1;

    for(int i = 0; i < maxRandomMoveNum; i++) {
      randomMoveNumProbSum += randomMoveNumProb[i];
      if(randomMoveNumProbSum >= randomMoveNumProbRandomDouble) {
        randomMoveNum = i;
        break;
      }
    }
    if(randomMoveNum == -1) {
      // ASSERT_UNREACHABLE;
      //if(playSettings.logGenerate) {
        // lock_guard<std::mutex> lock(outMutex2);
        cout << Rules::writeVCNRule(hist.rules.VCNRule) << " randomMoveNum = -1" << endl;
      //}
      return -1;
    }

    double avgDist = gameRand.nextExponential() * avgRandomDistFactor;
    for(int i = 0; i < randomMoveNum; i++) {
      Loc randomLoc = getRandomNearbyMove(boardCopy, gameRand, avgDist);
      histCopy.makeBoardMoveAssumeLegal(boardCopy, randomLoc, nextPlayerCopy);

      if(histCopy.isGameFinished)
        return 0;
      nextPlayerCopy = getOpp(nextPlayerCopy);
    }
  }

  if(extra_time_log) {
    auto finish = std::chrono::steady_clock::now();
    auto time = std::chrono::duration_cast<std::chrono::milliseconds>(finish - start);
    ostringstream out;
    out << "Generate with ";
    if(genProps.genAvgDist)
      out << "avgDist ";
    else
      out << "policy ";
    out << histCopy.getMovenum() << " moves :" << histCopy.getMoves(boardCopy.x_size, boardCopy.y_size)
        << " rule = " << histCopy.rules.toString()  << " for "
        << time.count() << " ms" << std::endl;

    //lock_guard<std::mutex> lock(outMutex2);
    cout << out.str();
  }

  if(genProps.isBalancsMove) {
    start = std::chrono::steady_clock::now();
    Loc balancedMove = getBalanceMove(
      botB,
      botW,
      boardCopy,
      histCopy,
      nextPlayerCopy,
      gameRand,
      playSettings.forSelfPlay,
      rejectProb,
      playSettings.logGenerate);

    if(balancedMove == Board::NULL_LOC)
      return 0;

    string moves = histCopy.getMoves(boardCopy.x_size, boardCopy.y_size);
    histCopy.makeBoardMoveAssumeLegal(boardCopy, balancedMove, nextPlayerCopy);
    if(histCopy.isGameFinished)
      return 0;

    if(extra_time_log) {
      auto finish = std::chrono::steady_clock::now();
      auto time = std::chrono::duration_cast<std::chrono::milliseconds>(finish - start);
      //lock_guard<std::mutex> lock(outMutex2);
      cout << "getBalanceMove : rule " << histCopy.rules.toString() << " moves :" << moves
           << (nextPlayerCopy == P_BLACK ? " black move " : " white move ")
           << histCopy.getMovenum() << "." << Global::toLower(Location::toString(balancedMove, boardCopy.x_size, boardCopy.y_size))
           << " for " << time.count() << " ms" << std::endl;
    }
    nextPlayerCopy = getOpp(nextPlayerCopy);
  }

  board = boardCopy;
  hist = histCopy;
  hist.rules.firstPassWin = firstPassWin;
  hist.rules.maxMoves = maxMoves;
  hist.rules.komi = komi;
  nextPlayer = nextPlayerCopy;

  return 1;
}

void RandomOpening::initializeBalancedRandomOpening(
  Search* botB,
  Search* botW,
  Board& board,
  BoardHistory& hist,
  Player& nextPlayer,
  Rand& gameRand,
  const GameRunner* gameRunner,
  const OtherGameProperties& otherGameProps) {
  int tryTimes = 0;
  double rejectProb = 0.995;

  PlaySettings playSettings = gameRunner->getPlaySettings();

  GenerationProperties genProps;
  
  genProps.genPolicy = playSettings.initGamesWithPolicy && otherGameProps.allowPolicyInit;
  genProps.genAvgDist = playSettings.initGamesWithAvgDist;

  if(!genProps.genPolicy && !genProps.genAvgDist)
    return;

  genProps.isBalancsMove = gameRand.nextBool(playSettings.addBalanceMoveProb / 100.0);

  if(genProps.genPolicy && genProps.genAvgDist) {
    genProps.genPolicy = gameRand.nextBool(playSettings.policyInitProb / 100.0);
    genProps.genAvgDist = !genProps.genPolicy;
  }

  /*
  BoardHistory histCopy = hist;
  histCopy.rules.VCNRule = Rules::VCNRULE_NOVC;
  histCopy.rules.firstPassWin = false;
  histCopy.rules.maxMoves = 0;
  histCopy.rules.komi = 0.0;
  */

  auto start = std::chrono::steady_clock::now();

  int count = 0;
  int balatce = 0;
  while(balatce == 0) {
    balatce = tryInitializeBalancedRandomOpening(
      botB, botW, board, hist, nextPlayer, gameRand, rejectProb, gameRunner, otherGameProps, genProps);
    if(playSettings.maxTryTimes == 0)
      break;

    count++;
    tryTimes++;
    if(tryTimes > playSettings.maxTryTimes) {
      tryTimes = 0;
      if(playSettings.logGenerate) {
        //lock_guard<std::mutex> lock(outMutex2);
        cout << "Reached max trying times for finding balanced openings, Rule=" << hist.rules.toString()
                  << std::endl;
      }
      rejectProb = 0.8;
    }
    if(count > 1000) {
      if(playSettings.logGenerate) {
        // lock_guard<std::mutex> lock(outMutex2);
        cout << "Reached greater 1000 times for finding balanced openings, Rule=" << hist.rules.toString()
             << std::endl;
      }
      break;
    }
  }

  if(extra_time_log && count > 1 && balatce == 1) {
    auto finish = std::chrono::steady_clock::now();
    auto time = std::chrono::duration_cast <std::chrono::milliseconds> (finish - start); 
    ostringstream out;
    out << "Generate try : " << count;
    if(genProps.isBalancsMove)
      out << " rejectProb= " << rejectProb;
    out << " for " << time.count() << " ms" << std::endl;
    //lock_guard<std::mutex> lock(outMutex2);
    cout << out.str();
  }

  if(playSettings.logGenerate && hist.getMovenum() >0 && balatce == 1) {
    //lock_guard<std::mutex> lock(outMutex2);
    cout << "   Rule:" << Rules::writeBasicRule(hist.rules.basicRule) << " ("
         << Rules::writeVCNRule(hist.rules.VCNRule)
         << ") [" << board.x_size << "x" << board.y_size
         << "] moves : " << hist.getMoves(board.x_size, board.y_size) << " last move: " << hist.getMovenum()
         << endl;
  }

  // succeedCount++;
  // if(succeedCount%500==0) {
  //   cout << "Generated " << succeedCount << " openings,"
  //        << " tried " << triedCount << ", nneval " << evalCount << endl;
  //  }
}

