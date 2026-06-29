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

#include "cobalt/shell/browser/picture_in_picture/picture_in_picture_window_manager.h"

#include "content/public/browser/picture_in_picture_window_controller.h"
#include "content/public/browser/video_picture_in_picture_window_controller.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_delegate.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/common/url_constants.h"

PictureInPictureWindowManager* PictureInPictureWindowManager::GetInstance() {
  return base::Singleton<PictureInPictureWindowManager>::get();
}

PictureInPictureWindowManager::PictureInPictureWindowManager() = default;

PictureInPictureWindowManager::~PictureInPictureWindowManager() = default;

content::WebContents* PictureInPictureWindowManager::GetWebContents() const {
  if (!pip_window_controller_) {
    return nullptr;
  }
  return pip_window_controller_->GetWebContents();
}

content::PictureInPictureResult
PictureInPictureWindowManager::EnterVideoPictureInPicture(
    content::WebContents* web_contents) {
  if (!pip_window_controller_ ||
      pip_window_controller_->GetWebContents() != web_contents ||
      !pip_window_controller_->GetWebContents()->HasPictureInPictureVideo()) {
    if (pip_window_controller_) {
      CloseWindowInternal();
    }

    pip_window_controller_ = content::PictureInPictureWindowController::
        GetOrCreateVideoPictureInPictureController(web_contents);
  }

  return content::PictureInPictureResult::kSuccess;
}

void PictureInPictureWindowManager::EnterPictureInPictureWithController(
    content::PictureInPictureWindowController* pip_window_controller) {
  if (pip_window_controller_) {
    CloseWindowInternal();
  }

  pip_window_controller_ = pip_window_controller;
}

class PictureInPictureWindowManager::VideoWebContentsObserver final
    : public content::WebContentsObserver {};

void PictureInPictureWindowManager::ExitPictureInPicture() {
  CloseWindowInternal();
}

void PictureInPictureWindowManager::CloseWindowInternal() {
  if (pip_window_controller_) {
    pip_window_controller_->Close(false /* should_pause_video */);
    pip_window_controller_ = nullptr;
  }
}
