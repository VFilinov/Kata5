#include "../neuralnet/nninputs.h"
#include "../forbiddenPoint/ForbiddenPointFinder.h"

using namespace std;

int NNPos::xyToPos(int x, int y, int nnXLen) {
  return y * nnXLen + x;
}
int NNPos::locToPos(Loc loc, int boardXSize, int nnXLen, int nnYLen) {
  if(loc == Board::PASS_LOC)
    return nnXLen * nnYLen;
  else if(loc == Board::NULL_LOC)
    return nnXLen * (nnYLen + 1);
  return Location::getY(loc,boardXSize) * nnXLen + Location::getX(loc,boardXSize);
}
Loc NNPos::posToLoc(int pos, int boardXSize, int boardYSize, int nnXLen, int nnYLen) {
  if(pos == nnXLen * nnYLen)
    return Board::PASS_LOC;
  int x = pos % nnXLen;
  int y = pos / nnXLen;
  if(x < 0 || x >= boardXSize || y < 0 || y >= boardYSize)
    return Board::NULL_LOC;
  return Location::getLoc(x,y,boardXSize);
}

int NNPos::getPassPos(int nnXLen, int nnYLen) {
  return nnXLen * nnYLen;
}

bool NNPos::isPassPos(int pos, int nnXLen, int nnYLen) {
  return pos == nnXLen * nnYLen;
}

int NNPos::getPolicySize(int nnXLen, int nnYLen) {
  return nnXLen * nnYLen + 1;
}

//-----------------------------------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------------------------------

const Hash128 MiscNNInputParams::ZOBRIST_PLAYOUT_DOUBLINGS =
  Hash128(0xa5e6114d380bfc1dULL, 0x4160557f1222f4adULL);
const Hash128 MiscNNInputParams::ZOBRIST_NN_POLICY_TEMP =
  Hash128(0xebcbdfeec6f4334bULL, 0xb85e43ee243b5ad2ULL);
const Hash128 MiscNNInputParams::ZOBRIST_NO_RESULT_UTILITY =
  Hash128(0x391d245011c6cbf9ULL, 0xf39b923e18c67a82ULL);
const Hash128 MiscNNInputParams::ZOBRIST_USE_VCF =
  Hash128(0xd8b858bf3159e999ULL, 0x2eeaaa4d750b89e0ULL);
const Hash128 MiscNNInputParams::ZOBRIST_USE_FORBIDDEN_FEATURE =
  Hash128(0xc07f051dcf020534ULL, 0xf24d6bb55323e505ULL);
const Hash128 MiscNNInputParams::ZOBRIST_POLICY_OPTIMISM =
  Hash128(0x88415c85c2801955ULL, 0x39bdf76b2aaa5eb1ULL);
const Hash128 MiscNNInputParams::ZOBRIST_FOUR_POLICY_REDUCE_BASE =
 Hash128(0x80FF1EFC3F63C521ULL, 0xC5725C983B4B7D74ULL);

//const Hash128 MiscNNInputParams::ZOBRIST_ZERO_HISTORY =
//  Hash128(0x78f02afdd1aa4910ULL, 0xda78d550486fe978ULL);

//-----------------------------------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------------------------------

double ScoreValue::whiteWinsOfWinner(Player winner, double noResultUtilityForWhite) {
  if(winner == P_WHITE)
    return 1.0;
  else if(winner == P_BLACK)
    return 0.0;

  assert(winner == C_EMPTY);
  return noResultUtilityForWhite;
}

static const double twoOverPi = 0.63661977236758134308;
static const double piOverTwo = 1.57079632679489661923;


NNOutput::NNOutput()
  :noisedPolicyProbs(NULL)
{}
NNOutput::NNOutput(const NNOutput& other) {
  nnHash = other.nnHash;
  whiteWinProb = other.whiteWinProb;
  whiteLossProb = other.whiteLossProb;
  whiteNoResultProb = other.whiteNoResultProb;
  varTimeLeft = other.varTimeLeft;
  shorttermWinlossError = other.shorttermWinlossError;

  nnXLen = other.nnXLen;
  nnYLen = other.nnYLen;

  if(other.noisedPolicyProbs != NULL) {
    noisedPolicyProbs = new float[NNPos::MAX_NN_POLICY_SIZE];
    std::copy(other.noisedPolicyProbs, other.noisedPolicyProbs + NNPos::MAX_NN_POLICY_SIZE, noisedPolicyProbs);
  }
  else
    noisedPolicyProbs = NULL;

  policyOptimismUsed = other.policyOptimismUsed;
#ifdef QUANTIZED_OUTPUT
  std::copy(other.policyProbsQuantized, other.policyProbsQuantized + NNPos::MAX_NN_POLICY_SIZE, policyProbsQuantized); // from hzy
#else
  std::copy(other.policyProbs, other.policyProbs + NNPos::MAX_NN_POLICY_SIZE, policyProbs);
#endif
}

NNOutput::NNOutput(const vector<shared_ptr<NNOutput>>& others) {
  assert(others.size() < 1000000);
  int len = (int)others.size();
  float floatLen = (float)len;
  assert(len > 0);
  for(int i = 1; i<len; i++) {
    assert(others[i]->nnHash == others[0]->nnHash);
  }
  nnHash = others[0]->nnHash;

  whiteWinProb = 0.0f;
  whiteLossProb = 0.0f;
  whiteNoResultProb = 0.0f;
  varTimeLeft = 0.0f;
  shorttermWinlossError = 0.0f;
  for(int i = 0; i<len; i++) {
    const NNOutput& other = *(others[i]);
    whiteWinProb += other.whiteWinProb;
    whiteLossProb += other.whiteLossProb;
    whiteNoResultProb += other.whiteNoResultProb;
    varTimeLeft += other.varTimeLeft;
    shorttermWinlossError += other.shorttermWinlossError;
  }
  whiteWinProb /= floatLen;
  whiteLossProb /= floatLen;
  whiteNoResultProb /= floatLen;
  varTimeLeft /= floatLen;
  shorttermWinlossError /= floatLen;

  nnXLen = others[0]->nnXLen;
  nnYLen = others[0]->nnYLen;

  noisedPolicyProbs = NULL;

  //For technical correctness in case of impossibly rare hash collisions:
  //Just give up if they don't all match in move legality
  {
    bool mismatch = false;
#ifdef QUANTIZED_OUTPUT
    float policyProbs[NNPos::MAX_NN_POLICY_SIZE]; // from hzy
#endif
    std::fill(policyProbs, policyProbs + NNPos::MAX_NN_POLICY_SIZE, 0.0f);
    for(int i = 0; i<len; i++) {
      const NNOutput& other = *(others[i]);
      for(int pos = 0; pos<NNPos::MAX_NN_POLICY_SIZE; pos++) {
#ifdef QUANTIZED_OUTPUT
        float p = other.getPolicyProb(pos);             // from hzy
#else
        float p = other.policyProbs[pos];
#endif
        if(i > 0 && (policyProbs[pos] < 0) != (p < 0))
          mismatch = true;
        policyProbs[pos] += p;
      }
    }
    //In case of mismatch, just take the first one
    //This should basically never happen, only on true hash collisions
#ifdef QUANTIZED_OUTPUT
    // from hzy
    if(mismatch) {
      const NNOutput& other = *(others[0]);
      std::copy(
        other.policyProbsQuantized, other.policyProbsQuantized + NNPos::MAX_NN_POLICY_SIZE, policyProbsQuantized);
    } else {
      for(int pos = 0; pos < NNPos::MAX_NN_POLICY_SIZE; pos++)
        policyProbsQuantized[pos] = policyQuant(policyProbs[pos] / floatLen);
    }
#else
    if(mismatch) {
      const NNOutput& other = *(others[0]);
      std::copy(other.policyProbs, other.policyProbs + NNPos::MAX_NN_POLICY_SIZE, policyProbs);
    }
    else {
      for(int pos = 0; pos<NNPos::MAX_NN_POLICY_SIZE; pos++)
        policyProbs[pos] /= floatLen;
    }
#endif
  }
  {
    bool allOptimismsMatch = true;
    for(int i = 1; i < len; i++) {
      if(others[i]->policyOptimismUsed != others[0]->policyOptimismUsed) {
        allOptimismsMatch = false;
        break;
      }
    }
    if(allOptimismsMatch) {
      policyOptimismUsed = others[0]->policyOptimismUsed;
    } else {
      policyOptimismUsed = 0.0;
      for(int i = 0; i < len; i++) {
        policyOptimismUsed += others[i]->policyOptimismUsed / (float)len;
      }
    }
  }
}

