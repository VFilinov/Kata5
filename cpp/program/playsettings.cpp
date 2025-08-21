#include "../program/playsettings.h"

extern bool extra_time_log;

PlaySettings::PlaySettings()
  : maxTryTimes(20),
    fileOpenings(""),
    logOpenings(false), logGenerate(false),
    initGamesWithPolicy(false), policyInitAvgMoveNum(0.0), startPosesPolicyInitAvgMoveNum(0.0), policyInitAreaTemperature(1.0), 
    sidePositionProb(0.0),
    cheapSearchProb(0),cheapSearchVisits(0),cheapSearchTargetWeight(0.0f),
    reduceVisits(false),reduceVisitsThreshold(100.0),reduceVisitsThresholdLookback(1),reducedVisitsMin(0),reducedVisitsWeight(1.0f),
    policySurpriseDataWeight(0.0),valueSurpriseDataWeight(0.0),scaleDataWeight(1.0),
    recordTreePositions(false),recordTreeThreshold(0),recordTreeTargetWeight(0.0f),
    noResolveTargetWeights(false),
    allowResignation(false),resignThreshold(0.0),resignConsecTurns(1),
    allowEarlyDraw(false), earlyDrawThreshold(0.99), earlyDrawConsecTurns(4), earlyDrawProbSelfplay(0.9),
    forSelfPlay(false),
    normalAsymmetricPlayoutProb(0.0), maxAsymmetricRatio(2.0),
    recordTimePerMove(false),
    initGamesWithAvgDist(false), 
    addBalanceMoveProb(100.0),
    policyInitGaussMoveNum(0.0),
    policyInitProb(0.0)
{}
PlaySettings::~PlaySettings()
{}

PlaySettings PlaySettings::loadForMatch(ConfigParser& cfg) {
  PlaySettings playSettings;
  if(cfg.contains("allowResignation"))
    playSettings.allowResignation = cfg.getBool("allowResignation");
  if(cfg.contains("resignThreshold"))
    playSettings.resignThreshold = cfg.getDouble("resignThreshold",-1.0,0.0); //Threshold on [-1,1], regardless of winLossUtilityFactor
  if(cfg.contains("resignConsecTurns"))
    playSettings.resignConsecTurns = cfg.getInt("resignConsecTurns", 1, 100);
  if(cfg.contains("initGamesWithPolicy"))
    playSettings.initGamesWithPolicy = cfg.getBool("initGamesWithPolicy");
  if(cfg.contains("allowEarlyDraw"))
      playSettings.allowEarlyDraw = cfg.getBool("allowEarlyDraw");
  if(cfg.contains("earlyDrawThreshold"))
    playSettings.earlyDrawThreshold = cfg.getDouble("earlyDrawThreshold", 0.8, 1.0);
  if(cfg.contains("earlyDrawConsecTurns"))
    playSettings.earlyDrawConsecTurns = cfg.getInt("earlyDrawConsecTurns", 1, 100);
  if(cfg.contains("earlyDrawProbSelfplay"))
    playSettings.earlyDrawProbSelfplay = cfg.getDouble("earlyDrawProbSelfplay", 0.0, 1.0);
  if(cfg.contains("logOpenings"))
    playSettings.logOpenings = cfg.getBool("logOpenings");
  if(cfg.contains("logGenerate"))
    playSettings.logGenerate = cfg.getBool("logGenerate");
  if(cfg.contains("policyInitAvgMoveNum"))
    playSettings.policyInitAvgMoveNum = cfg.getDouble("policyInitAvgMoveNum", 0.0, 100.0);
  if(cfg.contains("policyInitGaussMoveNum"))
    playSettings.policyInitGaussMoveNum = cfg.getDouble("policyInitGaussMoveNum", 0.0, 100.0);
  if(cfg.contains("startPosesPolicyInitAvgMoveNum"))
    playSettings.startPosesPolicyInitAvgMoveNum = cfg.getDouble("startPosesPolicyInitAvgMoveNum", 0.0, 100.0);
  if(cfg.contains("policyInitAreaTemperature"))
    playSettings.policyInitAreaTemperature = cfg.getDouble("policyInitAreaTemperature",0.1,5.0);
  if(cfg.contains("maxTryTimes"))
    playSettings.maxTryTimes = cfg.getInt("maxTryTimes", 0, 1000);
  if(cfg.contains("initGamesWithAvgDist"))
    playSettings.initGamesWithAvgDist = cfg.getBool("initGamesWithAvgDist");
  if(cfg.contains("addBalanceMoveProb"))
    playSettings.addBalanceMoveProb = cfg.getDouble("addBalanceMoveProb",0.0,100.0);
  if(cfg.contains("extraTimeLog") && cfg.getBool("extraTimeLog"))
    extra_time_log = true;
  if(cfg.contains("policyInitProb"))
    playSettings.policyInitProb = cfg.getDouble("policyInitProb",0.0,100.0);
  
  playSettings.recordTimePerMove = true;
  return playSettings;
}

