// Copyright 2026 The Cobalt Authors. All Rights Reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cobalt/browser/picture_in_picture/picture_in_picture_window_manager.h"

#include "content/public/browser/picture_in_picture_window_controller.h"
#include "content/public/browser/video_picture_in_picture_window_controller.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_delegate.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/common/url_constants.h"


PictureInPictureWindowManager* PictureInPictureWindowManager::GetInstance() {
  return base::Singleton<PictureInPictureWindowManager>::get();
}

content::PictureInPictureResult PictureInPictureWindowManager::EnterVideoPictureInPicture(
      content::WebContents*){
      }


void PictureInPictureWindowManager::EnterPictureInPictureWithController(
      content::PictureInPictureWindowController* pip_window_controller){
        
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