NNOutput& NNOutput::operator=(const NNOutput& other) {
  if(&other == this)
    return *this;
  nnHash = other.nnHash;
  whiteWinProb = other.whiteWinProb;
  whiteLossProb = other.whiteLossProb;
  whiteNoResultProb = other.whiteNoResultProb;
  varTimeLeft = other.varTimeLeft;
  shorttermWinlossError = other.shorttermWinlossError;

  nnXLen = other.nnXLen;
  nnYLen = other.nnYLen;

  if(noisedPolicyProbs != NULL)
    delete[] noisedPolicyProbs;
  if(other.noisedPolicyProbs != NULL) {
    noisedPolicyProbs = new float[NNPos::MAX_NN_POLICY_SIZE];
    std::copy(other.noisedPolicyProbs, other.noisedPolicyProbs + NNPos::MAX_NN_POLICY_SIZE, noisedPolicyProbs);
  }
  else
    noisedPolicyProbs = NULL;

  //std::copy(other.policyProbs, other.policyProbs+NNPos::MAX_NN_POLICY_SIZE, policyProbs);
  policyOptimismUsed = other.policyOptimismUsed;

#ifdef QUANTIZED_OUTPUT
  std::copy(other.policyProbsQuantized, other.policyProbsQuantized + NNPos::MAX_NN_POLICY_SIZE, policyProbsQuantized);
#endif
  return *this;
}


NNOutput::~NNOutput() {
  if(noisedPolicyProbs != NULL) {
    delete[] noisedPolicyProbs;
    noisedPolicyProbs = NULL;
  }
}


void NNOutput::debugPrint(ostream& out, const Board& board) {
  out << "Win " << Global::strprintf("%.2fc",whiteWinProb*100) << endl;
  out << "Loss " << Global::strprintf("%.2fc",whiteLossProb*100) << endl;
  out << "NoResult " << Global::strprintf("%.2fc",whiteNoResultProb*100) << endl;
  out << "VarTimeLeft " << Global::strprintf("%.1f",varTimeLeft) << endl;
  out << "STWinlossError " << Global::strprintf("%.2fc", shorttermWinlossError * 100) << endl;
  out << "OptimismUsed " << Global::strprintf("%.2f", policyOptimismUsed) << endl;

  out << "Policy" << endl;
  out << "Pass" << Global::strprintf("%4d ", 
#ifdef QUANTIZED_OUTPUT
           (int)round(getPolicyProb(NNPos::getPassPos(nnXLen, nnYLen)) * 1000))
#else
           (int)round(policyProbs[NNPos::getPassPos(nnXLen, nnYLen)] * 1000))
#endif
      << endl;
  for(int y = 0; y < board.y_size; y++) {
    for(int x = 0; x<board.x_size; x++) {
      int pos = NNPos::xyToPos(x,y,nnXLen);
#ifdef QUANTIZED_OUTPUT
      float prob = getPolicyProb(pos);
#else
      float prob = policyProbs[pos];
#endif
      if(prob < 0)
        out << "   - ";
      else
        out << Global::strprintf("%4d ", (int)round(prob * 1000));
    }
    out << endl;
  }

}

//-------------------------------------------------------------------------------------------------------------

static void copyWithSymmetry(const float* src, float* dst, int nSize, int hSize, int wSize, int cSize, bool useNHWC, int symmetry, bool reverse) {
  bool transpose = (symmetry & 0x4) != 0 && hSize == wSize;
  bool flipX = (symmetry & 0x2) != 0;
  bool flipY = (symmetry & 0x1) != 0;
  if(transpose && !reverse)
    std::swap(flipX,flipY);
  if(useNHWC) {
    int nStride = hSize * wSize * cSize;
    int hStride = wSize * cSize;
    int wStride = cSize;
    int hBaseNew = 0; int hStrideNew = hStride;
    int wBaseNew = 0; int wStrideNew = wStride;

    if(flipY) { hBaseNew = (hSize-1) * hStrideNew; hStrideNew = -hStrideNew; }
    if(flipX) { wBaseNew = (wSize-1) * wStrideNew; wStrideNew = -wStrideNew; }

    if(transpose)
      std::swap(hStrideNew,wStrideNew);

    for(int n = 0; n<nSize; n++) {
      for(int h = 0; h<hSize; h++) {
        int nhOld = n * nStride + h*hStride;
        int nhNew = n * nStride + hBaseNew + h*hStrideNew;
        for(int w = 0; w<wSize; w++) {
          int nhwOld = nhOld + w*wStride;
          int nhwNew = nhNew + wBaseNew + w*wStrideNew;
          for(int c = 0; c<cSize; c++) {
            dst[nhwNew + c] = src[nhwOld + c];
          }
        }
      }
    }
  }
  else {
    int ncSize = nSize * cSize;
    int ncStride = hSize * wSize;
    int hStride = wSize;
    int wStride = 1;
    int hBaseNew = 0; int hStrideNew = hStride;
    int wBaseNew = 0; int wStrideNew = wStride;

    if(flipY) { hBaseNew = (hSize-1) * hStrideNew; hStrideNew = -hStrideNew; }
    if(flipX) { wBaseNew = (wSize-1) * wStrideNew; wStrideNew = -wStrideNew; }

    if(transpose)
      std::swap(hStrideNew,wStrideNew);

    for(int nc = 0; nc<ncSize; nc++) {
      for(int h = 0; h<hSize; h++) {
        int nchOld = nc * ncStride + h*hStride;
        int nchNew = nc * ncStride + hBaseNew + h*hStrideNew;
        for(int w = 0; w<wSize; w++) {
          int nchwOld = nchOld + w*wStride;
          int nchwNew = nchNew + wBaseNew + w*wStrideNew;
          dst[nchwNew] = src[nchwOld];
        }
      }
    }
  }
}


void SymmetryHelpers::copyInputsWithSymmetry(const float* src, float* dst, int nSize, int hSize, int wSize, int cSize, bool useNHWC, int symmetry) {
  copyWithSymmetry(src, dst, nSize, hSize, wSize, cSize, useNHWC, symmetry, false);
}

