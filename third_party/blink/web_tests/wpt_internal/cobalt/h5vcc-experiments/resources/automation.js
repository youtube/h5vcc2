'use strict';

let mockH5vccExperiments = undefined;

function h5vcc_experiments_tests(func, name, properties) {
  promise_test(async (test) => {
    // Wait for any previous connection error to propagate.
    await new Promise(resolve => test.step_timeout(resolve, 200));

    assert_implements(window.h5vcc.experiments,
                      'missing window.h5vcc.experiments');
    if (mockH5vccExperiments === undefined) {
      const mocks = await import('./mock-h5vcc-experiments.js');
      mockH5vccExperiments = mocks.mockH5vccExperiments;
    }
    assert_implements(mockH5vccExperiments, 'missing mockH5vccExperiments');

    mockH5vccExperiments.start();
    try {
      await func(test, mockH5vccExperiments);
    } finally {
      mockH5vccExperiments.stop();
      mockH5vccExperiments.reset();
    }
    }, name, properties);
  }

function h5vcc_experiments_mojo_disconnection_tests(func, name, properties) {
  promise_test(async (test) => {
    const { H5vccExperiments } = await import(
      '/gen/cobalt/browser/h5vcc_experiments/public/mojom/h5vcc_experiments.mojom.m.js');
    let interceptor =
      new MojoInterfaceInterceptor(H5vccExperiments.$interfaceName);
    interceptor.oninterfacerequest = e => e.handle.close();
    interceptor.start();
    try {
      await func(test, interceptor);
    } finally {
      interceptor.stop();
    }
  }, name, properties);
}
