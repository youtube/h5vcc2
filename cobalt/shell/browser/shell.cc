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

#include "cobalt/shell/browser/shell.h"

#include <stddef.h>

#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/command_line.h"
#include "base/functional/callback_helpers.h"
#include "base/location.h"
#include "base/no_destructor.h"
#include "base/run_loop.h"
#include "base/strings/escape.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/bind_post_task.h"
#include "build/build_config.h"
#include "cobalt/browser/switches.h"
#include "cobalt/browser/picture_in_picture/picture_in_picture_window_manager.h"
#include "cobalt/shell/browser/migrate_storage_record/migration_manager.h"
#include "cobalt/shell/browser/shell_content_browser_client.h"
#include "cobalt/shell/browser/shell_devtools_frontend.h"
#include "cobalt/shell/browser/shell_javascript_dialog_manager.h"
#include "cobalt/shell/common/shell_switches.h"
#include "cobalt/shell/common/url_constants.h"
#include "cobalt/shell/embedded_resources/embedded_js.h"
#include "components/custom_handlers/protocol_handler.h"
#include "components/custom_handlers/protocol_handler_registry.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/devtools_agent_host.h"
#include "content/public/browser/document_picture_in_picture_window_controller.h"
#include "content/public/browser/picture_in_picture_window_controller.h"
#include "content/public/browser/overlay_window.h"
#include "content/public/browser/video_picture_in_picture_window_controller.h"
#include "content/public/browser/media_capture_devices.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/presentation_receiver_flags.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/render_view_host.h"
#include "content/public/browser/render_widget_host.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/renderer_preferences_util.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_delegate.h"
#include "content/public/common/content_switches.h"
#include "media/media_buildflags.h"
#include "net/base/url_util.h"
#include "third_party/blink/public/common/peerconnection/webrtc_ip_handling_policy.h"
#include "third_party/blink/public/common/renderer_preferences/renderer_preferences.h"
#include "third_party/blink/public/mojom/mediastream/media_stream.mojom-shared.h"
#include "third_party/blink/public/mojom/mediastream/media_stream.mojom.h"
#include "third_party/blink/public/mojom/window_features/window_features.mojom.h"

#if BUILDFLAG(USE_EVERGREEN)
#include "cobalt/updater/updater_module.h"  //nogncheck
#endif

#if BUILDFLAG(IS_ANDROID)
#if BUILDFLAG(USE_STARBOARD_MEDIA)
#include "starboard/android/shared/audio_permission_requester.h"
#endif  // BUILDFLAG(USE_STARBOARD_MEDIA)

#include "starboard/android/shared/starboard_bridge.h"

// TODO: (cobalt b/372559388) Update namespace to jni_zero.
using base::android::AttachCurrentThread;

using ::starboard::StarboardBridge;
#endif  // BUILDFLAG(IS_ANDROID)

#if BUILDFLAG(IS_ANDROIDTV)
#include "cobalt/android/oom_intervention/oom_intervention_tab_helper.h"
#endif

