#include "../search/search.h"

#include "../core/fancymath.h"
#include "../core/test.h"
#include "../search/searchnode.h"
#include "../search/patternbonustable.h"

//------------------------
#include "../core/using.h"
//------------------------

uint32_t Search::chooseIndexWithTemperature(
  Rand& rand,
  const double* relativeProbs,
  int numRelativeProbs,
  double temperature,
  double onlyBelowProb,
  double* processedRelProbsBuf
) {
  testAssert(numRelativeProbs > 0);
  testAssert(numRelativeProbs <= Board::MAX_ARR_SIZE); //We're just doing this on the stack
  double processedRelProbs[Board::MAX_ARR_SIZE];
  if(processedRelProbsBuf == NULL)
    processedRelProbsBuf = &processedRelProbs[0];

  double maxRelProb = 0.0;
  double sumRelProb = 0.0;
  for(int i = 0; i<numRelativeProbs; i++) {
    sumRelProb += std::max(0.0, relativeProbs[i]);
    if(relativeProbs[i] > maxRelProb)
      maxRelProb = relativeProbs[i];
  }
  testAssert(maxRelProb > 0.0);
  testAssert(sumRelProb > 0.0);

  //Temperature so close to 0 that we just calculate the max directly
  if(temperature <= 1.0e-4 && onlyBelowProb >= 1.0) {
    double bestProb = relativeProbs[0];
    int bestIdx = 0;
    processedRelProbsBuf[0] = 0;
    for(int i = 1; i<numRelativeProbs; i++) {
      processedRelProbsBuf[i] = 0;
      if(relativeProbs[i] > bestProb) {
        bestProb = relativeProbs[i];
        bestIdx = i;
      }
    }
    return bestIdx;
  }
  //Actual temperature
  else {
    double logMaxRelProb = log(maxRelProb);
    double logSumRelProb = log(sumRelProb);
    double logOnlyBelowProb = log(std::max(1e-50,onlyBelowProb));
    double sum = 0.0;
    for(int i = 0; i<numRelativeProbs; i++) {
      if(relativeProbs[i] <= 0.0)
        processedRelProbsBuf[i] = 0.0;
      else {
        double logRelProb = log(relativeProbs[i]) - logMaxRelProb;
        double logRelProbThreshold = std::min(0.0, logOnlyBelowProb + logSumRelProb - logMaxRelProb);
        double newLogRelProb;
        if(logRelProb > logRelProbThreshold)
          newLogRelProb = logRelProb;
        else
          newLogRelProb = (logRelProb - logRelProbThreshold) / temperature + logRelProbThreshold;
        processedRelProbsBuf[i] = exp(newLogRelProb);
      }
      sum += processedRelProbsBuf[i];
    }
    testAssert(sum > 0.0);
    uint32_t idxChosen = rand.nextUInt(processedRelProbsBuf,numRelativeProbs);
    return idxChosen;
  }
}

void Search::computeDirichletAlphaDistribution(int policySize, const float* policyProbs, double* alphaDistr) {
  int legalCount = 0;
  for(int i = 0; i<policySize; i++) {
    if(policyProbs[i] >= 0)
      legalCount += 1;
  }

  if(legalCount <= 0)
    throw StringError("computeDirichletAlphaDistribution: No move with nonnegative policy value - can't even pass?");

  //We're going to generate a gamma draw on each move with alphas that sum up to searchParams.rootDirichletNoiseTotalConcentration.
  //Half of the alpha weight are uniform.
  //The other half are shaped based on the log of the existing policy.
  double logPolicySum = 0.0;
  for(int i = 0; i<policySize; i++) {
    if(policyProbs[i] >= 0) {
      alphaDistr[i] = log(std::min(0.01, (double)policyProbs[i]) + 1e-20);
      logPolicySum += alphaDistr[i];
    }
  }
  double logPolicyMean = logPolicySum / legalCount;
  double alphaPropSum = 0.0;
  for(int i = 0; i<policySize; i++) {
    if(policyProbs[i] >= 0) {
      alphaDistr[i] = std::max(0.0, alphaDistr[i] - logPolicyMean);
      alphaPropSum += alphaDistr[i];
    }
  }
  double uniformProb = 1.0 / legalCount;
  if(alphaPropSum <= 0.0) {
    for(int i = 0; i<policySize; i++) {
      if(policyProbs[i] >= 0)
        alphaDistr[i] = uniformProb;
    }
  }
  else {
    for(int i = 0; i<policySize; i++) {
      if(policyProbs[i] >= 0)
        alphaDistr[i] = 0.5 * (alphaDistr[i] / alphaPropSum + uniformProb);
    }
  }
}

