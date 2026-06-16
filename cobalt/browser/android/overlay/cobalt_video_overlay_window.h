// Copyright 2026 The Cobalt Authors. All Rights Reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_BROWSER_ANDROID_OVERLAY_COBALT_VIDEO_OVERLAY_WINDOW_H_
#define COBALT_BROWSER_ANDROID_OVERLAY_COBALT_VIDEO_OVERLAY_WINDOW_H_

#include <memory>
#include <vector>

#include "base/android/scoped_java_ref.h"
#include "content/public/browser/overlay_window.h"
#include "ui/gfx/geometry/rect.h" 
#include "ui/gfx/geometry/size.h"
#include "components/thin_webview/compositor_view.h"
#include "ui/android/window_android.h"
#include "cc/slim/surface_layer.h"

namespace content {
class VideoPictureInPictureWindowController;
}  // namespace content

namespace cobalt {

class CobaltVideoOverlayWindow : public content::VideoOverlayWindow {
 public:
  explicit CobaltVideoOverlayWindow(
      content::VideoPictureInPictureWindowController* controller);

  CobaltVideoOverlayWindow(const CobaltVideoOverlayWindow&) = delete;
  CobaltVideoOverlayWindow& operator=(const CobaltVideoOverlayWindow&) = delete;

  ~CobaltVideoOverlayWindow() override;

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
  void SetMediaPosition(
      const media_session::MediaPosition& position) override;
  void SetSourceTitle(const std::u16string& source_title) override;
  void SetFaviconImages(
      const std::vector<media_session::MediaImage>& images) override;
  void SetSurfaceId(const viz::SurfaceId& surface_id) override;

  // JNI callbacks from CobaltPictureInPictureActivity.java
  void SetJavaActivity(JNIEnv* env, const base::android::JavaParamRef<jobject>& activity);
  void OnActivityDestroyed(JNIEnv* env);
  void OnViewSizeChanged(JNIEnv* env, int width, int height);
  void CompositorViewCreated(JNIEnv* env, const base::android::JavaParamRef<jobject>& compositor_view);



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
