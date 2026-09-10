import {H5vccMemory, H5vccMemoryReceiver} from '/gen/cobalt/browser/h5vcc_memory/public/mojom/h5vcc_memory.mojom.m.js';

// Implementation of h5vcc_memory.mojom.H5vccMemory.
class MockH5vccMemory {
  constructor() {
    this.interceptor_ =
        new MojoInterfaceInterceptor(H5vccMemory.$interfaceName);
    this.interceptor_.oninterfacerequest = e => this.bind(e.handle);
    this.receiver_ = new H5vccMemoryReceiver(this);
    this.reset();
  }

  start() {
    this.interceptor_.start();
  }

  stop() {
    this.interceptor_.stop();
  }

  bind(handle) {
    if (this.receiver_ && this.receiver_.$.isBound) {
      this.receiver_.$.close();
    }
    this.receiver_.$.bindHandle(handle);
  }

  reset() {
    this.lowMemoryListeners_ = [];
    this.listenerPromise_ = new Promise(resolve => {
      this.listenerResolver_ = resolve;
    });
    if (this.receiver_ && this.receiver_.$.isBound) {
      this.receiver_.$.close();
    }
  }

  // h5vcc_memory.mojom.H5vccMemory impl.
  addLowMemoryListener(listener) {
    this.lowMemoryListeners_.push(listener);
    if (this.listenerResolver_) {
      this.listenerResolver_(listener);
    }
  }

  async onLowMemory() {
    if (this.lowMemoryListeners_.length === 0) {
      await this.listenerPromise_;
    }
    for (const listener of this.lowMemoryListeners_) {
      listener.onLowMemory();
    }
  }
}

export const mockH5vccMemory = new MockH5vccMemory();
