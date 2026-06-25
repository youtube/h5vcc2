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

#ifndef COBALT_SHELL_BROWSER_ON_SCREEN_KEYBOARD_TVOS_COBALT_SEARCH_CONTROLLER_H_
#define COBALT_SHELL_BROWSER_ON_SCREEN_KEYBOARD_TVOS_COBALT_SEARCH_CONTROLLER_H_

#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>

// A protocol used by CobaltSearchController to forward Menu key presses
// received via its -pressesBegan and -pressesEnded methods.
//
// Menu keys need to be handled differently by Cobalt since they must be
// forwarded to Kabuki even if the search bar is focused.
@protocol CobaltSearchControllerPressesDelegate <NSObject>

- (void)searchControllerMenuPressesBegan:(NSSet<UIPress*>*)presses
                               withEvent:(UIPressesEvent*)event;

- (void)searchControllerMenuPressesEnded:(NSSet<UIPress*>*)presses
                               withEvent:(UIPressesEvent*)event;

@end

@interface CobaltSearchController : UISearchController

// The delegate that will receive notifications about Menu key presses.
@property(nonatomic) id<CobaltSearchControllerPressesDelegate>
    menuKeyPressDelegate;

@end

#endif  // COBALT_SHELL_BROWSER_ON_SCREEN_KEYBOARD_TVOS_COBALT_SEARCH_CONTROLLER_H_