namespace content {

namespace {

constexpr char kMusicTopic[] = "music";

// Helper function to check if a URL is a valid deep link for a specific topic.
// It requires both a "launch" parameter and a matching "topic" parameter.
bool IsDeepLinkTopic(const GURL& link_url, std::string_view target_topic) {
  if (!link_url.is_valid()) {
    return false;
  }

  // Helper lambda to check a query string
  auto check_query_string = [&](std::string_view query_str) {
    std::string unescaped_query = base::UnescapeURLComponent(
        query_str,
        base::UnescapeRule::REPLACE_PLUS_WITH_SPACE |
            base::UnescapeRule::URL_SPECIAL_CHARS_EXCEPT_PATH_SEPARATORS);

    // Use GURL::Replacements to safely set the query string without worrying
    // about special characters like '#' truncating the query.
    GURL dummy_url("http://dummy/");
    GURL::Replacements replacements;
    replacements.SetQueryStr(unescaped_query);
    dummy_url = dummy_url.ReplaceComponents(replacements);

    bool has_launch = false;
    bool has_target_topic = false;

    for (net::QueryIterator it(dummy_url); !it.IsAtEnd(); it.Advance()) {
      if (!has_launch && it.GetKey() == "launch") {
        has_launch = true;
      } else if (!has_target_topic && it.GetKey() == "topic" &&
                 it.GetUnescapedValue() == target_topic) {
        has_target_topic = true;
      }

      if (has_launch && has_target_topic) {
        return true;
      }
    }
    return false;
  };

  // 1. Check the standard query part
  if (link_url.has_query() && check_query_string(link_url.query())) {
    return true;
  }

  // 2. Check the fragment part if it looks like a query (starts with '?')
  if (link_url.has_ref()) {
    std::string ref = link_url.ref();
    if (!ref.empty() && ref[0] == '?') {
      // Skip the leading '?'
      if (check_query_string(std::string_view(ref).substr(1))) {
        return true;
      }
    }
  }

  return false;
}

// Null until/unless the default main message loop is running.
base::OnceClosure& GetMainMessageLoopQuitClosure() {
  static base::NoDestructor<base::OnceClosure> closure;
  return *closure;
}

bool RequestRecordAudioPermission() {
#if BUILDFLAG(IS_ANDROID) && BUILDFLAG(USE_STARBOARD_MEDIA)
  JNIEnv* env = AttachCurrentThread();
  return starboard::RequestRecordAudioPermission(env);
#else
  // It is expected that all 3P will have system-level permissions.
  return true;
#endif  // BUILDFLAG(IS_ANDROID) && BUILDFLAG(USE_STARBOARD_MEDIA)
}

const blink::MediaStreamDevice* GetRequestedDeviceOrDefault(
    const blink::MediaStreamDevices& devices,
    const std::vector<std::string>& requested_device_ids) {
  for (const auto& requested_device_id : requested_device_ids) {
    if (requested_device_id.empty()) {
      continue;
    }

    auto it = std::ranges::find(devices, requested_device_id,
                                &blink::MediaStreamDevice::id);
    if (it != devices.end()) {
      return &(*it);
    }
  }

  if (!devices.empty()) {
    return &devices[0];
  }

  return nullptr;
}

constexpr int kDefaultTestWindowWidthDip = 800;
constexpr int kDefaultTestWindowHeightDip = 600;

constexpr int kSplashTimeoutMs = 1500;

// Owning pointer. We can not use unique_ptr as a global. That introduces a
// static constructor/destructor.
// Acquired in Shell::Init(), released in Shell::Shutdown().
ShellPlatformDelegate* g_platform = nullptr;
}  // namespace

std::vector<Shell*> Shell::windows_;
base::OnceCallback<void(Shell*)> Shell::shell_created_callback_;

Shell::Shell(std::unique_ptr<WebContents> web_contents,
             std::unique_ptr<WebContents> splash_screen_web_contents,
             bool should_set_delegate,
             const std::string& topic,
             bool skip_for_testing)
    : WebContentsObserver(web_contents.get()),
      web_contents_(std::move(web_contents)),
      splash_screen_web_contents_(std::move(splash_screen_web_contents)),
      splash_state_(STATE_SPLASH_SCREEN_UNINITIALIZED),
      splash_topic_(topic),
      skip_for_testing_(skip_for_testing),
      is_video_splash_screen_(base::CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kForceVideoSplashScreen)) {
  if (should_set_delegate) {
    web_contents_->SetDelegate(this);
  }

  UpdateFontRendererPreferencesFromSystemSettings(
      web_contents_->GetMutableRendererPrefs());

  windows_.push_back(this);

  // Create browser-side mojo service component
  js_communication_host_ =
      std::make_unique<js_injection::JsCommunicationHost>(web_contents_.get());

  if (!skip_for_testing_) {
    RegisterInjectedJavaScript();
  }

#if BUILDFLAG(IS_ANDROIDTV)
  if (OomInterventionTabHelper::IsEnabled()) {
    OomInterventionTabHelper::CreateForWebContents(web_contents_.get());
  }
#endif

  if (splash_screen_web_contents_) {
    splash_state_ = STATE_SPLASH_SCREEN_INITIALIZED;
    splash_screen_web_contents_observer_ =
        std::make_unique<SplashScreenWebContentsObserver>(
            splash_screen_web_contents_.get(),
            base::BindOnce(&Shell::OnSplashScreenLoadComplete,
                           weak_factory_.GetWeakPtr()));
    splash_screen_web_contents_delegate_ =
        std::make_unique<SplashScreenWebContentsDelegate>(
            base::BindPostTaskToCurrentDefault(
                base::BindOnce(&Shell::ClosingSplashScreenWebContents,
                               weak_factory_.GetWeakPtr())));
    splash_screen_web_contents_->SetDelegate(
        splash_screen_web_contents_delegate_.get());
  }

  if (!GetPlatform()->IsVisible()) {
    // Notify the WebContents that it is starting in a hidden state so that it
    // can optimize resource usage (e.g. by lowering its process priority).
    web_contents_->WasHidden();
  }

  if (shell_created_callback_) {
    std::move(shell_created_callback_).Run(this);
  }
}

Shell::~Shell() {
  CHECK(g_platform);
  g_platform->CleanUp(this);

  for (size_t i = 0; i < windows_.size(); ++i) {
    if (windows_[i] == this) {
      windows_.erase(windows_.begin() + i);
      break;
    }
  }

  web_contents_->SetDelegate(nullptr);
  web_contents_.reset();

  if (windows().empty()) {
    g_platform->DidCloseLastWindow();
  }
}

// static
ShellPlatformDelegate* Shell::GetPlatform() {
  return g_platform;
}

// static
void Shell::OnBlur() {
  CHECK(g_platform);
  if (g_platform->IsVisible()) {
    g_platform->OnBlur();
  }
}

// static
void Shell::OnFocus() {
  CHECK(g_platform);
  if (g_platform->IsVisible()) {
    g_platform->OnFocus();
  }
}

// static
void Shell::OnConceal() {
  CHECK(g_platform);
  if (g_platform->IsVisible()) {
    g_platform->OnConceal();
  }
}

// static
void Shell::OnReveal() {
  CHECK(g_platform);
  if (!g_platform->IsVisible()) {
    g_platform->OnReveal();
  }
}

// static
void Shell::OnFreeze() {
  CHECK(g_platform);
  g_platform->OnFreeze();
}

// static
void Shell::OnUnfreeze() {
  CHECK(g_platform);
  g_platform->OnUnfreeze();
}

// static
void Shell::OnStop() {
  CHECK(g_platform);
  g_platform->OnStop();
}