void SymmetryHelpers::copyOutputsWithSymmetry(const float* src, float* dst, int nSize, int hSize, int wSize, int symmetry) {
  copyWithSymmetry(src, dst, nSize, hSize, wSize, 1, false, symmetry, true);
}

int SymmetryHelpers::invert(int symmetry) {
  if(symmetry == 5)
    return 6;
  if(symmetry == 6)
    return 5;
  return symmetry;
}

int SymmetryHelpers::compose(int firstSymmetry, int nextSymmetry) {
  if(isTranspose(firstSymmetry))
    nextSymmetry = (nextSymmetry & 0x4) | ((nextSymmetry & 0x2) >> 1) | ((nextSymmetry & 0x1) << 1);
  return firstSymmetry ^ nextSymmetry;
}

int SymmetryHelpers::compose(int firstSymmetry, int nextSymmetry, int nextNextSymmetry) {
  return compose(compose(firstSymmetry,nextSymmetry),nextNextSymmetry);
}

Loc SymmetryHelpers::getSymLoc(int x, int y, int xSize, int ySize, int symmetry) {
  bool transpose = (symmetry & 0x4) != 0;
  bool flipX = (symmetry & 0x2) != 0;
  bool flipY = (symmetry & 0x1) != 0;
  if(flipX) { x = xSize - x - 1; }
  if(flipY) { y = ySize - y - 1; }

  if(transpose)
    std::swap(x,y);
  return Location::getLoc(x,y,transpose ? ySize : xSize);
}

Loc SymmetryHelpers::getSymLoc(int x, int y, const Board& board, int symmetry) {
  return getSymLoc(x,y,board.x_size,board.y_size,symmetry);
}

Loc SymmetryHelpers::getSymLoc(Loc loc, const Board& board, int symmetry) {
  if(loc == Board::NULL_LOC || loc == Board::PASS_LOC)
    return loc;
  return getSymLoc(Location::getX(loc,board.x_size), Location::getY(loc,board.x_size), board, symmetry);
}

Loc SymmetryHelpers::getSymLoc(Loc loc, int xSize, int ySize, int symmetry) {
  if(loc == Board::NULL_LOC || loc == Board::PASS_LOC)
    return loc;
  return getSymLoc(Location::getX(loc,xSize), Location::getY(loc,xSize), xSize, ySize, symmetry);
}


Board SymmetryHelpers::getSymBoard(const Board& board, int symmetry) {
  bool transpose = (symmetry & 0x4) != 0;
  bool flipX = (symmetry & 0x2) != 0;
  bool flipY = (symmetry & 0x1) != 0;
  Board symBoard(
    transpose ? board.y_size : board.x_size,
    transpose ? board.x_size : board.y_size
  );
  for(int y = 0; y<board.y_size; y++) {
    for(int x = 0; x<board.x_size; x++) {
      Loc loc = Location::getLoc(x,y,board.x_size);
      int symX = flipX ? board.x_size - x - 1 : x;
      int symY = flipY ? board.y_size - y - 1 : y;
      if(transpose)
        std::swap(symX,symY);
      Loc symLoc = Location::getLoc(symX,symY,symBoard.x_size);
      bool suc = symBoard.setStone(symLoc,board.colors[loc]);
      assert(suc);
      (void)suc;
    }
  }
  return symBoard;
}

void SymmetryHelpers::markDuplicateMoveLocs(
  const Board& board,
  const BoardHistory& hist,
  const std::vector<int>* onlySymmetries,
  const std::vector<int>& avoidMoves,
  bool* isSymDupLoc,
  std::vector<int>& validSymmetries
) {
  std::fill(isSymDupLoc, isSymDupLoc + Board::MAX_ARR_SIZE, false);
  validSymmetries.clear();
  validSymmetries.reserve(SymmetryHelpers::NUM_SYMMETRIES);
  validSymmetries.push_back(0);


  //If board has different sizes of x and y, we will not search symmetries involved with transpose.
  int symmetrySearchUpperBound = board.x_size == board.y_size ? SymmetryHelpers::NUM_SYMMETRIES : SymmetryHelpers::NUM_SYMMETRIES_WITHOUT_TRANSPOSE;

  for(int symmetry = 1; symmetry < symmetrySearchUpperBound; symmetry++) {
    if(onlySymmetries != NULL && !contains(*onlySymmetries,symmetry))
      continue;

    bool isBoardSym = true;
    for(int y = 0; y < board.y_size; y++) {
      for(int x = 0; x < board.x_size; x++) {
        Loc loc = Location::getLoc(x, y, board.x_size);
        Loc symLoc = getSymLoc(x, y, board,symmetry);
        bool isStoneSym = (board.colors[loc] == board.colors[symLoc]);
        if(!isStoneSym ) {
          isBoardSym = false;
          break;
        }
      }
      if(!isBoardSym)
        break;
    }
    if(isBoardSym)
      validSymmetries.push_back(symmetry);
  }

  //The way we iterate is to achieve https://senseis.xmp.net/?PlayingTheFirstMoveInTheUpperRightCorner%2FDiscussion
  //Reverse the iteration order for white, so that natural openings result in white on the left and black on the right
  //as is common now in SGFs
  if(hist.presumedNextMovePla == P_BLACK) {
    for(int x = board.x_size-1; x >= 0; x--) {
      for(int y = 0; y < board.y_size; y++) {
        Loc loc = Location::getLoc(x, y, board.x_size);
        if(avoidMoves.size() > 0 && avoidMoves[loc] > 0)
          continue;
        for(int symmetry: validSymmetries) {
          if(symmetry == 0)
            continue;
          Loc symLoc = getSymLoc(x, y, board, symmetry);
          if(!isSymDupLoc[loc] && loc != symLoc)
            isSymDupLoc[symLoc] = true;
        }
      }
    }
  }
  else {
    for(int x = 0; x < board.x_size; x++) {
      for(int y = board.y_size-1; y >= 0; y--) {
        Loc loc = Location::getLoc(x, y, board.x_size);
        if(avoidMoves.size() > 0 && avoidMoves[loc] > 0)
          continue;
        for(int symmetry: validSymmetries) {
          if(symmetry == 0)
            continue;
          Loc symLoc = getSymLoc(x, y, board, symmetry);
          if(!isSymDupLoc[loc] && loc != symLoc)
            isSymDupLoc[symLoc] = true;
        }
      }
    }
  }
}

static double
getSymmetryDifference(const Board& board, const Board& other, int symmetry, double maxDifferenceToReport) {
  double thisDifference = 0.0;
  for(int y = 0; y < board.y_size; y++) {
    for(int x = 0; x < board.x_size; x++) {
      Loc loc = Location::getLoc(x, y, board.x_size);
      Loc symLoc = SymmetryHelpers::getSymLoc(x, y, board, symmetry);
      // Difference!
      if(board.colors[loc] != other.colors[symLoc]) {
        // One of them was empty, the other was a stone
        if(board.colors[loc] == C_EMPTY || other.colors[symLoc] == C_EMPTY)
          thisDifference += 1.0;
        // Differing stones - triple the penalty
        else
          thisDifference += 3.0;

        if(thisDifference > maxDifferenceToReport)
          return maxDifferenceToReport;
      }
    }
  }
  return thisDifference;
}

