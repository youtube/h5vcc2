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

#include "cobalt/browser/cobalt_browser_interface_binders.h"

#include "base/functional/bind.h"
#include "cobalt/browser/cobalt_content_browser_client.h"
#include "cobalt/browser/crash_annotator/public/mojom/crash_annotator.mojom.h"
#include "cobalt/browser/h5vcc_accessibility/h5vcc_accessibility_impl.h"
#include "cobalt/browser/h5vcc_accessibility/public/mojom/h5vcc_accessibility.mojom.h"
#include "cobalt/browser/h5vcc_experiments/h5vcc_experiments_impl.h"
#include "cobalt/browser/h5vcc_experiments/public/mojom/h5vcc_experiments.mojom.h"
#include "cobalt/browser/h5vcc_metrics/h5vcc_metrics_impl.h"
#include "cobalt/browser/h5vcc_metrics/public/mojom/h5vcc_metrics.mojom.h"
#include "cobalt/browser/h5vcc_runtime/h5vcc_runtime_impl.h"
#include "cobalt/browser/h5vcc_runtime/public/mojom/h5vcc_runtime.mojom.h"
#include "cobalt/browser/h5vcc_settings/h5vcc_settings_impl.h"
#include "cobalt/browser/h5vcc_settings/public/mojom/h5vcc_settings.mojom.h"
#include "cobalt/browser/h5vcc_storage/h5vcc_storage_impl.h"
#include "cobalt/browser/h5vcc_storage/public/mojom/h5vcc_storage.mojom.h"
#include "cobalt/browser/h5vcc_system/h5vcc_system_impl_base.h"
#include "cobalt/browser/h5vcc_system/public/mojom/h5vcc_system.mojom.h"
#include "cobalt/browser/h5vcc_updater/public/mojom/h5vcc_updater.mojom.h"
#include "cobalt/browser/performance/performance_impl.h"
#include "cobalt/browser/performance/public/mojom/performance.mojom.h"
#include "cobalt/build/configs/buildflags.h"
#include "cobalt/media/service/mojom/platform_window_provider.mojom.h"
#include "cobalt/media/service/platform_window_provider_service.h"

#if BUILDFLAG(USE_EVERGREEN)
#include "cobalt/browser/h5vcc_updater/h5vcc_updater_impl.h"

#if !BUILDFLAG(COBALT_IS_RELEASE_BUILD)
#include "cobalt/browser/h5vcc_updater/h5vcc_updater_sideloading_impl.h"
#endif  // !BUILDFLAG(COBALT_IS_RELEASE_BUILD)
#include "cobalt/browser/h5vcc_updater/public/mojom/h5vcc_updater.mojom.h"
#endif  // BUILDFLAG(USE_EVERGREEN)

#if BUILDFLAG(IS_ANDROIDTV)
#include "content/public/browser/render_frame_host.h"
#include "services/service_manager/public/cpp/interface_provider.h"
#else
#include "cobalt/browser/crash_annotator/crash_annotator_impl.h"
#endif  // BUILDFLAG(IS_ANDROIDTV)

#if BUILDFLAG(ENABLE_NATIVE_ON_SCREEN_KEYBOARD)
#include "cobalt/browser/on_screen_keyboard/on_screen_keyboard_impl.h"
#include "cobalt/browser/on_screen_keyboard/public/mojom/on_screen_keyboard.mojom.h"
#include "cobalt/shell/browser/shell.h"
#include "content/public/browser/web_contents.h"
#include "mojo/public/cpp/bindings/message.h"
#endif  // BUILDFLAG(ENABLE_NATIVE_ON_SCREEN_KEYBOARD)

#include "cobalt/browser/h5vcc_platform_service/h5vcc_platform_service_manager_impl.h"
#include "cobalt/browser/h5vcc_platform_service/public/mojom/h5vcc_platform_service.mojom.h"