void Shell::FinishShellInitialization(Shell* shell) {
  WebContents* raw_web_contents = shell->web_contents();
  base::CommandLine* command_line = base::CommandLine::ForCurrentProcess();
  if (command_line->HasSwitch(switches::kForceWebRtcIPHandlingPolicy)) {
    raw_web_contents->GetMutableRendererPrefs()->webrtc_ip_handling_policy =
        blink::ToWebRTCIPHandlingPolicy(command_line->GetSwitchValueASCII(
            switches::kForceWebRtcIPHandlingPolicy));
  }

  GetPlatform()->SetContents(shell);
  GetPlatform()->DidCreateOrAttachWebContents(shell, raw_web_contents);
  // If the RenderFrame was created during WebContents construction (as happens
  // for windows opened from the renderer) then the Shell won't hear about the
  // main frame being created as a WebContentsObservers. This gives the delegate
  // a chance to act on the main frame accordingly.
  if (raw_web_contents->GetPrimaryMainFrame()->IsRenderFrameLive()) {
    GetPlatform()->MainFrameCreated(shell);
  }

#if BUILDFLAG(USE_STARBOARD_MEDIA)
#if BUILDFLAG(IS_ANDROID)
  // TODO(b/390021478): Revisit this when decoupling from content_shell.
  if (!shell->skip_for_testing()) {
    GetPlatform()->SetOverlayMode(shell, true);
  }
  GetPlatform()->SetSkipForTesting(shell->skip_for_testing());
#endif  // BUILDFLAG(IS_ANDROID)
#endif  // BUILDFLAG(USE_STARBOARD_MEDIA)
}

Shell* Shell::CreateShell(
    std::unique_ptr<WebContents> web_contents,
    std::unique_ptr<WebContents> splash_screen_web_contents,
    const gfx::Size& initial_size,
    bool should_set_delegate,
    const std::string& topic) {
  Shell* shell =
      new Shell(std::move(web_contents), std::move(splash_screen_web_contents),
                should_set_delegate, topic);
  GetPlatform()->CreatePlatformWindow(shell, initial_size);
  FinishShellInitialization(shell);
  return shell;
}

// static
void Shell::SetMainMessageLoopQuitClosure(base::OnceClosure quit_closure) {
  GetMainMessageLoopQuitClosure() = std::move(quit_closure);
}

// static
void Shell::QuitMainMessageLoopForTesting() {
  auto& quit_loop = GetMainMessageLoopQuitClosure();
  if (quit_loop) {
    std::move(quit_loop).Run();
  }
}

// static
void Shell::SetShellCreatedCallback(
    base::OnceCallback<void(Shell*)> shell_created_callback) {
  DCHECK(!shell_created_callback_);
  shell_created_callback_ = std::move(shell_created_callback);
}

// static
bool Shell::ShouldHideToolbar() {
  return base::CommandLine::ForCurrentProcess()->HasSwitch(
      switches::kContentShellHideToolbar);
}

// static
Shell* Shell::FromWebContents(WebContents* web_contents) {
  for (Shell* window : windows_) {
    if (window->web_contents() && window->web_contents() == web_contents) {
      return window;
    }
  }
  return nullptr;
}

// static
void Shell::Initialize(std::unique_ptr<ShellPlatformDelegate> platform,
                       bool is_visible) {
  DCHECK(!g_platform);
  g_platform = platform.release();
  g_platform->Initialize(GetShellDefaultSize(), is_visible);
}

// static
void Shell::Shutdown() {
  if (!g_platform) {  // Shutdown has already been called.
    return;
  }

  DevToolsAgentHost::DetachAllClients();

  while (!Shell::windows().empty()) {
    Shell::windows().back()->Close();
  }

  delete g_platform;
  g_platform = nullptr;

  for (auto it = RenderProcessHost::AllHostsIterator(); !it.IsAtEnd();
       it.Advance()) {
    it.GetCurrentValue()->DisableRefCounts();
  }
  auto& quit_loop = GetMainMessageLoopQuitClosure();
  if (quit_loop) {
    std::move(quit_loop).Run();
  }

#if !BUILDFLAG(IS_STARBOARD)
  // Pump the message loop to allow window teardown tasks to run.
  base::RunLoop().RunUntilIdle();
#endif  // !BUILDFLAG(IS_STARBOARD)
}

gfx::Size Shell::AdjustWindowSize(const gfx::Size& initial_size) {
  if (!initial_size.IsEmpty()) {
    return initial_size;
  }
  return GetShellDefaultSize();
}

// static
Shell* Shell::CreateNewWindow(BrowserContext* browser_context,
                              const GURL& url,
                              const scoped_refptr<SiteInstance>& site_instance,
                              const gfx::Size& initial_size,
                              const bool create_splash_screen_web_contents,
                              const std::string& deep_link) {
#if BUILDFLAG(IS_ANDROIDTV)
  if (create_splash_screen_web_contents) {
    starboard::StarboardBridge::GetInstance()->SetStartupMilestone(19);
  } else {
    starboard::StarboardBridge::GetInstance()->SetStartupMilestone(18);
  }
#endif
  WebContents::CreateParams create_params(browser_context, site_instance);
  bool is_visible = GetPlatform()->IsVisible();
  create_params.initially_hidden = !is_visible;
  if (base::CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kForcePresentationReceiverForTesting)) {
    create_params.starting_sandbox_flags = kPresentationReceiverSandboxFlags;
  }
  std::unique_ptr<WebContents> web_contents =
      WebContents::Create(create_params);
  std::unique_ptr<WebContents> splash_screen_web_contents;
  if (create_splash_screen_web_contents && is_visible) {
    // Create splash screen WebContents. ATV creates splash screen WebContents
    // in JNI_ShellManager_LaunchShell(), whereas other platforms create it in
    // ShellBrowserMainParts::InitializeMessageLoopContext().
    WebContents::CreateParams splash_screen_create_params(browser_context,
                                                          nullptr);
    splash_screen_create_params.main_frame_name = kCobaltSplashMainFrameName;
    splash_screen_web_contents =
        WebContents::Create(splash_screen_create_params);
  }
  // Handle deeplink from url on all devices.
  std::string topic;
  GURL link_url;
  if (!deep_link.empty()) {
    link_url = GURL(deep_link);
  } else if (base::CommandLine::ForCurrentProcess()->HasSwitch(
                 cobalt::switches::kInitialURL)) {
    link_url = GURL(base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII(
        cobalt::switches::kInitialURL));
  }
  if (link_url.is_valid()) {
    if (IsDeepLinkTopic(link_url, kMusicTopic)) {
      topic = kMusicTopic;
    }
  }
  Shell* shell = CreateShell(
      std::move(web_contents), std::move(splash_screen_web_contents),
      AdjustWindowSize(initial_size), true /* should_set_delegate */, topic);

  if (!url.is_empty()) {
    shell->LoadURL(url);
  }
  return shell;
}

