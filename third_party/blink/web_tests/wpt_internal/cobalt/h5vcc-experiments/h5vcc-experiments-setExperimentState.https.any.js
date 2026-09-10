// META: global=window
// META: script=/resources/test-only-api.js
// META: script=resources/automation.js
const test_experiment_config = {
  "features": {
    "fake_feature": true
  },
  "featureParams": {
    "fake_feature_param": 2.0,
    "fake_feature_param_2": 2,
    "fake_feature_param_3": 1.234,
    "fake_feature_param_4": true,
    "fake_feature_param_5": "test_param_string",
  },
  "activeExperimentConfigData": "test_config_data",
  "latestExperimentConfigHashData": "test_config_hash",
}

const test_experiment_config_2 = {
  "features": {
    "fake_feature": false
  },
  "featureParams": {
  },
  "activeExperimentConfigData": "test_config_data",
  "latestExperimentConfigHashData": "test_config_hash",
}

h5vcc_experiments_tests(async (t, mockH5vccExperiments) => {
  await window.h5vcc.experiments.setExperimentState(test_experiment_config);
  assert_true(mockH5vccExperiments.hasCalledSetExperimentState());

  assert_true(mockH5vccExperiments.getStubResult('fake_feature'));
  // Floats that represent integers get converted into integer.
  assert_equals(mockH5vccExperiments.getStubResult('fake_feature_param'), "2");
  assert_equals(mockH5vccExperiments.getStubResult('fake_feature_param_2'), "2");
  assert_equals(mockH5vccExperiments.getStubResult('fake_feature_param_3'), "1.234");
  assert_equals(mockH5vccExperiments.getStubResult('fake_feature_param_4'), "true");
  assert_equals(mockH5vccExperiments.getStubResult('fake_feature_param_5'), "test_param_string");
}, 'setExperimentState() sends expected config content to browser endpoint');

h5vcc_experiments_tests(async (t, mockH5vccExperiments) => {
  await window.h5vcc.experiments.setExperimentState(test_experiment_config_2);
  assert_true(mockH5vccExperiments.hasCalledSetExperimentState());

  assert_false(mockH5vccExperiments.getStubResult('fake_feature'));
}, 'setExperimentState() sends a second config to browser endpoint');

h5vcc_experiments_mojo_disconnection_tests(async (t) => {
  return promise_rejects_exactly(t, 'Mojo connection error.',
    window.h5vcc.experiments.setExperimentState(test_experiment_config));
}, 'setExperimentState() rejects when unimplemented due to pipe closure');