namespace cobalt {

namespace {

void BindPlatformWindowProvider(
    content::RenderFrameHost* rfh,
    mojo::PendingReceiver<media::mojom::PlatformWindowProvider> receiver) {
#if BUILDFLAG(IS_STARBOARD)
  if (auto* client = CobaltContentBrowserClient::Get()) {
    client->AddPendingWindowReceiver(std::move(receiver));
  }
#endif
}

}  // namespace

#if BUILDFLAG(IS_ANDROIDTV)
template <typename Interface>
void ForwardToJavaFrame(content::RenderFrameHost* render_frame_host,
                        mojo::PendingReceiver<Interface> receiver) {
  render_frame_host->GetJavaInterfaces()->GetInterface(std::move(receiver));
}
#endif  // BUILDFLAG(IS_ANDROIDTV)

void PopulateCobaltFrameBinders(
    absl::optional<int64_t> app_startup_timestamp,
    content::RenderFrameHost* render_frame_host,
    mojo::BinderMapWithContext<content::RenderFrameHost*>* binder_map) {
// We want to use the Java Mojo implementation for 1P ATV only.
#if BUILDFLAG(IS_ANDROIDTV)
  binder_map->Add<crash_annotator::mojom::CrashAnnotator>(base::BindRepeating(
      &ForwardToJavaFrame<crash_annotator::mojom::CrashAnnotator>));
#else
  binder_map->Add<crash_annotator::mojom::CrashAnnotator>(
      base::BindRepeating(&crash_annotator::CrashAnnotatorImpl::Create));
#endif  // BUILDFLAG(IS_ANDROIDTV)
  binder_map->Add<h5vcc_accessibility::mojom::H5vccAccessibilityBrowser>(
      base::BindRepeating(
          &h5vcc_accessibility::H5vccAccessibilityImpl::Create));
  binder_map->Add<h5vcc_experiments::mojom::H5vccExperiments>(
      base::BindRepeating(&h5vcc_experiments::H5vccExperimentsImpl::Create));
  binder_map->Add<h5vcc_metrics::mojom::H5vccMetrics>(
      base::BindRepeating(&h5vcc_metrics::H5vccMetricsImpl::Create));
  binder_map->Add<h5vcc_system::mojom::H5vccSystem>(
      base::BindRepeating(&h5vcc_system::H5vccSystemImpl::Create));
  binder_map->Add<h5vcc_runtime::mojom::H5vccRuntime>(
      base::BindRepeating(&h5vcc_runtime::H5vccRuntimeImpl::Create));
  binder_map->Add<h5vcc_settings::mojom::H5vccSettings>(
      base::BindRepeating(&h5vcc_settings::H5vccSettingsImpl::Create));
  binder_map->Add<performance::mojom::CobaltPerformance>(base::BindRepeating(
      &performance::PerformanceImpl::Create, app_startup_timestamp));
#if BUILDFLAG(USE_EVERGREEN)
  binder_map->Add<h5vcc_updater::mojom::H5vccUpdater>(
      base::BindRepeating(&h5vcc_updater::H5vccUpdaterImpl::Create));
#else
  // Always register a binder for H5vccUpdater to prevent the browser
  // from killing the Mojo connection if the renderer probes for this interface.
  // If this is not registered, it disconnects the overall
  // BrowserInterfaceBroker for the frame.
  binder_map->Add<h5vcc_updater::mojom::H5vccUpdater>(base::BindRepeating(
      [](content::RenderFrameHost*,
         mojo::PendingReceiver<h5vcc_updater::mojom::H5vccUpdater>) {
        VLOG(1) << "Ignoring H5vccUpdater request for non-Evergreen build.";
      }));
#endif

  // Always register a binder for H5vccUpdaterSideloading to prevent the browser
  // from killing the Mojo connection if the renderer probes for this interface.
  // If this is not registered, it disconnects the overall
  // BrowserInterfaceBroker for the frame.
#if BUILDFLAG(USE_EVERGREEN) && !BUILDFLAG(COBALT_IS_RELEASE_BUILD)
  binder_map->Add<h5vcc_updater::mojom::H5vccUpdaterSideloading>(
      base::BindRepeating(&h5vcc_updater::H5vccUpdaterSideloadingImpl::Create));
#else
  binder_map->Add<
      h5vcc_updater::mojom::H5vccUpdaterSideloading>(base::BindRepeating(
      [](content::RenderFrameHost*,
         mojo::PendingReceiver<h5vcc_updater::mojom::H5vccUpdaterSideloading>) {
        VLOG(1) << "Ignoring H5vccUpdaterSideloading request.";
      }));
#endif

#if BUILDFLAG(ENABLE_NATIVE_ON_SCREEN_KEYBOARD)
  binder_map->Add<on_screen_keyboard::mojom::OnScreenKeyboard>(
      base::BindRepeating(
          [](content::RenderFrameHost* render_frame_host,
             mojo::PendingReceiver<on_screen_keyboard::mojom::OnScreenKeyboard>
                 receiver) {
            auto* web_contents =
                content::WebContents::FromRenderFrameHost(render_frame_host);
            auto* shell = content::Shell::FromWebContents(web_contents);
            if (!shell) {
              mojo::ReportBadMessage(
                  "Could not find Shell instance for OnScreenKeyboard");
              return;
            }
            on_screen_keyboard::OnScreenKeyboardImpl::GetOrCreate(
                web_contents, shell->GetPlatformOnScreenKeyboard())
                ->Bind(std::move(receiver));
          }));
#endif  // BUILDFLAG(ENABLE_NATIVE_ON_SCREEN_KEYBOARD)

  binder_map->Add<h5vcc_storage::mojom::H5vccStorage>(
      base::BindRepeating(&h5vcc_storage::H5vccStorageImpl::Create));
  binder_map->Add<media::mojom::PlatformWindowProvider>(
      base::BindRepeating(&BindPlatformWindowProvider));
  binder_map->Add<h5vcc_platform_service::mojom::H5vccPlatformServiceManager>(
      base::BindRepeating(&h5vcc_platform_service::
                              H5vccPlatformServiceManagerImpl::GetOrCreate));
}

}  // namespace cobalt
