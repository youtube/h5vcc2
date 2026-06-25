// Copyright 2025 The Cobalt Authors. All Rights Reserved.
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

#include "cobalt/shell/browser/shell_platform_delegate.h"

#include <CoreGraphics/CGGeometry.h>
#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>
#include <dispatch/dispatch.h>

#include "base/containers/contains.h"
#include "base/files/file.h"
#include "base/functional/callback_helpers.h"
#include "base/strings/sys_string_conversions.h"
#include "base/trace_event/trace_config.h"
#include "cobalt/browser/on_screen_keyboard/platform_on_screen_keyboard.h"
#include "cobalt/browser/on_screen_keyboard/public/mojom/on_screen_keyboard.mojom.h"
#include "cobalt/shell/app/resource.h"
#import "cobalt/shell/browser/on_screen_keyboard/tvos/cobalt_search_container_view_controller.h"
#import "cobalt/shell/browser/on_screen_keyboard/tvos/cobalt_search_controller.h"
#import "cobalt/shell/browser/on_screen_keyboard/tvos/cobalt_search_results_controller.h"
#include "cobalt/shell/browser/shell.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/visibility.h"
#include "content/public/browser/web_contents.h"
#include "services/tracing/public/cpp/perfetto/perfetto_config.h"
#include "services/tracing/public/mojom/constants.mojom.h"
#import "starboard/tvos/shared/starboard_application.h"
#include "third_party/perfetto/include/perfetto/tracing/core/trace_config.h"
#include "third_party/perfetto/include/perfetto/tracing/tracing.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/display/screen.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/native_widget_types.h"

#if !defined(__has_feature) || !__has_feature(objc_arc)
#error "This file requires ARC support."
#endif

namespace {

const char kGraphicsTracingCategories[] =
    "-*,blink,cc,gpu,renderer.scheduler,sequence_manager,v8,toplevel,viz,evdev,"
    "input,benchmark";

const char kDetailedGraphicsTracingCategories[] =
    "-*,blink,cc,gpu,renderer.scheduler,sequence_manager,v8,toplevel,viz,evdev,"
    "input,benchmark,disabled-by-default-skia,disabled-by-default-skia.gpu,"
    "disabled-by-default-skia.gpu.cache,disabled-by-default-skia.shaders";

const char kNavigationTracingCategories[] =
    "-*,benchmark,toplevel,ipc,base,browser,navigation,omnibox,ui,shutdown,"
    "safe_browsing,loading,startup,mojom,renderer_host,"
    "disabled-by-default-system_stats,disabled-by-default-cpu_profiler,dwrite,"
    "fonts,ServiceWorker,passwords,disabled-by-default-file,sql,"
    "disabled-by-default-user_action_samples,disk_cache";

const char kAllTracingCategories[] = "*";

}  // namespace

@interface TracingHandler : NSObject {
 @private
  std::unique_ptr<perfetto::TracingSession> _tracingSession;
  NSFileHandle* _traceFileHandle;
}

- (void)startWithHandler:(void (^)())startHandler
             stopHandler:(void (^)())stopHandler
              categories:(const char*)categories;
- (void)stop;
- (BOOL)isTracing;

@end

// Protocol used by ContentShellWindowDelegate to notify that certain on-screen
// keyboard events have occurred.
@protocol PlatformOnScreenKeyboardDelegate <NSObject>

- (void)keyboardBlurred;

- (void)keyboardFocused;

- (void)keyboardTextChanged:(NSString*)text;

@end

// Translates the notifications sent by PlatformOnScreenKeyboardDelegate to
// Chromium callbacks.
@interface OnScreenKeyboardObserver
    : NSObject <PlatformOnScreenKeyboardDelegate>

- (instancetype)initWithCallbacks:(base::RepeatingClosure)blurredCallback
                  focusedCallback:(base::RepeatingClosure)focusedCallback
              textChangedCallback:
                  (base::RepeatingCallback<void(const std::string&)>)
                      textChangedCallback;

@end

@implementation OnScreenKeyboardObserver {
 @private
  base::RepeatingClosure _blurredCallback;
  base::RepeatingClosure _focusedCallback;
  base::RepeatingCallback<void(const std::string&)> _textChangedCallback;
}

- (instancetype)initWithCallbacks:(base::RepeatingClosure)blurredCallback
                  focusedCallback:(base::RepeatingClosure)focusedCallback
              textChangedCallback:
                  (base::RepeatingCallback<void(const std::string&)>)
                      textChangedCallback {
  if (!(self = [super init])) {
    return nil;
  }
  _blurredCallback = std::move(blurredCallback);
  _focusedCallback = std::move(focusedCallback);
  _textChangedCallback = std::move(textChangedCallback);
  return self;
}

#pragma mark - PlatformOnScreenKeyboardDelegate

- (void)keyboardBlurred {
  _blurredCallback.Run();
}

- (void)keyboardFocused {
  _focusedCallback.Run();
}

- (void)keyboardTextChanged:(NSString*)text {
  _textChangedCallback.Run(base::SysNSStringToUTF8(text));
}

@end

@interface ContentShellWindowDelegate
    : UIViewController <UITextFieldDelegate,
                        UISearchResultsUpdating,
                        CobaltSearchControllerPressesDelegate,
                        CobaltSearchResultsControllerFocusDelegate,
                        CobaltSearchContainerViewControllerDelegate> {
 @private
  raw_ptr<content::Shell> _shell;

  CobaltSearchResultsController* _searchResultsViewController;
  CobaltSearchController* _searchController;
  CobaltSearchContainerViewController* _searchContainerViewController;
  enum class SearchKeyboardVisibilityState {
    // The native search keyboard is not being shown.
    kHidden,
    // The native search keyboard is being created but is not visible yet.
    kCreating,
    // The native search keyboard has been created and is visible.
    kVisible,
  } _keyboardVisibilityState;
  void (^_showOnScreenKeyboardCompletionHandler)(void);
  void (^_focusOnScreenKeyboardCompletionHandler)(void);
}
// Toolbar containing navigation buttons and |urlField|.
@property(nonatomic, strong) UIStackView* toolbarBackgroundView;
// Toolbar containing navigation buttons and |urlField|.
@property(nonatomic, strong) UIStackView* toolbarContentView;
// Button to navigate backwards.
@property(nonatomic, strong) UIButton* backButton;
// Button to navigate forwards.
@property(nonatomic, strong) UIButton* forwardButton;
// Button that either refresh the page or stops the page load.
@property(nonatomic, strong) UIButton* reloadOrStopButton;
// Button that shows the menu
@property(nonatomic, strong) UIButton* menuButton;
// Text field used for navigating to URLs.
@property(nonatomic, strong) UITextField* urlField;
// Container for the native search UI elements.
@property(nonatomic, strong) UIView* searchView;
// Container for |webView|.
@property(nonatomic, strong) UIView* contentView;
// Manages tracing and tracing state.
@property(nonatomic, strong) TracingHandler* tracingHandler;
// PlatformOnScreenKeyboardDelegate implementation.
@property(nonatomic, weak) id<PlatformOnScreenKeyboardDelegate>
    platformOnScreenKeyboardDelegate;

