// META: global=window
// META: script=/resources/test-only-api.js
// META: script=resources/automation.js

h5vcc_memory_tests(async (t, mockH5vccMemory) => {
    assert_implements(window.h5vcc, "window.h5vcc not supported");
    assert_implements(window.h5vcc.memory, "window.h5vcc.memory not supported");

    assert_true('onlowmemory' in window.h5vcc.memory, "onlowmemory should exist on window.h5vcc.memory");
}, 'H5vccMemory.onlowmemory: onlowmemory property exists on H5vccMemory');

h5vcc_memory_tests(async (t, mockH5vccMemory) => {
    assert_implements(window.h5vcc, "window.h5vcc not supported");
    assert_implements(window.h5vcc.memory, "window.h5vcc.memory not supported");

    let eventFired = false;
    const eventPromise = new Promise((resolve) => {
        window.h5vcc.memory.onlowmemory = (event) => {
            assert_equals(event.type, 'lowmemory');
            eventFired = true;
            resolve();
        };
    });
    t.add_cleanup(() => {
        window.h5vcc.memory.onlowmemory = null;
    });

    await mockH5vccMemory.onLowMemory();
    await eventPromise;
    assert_true(eventFired, "onlowmemory handler should have been called");
}, 'H5vccMemory.onlowmemory: Should receive lowmemory event via onlowmemory handler');

h5vcc_memory_tests(async (t, mockH5vccMemory) => {
    assert_implements(window.h5vcc, "window.h5vcc not supported");
    assert_implements(window.h5vcc.memory, "window.h5vcc.memory not supported");

    let eventFired = false;
    let resolvePromise;
    const eventPromise = new Promise((resolve) => {
        resolvePromise = resolve;
    });
    const listener = (event) => {
        assert_equals(event.type, 'lowmemory');
        eventFired = true;
        resolvePromise();
    };
    window.h5vcc.memory.addEventListener('lowmemory', listener);
    t.add_cleanup(() => {
        window.h5vcc.memory.removeEventListener('lowmemory', listener);
    });

    await mockH5vccMemory.onLowMemory();
    await eventPromise;
    assert_true(eventFired, "addEventListener lowmemory callback should have been called");
}, 'H5vccMemory.addEventListener: Should receive lowmemory event via addEventListener');
