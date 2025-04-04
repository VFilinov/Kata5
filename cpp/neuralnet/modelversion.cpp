#include "../neuralnet/nninputs.h"
#include "../neuralnet/modelversion.h"

//Old model versions, no longer supported:
//0 = V1 features, with old head architecture using crelus (no longer supported)
//1 = V1 features, with new head architecture, no crelus
//2 = V2 features, no internal architecture change.

// renju/gomoku
// 98 = V97(V7OLD) features, no VCF, before 2021.1
// 8 = V7 features, has VCF, "gomoku" branch on GitHub, before 2022.2
// 10 = V10 features, support multi rules in single net, "GomDev" branch on GitHub, before 2022.4
// 101 = V101 features, VCN+firstPassWin+maxmoves, "GomDevVCN" branch on GitHub
// 
// 
//Supported model versions:
//3 = V3 features, many architecture changes for new selfplay loop, including multiple board sizes
//4 = V3 features, scorebelief head
//5 = V4 features, changed current territory feature to just indicate pass-alive
//6 = V5 features, disable fancy features
//7 = V6 features, support new rules configurations
//8 = V7 features, unbalanced training, button go, lead and variance time
//9 = V7 features, shortterm value error
//10 = V7 features, shortterm value error done more properly
//11 = V7 features, supports mish activations by desc actually reading the activations
//12 = V7 features, optimisic policy head
//13 = V7 features, Adjusted scaling on shortterm score variance, and made C++ side read in scalings.
//14 = V7 features, Squared softplus for error variance predictions
//15 = V7 features, Extra nonlinearity for pass output

const int max_new_version = 15;

static void fail(int modelVersion) {
  throw StringError("NNModelVersion: Model version not currently implemented or supported: " + Global::intToString(modelVersion));
}

static void faili(int inputsVersion) {
  throw StringError("TrainingDataWriter: Unsupported inputs version: " + Global::intToString(inputsVersion));
}

// static_assert(NNModelVersion::oldestModelVersionImplemented == 8, "");
// static_assert(NNModelVersion::oldestInputsVersionImplemented == 8, "");
// static_assert(NNModelVersion::latestModelVersionImplemented == max_new_version, "");
// static_assert(NNModelVersion::latestInputsVersionImplemented == 10, "");

int NNModelVersion::getSupportedVersion(int modelVersion, VersionMode mode) {

  switch(mode) {
  case SUPPORTED_VERSION:
      if(modelVersion == 98)
        return 1;
      else if(modelVersion == 8)
        return 1;
      else if(modelVersion == 10)
        return 1;
      else if(modelVersion == 101)
        return 1;
      else if(modelVersion == 102)
        return 1;
      else if(modelVersion >= 11 && modelVersion <= max_new_version)
        return 1;
      break;
  case DOUBLE_POLICY_CHANELLS:
      if(modelVersion >= 12 && modelVersion <= max_new_version)
        return 1;
    break;
  case IS_MULTIPLIER:
    if(modelVersion >= 13 && modelVersion <= max_new_version)
      return 1;
    break;
  case SOFTPLUS_MODE:
    if(modelVersion >= 14 && modelVersion <= max_new_version)
      return SOFT_PLUS_PRESOFT_MULTIPLIER;
    else if(modelVersion >= 10)
      return SOFT_PLUS_MULTIPLIER;
    else
      return SOFT_PLUS_PRESOFT;
    break;

  case TYPE_VCN_RULE:
    if(modelVersion >= 8 && modelVersion <= 10)
      return RULE_NOT_VCN;
    else if(modelVersion == 98)
      return RULE_NOT_VCN;
    else if(modelVersion == 101)
      return RULE_IS_VCN;
    else if(modelVersion == 102)
      return RULE_IS_VCN;
    else if(modelVersion == 11)
      return RULE_IS_VCN;
    else if(modelVersion > 11 and modelVersion <= max_new_version) 
      return RULE_NOT_VCN;
    break;

  case SELECT_ACTIVATION:
    if(modelVersion >= 11 && modelVersion <= max_new_version) 
      return 1;
    if(modelVersion == 102)
      return 1;

  case NONLINEARITY_PASS_POLICY:
    if(modelVersion >= 15 && modelVersion <= max_new_version)
      return 1;
    break;

  case IS_TIME_LEFT:
    if(modelVersion >= 9)
      return 1;
    break;
  }

  if(mode == SUPPORTED_VERSION)
    fail(modelVersion);
  return 0;
}

int NNModelVersion::getInputsVersion(int modelVersion) {

  if(modelVersion == 98)
    return 97;
  else if(modelVersion == 8)
    return 7;
  else if(modelVersion == 10)
    return 10;  // c VCF&& multirules
  else if(modelVersion == 101)
    return 101; // VCN
  else if(modelVersion == 102)
    return 101;  // VCN
  else if(modelVersion == 11)
    return 101;  // compatible with VCN
  else if(modelVersion > 11 && modelVersion <= max_new_version)
    return 10;  // without VCN, c VCF && multirules

  fail(modelVersion);
  return -1;
}

bool NNModelVersion::isSupportedInputsVersion(int inputsVersion, bool is_fail) {

  if(inputsVersion==97)
    return true;
  else if(inputsVersion == 7)
    return true;
  else if(inputsVersion == 10)
    return true;
  else if(inputsVersion == 101)
    return true;
  if(is_fail)
    faili(inputsVersion);
  return false;
}


int NNModelVersion::getNumSpatialFeatures(int modelVersion) {

  int inputsVersion = getInputsVersion(modelVersion);
  return getNumSpatialFeaturesForInputs(inputsVersion);
}

int NNModelVersion::getNumGlobalFeatures(int modelVersion) {

  int inputsVersion = getInputsVersion(modelVersion);
  return getNumGlobalFeaturesForInputs(inputsVersion);
}

int NNModelVersion::getNumSpatialFeaturesForInputs(int inputsVersion) {

  if(inputsVersion == 7 || inputsVersion == 97 || inputsVersion == 10) {
    return NNInputs::NUM_FEATURES_SPATIAL_V7;
  } else if(inputsVersion == 101) {
    return NNInputs::NUM_FEATURES_SPATIAL_V101;
  }
  faili(inputsVersion);
  return -1;
}

int NNModelVersion::getNumGlobalFeaturesForInputs(int inputsVersion) {

  if(inputsVersion == 7 || inputsVersion == 97 || inputsVersion == 10) {
    return NNInputs::NUM_FEATURES_GLOBAL_V7;
  } else if(inputsVersion == 101) {
    return NNInputs::NUM_FEATURES_GLOBAL_V101;
  }
  faili(inputsVersion);
  return -1;
}

int NNModelVersion::getNumInputMetaChannels(int metaEncoderVersion) {
  if(metaEncoderVersion == 0)
    return 0;
  if(metaEncoderVersion == 1)
    return 192;
  throw StringError("NNModelVersion: metaEncoderVersion not currently implemented or supported: " + Global::intToString(metaEncoderVersion));
}