+ (UIColor*)backgroundColorDefault;
+ (UIColor*)backgroundColorTracing;
- (id)initWithShell:(content::Shell*)shell;
- (content::Shell*)shell;
- (UIStackView*)createToolbarBackgroundView;
- (UIStackView*)createToolbarContentView;
- (UIButton*)makeButton:(NSString*)imageName action:(SEL)action;
- (UITextField*)makeURLBar;
- (void)back;
- (void)forward;
- (void)reloadOrStop;
- (void)setURL:(NSString*)url;
- (void)stopTracing;
- (void)startTracingWithCategories:(const char*)categories;
- (UIAlertController*)actionSheetWithTitle:(nullable NSString*)title
                                   message:(nullable NSString*)message;
@end

@implementation ContentShellWindowDelegate
@synthesize backButton = _backButton;
@synthesize searchView = _searchView;
@synthesize contentView = _contentView;
@synthesize urlField = _urlField;
@synthesize forwardButton = _forwardButton;
@synthesize reloadOrStopButton = _reloadOrStopButton;
@synthesize menuButton = _menuButton;
@synthesize toolbarBackgroundView = _toolbarBackgroundView;
@synthesize toolbarContentView = _toolbarContentView;
@synthesize tracingHandler = _tracingHandler;

+ (UIColor*)backgroundColorDefault {
  return [UIColor colorWithRed:66.0 / 255.0
                         green:133.0 / 255.0
                          blue:244.0 / 255.0
                         alpha:1.0];
}

+ (UIColor*)backgroundColorTracing {
  return [UIColor colorWithRed:234.0 / 255.0
                         green:67.0 / 255.0
                          blue:53.0 / 255.0
                         alpha:1.0];
}

#if BUILDFLAG(IS_IOS_TVOS)
// Intercept UIPressTypeMenu event and do not forward it to the
// superclass's pressesBegan method, as it will cause the application to
// exit immediately. Instead, we save the UIPressTypeMenu press and event
// and only forward it when suspendApplication is invoked.
- (void)pressesBegan:(NSSet<UIPress*>*)presses
           withEvent:(UIPressesEvent*)event {
  NSMutableSet<UIPress*>* nonMenuPresses =
      [NSMutableSet setWithCapacity:presses.count];
  for (UIPress* press in presses) {
    if (press.type == UIPressTypeMenu) {
      [SBDGetApplication() registerMenuPressBegan:press pressesEvent:event];
    } else {
      [nonMenuPresses addObject:press];
    }
  }
  if (nonMenuPresses.count > 0) {
    [super pressesBegan:nonMenuPresses withEvent:event];
  }
}

- (void)pressesEnded:(NSSet<UIPress*>*)presses
           withEvent:(UIPressesEvent*)event {
  NSMutableSet<UIPress*>* nonMenuPresses =
      [NSMutableSet setWithCapacity:presses.count];
  for (UIPress* press in presses) {
    if (press.type == UIPressTypeMenu) {
      [SBDGetApplication() registerMenuPressEnded:press pressesEvent:event];
    } else {
      [nonMenuPresses addObject:press];
    }
  }
  if (nonMenuPresses.count > 0) {
    [super pressesEnded:nonMenuPresses withEvent:event];
  }
}
#endif  // BUILDFLAG(IS_IOS_TVOS)

- (void)viewDidLoad {
  [super viewDidLoad];

  // Create a web content view.
  self.contentView = [[UIView alloc] init];
  [self.view addSubview:_contentView];

  // Create a toolbar.
  if (!content::Shell::ShouldHideToolbar()) {
    self.toolbarBackgroundView = [self createToolbarBackgroundView];
    self.toolbarContentView = [self createToolbarContentView];

    self.backButton = [self makeButton:@"ic_back" action:@selector(back)];
    self.forwardButton = [self makeButton:@"ic_forward"
                                   action:@selector(forward)];
    self.reloadOrStopButton = [self makeButton:@"ic_reload"
                                        action:@selector(reloadOrStop)];
    self.menuButton = [self makeButton:@"ic_menu"
                                action:@selector(showMainMenu)];
    self.urlField = [self makeURLBar];
    self.tracingHandler = [[TracingHandler alloc] init];

    [self.view addSubview:_toolbarBackgroundView];
    [_toolbarBackgroundView addArrangedSubview:_toolbarContentView];

    [_toolbarContentView addArrangedSubview:_backButton];
    [_toolbarContentView addArrangedSubview:_forwardButton];
    [_toolbarContentView addArrangedSubview:_reloadOrStopButton];
    [_toolbarContentView addArrangedSubview:_menuButton];
    [_toolbarContentView addArrangedSubview:_urlField];

    self.view.accessibilityElements = @[ _toolbarBackgroundView, _contentView ];
    self.view.isAccessibilityElement = NO;

    // Constraint the toolbar background view.
    _toolbarBackgroundView.translatesAutoresizingMaskIntoConstraints = NO;
    [NSLayoutConstraint activateConstraints:@[
      [_toolbarBackgroundView.topAnchor
          constraintEqualToAnchor:self.view.topAnchor],
      [_toolbarBackgroundView.leadingAnchor
          constraintEqualToAnchor:self.view.leadingAnchor],
      [_toolbarBackgroundView.trailingAnchor
          constraintEqualToAnchor:self.view.trailingAnchor],
    ]];

    // Constraint the toolbar content view.
    _toolbarContentView.translatesAutoresizingMaskIntoConstraints = NO;
    [NSLayoutConstraint activateConstraints:@[
      // This height constraint is somewhat arbitrary: the idea is that it gives
      // us enough space to centralize the buttons inside |_toolbarContentView|
      // while having enough top and bottom margins.
      // Twice the size of a button also accounts for platforms such as tvOS,
      // where focused buttons are larger and have a drop shadow.
      [_toolbarContentView.heightAnchor
          constraintEqualToAnchor:_backButton.heightAnchor
                       multiplier:2.0],
    ]];
  }  // if (!content::Shell::ShouldHideToolbar())

  // Constraint the web content view.
  _contentView.translatesAutoresizingMaskIntoConstraints = NO;
  [NSLayoutConstraint activateConstraints:@[
    [_contentView.topAnchor
        constraintEqualToAnchor:content::Shell::ShouldHideToolbar()
                                    ? self.view.topAnchor
                                    : _toolbarBackgroundView.bottomAnchor],
    [_contentView.leadingAnchor
        constraintEqualToAnchor:self.view.leadingAnchor],
    [_contentView.trailingAnchor
        constraintEqualToAnchor:self.view.trailingAnchor],
    [_contentView.bottomAnchor constraintEqualToAnchor:self.view.bottomAnchor],
  ]];

  // The native search bar, shown only in the main search page.
  // SBDGetApplication().onScreenKeyboardManager = self;
  _searchView = [[UIView alloc] initWithFrame:_contentView.bounds];
  _searchView.accessibilityIdentifier = @"Search Container";
  // _searchContainer.backgroundColor = [UIColor redColor];
  // Ensure the view expands when _contentView is resized (using constraints
  // also works).
  _searchView.autoresizingMask =
      UIViewAutoresizingFlexibleHeight | UIViewAutoresizingFlexibleWidth;
  [_contentView addSubview:_searchView];

  // UIView that will contain the video rendered by SbPlayer. The non-tvOS code
  // path renders the video as an underlay, so add it before the web contents
  // view. Each video will be rendered as a subview of this view.
  // Note that the actual size and and position of this view are irrelevant at
  // this point: it will be changed in starboard's
  // AVSBVideoRenderer::SetBounds() when necessary.
  UIView* playerContainerView = [[UIView alloc] init];
  playerContainerView.accessibilityIdentifier = @"Player Container";
  [_contentView addSubview:playerContainerView];
  [SBDGetApplication() setPlayerContainerView:playerContainerView];

  UIView* web_contents_view = _shell->web_contents()->GetNativeView().Get();
  [_contentView addSubview:web_contents_view];
}

