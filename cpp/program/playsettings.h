#ifndef PROGRAM_PLAYSETTINGS_H_
#define PROGRAM_PLAYSETTINGS_H_

#include "../core/config_parser.h"

struct PlaySettings {
  //Play a bunch of mostly policy-distributed moves at the start to initialize a game.
  bool initGamesWithPolicy;
  bool initGamesWithAvgDist;
  
  double policyInitAvgMoveNum;           // Avg number of moves
  double startPosesPolicyInitAvgMoveNum;  // Avg number of moves when using a starting position from sgf
  // Temperature to use for placing handicap stones and for initializing the board position
  double policyInitAreaTemperature;
  bool policyInitGaussMoveNum;

  double addBalanceMoveProb;
  double policyInitProb;

  // Repeat generate
  int maxTryTimes;

  
  //Occasionally try some alternative moves and search the responses to them.
  double sidePositionProb;

  //With this probability, use only this many visits for a move, and record it with only this weight
  double cheapSearchProb;
  int cheapSearchVisits;
  float cheapSearchTargetWeight;

  //Attenuate the number of visits used in positions where one player or the other is extremely winning
  bool reduceVisits;
  double reduceVisitsThreshold; //When mcts value is more extreme than this
  int reduceVisitsThresholdLookback; //Value must be more extreme over the last this many turns
  int reducedVisitsMin; //Number of visits at the most extreme winrate
  float reducedVisitsWeight; //Amount of weight to put on the training sample at minimum visits winrate.

  //Probabilistically favor samples that had high policy surprise (kl divergence).
  double policySurpriseDataWeight;
  //Probabilistically favor samples that had high winLossValue surprise (kl divergence).
  double valueSurpriseDataWeight;
  //Scale frequency weights for writing data by this
  double scaleDataWeight;

  //Record positions from within the search tree that had at least this many visits, recording only with this weight.
  bool recordTreePositions;
  int recordTreeThreshold;
  float recordTreeTargetWeight;

  //Don't stochastically integerify target weights
  bool noResolveTargetWeights;

  //Resign conditions
  bool allowResignation;
  double resignThreshold; //Require that mcts win value is less than this
  double resignConsecTurns; //Require that both players have agreed on it continuously for this many turns

  //Enable full data recording and a variety of other minor tweaks applying only for self-play training.
  bool forSelfPlay;
  std::string fileOpenings;
  bool logOpenings;
  bool logGenerate;

  //Asymmetric playouts training
  double normalAsymmetricPlayoutProb; //Probability of asymmetric playouts on normal games
  double maxAsymmetricRatio;

  //Record time taken per move
  bool recordTimePerMove;

  // Early end game if draw rate very high
  bool allowEarlyDraw;
  double earlyDrawThreshold;    // Require that mcts draw value is more than this
  double earlyDrawConsecTurns;  // Require that both players have agreed on it continuously for this many turns
  double earlyDrawProbSelfplay; 
  
  PlaySettings();
  ~PlaySettings();

  static PlaySettings loadForMatch(ConfigParser& cfg);
  static PlaySettings loadForGatekeeper(ConfigParser& cfg);
  static PlaySettings loadForSelfplay(ConfigParser& cfg);
};

#endif // PROGRAM_PLAYSETTINGS_H_
