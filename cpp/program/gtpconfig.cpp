#include "../program/gtpconfig.h"

using namespace std;

static const string gtpBasePart1 = R"%%(
# Config for KataGo C++ GTP engine, i.e. "./katago.exe gtp"

# In this config, when a parameter is given as a commented out value,
# that value also is the default value, unless described otherwise. You can
# uncomment it (remove the pound sign) and change it if you want.

# ===========================================================================
# Command-line usage
# ===========================================================================
# All of the below values may be set or overridden via command-line arguments:
#
# -override-config KEY=VALUE,KEY=VALUE,...

# ===========================================================================
# Logs and files
# ===========================================================================
# This section defines where and what logging information is produced.

# Each run of KataGo will log to a separate file in this dir.
# This is the default.
logDir = gtp_logs
# Uncomment and specify this instead of logDir to write separate dated subdirs
# logDirDated = gtp_logs
# Uncomment and specify this instead of logDir to log to only a single file
# logFile = gtp.log

# Logging options
logAllGTPCommunication = true
logSearchInfo = true
# logMoves = false
logSearchInfoForChosenMove = false

# KataGo will display some info to stderr on GTP startup
# Uncomment the next line and set it to false to suppress that and remain silent
# startupPrintMessageToStderr = true

# Write information to stderr, for use in things like malkovich chat to OGS.
# ogsChatToStderr = false

# Uncomment and set this to a directory to override where openCLTuner files
# and other cached data is written. By default it saves into a subdir of the
# current directory on windows, and a subdir of ~/.katago on Linux.
# homeDataDir = PATH_TO_DIRECTORY

# ===========================================================================
# Analysis
# ===========================================================================
# This section configures analysis settings.
#
# The maximum number of moves after the first move displayed in variations
# from analysis commands like kata-analyze or lz-analyze.
# analysisPVLen = 15

# Report winrates for chat and analysis as (BLACK|WHITE|SIDETOMOVE).
# Most GUIs and analysis tools will expect SIDETOMOVE.
# reportAnalysisWinratesAs = SIDETOMOVE

# Extra noise for wider exploration. Large values will force KataGo to
# analyze a greater variety of moves than it normally would.
# An extreme value like 1 distributes playouts across every move on the board,
# even very bad moves.
# Affects analysis only, does not affect play.

# analysisWideRootNoise = 0.04

# ===========================================================================
# Rules
# ===========================================================================
# This section configures the scoring and playing rules. Rules can also be
# changed mid-run by issuing custom GTP commands.
#
# See https://lightvector.github.io/KataGo/rules.html for rules details.
#
# See https://github.com/lightvector/KataGo/blob/master/docs/GTP_Extensions.md
# for GTP commands.
# extension for 5-row games - VCNRules and Rules::basicRule

$$BASIC_RULE


# ===========================================================================
# Bot behavior
# ===========================================================================

# ------------------------------
# Resignation
# ------------------------------

# Resignation occurs if for at least resignConsecTurns in a row, the
# winLossUtility (on a [-1,1] scale) is below resignThreshold.
allowResignation = true
resignThreshold = -0.90
resignConsecTurns = 3

# ------------------------------
# Handicap
# ------------------------------

# Makes katago dynamically adjust in handicap or altered-komi games to assume
# based on those game settings that it must be stronger or weaker than the
# opponent and to play accordingly. Greatly improves handicap strength by
# biasing winrates and scores to favor appropriate safe/aggressive play.
# Does NOT affect analysis (lz-analyze, kata-analyze, used by programs like
# Lizzie) so analysis remains unbiased. Uncomment and set this to 0 to disable
# this and make KataGo play the same always.
# dynamicPlayoutDoublingAdvantageCapPerOppLead = 0.045

# Instead of "dynamicPlayoutDoublingAdvantageCapPerOppLead", you can comment
# that out and uncomment and set "playoutDoublingAdvantage" to a fixed value
# from -3.0 to 3.0 that will not change dynamically.
# ALSO affects analysis tools (lz-analyze, kata-analyze, used by e.g. Lizzie).
# Negative makes KataGo behave as if it is much weaker than the opponent.
# Positive makes KataGo behave as if it is much stronger than the opponent.
# KataGo will adjust to favor safe/aggressive play as appropriate based on
# the combination of who is ahead and how much stronger/weaker it thinks it is,
# and report winrates and scores taking the strength difference into account.
#
# If this and "dynamicPlayoutDoublingAdvantageCapPerOppLead" are both set
# then dynamic will be used for all games and this fixed value will be used
# for analysis tools.
# playoutDoublingAdvantage = 0.0

# Uncomment one of these when using "playoutDoublingAdvantage" to enforce
# that it will only apply when KataGo plays as the specified color and will be
# negated when playing as the opposite color.
# playoutDoublingAdvantagePla = BLACK
# playoutDoublingAdvantagePla = WHITE

# ------------------------------
# Passing
# ------------------------------
# Make the bot will suppress pass  Uncomment and set to true to enable.
# suppressPass = false