// For each symmetry, return a metric about the "amount" of difference that board would have with other
// if symmetry were applied to board.
void SymmetryHelpers::getSymmetryDifferences(
  const Board& board,
  const Board& other,
  double maxDifferenceToReport,
  double symmetryDifferences[SymmetryHelpers::NUM_SYMMETRIES]) {
  for(int symmetry = 0; symmetry < SymmetryHelpers::NUM_SYMMETRIES; symmetry++)
    symmetryDifferences[symmetry] = maxDifferenceToReport;

  // Don't bother handling ultra-fancy transpose logic
  if(board.x_size != other.x_size || board.y_size != other.y_size)
    return;

  int numSymmetries = SymmetryHelpers::NUM_SYMMETRIES;
  if(board.x_size != board.y_size)
    numSymmetries = SymmetryHelpers::NUM_SYMMETRIES_WITHOUT_TRANSPOSE;

  for(int symmetry = 0; symmetry < numSymmetries; symmetry++) {
    symmetryDifferences[symmetry] = getSymmetryDifference(board, other, symmetry, maxDifferenceToReport);
  }
}

//-------------------------------------------------------------------------------------------------------------

static void setRowBin(float* rowBin, int pos, int feature, float value, int posStride, int featureStride) {
  rowBin[pos * posStride + feature * featureStride] = value;
}

//Currently does NOT depend on history (except for marking ko-illegal spots)
Hash128 NNInputs::getHash(
  const Board& board, const BoardHistory& hist, Player nextPlayer,
  const MiscNNInputParams& nnInputParams
) {
  //Hash128 hash =
  //  BoardHistory::getSituationRulesHash(board, hist, nextPlayer);
  Hash128 hash = BoardHistory::getSituationRulesHash(
    board, hist, nextPlayer /*, nnInputParams.noResultUtilityForWhite * 0.5 + 0.5*/);


  //Fold in whether the game is over or not, since this affects how we compute input features
  //but is not a function necessarily of previous hashed values.
  //If the history is in a weird prolonged state, also treat it similarly.
  if(hist.isGameFinished )
    hash ^= Board::ZOBRIST_GAME_IS_OVER;

  // Distinguish between forcing the history to always be empty and allow any nonempty amount
  // We already tolerate caching and reusing evals across distinct transpositions with different
  // history, for performance reasons, but the case of no history at all is probably distinct
  // enough that we should distinguish it.
  /*if(nnInputParams.maxHistory <= 0) {
    hash.hash0 += MiscNNInputParams::ZOBRIST_ZERO_HISTORY.hash0;
    hash.hash1 += MiscNNInputParams::ZOBRIST_ZERO_HISTORY.hash1;
  }*/
  #ifdef USE_VCF
  if(nnInputParams.useVCFInput)
    hash ^= MiscNNInputParams::ZOBRIST_USE_VCF;
  #endif
  if(nnInputParams.useForbiddenInput)
    hash ^= MiscNNInputParams::ZOBRIST_USE_FORBIDDEN_FEATURE;

  //Fold in asymmetric playout indicator
  if(nnInputParams.playoutDoublingAdvantage != 0) {
    int64_t playoutDoublingsDiscretized = (int64_t)(nnInputParams.playoutDoublingAdvantage*256.0f);
    hash.hash0 += Hash::splitMix64((uint64_t)playoutDoublingsDiscretized);
    hash.hash1 += Hash::basicLCong((uint64_t)playoutDoublingsDiscretized);
    hash ^= MiscNNInputParams::ZOBRIST_PLAYOUT_DOUBLINGS;
  }

  //Fold in policy temperature
  if(nnInputParams.nnPolicyTemperature != 1.0f) {
    int64_t nnPolicyTemperatureDiscretized = (int64_t)(nnInputParams.nnPolicyTemperature*2048.0f);
    hash.hash0 ^= Hash::basicLCong2((uint64_t)nnPolicyTemperatureDiscretized);
    hash.hash1 = Hash::splitMix64(hash.hash1 + (uint64_t)nnPolicyTemperatureDiscretized);
    hash.hash0 += hash.hash1;
    hash ^= MiscNNInputParams::ZOBRIST_NN_POLICY_TEMP;
  }

  if (nnInputParams.fourAttackPolicyReduce != 1.0f) {
    int64_t fourAttackPolicyReduceDiscretized = (int64_t)(nnInputParams.fourAttackPolicyReduce * 2048.0f);

    Hash128 h = MiscNNInputParams::ZOBRIST_FOUR_POLICY_REDUCE_BASE;
    h.hash0 = Hash::rrmxmx(h.hash0 + fourAttackPolicyReduceDiscretized);
    h.hash1 = h.hash0 * Hash::rrmxmx(h.hash1 + fourAttackPolicyReduceDiscretized);
    hash ^= h;
  }

  // Fold in noResultUtilityForWhite
  if(nnInputParams.noResultUtilityForWhite != 0.0f) {
    int64_t noResultUtilityForWhiteDiscretized = (int64_t)(nnInputParams.noResultUtilityForWhite * 128.0f);
    hash.hash0 ^= Hash::rrmxmx((uint64_t)noResultUtilityForWhiteDiscretized);
    hash.hash1 = Hash::rrmxmx(hash.hash1 + (uint64_t)noResultUtilityForWhiteDiscretized);
    hash.hash0 += hash.hash1;
    hash ^= MiscNNInputParams::ZOBRIST_NO_RESULT_UTILITY;
    // Fold in noResultUtilityForWhite
    /*
    int64_t noResultUtilityForWhiteDiscretized = (int64_t)(nnInputParams.noResultUtilityForWhite * 2048.0f);
    hash.hash0 ^= Hash::murmurMix((uint64_t)noResultUtilityForWhiteDiscretized);
    hash.hash1 = Hash::rrmxmx(hash.hash1 + (uint64_t)noResultUtilityForWhiteDiscretized);
    hash.hash0 += hash.hash1;
    */
  }

  // Fold in noResultUtilityForWhite
  int64_t noResultUtilityForWhiteDiscretized = (int64_t)(nnInputParams.noResultUtilityForWhite * 2048.0f);
  hash.hash0 ^= Hash::murmurMix((uint64_t)noResultUtilityForWhiteDiscretized);
  hash.hash1 = Hash::rrmxmx(hash.hash1 + (uint64_t)noResultUtilityForWhiteDiscretized);
  hash.hash0 += hash.hash1;


  // Fold in policy optimism
  if(nnInputParams.policyOptimism > 0) {
    hash ^= MiscNNInputParams::ZOBRIST_POLICY_OPTIMISM;
    int64_t policyOptimismDiscretized = (int64_t)(nnInputParams.policyOptimism * 1024.0);
    hash.hash0 = Hash::rrmxmx(Hash::splitMix64(hash.hash0) + (uint64_t)policyOptimismDiscretized);
    hash.hash1 = Hash::rrmxmx(hash.hash1 + hash.hash0 + (uint64_t)policyOptimismDiscretized);
  }

  return hash;
}


