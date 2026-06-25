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

#import "cobalt/shell/browser/on_screen_keyboard/tvos/cobalt_search_controller.h"

@implementation CobaltSearchController

#pragma mark - UIResponder

// See CobaltSearchResultsController for why these methods are necessary.

- (void)pressesBegan:(NSSet<UIPress*>*)presses
           withEvent:(UIPressesEvent*)event {
  NSMutableSet<UIPress*>* menuPresses = [NSMutableSet setWithCapacity:1];
  NSMutableSet<UIPress*>* nonMenuPresses =
      [NSMutableSet setWithCapacity:presses.count];
  for (UIPress* press in presses) {
    if (press.type == UIPressTypeMenu) {
      [menuPresses addObject:press];
    } else {
      [nonMenuPresses addObject:press];
    }
  }
  if (menuPresses.count > 0) {
    [self.menuKeyPressDelegate searchControllerMenuPressesBegan:menuPresses
                                                      withEvent:event];
  }
  if (nonMenuPresses.count > 0) {
    [super pressesBegan:nonMenuPresses withEvent:event];
  }
}

- (void)pressesEnded:(NSSet<UIPress*>*)presses
           withEvent:(UIPressesEvent*)event {
  NSMutableSet<UIPress*>* menuPresses = [NSMutableSet setWithCapacity:1];
  NSMutableSet<UIPress*>* nonMenuPresses =
      [NSMutableSet setWithCapacity:presses.count];
  for (UIPress* press in presses) {
    if (press.type == UIPressTypeMenu) {
      [menuPresses addObject:press];
    } else {
      [nonMenuPresses addObject:press];
    }
  }
  if (menuPresses.count > 0) {
    [self.menuKeyPressDelegate searchControllerMenuPressesEnded:menuPresses
                                                      withEvent:event];
  }
  if (nonMenuPresses.count > 0) {
    [super pressesEnded:nonMenuPresses withEvent:event];
  }
}

@end