# ------------------------------
# Miscellaneous behavior
# ------------------------------
# If the board is symmetric, search only one copy of each equivalent move.
# Uncomment and set to false to disable.
# rootSymmetryPruning = true

)%%";
static const string gtpBasePart2 = R"%%(

# ===========================================================================
# Search limits
# ===========================================================================

# Terminology:
# "Playouts" is the number of new playouts of search performed each turn.
# "Visits" is the same as "Playouts" but also counts search performed on
# previous turns that is still applicable to this turn.
# "Time" is the time in seconds.

# For example, if KataGo searched 200 nodes on the previous turn, and then
# after the opponent's reply, 50 nodes of its search tree was still valid,
# then a visit limit of 200 would allow KataGo to search 150 new nodes
# (for a final tree size of 200 nodes), whereas a playout limit of of 200
# would allow KataGo to search 200 nodes (for a final tree size of 250 nodes).

# Additionally, KataGo may also move before than the limit in order to
# obey time controls (e.g. byo-yomi, etc) if the GTP controller has
# told KataGo that the game has is being played with a given time control.

# Limits for search on the current turn.
# If commented out or unspecified, the default is to have no limit.
$$MAX_VISITS
$$MAX_PLAYOUTS
$$MAX_TIME

# Ponder on the opponent's turn?
$$PONDERING

# ------------------------------
# Other search limits and behavior
# ------------------------------

# Approx number of seconds to buffer for lag for GTP time controls - will
# move a bit faster assuming there is this much lag per move.
lagBuffer = 1.0

# Number of threads to use in search
numSearchThreads = $$NUM_SEARCH_THREADS

# Play a little faster if super-winning, for human-friendliness.
# Comment these out to disable them, such as if running a controlled match
# where you are testing KataGo with fixed compute per move vs other bots.
searchFactorWhenWinning = 0.40
searchFactorWhenWinningThreshold = 0.95

# ===========================================================================
# GPU settings
# ===========================================================================
# This section configures GPU settings.
#
# Maximum number of positions to send to a single GPU at once. The default
# value is roughly equal to numSearchThreads, but can be specified manually
# if running out of memory, or using multiple GPUs that expect to share work.
# nnMaxBatchSize = <integer>

# Controls the neural network cache size, which is the primary RAM/memory use.
# KataGo will cache up to (2 ** nnCacheSizePowerOfTwo) many neural net
# evaluations in case of transpositions in the tree.
# Increase this to improve performance for searches with tens of thousands
# of visits or more. Decrease this to limit memory usage.
# If you're happy to do some math - each neural net entry takes roughly
# 1.5KB, except when using whole-board ownership/territory
# visualizations, where each entry will take roughly 3KB. The number of
# entries is (2 ** nnCacheSizePowerOfTwo). (E.g. 2 ** 18 = 262144.)
# You can compute roughly how much memory the cache will use based on this.
nnCacheSizePowerOfTwo = $$NN_CACHE_SIZE_POWER_OF_TWO

# Size of mutex pool for nnCache is (2 ** this).
nnMutexPoolSizePowerOfTwo = $$NN_MUTEX_POOL_SIZE_POWER_OF_TWO

$$MULTIPLE_GPUS

# ===========================================================================
# Root move selection and biases
# ===========================================================================
# Uncomment and edit any of the below values to change them from their default.

# If provided, force usage of a specific seed for various random things in
# the search. The default is to use a random seed.
# searchRandSeed = hijklmn

# Temperature for the early game, randomize between chosen moves with
# this temperature
# chosenMoveTemperatureEarly = 0.5

# Decay temperature for the early game by 0.5 every this many moves,
# scaled with board size.
# chosenMoveTemperatureHalflife = 19

# At the end of search after the early game, randomize between chosen
# moves with this temperature
# chosenMoveTemperature = 0.10

# Subtract this many visits from each move prior to applying
# chosenMoveTemperature (unless all moves have too few visits) to downweight
# unlikely moves
# chosenMoveSubtract = 0

# The same as chosenMoveSubtract but only prunes moves that fall below
# the threshold. This setting does not affect chosenMoveSubtract.
# chosenMovePrune = 1

# Number of symmetries to sample (without replacement) and average at the root
# rootNumSymmetriesToSample = 1

# Using LCB for move selection?
# useLcbForSelection = true

# How many stdevs a move needs to be better than another for LCB selection
# lcbStdevs = 5.0

# Only use LCB override when a move has this proportion of visits as the
# top move.
# minVisitPropForLCB = 0.15

# ===========================================================================
# Internal params
# ===========================================================================
# Uncomment and edit any of the below values to change them from their default.

# Scales the utility of winning/losing
# winLossUtilityFactor = 1.0

# Exploration constant for mcts
# cpuctExploration = 1.0
# cpuctExplorationLog = 0.45

# Parameters that control exploring more in volatile positions, exploring
# less in stable positions.
# cpuctUtilityStdevPrior = 0.40
# cpuctUtilityStdevPriorWeight = 2.0
# cpuctUtilityStdevScale = 0.85