void Search::addDirichletNoise(const SearchParams& searchParams, Rand& rand, int policySize, float* policyProbs) {
  double r[NNPos::MAX_NN_POLICY_SIZE];
  Search::computeDirichletAlphaDistribution(policySize, policyProbs, r);

  //r now contains the proportions with which we would like to split the alpha
  //The total of the alphas is searchParams.rootDirichletNoiseTotalConcentration
  //Generate gamma draw on each move
  double rSum = 0.0;
  for(int i = 0; i<policySize; i++) {
    if(policyProbs[i] >= 0) {
      r[i] = rand.nextGamma(r[i] * searchParams.rootDirichletNoiseTotalConcentration);
      rSum += r[i];
    }
    else
      r[i] = 0.0;
  }

  //Normalized gamma draws -> dirichlet noise
  for(int i = 0; i<policySize; i++)
    r[i] /= rSum;

  //At this point, r[i] contains a dirichlet distribution draw, so add it into the nnOutput.
  for(int i = 0; i<policySize; i++) {
    if(policyProbs[i] >= 0) {
      double weight = searchParams.rootDirichletNoiseWeight;
      policyProbs[i] = (float)(r[i] * weight + policyProbs[i] * (1.0-weight));
    }
  }
}


std::shared_ptr<NNOutput>* Search::maybeAddPolicyNoiseAndTemp(SearchThread& thread, bool isRoot, NNOutput* oldNNOutput) const {
  if(!isRoot)
    return NULL;
  if(!searchParams.rootNoiseEnabled &&
     searchParams.rootPolicyTemperature == 1.0 &&
     searchParams.rootPolicyTemperatureEarly == 1.0 &&
     rootHintLoc == Board::NULL_LOC &&
     !avoidMoveUntilRescaleRoot
  )
    return NULL;
  if(oldNNOutput == NULL)
    return NULL;
  if(oldNNOutput->noisedPolicyProbs != NULL)
    return NULL;

  //Copy nnOutput as we're about to modify its policy to add noise or temperature
  std::shared_ptr<NNOutput>* newNNOutputSharedPtr = new std::shared_ptr<NNOutput>(new NNOutput(*oldNNOutput));
  NNOutput* newNNOutput = newNNOutputSharedPtr->get();

  float* noisedPolicyProbs = new float[NNPos::MAX_NN_POLICY_SIZE];
  newNNOutput->noisedPolicyProbs = noisedPolicyProbs;
#ifdef QUANTIZED_OUTPUT
  for(int i = 0; i < NNPos::MAX_NN_POLICY_SIZE; i++)
    noisedPolicyProbs[i] = newNNOutput->getPolicyProb(i);
#else
  std::copy(newNNOutput->policyProbs, newNNOutput->policyProbs + NNPos::MAX_NN_POLICY_SIZE, noisedPolicyProbs);
#endif
  if(searchParams.rootPolicyTemperature != 1.0 || searchParams.rootPolicyTemperatureEarly != 1.0) {
    double rootPolicyTemperature = interpolateEarly(
      searchParams.chosenMoveTemperatureHalflife, searchParams.rootPolicyTemperatureEarly, searchParams.rootPolicyTemperature
    );

    double maxValue = 0.0;
    for(int i = 0; i<policySize; i++) {
      double prob = noisedPolicyProbs[i];
      if(prob > maxValue)
        maxValue = prob;
    }
    assert(maxValue > 0.0);

    double logMaxValue = log(maxValue);
    double invTemp = 1.0 / rootPolicyTemperature;
    double sum = 0.0;

    for(int i = 0; i<policySize; i++) {
      if(noisedPolicyProbs[i] > 0) {
        //Numerically stable way to raise to power and normalize
        float p = (float)exp((log((double)noisedPolicyProbs[i]) - logMaxValue) * invTemp);
        noisedPolicyProbs[i] = p;
        sum += p;
      }
    }
    assert(sum > 0.0);
    for(int i = 0; i<policySize; i++) {
      if(noisedPolicyProbs[i] >= 0) {
        noisedPolicyProbs[i] = (float)(noisedPolicyProbs[i] / sum);
      }
    }
  }

  if(searchParams.rootNoiseEnabled) {
    addDirichletNoise(searchParams, thread.rand, policySize, noisedPolicyProbs);
  }

  if(avoidMoveUntilRescaleRoot) {
    const std::vector<int>& avoidMoveUntilByLoc = rootPla == P_BLACK ? avoidMoveUntilByLocBlack : avoidMoveUntilByLocWhite;
    if(avoidMoveUntilByLoc.size() > 0) {
      assert(avoidMoveUntilByLoc.size() >= Board::MAX_ARR_SIZE);
      double policySum = 0.0;
      for(Loc loc = 0; loc<Board::MAX_ARR_SIZE; loc++) {
        if((rootBoard.isOnBoard(loc) || (loc == Board::PASS_LOC && !searchParams.suppressPass)) && avoidMoveUntilByLoc[loc] <= 0) {
          int pos = getPos(loc);
          if(noisedPolicyProbs[pos] > 0) {
            policySum += noisedPolicyProbs[pos];
          }
        }
      }
      if(policySum > 0.0) {
        for(int i = 0; i<policySize; i++) {
          if(noisedPolicyProbs[i] > 0) {
            noisedPolicyProbs[i] = (float)(noisedPolicyProbs[i] / policySum);
          }
        }
      }
    }
  }

  //Move a small amount of policy to the hint move, around the same level that noising it would achieve
  if(rootHintLoc != Board::NULL_LOC) {
    const float propToMove = 0.02f;
    int pos = getPos(rootHintLoc);
    if(noisedPolicyProbs[pos] >= 0) {
      double amountToMove = 0.0;
      for(int i = 0; i<policySize; i++) {
        if(noisedPolicyProbs[i] >= 0) {
          amountToMove += noisedPolicyProbs[i] * propToMove;
          noisedPolicyProbs[i] *= (1.0f-propToMove);
        }
      }
      noisedPolicyProbs[pos] += (float)amountToMove;
    }
  }

  return newNNOutputSharedPtr;
}

