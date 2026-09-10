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

package dev.cobalt.media;

import android.graphics.SurfaceTexture;
import android.view.Surface;
import androidx.annotation.GuardedBy;
import org.jni_zero.CalledByNative;
import org.jni_zero.CalledByNativeForTesting;
import org.jni_zero.JNINamespace;
import org.jni_zero.JniType;
import org.jni_zero.NativeMethods;

/**
 * A wrapper of SurfaceTexture class. VideoSurfaceTexture allows native code to receive
 * OnFrameAvailable event.
 */
@JNINamespace("starboard")
public class VideoSurfaceTexture extends SurfaceTexture {
  private final Object mLock = new Object();

  @GuardedBy("mLock")
  private long mNativeVideoSurfaceTextureBridge;

  // Cache the matrix to avoid allocating a new float[16] array on every call
  // to getTransformMatrix(), which is called on the rendering hot path (60fps)
  // and would cause significant Garbage Collection (GC) churn.
  private final float[] mTransformMatrix = new float[16];

  @CalledByNative
  VideoSurfaceTexture(int texName) {
    super(texName);
  }

  @CalledByNative
  void setOnFrameAvailableListener(final long nativeVideoSurfaceTextureBridge) {
    synchronized (mLock) {
      mNativeVideoSurfaceTextureBridge = nativeVideoSurfaceTextureBridge;
    }
    super.setOnFrameAvailableListener(
        new SurfaceTexture.OnFrameAvailableListener() {
          @Override
          public void onFrameAvailable(SurfaceTexture surfaceTexture) {
            synchronized (mLock) {
              if (mNativeVideoSurfaceTextureBridge != 0) {
                VideoSurfaceTextureJni.get().onFrameAvailable(mNativeVideoSurfaceTextureBridge);
              }
            }
          }
        });
  }

  @CalledByNative
  void removeOnFrameAvailableListener() {
    synchronized (mLock) {
      mNativeVideoSurfaceTextureBridge = 0;
    }
    super.setOnFrameAvailableListener(null);
  }

  @CalledByNative
  static Surface createSurface(VideoSurfaceTexture surfaceTexture) {
    return new Surface(surfaceTexture);
  }

  @CalledByNativeForTesting
  static Surface createSurfaceForTesting() {
    return new Surface(new SurfaceTexture(1));
  }

  @CalledByNative
  public void updateTexImage() {
    super.updateTexImage();
  }

  @CalledByNative
  public @JniType("std::array<float, 16>") float[] getTransformMatrix() {
    super.getTransformMatrix(mTransformMatrix);
    return mTransformMatrix;
  }

  @NativeMethods
  interface Natives {
    void onFrameAvailable(long nativeVideoSurfaceTextureBridge);
  }
}