//===========================================================================================
//INPUTSVERSION 97
//===========================================================================================
/*
void NNInputs::fillRowV97(
  const Board& board, const BoardHistory& hist, Player nextPlayer,
  const MiscNNInputParams& nnInputParams,
  int nnXLen, int nnYLen, bool useNHWC, float* rowBin, float* rowGlobal
) {
  assert(nnXLen <= NNPos::MAX_BOARD_LEN);
  assert(nnYLen <= NNPos::MAX_BOARD_LEN);
  assert(board.x_size <= nnXLen);
  assert(board.y_size <= nnYLen);
  std::fill(rowBin,rowBin+NUM_FEATURES_SPATIAL_V7*nnXLen*nnYLen,false);
  std::fill(rowGlobal,rowGlobal+NUM_FEATURES_GLOBAL_V7,0.0f);

  Player pla = nextPlayer;
  Player opp = getOpp(pla);
  int xSize = board.x_size;
  int ySize = board.y_size;

  int featureStride;
  int posStride;
  if(useNHWC) {
    featureStride = 1;
    posStride = NNInputs::NUM_FEATURES_SPATIAL_V7;
  }
  else {
    featureStride = nnXLen * nnYLen;
    posStride = 1;
  }

  bool hasForbiddenFeature = nnInputParams.useForbiddenInput&&hist.rules.basicRule == Rules::BASICRULE_RENJU;

  CForbiddenPointFinder fpf(board.x_size);
  if (hasForbiddenFeature) {

    for (int x = 0; x < board.x_size; x++)
      for (int y = 0; y < board.y_size; y++) {
        fpf.SetStone(x, y, board.colors[Location::getLoc(x, y, board.x_size)]);
      }
  }

  for (int y = 0; y < ySize; y++) {
    for (int x = 0; x < xSize; x++) {
      int pos = NNPos::xyToPos(x, y, nnXLen);
      Loc loc = Location::getLoc(x, y, xSize);

      //Feature 0 - on board
      setRowBin(rowBin, pos, 0, 1.0f, posStride, featureStride);

      Color stone = board.colors[loc];

      //Features 1,2 - pla,opp stone
      if (stone == pla)
        setRowBin(rowBin, pos, 1, 1.0f, posStride, featureStride);
      else if (stone == opp)
        setRowBin(rowBin, pos, 2, 1.0f, posStride, featureStride);

      if (hasForbiddenFeature) {
        if (pla == C_BLACK) {
          if (fpf.isForbidden(x, x)) setRowBin(rowBin, pos, 3, 1.0f, posStride, featureStride);
        }
        else if (pla == C_WHITE) {
          if (fpf.isForbidden(x, x)) setRowBin(rowBin, pos, 4, 1.0f, posStride, featureStride);
        }
      }
    }
  }

  rowGlobal[5] = (nextPlayer == P_BLACK ? -1 : 1);

}
*/

//===========================================================================================
//INPUTSVERSION 7
//===========================================================================================


void NNInputs::fillRowV7(
  const Board& board, const BoardHistory& hist, Player nextPlayer,
  const MiscNNInputParams& nnInputParams,
  int nnXLen, int nnYLen, bool useNHWC, float* rowBin, float* rowGlobal
) {
  assert(nnXLen <= NNPos::MAX_BOARD_LEN);
  assert(nnYLen <= NNPos::MAX_BOARD_LEN);
  assert(board.x_size <= nnXLen);
  assert(board.y_size <= nnYLen);
  std::fill(rowBin,rowBin+NUM_FEATURES_SPATIAL_V7*nnXLen*nnYLen,false);
  std::fill(rowGlobal,rowGlobal+NUM_FEATURES_GLOBAL_V7,0.0f);

  Player pla = nextPlayer;
  Player opp = getOpp(pla);
  int xSize = board.x_size;
  int ySize = board.y_size;

  int featureStride;
  int posStride;
  if(useNHWC) {
    featureStride = 1;
    posStride = NNInputs::NUM_FEATURES_SPATIAL_V7;
  }
  else {
    featureStride = nnXLen * nnYLen;
    posStride = 1;
  }

  bool hasForbiddenFeature = nnInputParams.useForbiddenInput && hist.rules.basicRule == Rules::BASICRULE_RENJU;

  CForbiddenPointFinder fpf(board.x_size);
  if (hasForbiddenFeature) {

    for (int x = 0; x < board.x_size; x++)
      for (int y = 0; y < board.y_size; y++) {
        fpf.SetStone(x, y, board.colors[Location::getLoc(x, y, board.x_size)]);
      }
  }

  for (int y = 0; y < ySize; y++) {
    for (int x = 0; x < xSize; x++) {
      int pos = NNPos::xyToPos(x, y, nnXLen);
      Loc loc = Location::getLoc(x, y, xSize);

      //Feature 0 - on board
      setRowBin(rowBin, pos, 0, 1.0f, posStride, featureStride);

      Color stone = board.colors[loc];

      //Features 1,2 - pla,opp stone
      if (stone == pla)
        setRowBin(rowBin, pos, 1, 1.0f, posStride, featureStride);
      else if (stone == opp)
        setRowBin(rowBin, pos, 2, 1.0f, posStride, featureStride);

      if (hasForbiddenFeature)
      {
        if (pla == C_BLACK) {
          if (fpf.isForbidden(x, y)) setRowBin(rowBin, pos, 3, 1.0f, posStride, featureStride);
        }
        else if (pla == C_WHITE) {
          if (fpf.isForbidden(x, y)) setRowBin(rowBin, pos, 4, 1.0f, posStride, featureStride);
        }
      }
    }
  }

  rowGlobal[5] = (nextPlayer == P_BLACK ? -1 : 1);

  #ifdef USE_VCF
  if(nnInputParams.useVCFInput) {
    GameLogic::ResultsBeforeNN resultsBeforeNN = nnInputParams.resultsBeforeNN;
    if(!resultsBeforeNN.inited) {
      resultsBeforeNN.init(board, hist, nextPlayer);
    }
    if(resultsBeforeNN.inited) {
      if(board.isOnBoard(resultsBeforeNN.myOnlyLoc))
        setRowBin(rowBin, NNPos::locToPos(resultsBeforeNN.myOnlyLoc, board.x_size, nnXLen, nnYLen), 5, 1.0f, posStride, featureStride);
      if(resultsBeforeNN.calculatedVCF) {
        if(resultsBeforeNN.winner == nextPlayer)
          rowGlobal[7] = 1.0;  // can win by five/lifeFour/vcf
        else if(resultsBeforeNN.myVCFresult == 2)
          rowGlobal[8] = 1.0;  // cannot vcf
        else if(resultsBeforeNN.myVCFresult == 3) ;
        //  rowGlobal[11] = 1.0;  // at least no short vcf
        else
          ASSERT_UNREACHABLE;
        if(resultsBeforeNN.oppVCFresult == 1)
          rowGlobal[9] = 1.0;  // opp can vcf
        else if(resultsBeforeNN.oppVCFresult == 2)
          rowGlobal[10] = 1.0;  // opp cannot vcf
        else if(resultsBeforeNN.oppVCFresult == 3) ;
          // rowGlobal[12] = 1.0;  // at least no short vcf
        else
          ASSERT_UNREACHABLE;
      }
    }
  }
  #endif
  rowGlobal[11] = nextPlayer == P_BLACK ?  - nnInputParams.noResultUtilityForWhite : nnInputParams.noResultUtilityForWhite;
  //rowGlobal[11] = 0;


  //Used for handicap play
  //Parameter 15 is used because there's actually a discontinuity in how training behavior works when this is
  //nonzero, no matter how slightly.
  if(nnInputParams.playoutDoublingAdvantage != 0) {
    rowGlobal[15] = 1.0;
    rowGlobal[16] = (float)(0.5 * nnInputParams.playoutDoublingAdvantage);
  }

}

