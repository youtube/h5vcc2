// Copyright 2024 The Cobalt Authors. All Rights Reserved.
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

#ifndef COBALT_BROWSER_COBALT_CONTENT_BROWSER_CLIENT_H_
#include "third_party/abseil-cpp/absl/types/optional.h"
#define COBALT_BROWSER_COBALT_CONTENT_BROWSER_CLIENT_H_

#include <string_view>

#include "cobalt/browser/client_hint_headers/cobalt_trusted_url_loader_header_client.h"
#include "cobalt/common/cobalt_thread_checker.h"
#include "cobalt/media/service/mojom/platform_window_provider.mojom.h"
#include "cobalt/shell/browser/shell_content_browser_client.h"
#include "content/public/browser/devtools_manager_delegate.h"
#include "content/public/browser/generated_code_cache_settings.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "starboard/window.h"

#if BUILDFLAG(IS_STARBOARD)
#include "ui/ozone/platform/starboard/platform_window_starboard.h"
#endif  // BUILDFLAG(IS_STARBOARD)

class PrefService;

namespace content {
class BrowserMainParts;
class RenderFrameHost;
class RenderProcessHost;
class WebContents;
}  // namespace content

namespace metrics_services_manager {
class MetricsServicesManager;
}  // namespace metrics_services_manager

namespace mojo {
template <typename>
class BinderMapWithContext;
}  // namespace mojo

namespace cobalt {

class GlobalFeatures;
class CobaltMetricsServicesManagerClient;
class CobaltWebContentsObserver;

void ParseAndApplyH5vccSettingsForTesting(std::string_view settings_value,
                                          GlobalFeatures* global_features);

// This class allows Cobalt to inject specific logic in the business of the
// browser (i.e. of Content), for example for startup or to override the UA.
// TODO(b/390021478): In time CobaltContentBrowserClient should derive and
// implement ContentBrowserClient, since ShellContentBrowserClient is more like
// a demo around Content.
class CobaltContentBrowserClient : public content::ShellContentBrowserClient {
 public:
  explicit CobaltContentBrowserClient(absl::optional<int64_t> startup_timestamp,
                                      const std::string& deep_link,
                                      bool is_visible = true);

  CobaltContentBrowserClient(const CobaltContentBrowserClient&) = delete;
  CobaltContentBrowserClient& operator=(const CobaltContentBrowserClient&) =
      delete;

  ~CobaltContentBrowserClient() override;

  static CobaltContentBrowserClient* Get();

  // ShellContentBrowserClient overrides.
  std::unique_ptr<content::VideoOverlayWindow> CreateWindowForVideoPictureInPicture(
    content::VideoPictureInPictureWindowController* controller) override;
  std::unique_ptr<content::BrowserMainParts> CreateBrowserMainParts(
      bool is_integration_test) override;
  std::unique_ptr<content::DevToolsManagerDelegate>
  CreateDevToolsManagerDelegate() override;
  void CreateThrottlesForNavigation(
      content::NavigationThrottleRegistry& registry) override;
  content::GeneratedCodeCacheSettings GetGeneratedCodeCacheSettings(
      content::BrowserContext* context) override;
  std::string GetApplicationLocale() override;
  std::string GetUserAgent() override;
  blink::UserAgentMetadata GetUserAgentMetadata() override;
  content::StoragePartitionConfig GetStoragePartitionConfigForSite(
      content::BrowserContext* browser_context,
      const GURL& site) override;
  void ConfigureNetworkContextParams(
      content::BrowserContext* context,
      bool in_memory,
      const base::FilePath& relative_partition_path,
      network::mojom::NetworkContextParams* network_context_params,
      cert_verifier::mojom::CertVerifierCreationParams*
          cert_verifier_creation_params) override;
  void OverrideWebPreferences(content::WebContents* web_contents,
                              content::SiteInstance& main_frame_site,
                              blink::web_pref::WebPreferences* prefs) override;
  void OnWebContentsCreated(content::WebContents* web_contents) override;
  void RegisterBrowserInterfaceBindersForFrame(
      content::RenderFrameHost* render_frame_host,
      mojo::BinderMapWithContext<content::RenderFrameHost*>* binder_map)
      override;
  void ExposeInterfacesToRenderer(
      service_manager::BinderRegistry* registry,
      blink::AssociatedInterfaceRegistry* associated_registry,
      content::RenderProcessHost* render_process_host) override;

  // Initializes all necessary parameters to create the feature list and calls
  // base::FeatureList::SetInstance() to set the global instance.
  void CreateFeatureListAndFieldTrials() override;

  // Read from the experiment config, override features, and associate feature
  // params for Cobalt experiments.
  void SetUpCobaltFeaturesAndParams(base::FeatureList* feature_list);

  void WillCreateURLLoaderFactory(
      content::BrowserContext* browser_context,
      content::RenderFrameHost* frame,
      int render_process_id,
      URLLoaderFactoryType type,
      const url::Origin& request_initiator,
      const net::IsolationInfo& isolation_info,
      std::optional<int64_t> navigation_id,
      ukm::SourceIdObj ukm_source_id,
      network::URLLoaderFactoryBuilder& factory_builder,
      mojo::PendingRemote<network::mojom::TrustedURLLoaderHeaderClient>*
          header_client,
      bool* bypass_redirect_checks,
      bool* disable_secure_dns,
      network::mojom::URLLoaderFactoryOverridePtr* factory_override,
      scoped_refptr<base::SequencedTaskRunner> navigation_response_task_runner)
      override;

  void FlushCookiesAndLocalStorage(base::OnceClosure = base::DoNothing());
  void DispatchBlur();
  void DispatchFocus();

  void AddPendingWindowReceiver(
      mojo::PendingReceiver<cobalt::media::mojom::PlatformWindowProvider>
          receiver);
  uint64_t GetSbWindowHandle() const { return cached_sb_window_; }

#if !BUILDFLAG(IS_ANDROIDTV)
  void SetUserAgentCrashAnnotation();
#endif  // !BUILDFLAG(IS_ANDROIDTV)

 private:
  void DispatchEvent(const std::string&, base::OnceClosure);
  void OnSbWindowCreated(SbWindow window);
  void OnSbWindowDestroyed(SbWindow window);
  const absl::optional<int64_t> startup_timestamp_;
  const std::string deep_link_;
  bool is_visible_;

  std::unique_ptr<CobaltWebContentsObserver> web_contents_observer_;

  uint64_t cached_sb_window_ = 0;
  std::vector<
      mojo::PendingReceiver<cobalt::media::mojom::PlatformWindowProvider>>
      pending_window_receivers_;

  COBALT_THREAD_CHECKER(thread_checker_);

  base::WeakPtrFactory<CobaltContentBrowserClient> weak_factory_{this};
};

}  // namespace cobalt

#endif  // COBALT_BROWSER_COBALT_CONTENT_BROWSER_CLIENT_H_
