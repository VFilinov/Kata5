#include "../game/boardhistory.h"
#include "../game/gamelogic.h"
#include <algorithm>

using namespace std;

BoardHistory::BoardHistory()
  :rules(),
   moveHistory(),
   initialBoard(),
   initialPla(P_BLACK),
   initialTurnNumber(0),
   blackPassNum(0),
   whitePassNum(0),
   recentBoards(),
   currentRecentBoardIdx(0),
   presumedNextMovePla(P_BLACK),
   isGameFinished(false),winner(C_EMPTY),
   isNoResult(false),isResignation(false)
{
}

BoardHistory::~BoardHistory()
{}

BoardHistory::BoardHistory(const Board& board, Player pla, const Rules& r)
  :rules(r),
   moveHistory(),
   initialBoard(),
   initialPla(),
   initialTurnNumber(0),
   blackPassNum(0),
   whitePassNum(0),
   recentBoards(),
   currentRecentBoardIdx(0),
   presumedNextMovePla(pla),
   isGameFinished(false),winner(C_EMPTY),
   isNoResult(false),isResignation(false)
{

  clear(board,pla,rules);
}

BoardHistory::BoardHistory(const BoardHistory& other)
  :rules(other.rules),
   moveHistory(other.moveHistory),
   initialBoard(other.initialBoard),
   initialPla(other.initialPla),
   initialTurnNumber(other.initialTurnNumber),
   blackPassNum(other.blackPassNum),
   whitePassNum(other.whitePassNum),
   recentBoards(),
   currentRecentBoardIdx(other.currentRecentBoardIdx),
   presumedNextMovePla(other.presumedNextMovePla),
   isGameFinished(other.isGameFinished),winner(other.winner),
   isNoResult(other.isNoResult),isResignation(other.isResignation)
{
  std::copy(other.recentBoards, other.recentBoards+NUM_RECENT_BOARDS, recentBoards);
}


BoardHistory& BoardHistory::operator=(const BoardHistory& other)
{
  if(this == &other)
    return *this;
  rules = other.rules;
  moveHistory = other.moveHistory;
  initialBoard = other.initialBoard;
  initialPla = other.initialPla;
  initialTurnNumber = other.initialTurnNumber;
  blackPassNum = other.blackPassNum;
  whitePassNum = other.whitePassNum;
  std::copy(other.recentBoards, other.recentBoards + NUM_RECENT_BOARDS, recentBoards);
  currentRecentBoardIdx = other.currentRecentBoardIdx;
  presumedNextMovePla = other.presumedNextMovePla;
  isGameFinished = other.isGameFinished;
  winner = other.winner;
  isNoResult = other.isNoResult;
  isResignation = other.isResignation;

  return *this;
}

BoardHistory::BoardHistory(BoardHistory&& other) noexcept
 :rules(other.rules),
  moveHistory(std::move(other.moveHistory)),
  initialBoard(other.initialBoard),
  initialPla(other.initialPla),
  initialTurnNumber(other.initialTurnNumber),
  blackPassNum(other.blackPassNum),
  whitePassNum(other.whitePassNum),
  recentBoards(),
  currentRecentBoardIdx(other.currentRecentBoardIdx),
  presumedNextMovePla(other.presumedNextMovePla),
  isGameFinished(other.isGameFinished),winner(other.winner),
  isNoResult(other.isNoResult),isResignation(other.isResignation)
{
  std::copy(other.recentBoards, other.recentBoards+NUM_RECENT_BOARDS, recentBoards);
}

BoardHistory& BoardHistory::operator=(BoardHistory&& other) noexcept
{
  rules = other.rules;
  moveHistory = std::move(other.moveHistory);
  initialBoard = other.initialBoard;
  initialPla = other.initialPla;
  initialTurnNumber = other.initialTurnNumber;
  blackPassNum = other.blackPassNum;
  whitePassNum = other.whitePassNum;
  std::copy(other.recentBoards, other.recentBoards + NUM_RECENT_BOARDS, recentBoards);
  currentRecentBoardIdx = other.currentRecentBoardIdx;
  presumedNextMovePla = other.presumedNextMovePla;
  isGameFinished = other.isGameFinished;
  winner = other.winner;
  isNoResult = other.isNoResult;
  isResignation = other.isResignation;

  return *this;
}