- (id)initWithShell:(content::Shell*)shell {
  if ((self = [super init])) {
    _shell = shell;
    _keyboardVisibilityState = SearchKeyboardVisibilityState::kHidden;
  }
  return self;
}

- (content::Shell*)shell {
  return _shell;
}

- (UIButton*)makeButton:(NSString*)imageName action:(SEL)action {
  UIButton* button = [UIButton buttonWithType:UIButtonTypeSystem];
  [button setImage:[UIImage imageNamed:imageName]
          forState:UIControlStateNormal];
  button.tintColor = [UIColor whiteColor];
  [button addTarget:self
                action:action
      forControlEvents:UIControlEventTouchUpInside |
                       UIControlEventPrimaryActionTriggered];
  return button;
}

- (UITextField*)makeURLBar {
  UITextField* field = [[UITextField alloc] init];
  field.placeholder = @"Search or type URL";
  field.tintColor = _toolbarBackgroundView.backgroundColor;
  [field setContentHuggingPriority:UILayoutPriorityDefaultLow - 1
                           forAxis:UILayoutConstraintAxisHorizontal];
  field.delegate = self;
  field.borderStyle = UITextBorderStyleRoundedRect;
  field.keyboardType = UIKeyboardTypeWebSearch;
  field.autocapitalizationType = UITextAutocapitalizationTypeNone;
  field.clearButtonMode = UITextFieldViewModeWhileEditing;
  field.autocorrectionType = UITextAutocorrectionTypeNo;
  return field;
}

- (UIStackView*)createToolbarBackgroundView {
  UIStackView* toolbarBackgroundView = [[UIStackView alloc] init];

  // |toolbarBackgroundView| is a 1-item UIStackView. We use a UIStackView so
  // that we can:
  // 1. Easily hide |toolbarContentView| when entering fullscreen mode in a
  // way that removes it from the layout.
  // 2. Let UIStackView figure out most constraints for |toolbarContentView|
  // so that we do not have to do it manually.
  toolbarBackgroundView.backgroundColor =
      [ContentShellWindowDelegate backgroundColorDefault];
  toolbarBackgroundView.alignment = UIStackViewAlignmentBottom;
  toolbarBackgroundView.axis = UILayoutConstraintAxisHorizontal;

  // Use the root view's layout margins (which account for safe areas and the
  // system's minimum margins).
  toolbarBackgroundView.layoutMarginsRelativeArrangement = YES;
  toolbarBackgroundView.preservesSuperviewLayoutMargins = YES;

  return toolbarBackgroundView;
}

- (UIStackView*)createToolbarContentView {
  UIStackView* toolbarContentView = [[UIStackView alloc] init];

#if BUILDFLAG(IS_IOS_TVOS)
  // On tvOS, make it impossible to focus `_toolbarContentView` by simply
  // swiping up on the remote control since this behavior is not intuitive.
  toolbarContentView.userInteractionEnabled = NO;
#endif

  toolbarContentView.alignment = UIStackViewAlignmentCenter;
  toolbarContentView.axis = UILayoutConstraintAxisHorizontal;
  toolbarContentView.spacing = 16.0;

  return toolbarContentView;
}

- (void)back {
  _shell->GoBackOrForward(-1);
}

- (void)forward {
  _shell->GoBackOrForward(1);
}

- (void)reloadOrStop {
  if (_shell->web_contents()->IsLoading()) {
    _shell->Stop();
  } else {
    _shell->Reload();
  }
}

- (void)showMainMenu {
  UIAlertController* alertController = [self actionSheetWithTitle:@"Main menu"
                                                          message:nil];

  [alertController
      addAction:[UIAlertAction actionWithTitle:@"Cancel"
                                         style:UIAlertActionStyleCancel
                                       handler:nil]];

  __weak ContentShellWindowDelegate* weakSelf = self;

  if ([_tracingHandler isTracing]) {
    [alertController
        addAction:[UIAlertAction actionWithTitle:@"End Tracing"
                                           style:UIAlertActionStyleDefault
                                         handler:^(UIAlertAction* action) {
                                           [weakSelf stopTracing];
                                         }]];
  } else {
    [alertController
        addAction:[UIAlertAction actionWithTitle:@"Begin Graphics Tracing"
                                           style:UIAlertActionStyleDefault
                                         handler:^(UIAlertAction* action) {
                                           [weakSelf
                                               startTracingWithCategories:
                                                   kGraphicsTracingCategories];
                                         }]];
    [alertController
        addAction:[UIAlertAction
                      actionWithTitle:@"Begin Detailed Graphics Tracing"
                                style:UIAlertActionStyleDefault
                              handler:^(UIAlertAction* action) {
                                [weakSelf
                                    startTracingWithCategories:
                                        kDetailedGraphicsTracingCategories];
                              }]];
    [alertController
        addAction:[UIAlertAction
                      actionWithTitle:@"Begin Navigation Tracing"
                                style:UIAlertActionStyleDefault
                              handler:^(UIAlertAction* action) {
                                [weakSelf startTracingWithCategories:
                                              kNavigationTracingCategories];
                              }]];
    [alertController
        addAction:[UIAlertAction actionWithTitle:@"Begin Tracing All Categories"
                                           style:UIAlertActionStyleDefault
                                         handler:^(UIAlertAction* action) {
                                           [weakSelf startTracingWithCategories:
                                                         kAllTracingCategories];
                                         }]];
  }

  [self presentViewController:alertController animated:YES completion:nil];
}