void Shell::RenderFrameCreated(RenderFrameHost* frame_host) {
  if (frame_host == web_contents_->GetPrimaryMainFrame()) {
#if BUILDFLAG(IS_ANDROIDTV)
    starboard::StarboardBridge::GetInstance()->SetStartupMilestone(20);
#endif
    g_platform->MainFrameCreated(this);
  }
}

void Shell::PrimaryMainDocumentElementAvailable() {
#if BUILDFLAG(IS_ANDROIDTV)
  starboard::StarboardBridge::GetInstance()->SetStartupMilestone(27);
#endif
#if BUILDFLAG(USE_EVERGREEN)
  cobalt::updater::UpdaterModule* updater_module =
      cobalt::updater::UpdaterModule::GetInstance();
  if (updater_module) {
    LOG(INFO) << "Mark the current installation as successful after the "
                 "PrimaryMainDocumentElement is available.";
    updater_module->MarkSuccessful();
  }
#endif
}

void Shell::DidFinishLoad(RenderFrameHost* render_frame_host,
                          const GURL& validated_url) {
#if BUILDFLAG(IS_ANDROIDTV)
  starboard::StarboardBridge::GetInstance()->SetStartupMilestone(31);
#endif
}

void Shell::DidStartNavigation(NavigationHandle* navigation_handle) {
#if BUILDFLAG(IS_ANDROIDTV)
  if (navigation_handle->IsInPrimaryMainFrame()) {
    if (navigation_handle->GetURL() ==
        "https://www.youtube.com/tv") {  // Splash
      starboard::StarboardBridge::GetInstance()->SetStartupMilestone(22);
    } else {
      starboard::StarboardBridge::GetInstance()->SetStartupMilestone(29);
    }
  }
#endif
}

void Shell::DidFinishNavigation(NavigationHandle* navigation_handle) {
#if BUILDFLAG(IS_ANDROIDTV)
  if (navigation_handle->IsInPrimaryMainFrame()) {
    if (navigation_handle->GetURL() ==
        "https://www.youtube.com/tv") {  // Splash
      starboard::StarboardBridge::GetInstance()->SetStartupMilestone(26);
    } else {
      starboard::StarboardBridge::GetInstance()->SetStartupMilestone(30);
    }
  }
#endif
  LOG(INFO) << "Navigated to " << navigation_handle->GetURL();
}

void Shell::DidStartLoading() {
#if BUILDFLAG(IS_ANDROIDTV)
  starboard::StarboardBridge::GetInstance()->SetStartupMilestone(21);
#endif
}

void Shell::DidStopLoading() {
  if (!is_main_frame_loaded_ &&
      splash_state_ != STATE_SPLASH_SCREEN_UNINITIALIZED) {
    VLOG(1) << "NativeSplash: Main frame WebContents DidStopLoading.";
    is_main_frame_loaded_ = true;

    if (splash_state_ < STATE_SPLASH_SCREEN_STARTED) {
      return;
    }

    if (splash_state_ >= STATE_SPLASH_SCREEN_ENDED) {
      SwitchToMainWebContents();
    } else {
      ScheduleSwitchToMainWebContents();
    }
  }
}

void Shell::RegisterInjectedJavaScript() {
  // Get the embedded header resource
  GeneratedResourceMap resource_map;
  CobaltJavaScriptPolyfill::GenerateMap(resource_map);

  for (const auto& [file_name, file_contents] : resource_map) {
    LOG(INFO) << "JS injection for filename: " << file_name;
    std::string js(reinterpret_cast<const char*>(file_contents.data),
                   file_contents.size);

    // Inject a script at document start for all origins
    const std::u16string script(base::UTF8ToUTF16(js));
    const std::vector<std::string> allowed_origins({"*"});
    auto result = js_communication_host_->AddDocumentStartJavaScript(
        script, allowed_origins);

    if (result.error_message.has_value()) {
      // error_message contains a value
      LOG(WARNING) << "Failed to register JS injection for:" << file_name
                   << ", error message: " << result.error_message.value();
    }
  }
}

void Shell::LoadSplashScreenWebContents() {
  if (splash_screen_web_contents_) {
    // Display splash screen.
    VLOG(1) << "NativeSplash: Loading splash screen WebContents.";
    splash_state_ = STATE_SPLASH_SCREEN_STARTED;
    GetPlatform()->LoadSplashScreenContents(this);

    GURL splash_screen_url = GURL(switches::kSplashScreenURL);
    if (!is_video_splash_screen_) {
      splash_screen_url =
          net::AppendQueryParameter(splash_screen_url, "force_image", "true");
    }
    if (!splash_topic_.empty()) {
      splash_screen_url =
          net::AppendQueryParameter(splash_screen_url, "cache", splash_topic_);
    }
    NavigationController::LoadURLParams params(splash_screen_url);
    params.frame_name = std::string();
    params.transition_type = ui::PageTransitionFromInt(
        ui::PAGE_TRANSITION_TYPED | ui::PAGE_TRANSITION_FROM_ADDRESS_BAR);
    splash_screen_web_contents_->GetController().LoadURLWithParams(params);

    if (is_main_frame_loaded_) {
      // Main frame loaded before splash screen started.
      VLOG(1) << "NativeSplash: Main frame loaded before splash start.";
      ScheduleSwitchToMainWebContents();
    }
  }
}

