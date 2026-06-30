// Copyright 2026 The Cobalt Authors. All Rights Reserved.
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

#ifndef COBALT_BROWSER_ANDROID_OVERLAY_COBALT_VIDEO_OVERLAY_WINDOW_H_
#define COBALT_BROWSER_ANDROID_OVERLAY_COBALT_VIDEO_OVERLAY_WINDOW_H_

#include <memory>
#include <vector>

#include "base/android/scoped_java_ref.h"
#include "cc/slim/surface_layer.h"
#include "components/thin_webview/compositor_view.h"
#include "content/public/browser/overlay_window.h"
#include "ui/android/window_android.h"
#include "ui/android/window_android_observer.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/size.h"

namespace content {
class VideoPictureInPictureWindowController;
}  // namespace content

namespace cobalt {

class CobaltVideoOverlayWindow : public content::VideoOverlayWindow,
                                 public ui::WindowAndroidObserver {
 public:
  explicit CobaltVideoOverlayWindow(
      content::VideoPictureInPictureWindowController* controller);

  CobaltVideoOverlayWindow(const CobaltVideoOverlayWindow&) = delete;
  CobaltVideoOverlayWindow& operator=(const CobaltVideoOverlayWindow&) = delete;

  ~CobaltVideoOverlayWindow() override;

  // ui::WindowAndroidObserver implementation.
  void OnRootWindowVisibilityChanged(bool visible) override;
  void OnAttachCompositor() override;
  void OnDetachCompositor() override;
  void OnAnimate(base::TimeTicks frame_begin_time) override;
  void OnActivityStopped() override;
  void OnActivityStarted() override;

  // VideoOverlayWindow implementation.
  bool IsActive() const override;
  void Close() override;
  void ShowInactive() override;
  void Hide() override;
  bool IsVisible() const override;
  gfx::Rect GetBounds() override;
  void UpdateNaturalSize(const gfx::Size& natural_size) override;
  void SetPlaybackState(PlaybackState playback_state) override;
  void SetPlayPauseButtonVisibility(bool is_visible) override;
  void SetSkipAdButtonVisibility(bool is_visible) override;
  void SetNextTrackButtonVisibility(bool is_visible) override;
  void SetPreviousTrackButtonVisibility(bool is_visible) override;
  void SetMicrophoneMuted(bool muted) override;
  void SetCameraState(bool turned_on) override;
  void SetToggleMicrophoneButtonVisibility(bool is_visible) override;
  void SetToggleCameraButtonVisibility(bool is_visible) override;
  void SetHangUpButtonVisibility(bool is_visible) override;
  void SetNextSlideButtonVisibility(bool is_visible) override;
  void SetPreviousSlideButtonVisibility(bool is_visible) override;
  void SetMediaPosition(const media_session::MediaPosition& position) override;
  void SetSourceTitle(const std::u16string& source_title) override;
  void SetFaviconImages(
      const std::vector<media_session::MediaImage>& images) override;
  void SetSurfaceId(const viz::SurfaceId& surface_id) override;

  // JNI callbacks from CobaltPictureInPictureActivity.java
  void SetJavaActivity(JNIEnv* env,
                       const base::android::JavaParamRef<jobject>& activity);
  void OnActivityDestroyed(JNIEnv* env);
  void OnViewSizeChanged(JNIEnv* env, int width, int height);
  void CompositorViewCreated(
      JNIEnv* env,
      const base::android::JavaParamRef<jobject>& compositor_view);

 private:
  // Pointer to the controller that owns this window.
  content::VideoPictureInPictureWindowController* controller_;

  // Tracks the visibility of the overlay window.
  bool is_visible_ = false;

  // Reference to the Java Activity instance.
  base::android::ScopedJavaGlobalRef<jobject> java_activity_ref_;

  ui::WindowAndroid* window_android_ = nullptr;
  thin_webview::android::CompositorView* compositor_view_ = nullptr;
  scoped_refptr<cc::slim::SurfaceLayer> surface_layer_;
  gfx::Size bounds_;

  // Helper method to trigger the Java Activity creation via JNI.
  void CreateJavaActivity();
};

}  // namespace cobalt

#endif  // COBALT_BROWSER_ANDROID_OVERLAY_COBALT_VIDEO_OVERLAY_WINDOW_H_