//===========================================================================================
//INPUTSVERSION 10
//===========================================================================================

void NNInputs::fillRowV10(
  const Board& board, const BoardHistory& hist, Player nextPlayer,
  const MiscNNInputParams& nnInputParams,
  int nnXLen, int nnYLen, bool useNHWC, float* rowBin, float* rowGlobal
) {
  assert(nnXLen <= NNPos::MAX_BOARD_LEN);
  assert(nnYLen <= NNPos::MAX_BOARD_LEN);
  assert(board.x_size <= nnXLen);
  assert(board.y_size <= nnYLen);
  std::fill(rowBin,rowBin+NUM_FEATURES_SPATIAL_V7*nnXLen*nnYLen,false);
  std::fill(rowGlobal,rowGlobal+NUM_FEATURES_GLOBAL_V7,0.0f);

  Player pla = nextPlayer;
  Player opp = getOpp(pla);
  int xSize = board.x_size;
  int ySize = board.y_size;

  int featureStride;
  int posStride;
  if(useNHWC) {
    featureStride = 1;
    posStride = NNInputs::NUM_FEATURES_SPATIAL_V7;
  }
  else {
    featureStride = nnXLen * nnYLen;
    posStride = 1;
  }

  // Go
  // Board features:
  // SetRowBin(rowBin,pos,feature,val, posStride, featureStride);
  // Feature 0 - on board
  // Features 1,2 - pla,opp stone
  // Features 3,4,5 - 1,2,3 libs
  // Feature 6 - ko-ban locations, including possibly superko.
  // Feature 6,7,8 - in the encore, no-second-ko-capture locations, encore ko prohibitions where we have to pass for ko
  // Features 18,19 - current territory, not counting group tax
  // History features 9,10,11,12,13
  // Ladder features 14,15,16,17
  // Features 20, 21 - second encore starting stones
  // 
  // Global features:
  // The first 5 of them were set already above to flag which of the past 5 moves were passes.
  // Ko rule - 6,7
  // Suicide - 8
  // Scoring rule (Rules::SCORING_TERRITORY) - 9
  // Tax - 10,11
  // Encore phase - 12,13
  // Does a pass end the current phase given the ruleset and history? - 14
  // Used for handicap play (PDA) - 15,16
  // Button - 17
  // Wave - 18

  // Five in row
  // Board features:
  // Feature 0 - on board
  // Features 1,2 - pla,opp stone
  // Features 3,4 - pla - forbidden, opp - forbidden
  // Feature 5 - single location after vcf
  // History features 6,7,8,9,10
  
  // Global features:
  // Rule Standard - 3
  // Rule renju - 4
  // 0 - standard or freestyle, 1 - renju, black move, -1 - renju, white move - fearture 5
  // forbidden feature - 6
  // can win by five / lifeFour / vcf - 7
  // cannot vcf - 8
  // opp can vcf - 9
  // opp cannot vcf - 10
  // at least no short vcf pla - 11
  // at least no short vcf opp - 12
  // noResultUtilityForWhite - 13 (not used)
  // PDA - 15,16

  bool hasForbiddenFeature = nnInputParams.useForbiddenInput && hist.rules.basicRule == Rules::BASICRULE_RENJU;
  rowGlobal[6] = hasForbiddenFeature;

  CForbiddenPointFinder fpf(board.x_size);
  if (hasForbiddenFeature) {
    for (int x = 0; x < board.x_size; x++)
      for (int y = 0; y < board.y_size; y++) {
        fpf.SetStone(x, y, board.colors[Location::getLoc(x, y, board.x_size)]);
      }
  }

  for (int y = 0; y < ySize; y++) {
    for (int x = 0; x < xSize; x++) {
      int pos = NNPos::xyToPos(x, y, nnXLen);
      Loc loc = Location::getLoc(x, y, xSize);

      //Feature 0 - on board
      setRowBin(rowBin, pos, 0, 1.0f, posStride, featureStride);

      Color stone = board.colors[loc];

      //Features 1,2 - pla,opp stone
      if (stone == pla)
        setRowBin(rowBin, pos, 1, 1.0f, posStride, featureStride);
      else if (stone == opp)
        setRowBin(rowBin, pos, 2, 1.0f, posStride, featureStride);

      if (hasForbiddenFeature) {
        if (pla == C_BLACK) {
          if (fpf.isForbidden(x, y))
            setRowBin(rowBin, pos, 3, 1.0f, posStride, featureStride);
        }
        else if (pla == C_WHITE) {
          if (fpf.isForbidden(x, y))
            setRowBin(rowBin, pos, 4, 1.0f, posStride, featureStride);
        }
      }
    }
  }

  rowGlobal[3] = hist.rules.basicRule == Rules::BASICRULE_STANDARD;
  rowGlobal[4] = hist.rules.basicRule == Rules::BASICRULE_RENJU;
  rowGlobal[5] = hist.rules.basicRule == Rules::BASICRULE_RENJU ? (nextPlayer == P_BLACK ? -1 : 1) :  0.0;
  #ifdef USE_VCF
  if (nnInputParams.useVCFInput) {
    GameLogic::ResultsBeforeNN resultsBeforeNN = nnInputParams.resultsBeforeNN;
    if(!resultsBeforeNN.inited)
      resultsBeforeNN.init(board, hist, nextPlayer);

    if(resultsBeforeNN.inited) {
      if(board.isOnBoard(resultsBeforeNN.myOnlyLoc))
        setRowBin(rowBin, NNPos::locToPos(resultsBeforeNN.myOnlyLoc, board.x_size, nnXLen, nnYLen), 5, 1.0f, posStride, featureStride);
      if(resultsBeforeNN.calculatedVCF) {
        if(resultsBeforeNN.winner == nextPlayer)
          rowGlobal[7] = 1.0;  // can win by five/lifeFour/vcf
        else if(resultsBeforeNN.myVCFresult == 2)
          rowGlobal[8] = 1.0;  // cannot vcf
        else if(resultsBeforeNN.myVCFresult == 3)
          rowGlobal[11] = 1.0;  // at least no short vcf
        else
          ASSERT_UNREACHABLE;
        if(resultsBeforeNN.oppVCFresult == 1)
          rowGlobal[9] = 1.0;  // opp can vcf
        else if(resultsBeforeNN.oppVCFresult == 2)
          rowGlobal[10] = 1.0;  // opp cannot vcf
        else if(resultsBeforeNN.oppVCFresult == 3)
          rowGlobal[12] = 1.0;  // at least no short vcf
        else
          ASSERT_UNREACHABLE;
      }
    }
  }
  #endif
  rowGlobal[13] = nextPlayer == P_BLACK ?  - nnInputParams.noResultUtilityForWhite : nnInputParams.noResultUtilityForWhite;
  //rowGlobal[13] = 0;


  //Used for handicap play
  //Parameter 15 is used because there's actually a discontinuity in how training behavior works when this is
  //nonzero, no matter how slightly.
  if(nnInputParams.playoutDoublingAdvantage != 0) {
    rowGlobal[15] = 1.0;
    rowGlobal[16] = (float)(0.5 * nnInputParams.playoutDoublingAdvantage);
  }

  // Hide history from the net if a pass would end things and we're behaving as if a pass won't.
  // Or if the game is in fact over right now!
  /*
  int maxTurnsOfHistoryToInclude = 5;
  if(hist.isGameFinished) {
    // Include one of the passes, at the end of that sequence
    maxTurnsOfHistoryToInclude = 1;
  }
  maxTurnsOfHistoryToInclude = std::min(maxTurnsOfHistoryToInclude, nnInputParams.maxHistory);
  int numTurnsOfHistoryIncluded = 0;
  // Features 6,7,8,9,10
  if(maxTurnsOfHistoryToInclude > 0) {
    const vector<Move>& moveHistory = hist.moveHistory;
    size_t moveHistoryLen = moveHistory.size();
    assert(moveHistoryLen >= hist.numApproxValidTurnsThisPhase);

    // Effectively wipe history as we change phase by also capping it
    int amountOfHistoryToTryToUse = maxTurnsOfHistoryToInclude;

    if(amountOfHistoryToTryToUse >= 1 && moveHistory[moveHistoryLen - 1].pla == opp) {
      Loc prev1Loc = moveHistory[moveHistoryLen - 1].loc;
      numTurnsOfHistoryIncluded = 1;
      if(prev1Loc == Board::PASS_LOC)
        rowGlobal[0] = 1.0;
      else if(prev1Loc != Board::NULL_LOC) {
        int pos = NNPos::locToPos(prev1Loc, xSize, nnXLen, nnYLen);
        setRowBin(rowBin, pos, 9, 1.0f, posStride, featureStride);
      }
      if(amountOfHistoryToTryToUse >= 2 && moveHistory[moveHistoryLen - 2].pla == pla) {
        Loc prev2Loc = moveHistory[moveHistoryLen - 2].loc;
        numTurnsOfHistoryIncluded = 2;
        if(prev2Loc == Board::PASS_LOC)
          rowGlobal[1] = 1.0;
        else if(prev2Loc != Board::NULL_LOC) {
          int pos = NNPos::locToPos(prev2Loc, xSize, nnXLen, nnYLen);
          setRowBin(rowBin, pos, 10, 1.0f, posStride, featureStride);
        }
        if(amountOfHistoryToTryToUse >= 3 && moveHistory[moveHistoryLen - 3].pla == opp) {
          Loc prev3Loc = moveHistory[moveHistoryLen - 3].loc;
          numTurnsOfHistoryIncluded = 3;
          if(prev3Loc == Board::PASS_LOC)
            rowGlobal[2] = 1.0;
          else if(prev3Loc != Board::NULL_LOC) {
            int pos = NNPos::locToPos(prev3Loc, xSize, nnXLen, nnYLen);
            setRowBin(rowBin, pos, 11, 1.0f, posStride, featureStride);
          }
          if(amountOfHistoryToTryToUse >= 4 && moveHistory[moveHistoryLen - 4].pla == pla) {
            Loc prev4Loc = moveHistory[moveHistoryLen - 4].loc;
            numTurnsOfHistoryIncluded = 4;
            if(prev4Loc == Board::PASS_LOC)
              rowGlobal[3] = 1.0;
            else if(prev4Loc != Board::NULL_LOC) {
              int pos = NNPos::locToPos(prev4Loc, xSize, nnXLen, nnYLen);
              setRowBin(rowBin, pos, 12, 1.0f, posStride, featureStride);
            }
            if(amountOfHistoryToTryToUse >= 5 && moveHistory[moveHistoryLen - 5].pla == opp) {
              Loc prev5Loc = moveHistory[moveHistoryLen - 5].loc;
              numTurnsOfHistoryIncluded = 5;
              if(prev5Loc == Board::PASS_LOC)
                rowGlobal[4] = 1.0;
              else if(prev5Loc != Board::NULL_LOC) {
                int pos = NNPos::locToPos(prev5Loc, xSize, nnXLen, nnYLen);
                setRowBin(rowBin, pos, 13, 1.0f, posStride, featureStride);
              }
            }
          }
        }
      }
    }
  }
  */
}