void Shell::LoadURL(const GURL& url) {
  LoadURLForFrame(
      url, std::string(),
      ui::PageTransitionFromInt(ui::PAGE_TRANSITION_TYPED |
                                ui::PAGE_TRANSITION_FROM_ADDRESS_BAR));
#if !BUILDFLAG(IS_ANDROID)
  // Load splash screen on linux/3p/tvOS platforms. On ATV, it is called by
  // JNI_Shell_LoadSplashScreenWebContents().
  LoadSplashScreenWebContents();
#endif
}

void Shell::LoadURLForFrame(const GURL& url,
                            const std::string& frame_name,
                            ui::PageTransition transition_type) {
  NavigationController::LoadURLParams params(url);
  params.frame_name = frame_name;
  params.transition_type = transition_type;
  web_contents_->GetController().LoadURLWithParams(params);
}

void Shell::LoadDataWithBaseURL(const GURL& url,
                                const std::string& data,
                                const GURL& base_url) {
  bool load_as_string = false;
  LoadDataWithBaseURLInternal(url, data, base_url, load_as_string);
}

#if BUILDFLAG(IS_ANDROID)
void Shell::LoadDataAsStringWithBaseURL(const GURL& url,
                                        const std::string& data,
                                        const GURL& base_url) {
  bool load_as_string = true;
  LoadDataWithBaseURLInternal(url, data, base_url, load_as_string);
}
#endif

void Shell::LoadDataWithBaseURLInternal(const GURL& url,
                                        const std::string& data,
                                        const GURL& base_url,
                                        bool load_as_string) {
#if !BUILDFLAG(IS_ANDROID)
  DCHECK(!load_as_string);  // Only supported on Android.
#endif

  NavigationController::LoadURLParams params{GURL()};
  const std::string data_url_header = "data:text/html;charset=utf-8,";
  if (load_as_string) {
    params.url = GURL(data_url_header);
    std::string data_url_as_string = data_url_header + data;
#if BUILDFLAG(IS_ANDROID)
    params.data_url_as_string = base::MakeRefCounted<base::RefCountedString>(
        std::move(data_url_as_string));
#endif
  } else {
    params.url = GURL(data_url_header + data);
  }

  params.load_type = NavigationController::LOAD_TYPE_DATA;
  params.base_url_for_data_url = base_url;
  params.virtual_url_for_special_cases = url;
  params.override_user_agent = NavigationController::UA_OVERRIDE_FALSE;
  web_contents_->GetController().LoadURLWithParams(params);
}

WebContents* Shell::AddNewContents(
    WebContents* source,
    std::unique_ptr<WebContents> new_contents,
    const GURL& target_url,
    WindowOpenDisposition disposition,
    const blink::mojom::WindowFeatures& window_features,
    bool user_gesture,
    bool* was_blocked) {
#if !BUILDFLAG(IS_ANDROID)
  // If the shell is opening a document picture-in-picture window, it needs to
  // inform the DocumentPictureInPictureWindowController.
  if (disposition == WindowOpenDisposition::NEW_PICTURE_IN_PICTURE) {
    DocumentPictureInPictureWindowController* controller =
        PictureInPictureWindowController::
            GetOrCreateDocumentPictureInPictureController(source);
    controller->SetChildWebContents(new_contents.get());
    controller->Show();
  }
#endif  // !BUILDFLAG(IS_ANDROID)

  WebContents* result = new_contents.get();
  CreateShell(
      std::move(new_contents), nullptr /* splash_screen_web_contents */,
      AdjustWindowSize(window_features.bounds.size()),
      !delay_popup_contents_delegate_for_testing_ /* should_set_delegate */);
  return result;
}

void Shell::GoBackOrForward(int offset) {
  web_contents_->GetController().GoToOffset(offset);
}

void Shell::Reload() {
  web_contents_->GetController().Reload(ReloadType::NORMAL, false);
}

void Shell::ReloadBypassingCache() {
  web_contents_->GetController().Reload(ReloadType::BYPASSING_CACHE, false);
}

void Shell::Stop() {
  web_contents_->Stop();
}

void Shell::UpdateNavigationControls(bool should_show_loading_ui) {
  int current_index = web_contents_->GetController().GetCurrentEntryIndex();
  int max_index = web_contents_->GetController().GetEntryCount() - 1;

  g_platform->EnableUIControl(this, ShellPlatformDelegate::BACK_BUTTON,
                              current_index > 0);
  g_platform->EnableUIControl(this, ShellPlatformDelegate::FORWARD_BUTTON,
                              current_index < max_index);
  g_platform->EnableUIControl(
      this, ShellPlatformDelegate::STOP_BUTTON,
      should_show_loading_ui && web_contents_->IsLoading());
}

void Shell::ShowDevTools() {
  if (!devtools_frontend_) {
    auto* devtools_frontend = ShellDevToolsFrontend::Show(web_contents());
    devtools_frontend_ = devtools_frontend->GetWeakPtr();
  }

  devtools_frontend_->Activate();
}

void Shell::CloseDevTools() {
  if (!devtools_frontend_) {
    return;
  }
  devtools_frontend_->Close();
  devtools_frontend_ = nullptr;
}

void Shell::ResizeWebContentForTests(const gfx::Size& content_size) {
  g_platform->ResizeWebContent(this, content_size);
}

