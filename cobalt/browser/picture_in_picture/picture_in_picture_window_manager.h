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

#ifndef COBALT_BROWSER_PICTURE_IN_PICTURE_PICTURE_IN_PICTURE_WINDOW_MANAGER_H_
#define COBALT_BROWSER_PICTURE_IN_PICTURE_PICTURE_IN_PICTURE_WINDOW_MANAGER_H_

#include <memory>

#include "base/memory/raw_ptr.h"
#include "base/memory/singleton.h"

namespace content {
class PictureInPictureWindowController;
class WebContents;
enum class PictureInPictureResult;
}  // namespace content

class PictureInPictureWindowManager {
 public:
  // Returns the singleton instance.
  static PictureInPictureWindowManager* GetInstance();

  content::PictureInPictureResult EnterVideoPictureInPicture(
      content::WebContents*);

  // Delete copy constructors since this is a singleton.
  PictureInPictureWindowManager(const PictureInPictureWindowManager&) = delete;
  PictureInPictureWindowManager& operator=(
      const PictureInPictureWindowManager&) = delete;

  // Shows the PiP window using the provided controller.
  void EnterPictureInPictureWithController(
      content::PictureInPictureWindowController* pip_window_controller);

  // Closes the active PiP window.
  void ExitPictureInPicture();

  // Gets the web contents of the active PiP window. Returns null if no window
  // is active.
  content::WebContents* GetWebContents() const;

 private:
  friend struct base::DefaultSingletonTraits<PictureInPictureWindowManager>;
  class VideoWebContentsObserver;

  PictureInPictureWindowManager();
  ~PictureInPictureWindowManager();

  void CloseWindowInternal();

  // The active Picture-in-Picture window controller.
  raw_ptr<content::PictureInPictureWindowController> pip_window_controller_ =
      nullptr;

  // Observes the active WebContents and closes the PiP window if it is
  // destroyed or navigates.
  std::unique_ptr<VideoWebContentsObserver> video_web_contents_observer_;
};

#endif  // COBALT_BROWSER_PICTURE_IN_PICTURE_PICTURE_IN_PICTURE_WINDOW_MANAGER_H_