double Search::getResultUtility(double winLossValue, double noResultValue) const {
  return (
    winLossValue * searchParams.winLossUtilityFactor +
    noResultValue * searchParams.noResultUtilityForWhite
  );
}

double Search::getResultUtilityFromNN(const NNOutput& nnOutput) const {
  return (
    (nnOutput.whiteWinProb - nnOutput.whiteLossProb) * searchParams.winLossUtilityFactor +
    nnOutput.whiteNoResultProb * searchParams.noResultUtilityForWhite
  );
}


double Search::getUtilityFromNN(const NNOutput& nnOutput) const {
  double resultUtility = getResultUtilityFromNN(nnOutput);
  return resultUtility;
}


bool Search::isAllowedRootMove(Loc moveLoc) const {
  assert(moveLoc == Board::PASS_LOC || rootBoard.isOnBoard(moveLoc));

  //A bad situation that can happen that unnecessarily prolongs training games is where one player
  //repeatedly passes and the other side repeatedly fills the opponent's space and/or suicides over and over.
  //To mitigate some of this and save computation, we make it so that at the root, if the last four moves by the opponent
  //were passes, we will never play a move in either player's pass-alive area. In theory this could prune
  //a good move in situations like https://senseis.xmp.net/?1EyeFlaw, but this should be extraordinarly rare,

  if(searchParams.rootSymmetryPruning && moveLoc != Board::PASS_LOC && rootSymDupLoc[moveLoc]) {
    return false;
  }
  return true;
}