gfx::NativeView Shell::GetContentView() {
  if (!web_contents_) {
    return gfx::NativeView();
  }
  return web_contents_->GetNativeView();
}

#if !BUILDFLAG(IS_ANDROID)
gfx::NativeWindow Shell::window() {
  return g_platform->GetNativeWindow(this);
}
#endif

WebContents* Shell::OpenURLFromTab(
    WebContents* source,
    const OpenURLParams& params,
    base::OnceCallback<void(content::NavigationHandle&)>
        navigation_handle_callback) {
  WebContents* target = nullptr;
  switch (params.disposition) {
    case WindowOpenDisposition::CURRENT_TAB:
      target = source;
      break;

    // Normally, the difference between NEW_POPUP and NEW_WINDOW is that a popup
    // should have no toolbar, no status bar, no menu bar, no scrollbars and be
    // not resizable.  For simplicity and to enable new testing scenarios in
    // content shell and web tests, popups don't get special treatment below
    // (i.e. they will have a toolbar and other things described here).
    case WindowOpenDisposition::NEW_POPUP:
    case WindowOpenDisposition::NEW_WINDOW:
    // content_shell doesn't really support tabs, but some web tests use
    // middle click (which translates into kNavigationPolicyNewBackgroundTab),
    // so we treat the cases below just like a NEW_WINDOW disposition.
    case WindowOpenDisposition::NEW_BACKGROUND_TAB:
    case WindowOpenDisposition::NEW_FOREGROUND_TAB: {
      Shell* new_window =
          Shell::CreateNewWindow(source->GetBrowserContext(),
                                 GURL(),  // Don't load anything just yet.
                                 params.source_site_instance,
                                 gfx::Size());  // Use default size.
      target = new_window->web_contents();
      break;
    }

    // No tabs in content_shell:
    case WindowOpenDisposition::SINGLETON_TAB:
    // No incognito mode in content_shell:
    case WindowOpenDisposition::OFF_THE_RECORD:
    // TODO(lukasza): Investigate if some web tests might need support for
    // SAVE_TO_DISK disposition.  This would probably require that
    // WebTestControlHost always sets up and cleans up a temporary directory
    // as the default downloads destinations for the duration of a test.
    case WindowOpenDisposition::SAVE_TO_DISK:
    // Ignoring requests with disposition == IGNORE_ACTION...
    case WindowOpenDisposition::IGNORE_ACTION:
    default:
      return nullptr;
  }

  base::WeakPtr<NavigationHandle> navigation_handle =
      target->GetController().LoadURLWithParams(
          NavigationController::LoadURLParams(params));

  if (navigation_handle_callback && navigation_handle) {
    std::move(navigation_handle_callback).Run(*navigation_handle);
  }

  return target;
}

void Shell::LoadingStateChanged(WebContents* source,
                                bool should_show_loading_ui) {
  UpdateNavigationControls(should_show_loading_ui);
  g_platform->SetIsLoading(this, source->IsLoading());
}

void Shell::EnterFullscreenModeForTab(
    RenderFrameHost* requesting_frame,
    const blink::mojom::FullscreenOptions& options) {
  ToggleFullscreenModeForTab(WebContents::FromRenderFrameHost(requesting_frame),
                             true);
}

void Shell::ExitFullscreenModeForTab(WebContents* web_contents) {
  ToggleFullscreenModeForTab(web_contents, false);
}

void Shell::ToggleFullscreenModeForTab(WebContents* web_contents,
                                       bool enter_fullscreen) {
#if BUILDFLAG(IS_ANDROID) || BUILDFLAG(IS_IOS)
  g_platform->ToggleFullscreenModeForTab(this, web_contents, enter_fullscreen);
#endif
  if (is_fullscreen_ != enter_fullscreen) {
    is_fullscreen_ = enter_fullscreen;
    web_contents->GetPrimaryMainFrame()
        ->GetRenderViewHost()
        ->GetWidget()
        ->SynchronizeVisualProperties();
  }
}

bool Shell::IsFullscreenForTabOrPending(const WebContents* web_contents) {
#if BUILDFLAG(IS_ANDROID) || BUILDFLAG(IS_IOS)
  return g_platform->IsFullscreenForTabOrPending(this, web_contents);
#else
  return is_fullscreen_;
#endif
}

blink::mojom::DisplayMode Shell::GetDisplayMode(
    const WebContents* web_contents) {
  // TODO: should return blink::mojom::DisplayModeFullscreen wherever user puts
  // a browser window into fullscreen (not only in case of renderer-initiated
  // fullscreen mode): crbug.com/476874.
  return IsFullscreenForTabOrPending(web_contents)
             ? blink::mojom::DisplayMode::kFullscreen
             : blink::mojom::DisplayMode::kBrowser;
}

void Shell::RequestPointerLock(WebContents* web_contents,
                               bool user_gesture,
                               bool last_unlocked_by_target) {
  // Give the platform a chance to handle the lock request, if it doesn't
  // indicate it handled it, allow the request.
  if (!g_platform->HandlePointerLockRequest(this, web_contents, user_gesture,
                                            last_unlocked_by_target)) {
    web_contents->GotResponseToPointerLockRequest(
        blink::mojom::PointerLockResult::kSuccess);
  }
}

void Shell::Close() {
  // Shell is "self-owned" and destroys itself. The ShellPlatformDelegate
  // has the chance to co-opt this and do its own destruction.
  CHECK(g_platform);
  if (!g_platform->DestroyShell(this)) {
    delete this;
  }
}

void Shell::CloseContents(WebContents* source) {
#if BUILDFLAG(IS_ANDROID)
  JNIEnv* env = AttachCurrentThread();
  StarboardBridge* starboard_bridge = StarboardBridge::GetInstance();
  starboard_bridge->CloseApp(env);
#endif
  Close();
}