- (void)updateBackground {
  _toolbarBackgroundView.backgroundColor =
      [_tracingHandler isTracing]
          ? [ContentShellWindowDelegate backgroundColorTracing]
          : [ContentShellWindowDelegate backgroundColorDefault];
}

- (void)stopTracing {
  [_tracingHandler stop];
}

- (void)startTracingWithCategories:(const char*)categories {
  __weak ContentShellWindowDelegate* weakSelf = self;
  [_tracingHandler
      startWithHandler:^{
        [weakSelf updateBackground];
      }
      stopHandler:^{
        [weakSelf updateBackground];
      }
      categories:categories];
}

- (void)setURL:(NSString*)url {
  _urlField.text = url;
}

- (BOOL)textFieldShouldReturn:(UITextField*)field {
  std::string field_value = base::SysNSStringToUTF8(field.text);
  GURL url(field_value);
  if (!url.has_scheme()) {
    // TODOD(dtapuska): Fix this to URL encode the query.
    std::string search_url = "https://www.google.com/search?q=" + field_value;
    url = GURL(search_url);
  }
  _shell->LoadURL(url);
  return YES;
}

- (UIAlertController*)actionSheetWithTitle:(nullable NSString*)title
                                   message:(nullable NSString*)message {
  UIAlertController* alertController = [UIAlertController
      alertControllerWithTitle:title
                       message:message
                preferredStyle:UIAlertControllerStyleActionSheet];
  alertController.popoverPresentationController.sourceView = _menuButton;
  alertController.popoverPresentationController.sourceRect =
      CGRectMake(CGRectGetWidth(_menuButton.bounds) / 2,
                 CGRectGetHeight(_menuButton.bounds), 1, 1);
  return alertController;
}

#pragma mark - SBDOnScreenKeyboardManager

- (void)showOnScreenKeyboard:(NSString*)searchText
               keyboardStyle:(UIUserInterfaceStyle)keyboardStyle
               colorOverride:(UIColor*)colorOverride
           completionHandler:(void (^)())completionHandler {
  if (_keyboardVisibilityState != SearchKeyboardVisibilityState::kHidden) {
    // From C25 (b/182076373):
    // For some languages such as Hebrew, setting the search bar text to the
    // same value may result in the text being cleared. Work around this bug.
    if (![_searchController.searchBar.text isEqualToString:searchText]) {
      _searchController.searchBar.text = searchText;
    }
    completionHandler();
    return;
  }

  // This block comes from C25. The original commit has no associated bug and
  // only mentions "make sure show/hide are always done even if when the event
  // is originally received the keyboard isBeingPresented or Dismissed".
  if (_searchController.isBeingPresented ||
      _searchController.isBeingDismissed) {
    __weak __typeof(self) weakself = self;
    dispatch_after(
        dispatch_time(DISPATCH_TIME_NOW, (int64_t)(0.1 * NSEC_PER_SEC)),
        dispatch_get_main_queue(), ^{
          ContentShellWindowDelegate* strongself = weakself;
          if (!strongself) {
            return;
          }
          [strongself showOnScreenKeyboard:searchText
                             keyboardStyle:keyboardStyle
                             colorOverride:colorOverride
                         completionHandler:completionHandler];
        });
    return;
  }

  _keyboardVisibilityState = SearchKeyboardVisibilityState::kCreating;
  _showOnScreenKeyboardCompletionHandler = completionHandler;

  _searchResultsViewController = [[CobaltSearchResultsController alloc] init];
  _searchResultsViewController.focusDelegate = self;
  _searchController = [[CobaltSearchController alloc]
      initWithSearchResultsController:_searchResultsViewController];
  _searchController.searchBar.text = searchText;
  _searchController.obscuresBackgroundDuringPresentation = NO;
  _searchController.hidesNavigationBarDuringPresentation = NO;
  _searchController.overrideUserInterfaceStyle = keyboardStyle;
  _searchController.view.backgroundColor = colorOverride;
  _searchController.searchResultsUpdater = self;
  _searchController.menuKeyPressDelegate = self;

  _searchContainerViewController = [[CobaltSearchContainerViewController alloc]
      initWithSearchController:_searchController];
  _searchContainerViewController.delegate = self;

  [self addChildViewController:_searchContainerViewController];
  [_searchContainerViewController didMoveToParentViewController:self];
  [_searchView addSubview:_searchContainerViewController.view];
  _searchController.active = YES;
}

- (void)hideOnScreenKeyboard {
  if (_keyboardVisibilityState == SearchKeyboardVisibilityState::kHidden) {
    return;
  }

  // This block comes from C25. The original commit has no associated bug and
  // only mentions "make sure show/hide are always done even if when the event
  // is originally received the keyboard isBeingPresented or Dismissed".
  if (_searchController.isBeingPresented ||
      _searchController.isBeingDismissed) {
    __weak __typeof(self) weakself = self;
    dispatch_after(
        dispatch_time(DISPATCH_TIME_NOW, (int64_t)(0.1 * NSEC_PER_SEC)),
        dispatch_get_main_queue(), ^{
          ContentShellWindowDelegate* strongself = weakself;
          if (!strongself) {
            return;
          }
          [strongself hideOnScreenKeyboard];
        });
    return;
  }

  _keyboardVisibilityState = SearchKeyboardVisibilityState::kHidden;
  [_searchContainerViewController.view removeFromSuperview];
  _searchController.active = NO;
  [_searchContainerViewController willMoveToParentViewController:nil];
  [_searchContainerViewController removeFromParentViewController];
}