void BoardHistory::clear(const Board& board, Player pla, const Rules& r) {
  rules = r;
  moveHistory.clear();

  initialBoard = board;
  initialPla = pla;
  initialTurnNumber = 0;
  blackPassNum = 0;
  whitePassNum = 0;

  //This makes it so that if we ask for recent boards with a lookback beyond what we have a history for,
  //we simply return copies of the starting board.
  for(int i = 0; i<NUM_RECENT_BOARDS; i++)
    recentBoards[i] = board;
  currentRecentBoardIdx = 0;

  presumedNextMovePla = pla;


  isGameFinished = false;
  winner = C_EMPTY;
  isNoResult = false;
  isResignation = false;

}

BoardHistory BoardHistory::copyToInitial() const {
  BoardHistory hist(initialBoard, initialPla, rules);
  hist.setInitialTurnNumber(initialTurnNumber);
  return hist;
}

/*
float BoardHistory::whiteKomiAdjustmentForDraws(double drawEquivalentWinsForWhite) const {
  //We fold the draw utility into the komi, for input into things like the neural net.
  //Basically we model it as if the final score were jittered by a uniform draw from [-0.5,0.5].
  //E.g. if komi from self perspective is 7 and a draw counts as 0.75 wins and 0.25 losses,
  //then komi input should be as if it was 7.25, which in a jigo game when jittered by 0.5 gives white 75% wins and 25% losses.
  float drawAdjustment = rules.gameResultWillBeInteger() ? (float)(drawEquivalentWinsForWhite - 0.5) : 0.0f;
  return drawAdjustment;
}
*/

void BoardHistory::setInitialTurnNumber(int64_t n) {
  initialTurnNumber = n;
}

void BoardHistory::printBasicInfo(ostream& out, const Board& board) const {
  Board::printBoard(out, board, Board::NULL_LOC, &moveHistory);
  out << "Next player: " << PlayerIO::playerToString(presumedNextMovePla) << endl;
  out << "Rules: " << rules.toJsonString() << endl;
}

void BoardHistory::printDebugInfo(ostream& out, const Board& board) const {
  out << board << endl;
  out << "Initial pla " << PlayerIO::playerToString(initialPla) << endl;
  out << "Rules " << rules << endl;
  out << "Presumed next pla " << PlayerIO::playerToString(presumedNextMovePla) << endl;
  out << "Game result " << isGameFinished << " " << PlayerIO::playerToString(winner) << " "
      << isNoResult << " " << isResignation << endl;
  out << "Last moves ";
  for(int i = 0; i<moveHistory.size(); i++)
    out << Location::toString(moveHistory[i].loc,board) << " ";
  out << endl;
}


const Board& BoardHistory::getRecentBoard(int numMovesAgo) const {
  assert(numMovesAgo >= 0 && numMovesAgo < NUM_RECENT_BOARDS);
  int idx = (currentRecentBoardIdx - numMovesAgo + NUM_RECENT_BOARDS) % NUM_RECENT_BOARDS;
  return recentBoards[idx];
}

void BoardHistory::setWinnerByResignation(Player pla) {  isGameFinished = true;
  isNoResult = false;
  isResignation = true;
  winner = pla;
}

void BoardHistory::setWinner(Player pla) {
  isGameFinished = true;
  isNoResult = false;
  isResignation = false;
  winner = pla;
}

bool BoardHistory::isLegal(const Board& board, Loc moveLoc, Player movePla) const {
  if(!board.isLegal(moveLoc,movePla))
    return false;

  return true;
}

int64_t BoardHistory::getCurrentTurnNumber() const {
  return std::max((int64_t)0,initialTurnNumber + (int64_t)moveHistory.size());
}

bool BoardHistory::isLegalTolerant(const Board& board, Loc moveLoc, Player movePla) const {
  return board.isLegal(moveLoc, movePla);
}
bool BoardHistory::makeBoardMoveTolerant(Board& board, Loc moveLoc, Player movePla) {
  if(!board.isLegal(moveLoc,movePla))
    return false;
  makeBoardMoveAssumeLegal(board,moveLoc,movePla);
  return true;
}


