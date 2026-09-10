'use strict';

let mockH5vccMemory = undefined;

function h5vcc_memory_tests(func, name, properties) {
  promise_test(async (test) => {
    assert_implements(window.h5vcc.memory,
                      'missing window.h5vcc.memory');
    if (mockH5vccMemory === undefined) {
      const mocks = await import('./mock-h5vcc-memory.js');
      mockH5vccMemory = mocks.mockH5vccMemory;
    }
    assert_implements(
        mockH5vccMemory, 'missing mockH5vccMemory after initialization');

    mockH5vccMemory.start();
    try {
      await func(test, mockH5vccMemory);
    } finally {
      mockH5vccMemory.stop();
      mockH5vccMemory.reset();
    }
  }, name, properties);
}