- (void)focusOnScreenKeyboard:(void (^)(void))completionHandler {
  if (_keyboardVisibilityState == SearchKeyboardVisibilityState::kHidden) {
    completionHandler();
    return;
  } else if (_keyboardVisibilityState ==
             SearchKeyboardVisibilityState::kCreating) {
    _focusOnScreenKeyboardCompletionHandler = completionHandler;
    return;
  }

  // Disable user interaction in the web contents view just while updating focus
  // so that it switches back to the keyboard view.
  _shell->web_contents()->GetNativeView().Get().userInteractionEnabled = NO;
  [self setNeedsFocusUpdate];
  [self updateFocusIfNeeded];
  _shell->web_contents()->GetNativeView().Get().userInteractionEnabled = YES;

  completionHandler();
}

- (CGRect)boundingRect {
  if (_keyboardVisibilityState != SearchKeyboardVisibilityState::kVisible) {
    return CGRectMake(0, 0, 0, 0);
  }

  // From C25:
  // UISearchControl has three different keyboard layouts:
  //
  // case 1: Siri remote.
  // +-------------------------------+
  // | Searchbar (shows search text) |
  // +-------------------------------+
  // |           Keyboard            |
  // +-------------------------------+
  // |        Search Results         |
  // |             ...               |
  // +-------------------------------+
  //
  // case 2: Non-Siri remote, with LTR language in device's general
  // setting.
  // +-------------------------------+
  // | Searchbar (shows search text) |
  // +--------------+----------------+
  // |   Keyboard   |     Search     |
  // |              |     Results    |
  // |     ...      |       ...      |
  // +--------------+----------------+
  //
  // case 3: Non-Siri remote, with RTL language in device's general
  // setting.
  // +-------------------------------+
  // | Searchbar (shows search text) |
  // +--------------+----------------+
  // |   Search     |     Keyboard   |
  // |   Results    |                |
  // |     ...      |       ...      |
  // +--------------+----------------+

  UIView* resultsView = _searchResultsViewController.view;
  CGRect resultsFrameInMainScreen =
      [resultsView convertRect:resultsView.frame
             toCoordinateSpace:UIScreen.mainScreen.coordinateSpace];

  // Infer the keyboard frame based on the search result's frame.
  CGSize screenSize = UIScreen.mainScreen.bounds.size;
  CGRect keyboardFrame;
  CGFloat resultsFrameLeftPadding = resultsFrameInMainScreen.origin.x;
  CGFloat resultsFrameRightPadding = screenSize.width -
                                     resultsFrameInMainScreen.origin.x -
                                     resultsFrameInMainScreen.size.width;

  if (resultsFrameInMainScreen.origin.x == 0) {
    // This is the horizontal keyboard layout illustrated in case 1.
    // Specify keyboard frame as including keyboard and search bar.
    keyboardFrame =
        CGRectMake(0, 0, screenSize.width, resultsFrameInMainScreen.origin.y);
  } else {
    if (resultsFrameLeftPadding > resultsFrameRightPadding) {
      // This is the LTR grid keyboard layout illustrated in case 2.
      // Specify keyboard frame as only the keyboard area. This allows the
      // caller to infer the space taken up by the search bar.
      keyboardFrame =
          CGRectMake(0, resultsFrameInMainScreen.origin.y,
                     resultsFrameInMainScreen.origin.x,
                     screenSize.height - resultsFrameInMainScreen.origin.y);
    } else {
      // This is the RTL grid keyboard layout illustrated in case 3.
      // Specify keyboard frame as only the keyboard area. This allows the
      // caller to infer the space taken up by the search bar.
      keyboardFrame =
          CGRectMake(resultsFrameInMainScreen.origin.x +
                         resultsFrameInMainScreen.size.width,
                     resultsFrameInMainScreen.origin.y,
                     screenSize.width - resultsFrameInMainScreen.size.width -
                         resultsFrameInMainScreen.origin.x,
                     screenSize.height - resultsFrameInMainScreen.origin.y);
    }
  }

  // Convert from points to pixels.
  CGFloat scale = UIScreen.mainScreen.scale;
  return CGRectMake(static_cast<int>(keyboardFrame.origin.x * scale),
                    static_cast<int>(keyboardFrame.origin.y * scale),
                    static_cast<int>(keyboardFrame.size.width * scale),
                    static_cast<int>(keyboardFrame.size.height * scale));
}

- (BOOL)isShowingKeyboard {
  return _keyboardVisibilityState != SearchKeyboardVisibilityState::kHidden;
}

#pragma mark - CobaltSearchControllerPressesDelegate

- (void)searchControllerMenuPressesBegan:(NSSet<UIPress*>*)presses
                               withEvent:(UIPressesEvent*)event {
  if (auto* rwhv = _shell->web_contents()->GetRenderWidgetHostView()) {
    UIView* native_rwhv = rwhv->GetNativeView().Get();
    [native_rwhv pressesBegan:presses withEvent:event];
  }
}

- (void)searchControllerMenuPressesEnded:(NSSet<UIPress*>*)presses
                               withEvent:(UIPressesEvent*)event {
  if (auto* rwhv = _shell->web_contents()->GetRenderWidgetHostView()) {
    UIView* native_rwhv = rwhv->GetNativeView().Get();
    [native_rwhv pressesEnded:presses withEvent:event];
  }
}

#pragma mark - CobaltSearchResultsControllerFocusDelegate

- (void)resultsDidLoseFocus {
  LOG(ERROR) << __func__;
  [_platformOnScreenKeyboardDelegate keyboardFocused];
}

- (void)resultsDidReceiveFocus {
  LOG(ERROR) << __func__;
  [_platformOnScreenKeyboardDelegate keyboardBlurred];
}

#pragma mark - UISearchResultsUpdating

- (void)updateSearchResultsForSearchController:
    (UISearchController*)searchController {
  if (_keyboardVisibilityState == SearchKeyboardVisibilityState::kHidden) {
    return;
  }

  if (searchController.searchBar.text.length == 0) {
    return;
  }

  // From C25 (b/185122939):
  // Debouncing searchResults to avoid sending too many intermediate input
  // events to Kabuki and a bug where voice search queries are cleared.
  static constexpr NSTimeInterval kSearchResultDebounceTime = 0.5;
  static NSDate* searchResultLastDate;
  static NSString* searchResultLastString;
  searchResultLastDate = [NSDate date];
  __weak id<PlatformOnScreenKeyboardDelegate> weakDelegate =
      _platformOnScreenKeyboardDelegate;
  dispatch_after(
      dispatch_time(DISPATCH_TIME_NOW,
                    (int64_t)(kSearchResultDebounceTime * NSEC_PER_SEC)),
      dispatch_get_main_queue(), ^{
        if (-searchResultLastDate.timeIntervalSinceNow <=
            kSearchResultDebounceTime) {
          return;
        }
        NSString* searchString = searchController.searchBar.text;
        if (searchResultLastString &&
            [searchResultLastString isEqualToString:searchString]) {
          return;
        }
        searchResultLastString = searchString;
        LOG(ERROR) << __func__;
        [weakDelegate keyboardTextChanged:[searchString copy]];
      });
}

