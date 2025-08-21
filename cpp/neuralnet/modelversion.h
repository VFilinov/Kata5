#ifndef NEURALNET_MODELVERSION_H_
#define NEURALNET_MODELVERSION_H_

// Model versions
namespace NNModelVersion {
  enum VersionMode { SUPPORTED_VERSION, DOUBLE_POLICY_CHANELLS, IS_MULTIPLIER, SOFTPLUS_MODE, TYPE_VCN_RULE, SELECT_ACTIVATION, IS_TIME_LEFT, NONLINEARITY_PASS_POLICY, Q_VALUE_PREDICTIONS };
  enum TypeRuleMode { RULE_NOT_SUPPORT, RULE_NOT_VCN, RULE_IS_VCN };
  enum SoftPlusMode { SOFT_PLUS_PRESOFT, SOFT_PLUS_MULTIPLIER, SOFT_PLUS_PRESOFT_MULTIPLIER };

  constexpr int latestModelVersionImplemented = 15;
  constexpr int latestInputsVersionImplemented = 10;
  constexpr int defaultModelVersion = 15;

  constexpr int oldestModelVersionImplemented = 8;
  constexpr int oldestInputsVersionImplemented = 8;
    

  // Which V* feature version from NNInputs does a given model version consume?
  int getInputsVersion(int modelVersion);

  // Convenience functions, feeds forward the number of features and the size of
  // the row vector that the net takes as input
  int getNumSpatialFeatures(int modelVersion);
  int getNumGlobalFeatures(int modelVersion);

  int getNumSpatialFeaturesForInputs(int inputsVersion);
  int getNumGlobalFeaturesForInputs(int inputsVersion);

  int getSupportedVersion(int modelVersion, VersionMode mode);
  bool isSupportedInputsVersion(int inputsVersion, bool is_fail=true);

  // SGF metadata encoder input versions
  int getNumInputMetaChannels(int metaEncoderVersion);

}  // namespace NNModelVersion

#endif  // NEURALNET_MODELVERSION_H_
