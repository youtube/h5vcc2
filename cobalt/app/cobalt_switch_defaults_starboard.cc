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

#include <vector>

#include "base/base_switches.h"
#include "build/buildflag.h"
#include "cc/base/switches.h"
#include "cobalt/app/cobalt_switch_defaults.h"
#include "cobalt/browser/switches.h"
#include "cobalt/shell/common/shell_switches.h"
#include "components/network_session_configurator/common/network_switches.h"
#include "content/public/common/content_switches.h"
#include "gpu/config/gpu_switches.h"
#include "media/base/media_switches.h"
#include "sandbox/policy/switches.h"
#include "third_party/blink/public/common/switches.h"
#include "ui/base/ui_base_switches.h"
#include "ui/gl/gl_switches.h"

#if BUILDFLAG(IS_OZONE)
#endif

namespace cobalt {

const std::vector<const char*>&
CommandLinePreprocessor::GetCobaltToggleSwitches() {
  // ==========
  // IMPORTANT:
  //
  // These command line switches defaults do not affect non-POSIX platforms.
  // They only affect platforms such as Linux and AOSP. If you are making
  // changes to these values, please check that other platforms (such as
  // AndroidTV) are getting corresponding updates.

  // List of toggleable default switches.
  static const std::vector<const char*> kCobaltToggleSwitches{
      // Enable Blink to work in overlay video mode
      ::switches::kForceVideoOverlays,
      // Disable multiprocess mode.
      ::switches::kSingleProcess,
      // Hide content shell toolbar.
      ::switches::kContentShellHideToolbar,
      // Accelerated GL is blanket disabled for Linux. Ignore the GPU
      // blocklist to enable it.
      ::switches::kIgnoreGpuBlocklist,
      // Disable Zygote (a process fork utility); in turn needs sandbox
      // disabled.
      ::switches::kNoZygote,
      sandbox::policy::switches::kNoSandbox,
      // Rasterize Tiles directly to GPU memory
      // (ZeroCopyRasterBufferProvider).
      blink::switches::kEnableZeroCopy,
      // Enable low-end device mode. This comes with a load of memory and CPU
      // saving goodies but can degrade the experience considerably. One of
      // the known regressions is 4444 textures, which are then disabled
      // explicitly.
      ::switches::kEnableLowEndDeviceMode,
      blink::switches::kDisableRGBA4444Textures,
      // For Starboard the signal handlers are already setup. Disable the
      // Chromium registrations to avoid overriding the Starboard ones.
      ::switches::kDisableInProcessStackTraces,
      // Cobalt doesn't use Chrome's accelerated video decoding/encoding.
      ::switches::kDisableAcceleratedVideoDecode,
      ::switches::kDisableAcceleratedVideoEncode,
      // Force to use dark mode.
      ::switches::kForceDarkMode,
      // Hide scrollbars to avoid memory allocation.
      ::switches::kHideScrollbars,
  };
  return kCobaltToggleSwitches;
}

const base::CommandLine::SwitchMap&
CommandLinePreprocessor::GetCobaltParamSwitchDefaults() {
  static const base::CommandLine::SwitchMap kCobaltSwitchDefaults{
      // Disable Vulkan.
      {::switches::kDisableFeatures, "Vulkan,MemoryCacheStrongReference"},
      {::switches::kEnableFeatures,
       "LimitImageDecodeCacheSize:mb/24, "
       // When DefaultEnableANGLEValidation is disabled (e.g gold/qa), EGL
       // attribute EGL_CONTEXT_OPENGL_NO_ERROR_KHR is set during egl context
       // creation, but egl extension required to support the attribute is
       // missing and causes errors. So Enable it by default. (More context in
       // b/444042898)
       "DefaultEnableANGLEValidation, "
       "SmallerInterestArea, "
       "ReclaimPrepaintTilesWhenIdle, "
       "ReclaimOldPrepaintTiles"},
  // Force some ozone settings.
#if BUILDFLAG(IS_OZONE)
      {::switches::kUseGL, "angle"},
      {::switches::kUseANGLE, "gles-egl"},
#endif
      // Use passthrough command decoder.
      {::switches::kUseCmdDecoder, "passthrough"},
      // Set the default size for the content shell/starboard window.
      {::switches::kContentShellHostWindowSize, "1920x1080"},
      // Enable remote Devtools access.
      {::switches::kRemoteDebuggingPort, "9222"},
      {::switches::kRemoteAllowOrigins, "http://localhost:9222"},
      // kEnableLowEndDeviceMode sets MSAA to 4 (and not 8, the default). But
      // we set it explicitly just in case.
      {blink::switches::kGpuRasterizationMSAASampleCount, "4"},
      // Enable precise memory info so we can make accurate client-side
      // measurements.
      {::switches::kEnableBlinkFeatures, "PreciseMemoryInfo"},
      // Enable autoplay video/audio, as Cobalt may launch directly into media
      // playback before user interaction.
      {::switches::kAutoplayPolicy, "no-user-gesture-required"},
      {blink::switches::kJavaScriptFlags,
       // Disable decommitting pooled pages to prevent virtual memory
       // fragmentation.
       "--no-decommit-pooled-pages "
       // Enable memory saving mode with little v8 performance tradeoff.
       "--optimize-for-size "
       // Set initial old space size to 64MB and max old space size to 512MB.
       "--initial-old-space-size=64 "
       "--max-old-space-size=512 "
       // Disable v8 optimizing compilers (turbofan, maglev, sparkplug).
       "--disable-optimizing-compilers "
       "--no-sparkplug "
       // Disable v8 concurrent marking by default.
       "--no-concurrent-marking"},
      // Disable CC image cache items limit.
      {::switches::kCCImageCacheLimitItems, "0"},
  };
  return kCobaltSwitchDefaults;
}

}  // namespace cobalt