double Search::getPatternBonus(Hash128 patternBonusHash, Player prevMovePla) const {
  if(patternBonusTable == NULL || prevMovePla != plaThatSearchIsFor)
    return 0;
  return patternBonusTable->get(patternBonusHash).utilityBonus;
}

bool Search::shouldSuppressPass(const SearchNode* n) const {
  if(n == NULL || n != rootNode)
    return false;

  if(searchParams.suppressPass && !rootHistory.maybePassMove(rootPla))
    return true;

  const SearchNode& node = *n;
  const NNOutput* nnOutput = node.getNNOutput();
  if(nnOutput == NULL)
    return false;
  assert(nnOutput->nnXLen == nnXLen);
  assert(nnOutput->nnYLen == nnYLen);

  //Find the pass move
  const SearchNode* passNode = NULL;
  int64_t passEdgeVisits = 0;

  ConstSearchNodeChildrenReference children = node.getChildren();
  int childrenCapacity = children.getCapacity();
  for(int i = 0; i < childrenCapacity; i++) {
    const SearchChildPointer& childPointer = children[i];
    const SearchNode* child = childPointer.getIfAllocated();
    if(child == NULL)
      break;
    Loc moveLoc = childPointer.getMoveLocRelaxed();
    if(moveLoc == Board::PASS_LOC) {
      passNode = child;
      passEdgeVisits = childPointer.getEdgeVisits();
      break;
    }
  }
  if(passNode == NULL)
    return false;

  double passWeight;
  double passUtility;
  double passWinLossValue;
  double passNoResultValue;
  {
    int64_t passVisits = passNode->stats.visits.load(std::memory_order_acquire);
    double utilityAvg = passNode->stats.utilityAvg.load(std::memory_order_acquire);
    double childWeight = passNode->stats.getChildWeight(passEdgeVisits,passVisits);
    double winLossValueAvg = passNode->stats.winLossValueAvg.load(std::memory_order_acquire);
    double noResultValueAvg = passNode->stats.noResultValueAvg.load(std::memory_order_acquire);

    if(passVisits <= 0 || childWeight <= 1e-10)
      return false;
    passWeight = childWeight;
    passUtility = utilityAvg;
    passWinLossValue = winLossValueAvg;
    passNoResultValue = noResultValueAvg;
  }

  const double extreme = 0.95;

  //Suppress pass if we find a move that is not a spot that the opponent almost certainly owns
  //or that is adjacent to a pla owned spot, and is not greatly worse than pass.
  for(int i = 0; i<childrenCapacity; i++) {
    const SearchChildPointer& childPointer = children[i];
    const SearchNode* child = childPointer.getIfAllocated();
    if(child == NULL)
      break;
    Loc moveLoc = childPointer.getMoveLocRelaxed();
    if(moveLoc == Board::PASS_LOC)
      continue;
    int pos = NNPos::locToPos(moveLoc,rootBoard.x_size,nnXLen,nnYLen);

    int64_t edgeVisits = childPointer.getEdgeVisits();

    double utilityAvg = child->stats.utilityAvg.load(std::memory_order_acquire);
    double childWeight = child->stats.getChildWeight(edgeVisits);
    double winLossValueAvg = child->stats.winLossValueAvg.load(std::memory_order_acquire);
    double noResultValueAvg = child->stats.noResultValueAvg.load(std::memory_order_acquire);

    //Too few visits - reject move
    if((edgeVisits <= 500 && childWeight <= 2 * sqrt(passWeight)) || childWeight <= 1e-10)
      continue;

    double utility = utilityAvg;
    double winLossValue = winLossValueAvg;
    double noResultValue = noResultValueAvg;

    if(rootPla == P_WHITE && utility > passUtility - 0.1)
      return true;
    if(rootPla == P_BLACK && utility < passUtility + 0.1)
      return true;
  }
  return false;
}

