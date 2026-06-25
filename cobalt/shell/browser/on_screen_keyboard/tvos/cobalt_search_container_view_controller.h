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

#ifndef COBALT_SHELL_BROWSER_ON_SCREEN_KEYBOARD_TVOS_COBALT_SEARCH_CONTAINER_VIEW_CONTROLLER_H_
#define COBALT_SHELL_BROWSER_ON_SCREEN_KEYBOARD_TVOS_COBALT_SEARCH_CONTAINER_VIEW_CONTROLLER_H_

#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>

// A protocol for monitoring changes in CobaltSearchContainerViewController's
// lifecycle.
@protocol CobaltSearchContainerViewControllerDelegate <NSObject>

// Invoked when CobaltSearchContainerViewController's -viewDidAppear is called.
- (void)searchDidAppear;

// Invoked when CobaltSearchContainerViewController's -viewDidDisappear is
// called.
- (void)searchDidDisappear;

@end

// The UISearchContainerViewController used by Kabuki's search page for the
// native on-screen keyboard.
@interface CobaltSearchContainerViewController : UISearchContainerViewController

// A delegate that receives notifications about changes to this view
// controller's lifecycle.
@property(weak, nonatomic) id<CobaltSearchContainerViewControllerDelegate>
    delegate;

@end

#endif  // COBALT_SHELL_BROWSER_ON_SCREEN_KEYBOARD_TVOS_COBALT_SEARCH_CONTAINER_VIEW_CONTROLLER_H_
