// Copyright 2019 The Cobalt Authors. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

package dev.cobalt.coat;

import static dev.cobalt.util.Log.TAG;

import dev.cobalt.util.Log;
import org.jni_zero.CalledByNative;
import org.jni_zero.JNINamespace;
import org.jni_zero.JniType;
import org.jni_zero.NativeMethods;

/**
 * Abstract class that provides an interface for Cobalt to interact with a platform service.
 *
 * <p>Threading model:
 *
 * <ul>
 *   <li>{@code openCobaltService}, {@code closeCobaltService}, and {@link #receiveFromClient} are
 *       invoked on the browser UI thread (Android main looper) and are serialized with Activity
 *       lifecycle callbacks.
 *   <li>{@link #sendToClient} may be invoked from any thread; synchronization ensures safety
 *       against concurrent {@link #onClose}.
 * </ul>
 */
public abstract class CobaltService {
  // Indicate is the service opened, and be able to send data to client
  protected boolean opened = true;
  private volatile String mServiceName;
  private volatile long mNativeService;
  private final Object lock = new Object();

  public CobaltService() {
    this.mNativeService = 0;
  }

  public CobaltService(long nativeService) {
    this.mNativeService = nativeService;
  }

  void setNativeService(long nativeService) {
    this.mNativeService = nativeService;
  }

  public long getNativeService() {
    return mNativeService;
  }

  void setServiceName(String serviceName) {
    mServiceName = serviceName;
  }

  public String getServiceName() {
    return mServiceName;
  }

  @JNINamespace("starboard")
  @NativeMethods
  public interface Natives {
    // Can not set it as nativeService, JNI zero has template code to convert it to a Service object
    void nativeSendToClient(long service, byte[] data);
  }

  /** Interface that returns an object that extends CobaltService. */
  public interface Factory {
    /** Create the service. */
    public CobaltService createCobaltService(long nativeService);

    /** Get the name of the service. */
    public String getServiceName();
  }

  /** Take in a reference to StarboardBridge & use it as needed. Default behavior is no-op. */
  public void receiveStarboardBridge(StarboardBridge bridge) {}

  /** Take in a reference to BaseStarboardBridge & use it as needed. Default behavior is no-op. */
  public void receiveBaseStarboardBridge(BaseStarboardBridge bridge) {}

  // Lifecycle
  /** Prepare service for start or resume. */
  public abstract void beforeStartOrResume();

  /** Prepare service for suspend. */
  public abstract void beforeSuspend();

  /** Prepare service for stop. */
  public abstract void afterStopped();

  // Service API
  /** Response to client from calls to receiveFromClient(). */
  public static class ResponseToClient {
    /** Indicate if the service was unable to receive data because it is in an invalid state. */
    public boolean invalidState;

    /** The synchronous response data from the service. */
    public byte[] data;

    @CalledByNative("ResponseToClient")
    public boolean getInvalidState() {
      return invalidState;
    }

    @CalledByNative("ResponseToClient")
    public byte[] getData() {
      return data;
    }
  }

  /** Receive data from client of the service. */
  @CalledByNative
  public abstract @JniType("ResponseToClientInfo") ResponseToClient receiveFromClient(byte[] data);

  /**
   * Close the service.
   *
   * <p>Once this function returns, it is invalid to call sendToClient for the nativeService, so
   * synchronization must be used to protect against this.
   */
  public void onClose() {
    synchronized (lock) {
      if (!opened) {
        return;
      }
      opened = false;
      close();
    }
  }

  public abstract void close();

  private static Natives sNatives;

  public static void setNativesForTesting(Natives natives) {
    sNatives = natives;
  }

  private static Natives getNatives() {
    if (sNatives != null) {
      return sNatives;
    }
    return CobaltServiceJni.get();
  }

  /**
   * Send data from the service to the client using the service's bound native handle.
   *
   * <p>This may be called from a separate thread.
   */
  protected void sendToClient(byte[] data) {
    sendToClient(mNativeService, data);
  }

  /**
   * Send data from the service to the client.
   *
   * <p>This may be called from a separate thread, do not call nativeSendToClient() once onClose()
   * is processed.
   */
  protected void sendToClient(long nativeService, byte[] data) {
    synchronized (lock) {
      if (!opened) {
        Log.w(
            TAG,
            "Platform service did not send data to client, because client already closed the"
                + " platform service.");
        return;
      }

      getNatives().nativeSendToClient(nativeService, data);
    }
  }
}
