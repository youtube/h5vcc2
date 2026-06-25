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

#import "cobalt/shell/browser/on_screen_keyboard/tvos/cobalt_search_results_controller.h"

@implementation CobaltSearchResultsController

#pragma mark - UIResponder

// These -pressesBegan and -pressesEnded implementations work together with
// CobaltSearchController's and ContentShellWindowDelegate's.
//
// CobaltSearchController's -presses{Began,Ended} handle Menu key presses
// specially by forwarding them to Kabuki via ContentShellWindowDelegate's
// implementation of CobaltSearchResultsControllerFocusDelegate.
//
// RenderWidgetUIView will eventually receive these events and pass them up, but
// since the web contents view will be part of CobaltSearchController's view
// hierarchy the event will reach its -pressesBegan and -pressesEnded and
// recurse forever. This special case is intercepted and disposed of here
// because this view controller sits between RenderWidgetUIView and
// CobaltSearchController.
- (void)pressesBegan:(NSSet<UIPress*>*)presses
           withEvent:(UIPressesEvent*)event {
  for (UIPress* press in presses) {
    if (press.type == UIPressTypeMenu) {
      return;
    }
  }
  [super pressesBegan:presses withEvent:event];
}

- (void)pressesEnded:(NSSet<UIPress*>*)presses
           withEvent:(UIPressesEvent*)event {
  for (UIPress* press in presses) {
    if (press.type == UIPressTypeMenu) {
      return;
    }
  }
  [super pressesEnded:presses withEvent:event];
}

#pragma mark - UIFocusEnvironment

- (void)didUpdateFocusInContext:(UIFocusUpdateContext*)context
       withAnimationCoordinator:(UIFocusAnimationCoordinator*)coordinator {
  if ([context.nextFocusedView isDescendantOfView:self.view]) {
    if (![context.previouslyFocusedView isDescendantOfView:self.view]) {
      [_focusDelegate resultsDidReceiveFocus];
    }
  } else if ([context.previouslyFocusedView isDescendantOfView:self.view]) {
    [_focusDelegate resultsDidLoseFocus];
  }
}

@end