bool Shell::CanOverscrollContent() {
#if defined(USE_AURA)
  return true;
#else
  return false;
#endif
}

void Shell::NavigationStateChanged(WebContents* source,
                                   InvalidateTypes changed_flags) {
  if (changed_flags & INVALIDATE_TYPE_URL) {
    g_platform->SetAddressBarURL(this, source->GetVisibleURL());
  }
}

JavaScriptDialogManager* Shell::GetJavaScriptDialogManager(
    WebContents* source) {
  if (!dialog_manager_) {
    dialog_manager_ = g_platform->CreateJavaScriptDialogManager(this);
  }
  if (!dialog_manager_) {
    dialog_manager_ = std::make_unique<ShellJavaScriptDialogManager>();
  }
  return dialog_manager_.get();
}

bool Shell::DidAddMessageToConsole(WebContents* source,
                                   blink::mojom::ConsoleMessageLevel log_level,
                                   const std::u16string& message,
                                   int32_t line_no,
                                   const std::u16string& source_id) {
  return false;
}

void Shell::RendererUnresponsive(
    WebContents* source,
    RenderWidgetHost* render_widget_host,
    base::RepeatingClosure hang_monitor_restarter) {
  LOG(WARNING) << "renderer unresponsive";
}

void Shell::ActivateContents(WebContents* contents) {
  if (!g_platform->IsWaitingForRevealAck()) {
    contents->GetPrimaryMainFrame()->GetRenderWidgetHost()->Focus();
  }
}

bool Shell::IsBackForwardCacheSupported(WebContents& web_contents) {
  return true;
}

PreloadingEligibility Shell::IsPrerender2Supported(
    WebContents& web_contents,
    PreloadingTriggerType trigger_type) {
  return PreloadingEligibility::kEligible;
}

namespace {
class PendingCallback : public base::RefCounted<PendingCallback> {
 public:
  explicit PendingCallback(base::OnceCallback<void()> cb)
      : callback_(std::move(cb)) {}

 private:
  friend class base::RefCounted<PendingCallback>;
  ~PendingCallback() { std::move(callback_).Run(); }
  base::OnceCallback<void()> callback_;
};
}  // namespace

bool Shell::ShouldAllowRunningInsecureContent(WebContents* web_contents,
                                              bool allowed_per_prefs,
                                              const url::Origin& origin,
                                              const GURL& resource_url) {
  if (allowed_per_prefs) {
    return true;
  }

  return g_platform->ShouldAllowRunningInsecureContent(this);
}

PictureInPictureResult Shell::EnterPictureInPicture(WebContents* web_contents) {

  return PictureInPictureWindowManager::GetInstance()->EnterVideoPictureInPicture(web_contents);
}


void Shell::ExitPictureInPicture() {
  PictureInPictureWindowManager::GetInstance()->ExitPictureInPicture();
}

bool Shell::ShouldResumeRequestsForCreatedWindow() {
  return !delay_popup_contents_delegate_for_testing_;
}

void Shell::SetContentsBounds(WebContents* source, const gfx::Rect& bounds) {
  DCHECK(source == web_contents());  // There's only one WebContents per Shell.
}

void Shell::RequestMediaAccessPermission(WebContents* web_contents,
                                         const MediaStreamRequest& request,
                                         MediaResponseCallback callback) {
  if (request.audio_type !=
      blink::mojom::MediaStreamType::DEVICE_AUDIO_CAPTURE) {
    std::move(callback).Run(
        blink::mojom::StreamDevicesSet(),
        blink::mojom::MediaStreamRequestResult::PERMISSION_DENIED,
        /*ui=*/nullptr);
    return;
  }
  bool can_record_audio = RequestRecordAudioPermission();
  if (!can_record_audio) {
    std::move(callback).Run(
        blink::mojom::StreamDevicesSet(),
        blink::mojom::MediaStreamRequestResult::PERMISSION_DENIED,
        /*ui=*/nullptr);
    return;
  }
  auto audio_devices =
      MediaCaptureDevices::GetInstance()->GetAudioCaptureDevices();
  const blink::MediaStreamDevice* device = GetRequestedDeviceOrDefault(
      audio_devices, request.requested_audio_device_ids);
  if (!device) {
    std::move(callback).Run(blink::mojom::StreamDevicesSet(),
                            blink::mojom::MediaStreamRequestResult::NO_HARDWARE,
                            /*ui=*/nullptr);
    return;
  }
  blink::mojom::StreamDevicesSet stream_devices_set;
  stream_devices_set.stream_devices.emplace_back(
      blink::mojom::StreamDevices::New());
  stream_devices_set.stream_devices[0]->audio_device = *device;
  std::move(callback).Run(stream_devices_set,
                          blink::mojom::MediaStreamRequestResult::OK,
                          std::unique_ptr<MediaStreamUI>());
}

bool Shell::CheckMediaAccessPermission(RenderFrameHost*,
                                       const url::Origin&,
                                       blink::mojom::MediaStreamType) {
  return true;
}

bool Shell::ShouldFocusPageAfterCrash(WebContents* source) {
  return !g_platform->IsWaitingForRevealAck();
}