PlaySettings PlaySettings::loadForGatekeeper(ConfigParser& cfg) {
  PlaySettings playSettings;
  if(cfg.contains("allowResignation"))
    playSettings.allowResignation = cfg.getBool("allowResignation");
  if(cfg.contains("resignThreshold"))
    playSettings.resignThreshold = cfg.getDouble("resignThreshold", -1.0, 0.0);  // Threshold on [-1,1], regardless of winLossUtilityFactor
  if(cfg.contains("resignConsecTurns"))
    playSettings.resignConsecTurns = cfg.getInt("resignConsecTurns", 1, 100);

  if(cfg.contains("initGamesWithPolicy"))
    playSettings.initGamesWithPolicy = cfg.getBool("initGamesWithPolicy");
  if(cfg.contains("allowEarlyDraw"))
    playSettings.allowEarlyDraw = cfg.getBool("allowEarlyDraw");
  if(cfg.contains("earlyDrawThreshold"))
    playSettings.earlyDrawThreshold = cfg.getDouble("earlyDrawThreshold", 0.8, 1.0);
  if(cfg.contains("earlyDrawConsecTurns"))
    playSettings.earlyDrawConsecTurns = cfg.getInt("earlyDrawConsecTurns", 1, 100);
  if(cfg.contains("earlyDrawProbSelfplay"))
    playSettings.earlyDrawProbSelfplay = cfg.getDouble("earlyDrawProbSelfplay", 0.0, 1.0);
  if(cfg.contains("logOpenings"))
    playSettings.logOpenings = cfg.getBool("logOpenings");
  if(cfg.contains("logGenerate"))
    playSettings.logGenerate = cfg.getBool("logGenerate");
  if(cfg.contains("policyInitAvgMoveNum"))
    playSettings.policyInitAvgMoveNum = cfg.getDouble("policyInitAvgMoveNum", 0.0, 100.0);
  if(cfg.contains("policyInitGaussMoveNum"))
    playSettings.policyInitGaussMoveNum = cfg.getDouble("policyInitGaussMoveNum", 0.0, 100.0);
  if(cfg.contains("startPosesPolicyInitAvgMoveNum"))
    playSettings.startPosesPolicyInitAvgMoveNum = cfg.getDouble("startPosesPolicyInitAvgMoveNum", 0.0, 100.0);
  if(cfg.contains("policyInitAreaTemperature"))
    playSettings.policyInitAreaTemperature = cfg.getDouble("policyInitAreaTemperature", 0.1, 5.0);
  if(cfg.contains("maxTryTimes"))
    playSettings.maxTryTimes = cfg.getInt("maxTryTimes", 0, 1000);
  if(cfg.contains("initGamesWithAvgDist"))
    playSettings.initGamesWithAvgDist = cfg.getBool("initGamesWithAvgDist");
  if(cfg.contains("addBalanceMoveProb"))
    playSettings.addBalanceMoveProb = cfg.getDouble("addBalanceMoveProb", 0.0, 100.0);
  if(cfg.contains("extraTimeLog") && cfg.getBool("extraTimeLog"))
    extra_time_log = true;
  if(cfg.contains("policyInitProb"))
    playSettings.policyInitProb = cfg.getDouble("policyInitProb", 0.0, 100.0);

  return playSettings;
}