#pragma mark - CobaltSearchContainerViewControllerDelegate

- (void)searchDidAppear {
  [_contentView bringSubviewToFront:_searchView];
  [self setNeedsFocusUpdate];
  [self updateFocusIfNeeded];

  UIView* web_contents_view = _shell->web_contents()->GetNativeView().Get();
  [_searchResultsViewController.view addSubview:web_contents_view];
  // Use constraints instead of an autoresizing mask for more control:
  // `web_contents_view`'s trailingAnchor needs to follow
  // `_searchController.view`'s, not `_searchResultsViewController.view`s.
  //
  // This makes a difference when using the system keyboard in grid mode (either
  // because a non-Siri remote is being used or because this was specifically
  // chosen in system settings), as in this case we follow C25's behavior of
  // extending the web contents beyond the area allocated to the search results
  // and making it reach the end of the page's width, in both RTL and LTR modes.
  web_contents_view.translatesAutoresizingMaskIntoConstraints = NO;
  [NSLayoutConstraint activateConstraints:@[
    [web_contents_view.topAnchor
        constraintEqualToAnchor:_searchResultsViewController.view.topAnchor],
    [web_contents_view.leadingAnchor
        constraintEqualToAnchor:_searchResultsViewController.view
                                    .leadingAnchor],
    [web_contents_view.trailingAnchor
        constraintEqualToAnchor:_searchController.view.trailingAnchor],
    [web_contents_view.bottomAnchor
        constraintEqualToAnchor:_searchResultsViewController.view.bottomAnchor],
  ]];

  if (auto* rwhv = _shell->web_contents()->GetRenderWidgetHostView()) {
    rwhv->SetAllowAutomaticViewBoundsUpdates(false);
  }

  _keyboardVisibilityState = SearchKeyboardVisibilityState::kVisible;
  if (_showOnScreenKeyboardCompletionHandler) {
    _showOnScreenKeyboardCompletionHandler();
    _showOnScreenKeyboardCompletionHandler = nil;
  }
  if (_focusOnScreenKeyboardCompletionHandler) {
    _focusOnScreenKeyboardCompletionHandler();
    _focusOnScreenKeyboardCompletionHandler = nil;
  }
}

- (void)searchDidDisappear {
  [_contentView sendSubviewToBack:_searchView];

  [self setNeedsFocusUpdate];
  [self updateFocusIfNeeded];

  UIView* web_contents_view = _shell->web_contents()->GetNativeView().Get();
  // Although this should be similar to the setup in -viewDidLoad, using the
  // original autoresize mask does not seem to have the desired effect (maybe
  // because `_contentView` itself is not resized in this case).
  web_contents_view.translatesAutoresizingMaskIntoConstraints = NO;
  [_contentView addSubview:web_contents_view];
  [NSLayoutConstraint activateConstraints:@[
    [web_contents_view.topAnchor
        constraintEqualToAnchor:_contentView.topAnchor],
    [web_contents_view.leadingAnchor
        constraintEqualToAnchor:_contentView.leadingAnchor],
    [web_contents_view.trailingAnchor
        constraintEqualToAnchor:_contentView.trailingAnchor],
    [web_contents_view.bottomAnchor
        constraintEqualToAnchor:_contentView.bottomAnchor],
  ]];

  if (auto* rwhv = _shell->web_contents()->GetRenderWidgetHostView()) {
    rwhv->SetAllowAutomaticViewBoundsUpdates(true);
  }

  _searchResultsViewController = nil;
  _keyboardVisibilityState = SearchKeyboardVisibilityState::kHidden;
}

@end

@implementation TracingHandler

- (void)startWithHandler:(void (^)())startHandler
             stopHandler:(void (^)())stopHandler
              categories:(const char*)categories {
  int i = 0;
  NSString* filename;
  NSFileManager* fileManager = [NSFileManager defaultManager];
  NSString* path = NSSearchPathForDirectoriesInDomains(
      NSDocumentDirectory, NSUserDomainMask, YES)[0];

  do {
    filename =
        [path stringByAppendingPathComponent:
                  [NSString stringWithFormat:@"trace_%d.pftrace.gz", i++]];
  } while ([fileManager fileExistsAtPath:filename]);

  if (![fileManager createFileAtPath:filename contents:nil attributes:nil]) {
    NSLog(@"Failed to create tracefile: %@", filename);
    return;
  }

  _traceFileHandle = [NSFileHandle fileHandleForWritingAtPath:filename];
  if (_traceFileHandle == nil) {
    NSLog(@"Failed to open tracefile: %@", filename);
    return;
  }

  NSLog(@"Will trace to file: %@", filename);

  perfetto::TraceConfig perfetto_config = tracing::GetDefaultPerfettoConfig(
      base::trace_event::TraceConfig(categories, ""),
      /*privacy_filtering_enabled=*/false,
      /*convert_to_legacy_json=*/true);

  perfetto_config.set_write_into_file(true);
  _tracingSession =
      perfetto::Tracing::NewTrace(perfetto::BackendType::kCustomBackend);

  _tracingSession->Setup(perfetto_config, [_traceFileHandle fileDescriptor]);

  __weak TracingHandler* weakSelf = self;
  auto runner = base::SequencedTaskRunner::GetCurrentDefault();

  _tracingSession->SetOnStartCallback([runner, startHandler]() {
    runner->PostTask(FROM_HERE, base::BindOnce(^{
                       startHandler();
                     }));
  });

  _tracingSession->SetOnStopCallback([runner, weakSelf, stopHandler]() {
    runner->PostTask(FROM_HERE, base::BindOnce(^{
                       [weakSelf onStopped];
                       stopHandler();
                     }));
  });

  _tracingSession->Start();
}

- (void)stop {
  _tracingSession->Stop();
}

- (void)onStopped {
  [_traceFileHandle closeFile];
  _traceFileHandle = nil;
  _tracingSession.reset();
}

- (id)init {
  _traceFileHandle = nil;
  return self;
}

- (BOOL)isTracing {
  return !!_tracingSession.get();
}

@end