gfx::Size Shell::GetShellDefaultSize() {
  static gfx::Size default_shell_size;  // Only go through this method once.

  if (!default_shell_size.IsEmpty()) {
    return default_shell_size;
  }

  base::CommandLine* command_line = base::CommandLine::ForCurrentProcess();
  if (command_line->HasSwitch(switches::kContentShellHostWindowSize)) {
    const std::string size_str = command_line->GetSwitchValueASCII(
        switches::kContentShellHostWindowSize);
    int width, height;
    if (sscanf(size_str.c_str(), "%dx%d", &width, &height) == 2) {
      default_shell_size = gfx::Size(width, height);
    } else {
      LOG(ERROR) << "Invalid size \"" << size_str << "\" given to --"
                 << switches::kContentShellHostWindowSize;
    }
  }

  if (default_shell_size.IsEmpty()) {
    default_shell_size =
        gfx::Size(kDefaultTestWindowWidthDip, kDefaultTestWindowHeightDip);
  }

  return default_shell_size;
}

void Shell::Focus() {
  // Aura silently ignores focus requests for hidden windows. If the shell is
  // not yet visible (e.g. during a rapid Reveal -> Focus sequence), we defer
  // the focus until the WebContents signals it has become visible.
  if (web_contents_->GetVisibility() == Visibility::VISIBLE) {
    web_contents_->Focus();
    pending_focus_ = false;
  } else {
    pending_focus_ = true;
  }
}

void Shell::OnVisibilityChanged(Visibility visibility) {
  if (visibility == Visibility::VISIBLE && pending_focus_) {
    // Retry the pending focus now that the window is visible in Aura.
    Focus();
  }
}

void Shell::LoadProgressChanged(double progress) {
#if BUILDFLAG(IS_ANDROID)
  if (!skip_for_testing_) {
    g_platform->LoadProgressChanged(this, progress);
  }
#endif
  if (progress >= 1.0 && !is_main_frame_loaded_ &&
      splash_state_ != STATE_SPLASH_SCREEN_UNINITIALIZED) {
    is_main_frame_loaded_ = true;

    // If splash screen hasn't started yet, we don't need to do anything here.
    // The switch logic will be handled in LoadSplashScreenWebContents().
    if (splash_state_ < STATE_SPLASH_SCREEN_STARTED) {
      return;
    }

    if (splash_state_ >= STATE_SPLASH_SCREEN_ENDED) {
      VLOG(1) << "NativeSplash: Main frame WebContents is loaded.";
      SwitchToMainWebContents();
    } else {
      ScheduleSwitchToMainWebContents();
    }
  }
}

void Shell::ScheduleSwitchToMainWebContents() {
  if (splash_screen_start_time_.is_null()) {
    LOG(INFO) << "NativeSplash: Splash screen not loaded yet, waiting.";
    return;
  }
  base::TimeDelta splash_screen_elapsed =
      base::TimeTicks::Now() - splash_screen_start_time_;

  int splash_timeout_ms = kSplashTimeoutMs;
  base::CommandLine* command_line = base::CommandLine::ForCurrentProcess();
  if (command_line->HasSwitch(switches::kSplashScreenShutdownDelayMs)) {
    std::string switch_value = command_line->GetSwitchValueASCII(
        switches::kSplashScreenShutdownDelayMs);
    base::StringToInt(switch_value, &splash_timeout_ms);
  }

  base::TimeDelta min_splash_screen_duration =
      base::Milliseconds(splash_timeout_ms);
  base::TimeDelta remaining_delay =
      min_splash_screen_duration - splash_screen_elapsed;

  if (remaining_delay.is_negative()) {
    // No more delay needed
    remaining_delay = base::TimeDelta();
  }

  VLOG(1) << "NativeSplash: Main frame WebContents is loaded, splash "
             "screen elapsed: "
          << splash_screen_elapsed.InMilliseconds()
          << "ms, remaining delay: " << remaining_delay.InMilliseconds()
          << "ms.";
  if (web_contents_) {
    if (is_video_splash_screen_) {
      // Send hidden event to WebApp if it's video-based splash screen.
      web_contents_->WasHidden();
    }
    content::GetUIThreadTaskRunner({})->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(&Shell::SwitchToMainWebContents,
                       weak_factory_.GetWeakPtr()),
        remaining_delay);
  }
}

void Shell::TitleWasSet(NavigationEntry* entry) {
  if (entry) {
    g_platform->SetTitle(this, entry->GetTitle());
  }
}

void Shell::SwitchToMainWebContents() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  // It is safe to use |has_switched_to_main_frame_|
  // instead of a lock due to it is on a single thread.
  // This could be called multiple times.
  if (!has_switched_to_main_frame_) {
    VLOG(1) << "NativeSplash: Switching to main frame WebContents.";
    has_switched_to_main_frame_ = true;
    if (web_contents_) {
      CHECK(GetPlatform());
      GetPlatform()->UpdateContents(this);
      if (GetPlatform()->IsVisible() || is_video_splash_screen_) {
        web_contents_->WasShown();
      }
      if (web_contents()->GetRenderWidgetHostView()) {
        web_contents()->GetRenderWidgetHostView()->Focus();
      }
    }
    if (splash_screen_web_contents_) {
      splash_screen_web_contents_.reset();
      splash_screen_web_contents_observer_.reset();
      splash_screen_web_contents_delegate_.reset();
    }
  }
}

void Shell::OnSplashScreenLoadComplete() {
  if (splash_state_ >= STATE_SPLASH_SCREEN_STARTED &&
      splash_screen_start_time_.is_null()) {
    splash_screen_start_time_ = base::TimeTicks::Now();
    if (is_main_frame_loaded_) {
      ScheduleSwitchToMainWebContents();
    }
  }
}

void Shell::ClosingSplashScreenWebContents() {
  VLOG(1) << "NativeSplash: Closing splash screen WebContents.";
  splash_state_ = STATE_SPLASH_SCREEN_ENDED;
  if (is_main_frame_loaded_) {
    // If main frame WebContents is loaded, switch to it.
    SwitchToMainWebContents();
  }
}

}  // namespace content