PlaySettings PlaySettings::loadForSelfplay(ConfigParser& cfg) {
  PlaySettings playSettings;

  if(cfg.contains("allowEarlyDraw"))
    playSettings.allowEarlyDraw = cfg.getBool("allowEarlyDraw");
  if(cfg.contains("earlyDrawThreshold"))
    playSettings.earlyDrawThreshold = cfg.getDouble("earlyDrawThreshold", 0.8, 1.0);
  if(cfg.contains("earlyDrawConsecTurns"))
    playSettings.earlyDrawConsecTurns = cfg.getInt("earlyDrawConsecTurns", 1, 100);
  if(cfg.contains("earlyDrawProbSelfplay"))
    playSettings.earlyDrawProbSelfplay = cfg.getDouble("earlyDrawProbSelfplay", 0.0, 1.0);

  if(cfg.contains("initGamesWithPolicy"))
    playSettings.initGamesWithPolicy = cfg.getBool("initGamesWithPolicy");
  if(cfg.contains("policyInitAvgMoveNum"))
    playSettings.policyInitAvgMoveNum = cfg.getDouble("policyInitAvgMoveNum", 0.0, 100.0);
  if(cfg.contains("startPosesPolicyInitAvgMoveNum"))
    playSettings.startPosesPolicyInitAvgMoveNum = cfg.getDouble("startPosesPolicyInitAvgMoveNum", 0.0, 100.0);
  if(cfg.contains("policyInitAreaTemperature"))
    playSettings.policyInitAreaTemperature = cfg.getDouble("policyInitAreaTemperature", 0.1, 5.0);
  if(cfg.contains("policyInitGaussMoveNum"))
    playSettings.policyInitGaussMoveNum = cfg.getDouble("policyInitGaussMoveNum", 0.0, 100.0);
  if(cfg.contains("maxTryTimes"))
    playSettings.maxTryTimes = cfg.getInt("maxTryTimes", 0, 1000);
  if(cfg.contains("initGamesWithAvgDist"))
    playSettings.initGamesWithAvgDist = cfg.getBool("initGamesWithAvgDist");
  if(cfg.contains("addBalanceMoveProb"))
    playSettings.addBalanceMoveProb = cfg.getDouble("addBalanceMoveProb", 0, 100.0);

  //forkSidePositionProb is the legacy name, included for backward compatibility
  playSettings.sidePositionProb = cfg.contains("forkSidePositionProb")?cfg.getDouble("forkSidePositionProb",0.0,1.0):(cfg.contains("sidePositionProb")?cfg.getDouble("sidePositionProb",0.0,1.0):0.0);

  playSettings.cheapSearchProb = cfg.getDouble("cheapSearchProb",0.0,1.0);
  playSettings.cheapSearchVisits = cfg.getInt("cheapSearchVisits",1,10000000);
  playSettings.cheapSearchTargetWeight = cfg.getFloat("cheapSearchTargetWeight",0.0f,1.0f);
  playSettings.reduceVisits = cfg.getBool("reduceVisits");
  playSettings.reduceVisitsThreshold = cfg.getDouble("reduceVisitsThreshold",0.0,0.999999);
  playSettings.reduceVisitsThresholdLookback = cfg.getInt("reduceVisitsThresholdLookback",0,1000);
  playSettings.reducedVisitsMin = cfg.getInt("reducedVisitsMin",1,10000000);
  playSettings.reducedVisitsWeight = cfg.getFloat("reducedVisitsWeight",0.0f,1.0f);
  playSettings.policySurpriseDataWeight = cfg.getDouble("policySurpriseDataWeight",0.0,1.0);
  playSettings.valueSurpriseDataWeight = cfg.getDouble("valueSurpriseDataWeight",0.0,1.0);
  playSettings.scaleDataWeight = cfg.contains("scaleDataWeight") ? cfg.getDouble("scaleDataWeight",0.01,10.0) : 1.0;
  playSettings.normalAsymmetricPlayoutProb = cfg.getDouble("normalAsymmetricPlayoutProb",0.0,1.0);
  playSettings.maxAsymmetricRatio = cfg.getDouble("maxAsymmetricRatio",1.0,100.0);

  if(cfg.contains("logOpenings"))
    playSettings.logOpenings = cfg.getBool("logOpenings");
  if(cfg.contains("logGenerate"))
    playSettings.logGenerate = cfg.getBool("logGenerate");

  if(cfg.contains("extraTimeLog") && cfg.getBool("extraTimeLog"))
    extra_time_log = true;
  if(cfg.contains("policyInitProb"))
    playSettings.policyInitProb = cfg.getDouble("policyInitProb", 0.0, 100.0);

  playSettings.forSelfPlay = true;

  if(playSettings.policySurpriseDataWeight + playSettings.valueSurpriseDataWeight > 1.0)
    throw StringError("policySurpriseDataWeight + valueSurpriseDataWeight > 1.0");

  return playSettings;
}