namespace content {

namespace {

// Computes the default background color for the on-screen keyboard by first
// trying to read it from the bundle and then falling back to a hardcoded
// default.
//
// Note: this logic was copied from C25. In practice, Kabuki always ends up
// overriding the background color to a specific value.
UIColor* defaultKeyboardBackgroundColor() {
  static UIColor* background_color;
  if (!background_color) {
    NSString* colorFromPList = [[NSBundle mainBundle]
        objectForInfoDictionaryKey:@"YTOSKBackgroundColor"];
    if (!colorFromPList) {
      colorFromPList = [[NSBundle mainBundle]
          objectForInfoDictionaryKey:@"YTApplicationBackgroundColor"];
    }
    NSArray<NSString*>* colorComponents =
        [colorFromPList componentsSeparatedByString:@", "];
    if (colorComponents.count == 4) {
      background_color =
          [UIColor colorWithRed:[colorComponents[0] floatValue] / 255.0
                          green:[colorComponents[1] floatValue] / 255.0
                           blue:[colorComponents[2] floatValue] / 255.0
                          alpha:[colorComponents[3] floatValue]];
    } else {
      background_color = [UIColor colorWithRed:30 / 255.0
                                         green:30 / 255.0
                                          blue:30 / 255.0
                                         alpha:1];
    }
  }
  return background_color;
}

class PlatformOnScreenKeyboardTvos final
    : public on_screen_keyboard::PlatformOnScreenKeyboard {
 public:
  explicit PlatformOnScreenKeyboardTvos(
      ContentShellWindowDelegate* window_delegate)
      : window_delegate_(window_delegate) {
    on_screen_keyboard_observer_ = [[OnScreenKeyboardObserver alloc]
          initWithCallbacks:base::BindRepeating(
                                &PlatformOnScreenKeyboardTvos::KeyboardBlurred,
                                GetWeakPtr())
            focusedCallback:base::BindRepeating(
                                &PlatformOnScreenKeyboardTvos::KeyboardFocused,
                                GetWeakPtr())
        textChangedCallback:base::BindRepeating(&PlatformOnScreenKeyboardTvos::
                                                    KeyboardTextChanged,
                                                GetWeakPtr())];
    window_delegate_.platformOnScreenKeyboardDelegate =
        on_screen_keyboard_observer_;
  }

  ~PlatformOnScreenKeyboardTvos() override {
    if (window_delegate_.platformOnScreenKeyboardDelegate ==
        on_screen_keyboard_observer_) {
      window_delegate_.platformOnScreenKeyboardDelegate = nil;
    }
  }

  // on_screen_keyboard::PlatformOnScreenKeyboardTvos overrides.
  void Show(const std::string& text,
            on_screen_keyboard::mojom::KeyboardOptionsPtr options,
            base::OnceClosure done_callback) override {
    UIUserInterfaceStyle theme_override = UIUserInterfaceStyleUnspecified;
    if (options->theme_override.has_value()) {
      theme_override =
          *options->theme_override ==
                  on_screen_keyboard::mojom::ThemeOverride::kDarkTheme
              ? UIUserInterfaceStyleDark
              : UIUserInterfaceStyleLight;
    }
    UIColor* color_override = defaultKeyboardBackgroundColor();
    if (options->background_color) {
      color_override =
          [UIColor colorWithRed:options->background_color->red / 255.0
                          green:options->background_color->green / 255.0
                           blue:options->background_color->blue / 255.0
                          alpha:1];
    }

    [window_delegate_
        showOnScreenKeyboard:base::SysUTF8ToNSString(text)
               keyboardStyle:theme_override
               colorOverride:color_override
           completionHandler:base::CallbackToBlock(std::move(done_callback))];
  }

  void Hide(base::OnceClosure done_callback) override {
    [window_delegate_ hideOnScreenKeyboard];
    std::move(done_callback).Run();
  }

  void Focus(base::OnceClosure done_callback) override {
    [window_delegate_
        focusOnScreenKeyboard:base::CallbackToBlock(std::move(done_callback))];
  }

  void Blur(base::OnceClosure done_callback) override {
    // TODO: b/513162149 - This method was present in C25 and added here for
    // backward compatibility. The current implementation is empty because not
    // only is it not called from Kabuki but it is not entirely clear what
    // should happen here given how the tvOS search keyboard is presented on the
    // screen: if this is called from Kabuki, the web view already has focus
    // anyway.
    std::move(done_callback).Run();
  }

  void UpdateSuggestions(const std::vector<std::string>& suggestions,
                         base::OnceClosure done_callback) override {
    // Not supported on tvOS.
    std::move(done_callback).Run();
  }

  void SetKeepFocusOnKeyboard(bool keep_focus) override {
    // Do nothing on tvOS. Focus handling works as expected without having to
    // force it to be on the keyboard (and having to deal with
    // -preferredFocusEnvironments when focus() and blur() are called).
  }

  void SupportsSuggestions(
      base::OnceCallback<void(bool)> done_callback) override {
    std::move(done_callback).Run(false);
  }

  void IsBeingShown(base::OnceCallback<void(bool)> done_callback) override {
    std::move(done_callback).Run(window_delegate_.isShowingKeyboard);
  }

  void BoundingRect(
      base::OnceCallback<void(const gfx::RectF&)> done_callback) override {
    std::move(done_callback).Run(gfx::RectF(window_delegate_.boundingRect));
  }

 private:
  OnScreenKeyboardObserver* __strong on_screen_keyboard_observer_;

  ContentShellWindowDelegate* __weak window_delegate_;
};

}  // namespace

struct ShellPlatformDelegate::ShellData {
  UIWindow* window;
  bool fullscreen = false;
  std::unique_ptr<on_screen_keyboard::PlatformOnScreenKeyboard>
      on_screen_keyboard;
};

struct ShellPlatformDelegate::PlatformData {};

ShellPlatformDelegate::ShellPlatformDelegate() = default;
ShellPlatformDelegate::~ShellPlatformDelegate() = default;

void ShellPlatformDelegate::Initialize(const gfx::Size& default_window_size,
                                       bool is_visible) {
  is_visible_ = is_visible;
  screen_ = std::make_unique<display::ScopedNativeScreen>();
}

void ShellPlatformDelegate::RevealShell(Shell* shell) {}

void ShellPlatformDelegate::ConcealShell(Shell* shell) {}

void ShellPlatformDelegate::CreatePlatformWindowInternal(
    Shell* shell,
    const gfx::Size& initial_size) {}

void ShellPlatformDelegate::CreatePlatformWindow(
    Shell* shell,
    const gfx::Size& initial_size) {
  DCHECK(!base::Contains(shell_data_map_, shell));
  ShellData& shell_data = shell_data_map_[shell];

  UIWindow* window =
      [[UIWindow alloc] initWithFrame:[[UIScreen mainScreen] bounds]];
  // Use black as background color to match the splash screen's background.
  window.backgroundColor = [UIColor blackColor];
  window.tintColor = [UIColor darkGrayColor];

  ContentShellWindowDelegate* controller =
      [[ContentShellWindowDelegate alloc] initWithShell:shell];
  // Gives a restoration identifier so that state restoration works.
  controller.restorationIdentifier = @"rootViewController";
  window.rootViewController = controller;
  [controller loadViewIfNeeded];

  shell_data.window = window;
}

gfx::NativeWindow ShellPlatformDelegate::GetNativeWindow(Shell* shell) {
  DCHECK(base::Contains(shell_data_map_, shell));
  ShellData& shell_data = shell_data_map_[shell];

  return gfx::NativeWindow(shell_data.window);
}

void ShellPlatformDelegate::CleanUp(Shell* shell) {
  DCHECK(base::Contains(shell_data_map_, shell));
  shell_data_map_.erase(shell);
}

void ShellPlatformDelegate::SetContents(Shell* shell) {
  DCHECK(base::Contains(shell_data_map_, shell));
  //  ShellData& shell_data = shell_data_map_[shell];

  //  UIView* web_contents_view = shell->web_contents()->GetNativeView();
  //  [((ContentShellWindowDelegate *)shell_data.window.rootViewController)
  //  setContents:web_contents_view];
}

void ShellPlatformDelegate::LoadSplashScreenContents(Shell* shell) {
  DCHECK(base::Contains(shell_data_map_, shell));
  ShellData& shell_data = shell_data_map_[shell];

  // Add the splash screen UIView on top of the main web contents view. It will
  // be automatically removed from the view hierarchy once it finishes loading
  // and window.close() is called.
  UIView* splash_screen_web_contents_view =
      shell->splash_screen_web_contents()->GetNativeView().Get();
  [static_cast<ContentShellWindowDelegate*>(
       shell_data.window.rootViewController)
          .contentView addSubview:splash_screen_web_contents_view];
}

void ShellPlatformDelegate::UpdateContents(Shell* shell) {}

void ShellPlatformDelegate::ResizeWebContent(Shell* shell,
                                             const gfx::Size& content_size) {
  DCHECK(base::Contains(shell_data_map_, shell));
}

void ShellPlatformDelegate::EnableUIControl(Shell* shell,
                                            UIControl control,
                                            bool is_enabled) {
  if (content::Shell::ShouldHideToolbar()) {
    return;
  }

  DCHECK(base::Contains(shell_data_map_, shell));
  ShellData& shell_data = shell_data_map_[shell];
  UIButton* button = nil;
  switch (control) {
    case BACK_BUTTON:
      button = [((ContentShellWindowDelegate*)
                     shell_data.window.rootViewController) backButton];
      break;
    case FORWARD_BUTTON:
      button = [((ContentShellWindowDelegate*)
                     shell_data.window.rootViewController) forwardButton];
      break;
    case STOP_BUTTON: {
      NSString* imageName = is_enabled ? @"ic_stop" : @"ic_reload";
      [[((ContentShellWindowDelegate*)shell_data.window.rootViewController)
          reloadOrStopButton] setImage:[UIImage imageNamed:imageName]
                              forState:UIControlStateNormal];
      break;
    }
    default:
      NOTREACHED() << "Unknown UI control";
  }
  [button setEnabled:is_enabled];
}

void ShellPlatformDelegate::SetAddressBarURL(Shell* shell, const GURL& url) {
  if (Shell::ShouldHideToolbar()) {
    return;
  }
  DCHECK(base::Contains(shell_data_map_, shell));
  ShellData& shell_data = shell_data_map_[shell];

  NSString* url_string = base::SysUTF8ToNSString(url.spec());
  [((ContentShellWindowDelegate*)shell_data.window.rootViewController)
      setURL:url_string];
}

void ShellPlatformDelegate::SetIsLoading(Shell* shell, bool loading) {}

void ShellPlatformDelegate::SetTitle(Shell* shell,
                                     const std::u16string& title) {
  DCHECK(base::Contains(shell_data_map_, shell));
}

void ShellPlatformDelegate::MainFrameCreated(Shell* shell) {
  DCHECK(base::Contains(shell_data_map_, shell));
  // Change the default background color to match that of the UIWindow itself,
  // otherwise there can be a white flash while the page is being loaded.
  if (auto* view = shell->web_contents()->GetRenderWidgetHostView()) {
    view->SetBackgroundColor(SK_ColorBLACK);
  }
}

bool ShellPlatformDelegate::DestroyShell(Shell* shell) {
  DCHECK(base::Contains(shell_data_map_, shell));
  ShellData& shell_data = shell_data_map_[shell];

  [shell_data.window resignKeyWindow];
  return false;  // We have not destroyed the shell here.
}

void ShellPlatformDelegate::ToggleFullscreenModeForTab(
    Shell* shell,
    WebContents* web_contents,
    bool enter_fullscreen) {
  DCHECK(base::Contains(shell_data_map_, shell));
  ShellData& shell_data = shell_data_map_[shell];

  if (shell_data.fullscreen == enter_fullscreen) {
    return;
  }
  shell_data.fullscreen = enter_fullscreen;
  [((ContentShellWindowDelegate*)shell_data.window.rootViewController)
      toolbarContentView]
      .hidden = enter_fullscreen;
}

bool ShellPlatformDelegate::IsFullscreenForTabOrPending(
    Shell* shell,
    const WebContents* web_contents) const {
  DCHECK(base::Contains(shell_data_map_, shell));
  auto iter = shell_data_map_.find(shell);
  return iter->second.fullscreen;
}

base::WeakPtr<on_screen_keyboard::PlatformOnScreenKeyboard>
ShellPlatformDelegate::GetOrCreatePlatformOnScreenKeyboard(Shell* shell) {
  DCHECK(base::Contains(shell_data_map_, shell));
  ShellData& shell_data = shell_data_map_[shell];
  if (!shell_data.on_screen_keyboard) {
    shell_data.on_screen_keyboard =
        std::make_unique<PlatformOnScreenKeyboardTvos>(
            static_cast<ContentShellWindowDelegate*>(
                shell_data.window.rootViewController));
  }
  return shell_data.on_screen_keyboard->GetWeakPtr();
}

}  // namespace content