double Search::interpolateEarly(double halflife, double earlyValue, double value) const {
  double rawHalflives = (double)rootHistory.getCurrentTurnNumber() / halflife;
  double halflives = rawHalflives * 19.0 / sqrt(rootBoard.x_size * rootBoard.y_size);
  return value + (earlyValue - value) * pow(0.5, halflives);
}

void Search::getSelfUtilityLCBAndRadiusZeroVisits(double& lcbBuf, double& radiusBuf) const {
  // Max radius of the entire utility range
  double utilityRangeRadius = searchParams.winLossUtilityFactor;
  radiusBuf = 2.0 * utilityRangeRadius * searchParams.lcbStdevs;
  lcbBuf = -radiusBuf;
  return;
}

void Search::getSelfUtilityLCBAndRadius(const SearchNode& parent, const SearchNode* child, int64_t edgeVisits, Loc moveLoc, double& lcbBuf, double& radiusBuf) const {
  int64_t childVisits = child->stats.visits.load(std::memory_order_acquire);
  double utilityAvg = child->stats.utilityAvg.load(std::memory_order_acquire);
  double utilitySqAvg = child->stats.utilitySqAvg.load(std::memory_order_acquire);
  double weightSum = child->stats.getChildWeight(edgeVisits,childVisits);
  double weightSqSum = child->stats.getChildWeightSq(edgeVisits,childVisits);

  // Max radius of the entire utility range
  double utilityRangeRadius = searchParams.winLossUtilityFactor;
  radiusBuf = 2.0 * utilityRangeRadius * searchParams.lcbStdevs;
  lcbBuf = -radiusBuf;
  if(childVisits <= 0 || weightSum <= 0.0 || weightSqSum <= 0.0)
    return;

  // Effective sample size for weighted data
  double ess = weightSum * weightSum / weightSqSum;

  // To behave well at low playouts, we'd like a variance approximation that makes sense even with very small sample sizes.
  // We'd like to avoid using a T distribution approximation because we actually know a bound on the scale of the utilities
  // involved, namely utilityRangeRadius. So instead add a prior with a small weight that the variance is the largest it can be.
  // This should give a relatively smooth scaling that works for small discrete samples but diminishes for larger playouts.
  double priorWeight = weightSum / (ess * ess * ess);
  utilitySqAvg = std::max(utilitySqAvg, utilityAvg * utilityAvg + 1e-8);
  utilitySqAvg = (utilitySqAvg * weightSum + (utilitySqAvg + utilityRangeRadius * utilityRangeRadius) * priorWeight) / (weightSum + priorWeight);
  weightSum += priorWeight;
  weightSqSum += priorWeight*priorWeight;

  // Recompute effective sample size now that we have the prior
  ess = weightSum * weightSum / weightSqSum;

  double utilityWithBonus = utilityAvg /* + utilityDiff*/;
  double selfUtility = parent.nextPla == P_WHITE ? utilityWithBonus : -utilityWithBonus;

  double utilityVariance = utilitySqAvg - utilityAvg * utilityAvg;
  double estimateStdev = sqrt(utilityVariance / ess);
  double radius = estimateStdev * searchParams.lcbStdevs;

  lcbBuf = selfUtility - radius;
  radiusBuf = radius;
}