void BoardHistory::makeBoardMoveAssumeLegal(Board& board, Loc moveLoc, Player movePla) {

  //If somehow we're making a move after the game was ended, just clear those values and continue
  isGameFinished = false;
  winner = C_EMPTY;
  isNoResult = false;
  isResignation = false;

  board.playMoveAssumeLegal(moveLoc,movePla);

  if(moveLoc == Board::PASS_LOC) {
    if(movePla == C_BLACK)
      blackPassNum++;
    if(movePla == C_WHITE)
      whitePassNum++;
  }

  //Update recent boards
  currentRecentBoardIdx = (currentRecentBoardIdx + 1) % NUM_RECENT_BOARDS;
  recentBoards[currentRecentBoardIdx] = board;

  moveHistory.push_back(Move(moveLoc,movePla));
  presumedNextMovePla = getOpp(movePla);

  Color maybeWinner = GameLogic::checkWinnerAfterPlayed(board, *this, movePla, moveLoc);
  if(maybeWinner == C_BLACK || maybeWinner == C_WHITE) {  // game finished
    setWinner(maybeWinner);
  } else if(fullBoard(0) /* moveHistory.size() == board.x_size * board.y_size*/) {
    // full board 
    isNoResult = true;
    isGameFinished = true;
  }
}

bool BoardHistory::fullBoard(int restMoves, int depth) const {
  return getCurrentTurnNumber() + depth >=
    getRecentBoard(0).x_size * getRecentBoard(0).y_size - restMoves;
}

bool BoardHistory::maybePassMove(Player pla, int depth, int restMoves) const {
  Player nextPla = pla;
  for(int i = 0; i < depth; i++)
    nextPla = getOpp(nextPla);

  if(rules.basicRule == Rules::BASICRULE_RENJU && fullBoard(restMoves, depth) && nextPla == P_BLACK) 
    return true;

  return false;
}

Hash128 BoardHistory::getSituationRulesHash(
  const Board& board,
  const BoardHistory& hist,
  Player nextPlayer 
  )
{
  Hash128 hash = board.pos_hash;
  hash ^= Board::ZOBRIST_PLAYER_HASH[nextPlayer];

  float selfKomi = hist.currentSelfKomi(nextPlayer);

  //Discretize the komi for the purpose of matching hash
  int64_t komiDiscretized = (int64_t)(selfKomi*256.0f);
  uint64_t komiHash = Hash::murmurMix((uint64_t)komiDiscretized);
  hash.hash0 ^= komiHash;
  hash.hash1 ^= Hash::basicLCong(komiHash);

  hash ^= hist.getPassnumHash();
  hash ^= hist.getRulesHash();

  return hash;
}

Hash128 BoardHistory::getPassnumHash() const {
  Hash128 hash = Hash128();
  hash ^= Hash128::mixInt(Rules::ZOBRIST_PASSNUM_B_HASH_BASE, blackPassNum);
  hash ^= Hash128::mixInt(Rules::ZOBRIST_PASSNUM_W_HASH_BASE, whitePassNum);
  return hash;
}

Hash128 BoardHistory::getRulesHash() const {
  Hash128 hash = Hash128();
  hash ^= Rules::ZOBRIST_BASIC_RULE_HASH[rules.basicRule];
  hash ^= Hash128::mixInt(Rules::ZOBRIST_VCNRULE_HASH_BASE, rules.VCNRule);
  hash ^= Hash128::mixInt(Rules::ZOBRIST_MAXMOVES_HASH_BASE, rules.maxMoves);
  if(rules.firstPassWin)
    hash ^= Rules::ZOBRIST_FIRSTPASSWIN_HASH;
  return hash;
}

int BoardHistory::getMovenum() const {
  return initialTurnNumber + moveHistory.size();
}

std::string BoardHistory::getMoves(int x_size, int y_size) const {
  std::string game;
  for(int i=0; i<moveHistory.size(); i++) {
    Loc moveLoc = moveHistory[i].loc;
    // ij coordinates
    game += Location::toStringPsq(moveLoc, x_size, y_size, 1);
  }
  return Global::toLower(game);
}

void BoardHistory::setKomi(float newKomi) {
  rules.komi = newKomi;
}

float BoardHistory::currentSelfKomi(Player pla) const {
  float whiteKomiAdjusted = rules.komi;

  if(pla == P_WHITE)
    return whiteKomiAdjusted;
  else if(pla == P_BLACK)
    return -whiteKomiAdjusted;
  else {
    assert(false);
    return 0.0f;
  }
}