# FPU reduction constant for mcts
# fpuReductionMax = 0.2
# rootFpuReductionMax = 0.1
# fpuParentWeightByVisitedPolicy = true

# Parameters that control weighting of evals based on the net's own
# self-reported uncertainty.
# useUncertainty = true
# uncertaintyExponent = 1.0
# uncertaintyCoeff = 0.25

# Explore using optimistic policy
# rootPolicyOptimism = 0.2
# policyOptimism = 1.0

# Amount to apply a downweighting of children with very bad values relative
# to good ones.
# valueWeightExponent = 0.25

# Make the bot prune useless moves that are just prolonging the game to
# avoid losing yet.
# rootPruneUselessMoves = true

# Apply bias correction based on local pattern keys
# subtreeValueBiasFactor = 0.45
# subtreeValueBiasWeightExponent = 0.85

# Use graph search rather than tree search - identify and share search for
# transpositions.
# useGraphSearch = true

# How much to shard the node table for search synchronization
# nodeTableShardsPowerOfTwo = 16

# How many virtual losses to add when a thread descends through a node
# numVirtualLossesPerThread = 1

# Improve the quality of evals under heavy multithreading
# useNoisePruning = true

)%%";


string GTPConfig::makeConfig(
  const Rules& rules,
  int64_t maxVisits,
  int64_t maxPlayouts,
  double maxTime,
  double maxPonderTime,
  std::vector<int> deviceIdxs,
  int nnCacheSizePowerOfTwo,
  int nnMutexPoolSizePowerOfTwo,
  int numSearchThreads
) {
  string config = gtpBasePart1 + gtpBasePart2;
  auto replace = [&](const string& key, const string& replacement) {
    size_t pos = config.find(key);
    assert(pos != string::npos);
    config.replace(pos, key.size(), replacement);
  };

  if(rules.basicRule == Rules::BASICRULE_FREESTYLE)
    replace("$$BASIC_RULE", "basicRule = FREESTYLE  # options: FREESTYLE, STANDARD, RENJU");
  else if(rules.basicRule == Rules::BASICRULE_STANDARD)
    replace("$$BASIC_RULE", "basicRule = STANDARD  # options: FREESTYLE, STANDARD, RENJU");
  else if(rules.basicRule == Rules::BASICRULE_RENJU)
    replace("$$BASIC_RULE", "basicRule = RENJU  # options: FREESTYLE, STANDARD, RENJU");
  else {
    ASSERT_UNREACHABLE;
  }


  if(maxVisits < ((int64_t)1 << 50)) replace("$$MAX_VISITS", "maxVisits = " + Global::int64ToString(maxVisits));
  else                               replace("$$MAX_VISITS", "# maxVisits = 500");
  if(maxPlayouts < ((int64_t)1 << 50)) replace("$$MAX_PLAYOUTS", "maxPlayouts = " + Global::int64ToString(maxPlayouts));
  else                                 replace("$$MAX_PLAYOUTS", "# maxPlayouts = 300");
  if(maxTime < 1e20)                   replace("$$MAX_TIME", "maxTime = " + Global::doubleToString(maxTime));
  else                                 replace("$$MAX_TIME", "# maxTime = 10.0");

  if(maxPonderTime <= 0)               replace("$$PONDERING", "ponderingEnabled = false\n# maxTimePondering = 60.0");
  else if(maxPonderTime < 1e20)        replace("$$PONDERING", "ponderingEnabled = true\nmaxTimePondering = " + Global::doubleToString(maxPonderTime));
  else                                 replace("$$PONDERING", "ponderingEnabled = true\n# maxTimePondering = 60.0");

  replace("$$NUM_SEARCH_THREADS", Global::intToString(numSearchThreads));
  replace("$$NN_CACHE_SIZE_POWER_OF_TWO", Global::intToString(nnCacheSizePowerOfTwo));
  replace("$$NN_MUTEX_POOL_SIZE_POWER_OF_TWO", Global::intToString(nnMutexPoolSizePowerOfTwo));

  if(deviceIdxs.size() <= 0) {
    replace("$$MULTIPLE_GPUS", "");
  }
  else {
    string replacement = "";
    replacement += "numNNServerThreadsPerModel = " + Global::uint64ToString(deviceIdxs.size()) + "\n";

    for(int i = 0; i<deviceIdxs.size(); i++) {
#ifdef USE_CUDA_BACKEND
      replacement += "cudaDeviceToUseThread" + Global::intToString(i) + " = " + Global::intToString(deviceIdxs[i]) + "\n";
#endif
#ifdef USE_TENSORRT_BACKEND
      replacement += "trtDeviceToUseThread" + Global::intToString(i) + " = " + Global::intToString(deviceIdxs[i]) + "\n";
#endif
#ifdef USE_OPENCL_BACKEND
      replacement += "openclDeviceToUseThread" + Global::intToString(i) + " = " + Global::intToString(deviceIdxs[i]) + "\n";
#endif
    }
    replace("$$MULTIPLE_GPUS", replacement);
  }

  return config;
}