//===========================================================================================
//INPUTSVERSION 101
//===========================================================================================


void NNInputs::fillRowV101(
  const Board& board, const BoardHistory& hist, Player nextPlayer,
  const MiscNNInputParams& nnInputParams,
  int nnXLen, int nnYLen, bool useNHWC, float* rowBin, float* rowGlobal
) {
  assert(nnXLen <= NNPos::MAX_BOARD_LEN);
  assert(nnYLen <= NNPos::MAX_BOARD_LEN);
  assert(board.x_size <= nnXLen);
  assert(board.y_size <= nnYLen);
  std::fill(rowBin, rowBin + NUM_FEATURES_SPATIAL_V101 * nnXLen * nnYLen, false);
  std::fill(rowGlobal, rowGlobal + NUM_FEATURES_GLOBAL_V101, 0.0f);

  Player pla = nextPlayer;
  Player opp = getOpp(pla);
  int xSize = board.x_size;
  int ySize = board.y_size;

  int featureStride;
  int posStride;
  if (useNHWC) {
    featureStride = 1;
    posStride = NNInputs::NUM_FEATURES_SPATIAL_V101;
  }
  else {
    featureStride = nnXLen * nnYLen;
    posStride = 1;
  }

  // Чтобы быть совместимым со старой версией, порядок входных слоев очень беспорядочный

  //bf
  //0       onBoard
  //1       Собственная шахматная фигура
  //2       Шахматная фигура противника
  //3       Собственная запрещенная рука в черных шахматах
  //4       Запрещенная рука противника в черных шахматах
  //5       Выигрышные очки (если таковые имеются)

  //gf
  //3       Без бана/без бана 0, без бана шесть побед 1
  //4       Нет бана/Без бана шесть побед 0, есть бан 1
  //5       Нет бана/Нет бана шесть побед 0, есть бан на черных-1, есть бан на белых- 1
  //6       Следует ли использовать функцию запрещенной раздачи (две не запрещенные константы равны 0)
  //7~12    Собственный VCF и VCF противника (использовать ли vcf, каков результат vcf)
  //38      Является ли победное очко пасом (возможно только для защитников vcn)

  //13  Режим без VCN: Коэффициент выигрыша в шахматах, 1,0 - победа соперника в шахматах, -1,0 - победа соперника в шахматах
  //    VCN режим: 0
  //14  Режим без VCN: = Прошел ли противник уже
  //    VCN режим: 0
  // 
  //15,16   PDA
  // 
  //17  firstPassWin
  //18  firstPassWin И свой собственный первый pass
  //19  firstPassWin И свой собственный первый pass
  // 
  //20  Собственный vc1
  //21  Собственный vc2
  //22  Собственный vc3
  //23  Собственный vc4
  //24  Собственный vc5
  //25  сторона vc1
  //26  сторона vc2
  //27  сторона vc3
  //28  сторона vc4
  //29  сторона vc5
  //
  //30  maxmoves!=0
  //if(maxmoves!=0)
  // 31  maxmoves/boardarea
  // 32  moves/boardarea
  // 33  exp(-(maxmoves-moves)/50.0)
  // 34  exp(-(maxmoves-moves)/15.0)
  // 35  exp(-(maxmoves-moves)/5.0)
  // 36  exp(-(maxmoves-moves)/1.5)
  // 37 2*((maxmoves-moves)%2)-1


  bool hasForbiddenFeature = nnInputParams.useForbiddenInput && hist.rules.basicRule == Rules::BASICRULE_RENJU;
  
  CForbiddenPointFinder fpf(board.x_size);
  if (hasForbiddenFeature) {
    for (int x = 0; x < board.x_size; x++)
      for (int y = 0; y < board.y_size; y++)  {
        fpf.SetStone(x, y, board.colors[Location::getLoc(x, y, board.x_size)]);
      }
  }

  for (int y = 0; y < ySize; y++) {
    for (int x = 0; x < xSize; x++) {
      int pos = NNPos::xyToPos(x, y, nnXLen);
      Loc loc = Location::getLoc(x, y, xSize);

      //Feature 0 - on board
      setRowBin(rowBin, pos, 0, 1.0f, posStride, featureStride);

      Color stone = board.colors[loc];

      //Features 1,2 - pla,opp stone
      if (stone == pla)
        setRowBin(rowBin, pos, 1, 1.0f, posStride, featureStride);
      else if (stone == opp)
        setRowBin(rowBin, pos, 2, 1.0f, posStride, featureStride);

      if (hasForbiddenFeature)  {
        if (pla == C_BLACK) {
          if (fpf.isForbidden(x, y)) setRowBin(rowBin, pos, 3, 1.0f, posStride, featureStride);
        }
        else if (pla == C_WHITE) {
          if (fpf.isForbidden(x, y)) setRowBin(rowBin, pos, 4, 1.0f, posStride, featureStride);
        }
      }
    }
  }

  rowGlobal[3] = hist.rules.basicRule == Rules::BASICRULE_STANDARD;
  rowGlobal[4] = hist.rules.basicRule == Rules::BASICRULE_RENJU;
  rowGlobal[5] = hist.rules.basicRule == Rules::BASICRULE_RENJU ?  (nextPlayer == P_BLACK ? -1 : 1) : 0.0;
  rowGlobal[6] = hasForbiddenFeature;
  #ifdef USE_VCF
  if(nnInputParams.useVCFInput) {
    GameLogic::ResultsBeforeNN resultsBeforeNN = nnInputParams.resultsBeforeNN;
    if(!resultsBeforeNN.inited) {
      resultsBeforeNN.init(board, hist, nextPlayer);
    }
    if(resultsBeforeNN.inited) {
      if(board.isOnBoard(resultsBeforeNN.myOnlyLoc))
        setRowBin(rowBin, NNPos::locToPos(resultsBeforeNN.myOnlyLoc, board.x_size, nnXLen, nnYLen), 5, 1.0f, posStride, featureStride);
      else if(resultsBeforeNN.myOnlyLoc == Board::PASS_LOC)
        rowGlobal[38] = 1.0;

      if(resultsBeforeNN.calculatedVCF) {
        if(resultsBeforeNN.winner == nextPlayer)
          rowGlobal[7] = 1.0;  // can win by five/lifeFour/vcf
        else if(resultsBeforeNN.myVCFresult == 2)
          rowGlobal[8] = 1.0;  // cannot vcf
        else if(resultsBeforeNN.myVCFresult == 3)
          rowGlobal[9] = 1.0;  // at least no short vcf
        else
          ASSERT_UNREACHABLE;
        if(resultsBeforeNN.oppVCFresult == 1)
          rowGlobal[10] = 1.0;  // opp can vcf
        else if(resultsBeforeNN.oppVCFresult == 2)
          rowGlobal[11] = 1.0;  // opp cannot vcf
        else if(resultsBeforeNN.oppVCFresult == 3)
          rowGlobal[12] = 1.0;  // at least no short vcf
        else
          ASSERT_UNREACHABLE;
      }
    }
  }
  #endif
  int myPassNum = nextPlayer == C_BLACK ? hist.blackPassNum : hist.whitePassNum;
  int oppPassNum = nextPlayer == C_WHITE ? hist.blackPassNum : hist.whitePassNum;
  //if (myPassNum > 0 && oppPassNum > 0)
  //  cerr << "MESSAGE myPassNum>0 && oppPassNum>0 in nninput";

  if (!hist.rules.firstPassWin && hist.rules.VCNRule == Rules::VCNRULE_NOVC) {
    rowGlobal[13] = nextPlayer == P_BLACK ? -nnInputParams.noResultUtilityForWhite : nnInputParams.noResultUtilityForWhite;
    //rowGlobal[13] = 0;
    rowGlobal[14] = oppPassNum>0;
  }
  else {
    rowGlobal[13] = 0;
    rowGlobal[14] = 0;
  }


  //Used for handicap play
  //Parameter 15 is used because there's actually a discontinuity in how training behavior works when this is
  //nonzero, no matter how slightly.
  if(nnInputParams.playoutDoublingAdvantage != 0) {
    rowGlobal[15] = 1.0;
    rowGlobal[16] = (float)(0.5 * nnInputParams.playoutDoublingAdvantage);
  }

  if (hist.rules.firstPassWin) {
    rowGlobal[17] = 1.0;
    rowGlobal[18] = myPassNum>0;
    rowGlobal[19] = oppPassNum>0;
  }

  if (hist.rules.VCNRule != Rules::VCNRULE_NOVC) {
    Color VCside = hist.rules.vcSide();
    int VClevel = hist.rules.vcLevel();
    int realVClevel = VClevel + myPassNum + oppPassNum;
    if (realVClevel >= 1 && realVClevel <= 5) {
      if (VCside == nextPlayer)
        rowGlobal[19 + realVClevel] = 1.0;
      else if (VCside == opp)
        rowGlobal[24 + realVClevel] = 1.0;
    }
    else {
      cerr << "MESSAGE illegal VCN rule in nninput:" << realVClevel << " "<<VClevel<<endl;
    }
  }

  //if(maxmoves!=0)
  // 31  maxmoves/boardarea
  // 32  moves/boardarea
  // 33  exp(-(maxmoves-moves)/50.0)
  // 34  exp(-(maxmoves-moves)/15.0)
  // 35  exp(-(maxmoves-moves)/5.0)
  // 36  exp(-(maxmoves-moves)/1.5)

  if (hist.rules.maxMoves != 0) {
    rowGlobal[30] = 1.0;
    float boardArea = board.x_size * board.y_size;
    float movenum = hist.getMovenum();
    float maxmoves = hist.rules.maxMoves;
    rowGlobal[31] = maxmoves / boardArea;
    rowGlobal[32] = movenum / boardArea;
    float val = exp(-(maxmoves-movenum)/50.0);
    if (isfinite(val))
      rowGlobal[33] = val;
    val = exp(-(maxmoves-movenum)/15.0);
    if (isfinite(val))
      rowGlobal[34] = val;
    val = exp(-(maxmoves-movenum)/5.0);
    if (isfinite(val))
      rowGlobal[35] = val;
    val = exp(-(maxmoves-movenum)/1.5);
    if (isfinite(val))
      rowGlobal[36] = val;
    rowGlobal[37] = 2*((int(maxmoves-movenum))%2)-1;
  }

}
