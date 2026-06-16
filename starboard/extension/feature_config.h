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
//
// ========================================================================
//
// Feature configuration file for Starboard features and feature parameters.
// This file will help create the structure that is defined in
// other files.
//
// This file will help setup the necessary
// structure to define the features and parameters for the corresponding feature
// file. That file must define all 6 macros, then include this file, and then
// undefine all 6 macros. Each file will have their own definition of each
// macro for their own specific use.
//
// Doing it this way guarantees that features defined in the Chrobalt side and
// starboard side share the same names, param types and initial default values.

// NOLINT(build/header_guard)

#include "build/build_config.h"
#include "starboard/configuration.h"

#ifndef STARBOARD_FEATURE
#error "STARBOARD_FEATURE has to be defined before including this file"
#endif

#ifndef STARBOARD_FEATURE_PARAM
#error "STARBOARD_FEATURE_PARAM has to be defined before including this file"
#endif

#ifndef FEATURE_LIST_START
#error "FEATURE_LIST_START has to be defined before including this file"
#endif

#ifndef FEATURE_LIST_END
#error "FEATURE_LIST_END has to be defined before including this file"
#endif

#ifndef FEATURE_PARAM_LIST_START
#error "FEATURE_PARAM_LIST_START has to be defined before including this file"
#endif

#ifndef FEATURE_PARAM_LIST_END
#error "FEATURE_PARAM_LIST_END has to be defined before including this file"
#endif

#ifndef STARBOARD_FEATURE_PARAM_TIME_TYPE
#error \
    "STARBOARD_FEATURE_PARAM_TIME_TYPE has to be defined before including this file"
#endif

// To add a feature to Starboard, use the macro
//
// STARBOARD_FEATURE(kFeature, featureName, defaultState)
//
// where:
//
// - `kFeature` is the C++ identifier that will be used for the Starboard
//    Features and `base::Feature`.
// - `featureName` is the feature name, which must be globally unique. This name
//    is used to enable/disable features via experiments and command-line flags.
// - `default_state` is the default state to use for the feature, i.e true or
//    false.
//
// Features should also be guarded with BUILDFLAGS indicating what platform they
// belong to (IS_ANDROID for example) along with the STARBOARD_VERSION they were
// introduced in (e.g. 17).

FEATURE_LIST_START
// Be sure to add the feature in between FEATURE_LIST_START and
// FEATURE_LIST_END. Some examples of what a feature might look like, with a
// defined platform and starboard version is:
//
// #if BUILDFLAG(IS_ANDROID) && (SB_API_VERSION >= 17)
//   STARBOARD_FEATURE(kFeatureFoo, "foo", true)
//   STARBOARD_FEATURE(kCobaltMaxVideoExperiment,
//                     "CobaltMaxVideoExperiment",
//                     false)
//   STARBOARD_FEATURE(kCobaltCustomPlayback,
//                     "CobaltCustomPlayback",
//                     false)
//   STARBOARD_FEATURE(kCobaltArtificialDelay,
//                     "CobaltArtificialDelay",
//                     false)
//   STARBOARD_FEATURE(kCobaltPlaybackParamsExperiment,
//                     "CobaltPlaybackParamsExperiment",
//                     false)
//   STARBOARD_FEATURE(kCobaltVideoDebug, "CobaltVideoDebug", false)
// #endif // BUILDFLAG(IS_ANDROID) && (SB_API_VERSION >= 17)

#if BUILDFLAG(IS_ANDROID) && (SB_API_VERSION >= 17)
// keep-sorted start newline_separated=yes
// By default, app provisioning is disabled. Set the following variable to true
// to enable app provisioning.
STARBOARD_FEATURE(kEnableAppProvisioning, "EnableAppProvisioning", false)

// Set the following variable to true to enable av1 startup optimization.
STARBOARD_FEATURE(kEnableAv1StartupOptimization,
                  "EnableAv1StartupOptimization",
                  false)

// By default, Cobalt destroys and recreates AudioTrack during Seek().
// Set the following variable to true to force it to Flush() AudioTrack
// during Seek().
STARBOARD_FEATURE(kForceFlushAudioTrackDuringReset,
                  "ForceFlushAudioTrackDuringReset",
                  false)

// By default, Cobalt recreates MediaCodec when Reset() during Seek().
// Set the following variable to true to force it Flush() MediaCodec
// during Seek().
STARBOARD_FEATURE(kForceFlushDecoderDuringReset,
                  "ForceFlushDecoderDuringReset",
                  false)

// By default, the platform Opus decoder is only enabled for encrypted playback.
// Set the following variable to true to force it for clear playback.
STARBOARD_FEATURE(kForcePlatformOpusDecoder, "ForcePlatformOpusDecoder", false)

// By default, Cobalt teardowns AudioDecoder during Reset().
// Set the following variable to true to force it reset audio decoder
// during Reset(). This should be enabled with kForceFlushDecoderDuringReset.
STARBOARD_FEATURE(kForceResetAudioDecoder, "ForceResetAudioDecoder", false)

// By default, tunnel mode has to be enabled explicitly by the web app via the
// mime attribute "tunnelmode=true". Set the following variable to true to force
// enabling tunnel mode on all playbacks.
STARBOARD_FEATURE(kForceTunnelMode, "ForceTunnelMode", false)

// By default, NDK Video (NDK MediaCodec) backend is disabled. Set the following
// variable to true to enable NDK Video.
STARBOARD_FEATURE(kNdkVideo, "NdkVideo", false)

// By default, software video codec can be selected when software codec is not
// required. Set the following variable to true to prevent using low performance
// software video decoder in MediaCapabilitiesCache when software codec is not
// explicitly required.
STARBOARD_FEATURE(kRejectLowPerformanceSoftwareDecoder,
                  "RejectLowPerformanceSoftwareDecoder",
                  false)

// Cobalt VideoRenderAlgorithm used to release video frames immediately after
// playback starts. Set the following variable to true to make it release video
// frames until the underlying audio sink actually starts.
STARBOARD_FEATURE(kReleaseVideoFramesAfterAudioStarts,
                  "ReleaseVideoFramesAfterAudioStarts",
                  false)

// By default, stub audio decoder is disabled.
STARBOARD_FEATURE(kUseStubAudioDecoder, "UseStubAudioDecoder", false)

// By default, stub video decoder is disabled.
STARBOARD_FEATURE(kUseStubVideoDecoder, "UseStubVideoDecoder", false)

// By default, Cobalt restarts MediaCodec after stops/flushes during
// Reset()/Flush(). Set the following variable to true with parameters
// kResetDelayUsec and kFlushDelayUsec to force it to wait during
// Reset()/Flush(). For example, with args
// '--enable-features=VideoDecoderDelayUsecOverride:ResetDelayUsec/10ms'.
STARBOARD_FEATURE(kVideoDecoderDelayUsecOverride,
                  "VideoDecoderDelayUsecOverride",
                  false)
// keep-sorted end
#endif  // BUILDFLAG(IS_ANDROID) && (SB_API_VERSION >= 17)
FEATURE_LIST_END

// To add a parameter to Starboard, use the macro:
//
// STARBOARD_FEATURE_PARAM(T, kParam, kFeature, paramName, defaultValue)
//
// where:
//
// - `T` is the value type of the parameter.  The five supported types are:
//
//    - bool
//    - int
//    - std::string
//    - double
//    - base::TimeDelta (represented as int64_t below Starboard)
//
// When wanting to use base::TimeDelta, use the
// |STARBOARD_FEATURE_PARAM_TIME_TYPE| macro. Starboard does not support
// base::TimeDelta, so a base::TimeDelta value is read as an int64_t value
// instead. The |STARBOARD_FEATURE_PARAM_TIME_TYPE| macro is used to help
// differentiate whether base::TimeDelta or int64_t is used. When a parameter
// using type |STARBOARD_FEATURE_PARAM_TIME_TYPE| is used on the Chrobalt level,
// |STARBOARD_FEATURE_PARAM_TIME_TYPE| is defined as base::TimeDelta. When a
// parameter using type |STARBOARD_FEATURE_PARAM_TIME_TYPE| is used on the
// Starboard level, |STARBOARD_FEATURE_PARAM_TIME_TYPE| is defined as int64_t.
//
// - `kParam` is the C++ identifier that will be used for Starboard Parameters
//    and base::FeatureParameters.
// - `kFeature` is the C++ identifier for the feature that owns the parameter.
// - `paramName` is the parameter name, which must be unique to the
//    corresponding feature's list of parameters.
// - `defaultValue` is the default value that the parameter has. If the value
//    isn't overridden, this value will be used.
//
// Parameters should also be guarded with BUILDFLAGS indicating what platform
// they belong to (IS_ANDROID for example) along with the STARBOARD_VERSION they
// were introduced in (e.g. 17).

FEATURE_PARAM_LIST_START
// Be sure to add the parameter macro in between FEATURE_PARAM_LIST_START and
// FEATURE_PARAM_LIST_END. Some examples of what a feature parameter
// might look like with a defined platform and starboard version is:
//
// #if BUILDFLAG(IS_ANDROID) && (SB_API_VERSION >= 17)
//   STARBOARD_FEATURE_PARAM(int, kFeatureParamBoo, kFeatureFoo, "boo", 10)
//   STARBOARD_FEATURE_PARAM(double,
//                           kCustomPlaybackRate,
//                           kCobaltCustomPlayback,
//                           "CustomPlaybackRate",
//                           .75)
//   STARBOARD_FEATURE_PARAM(STARBOARD_FEATURE_PARAM_TIME_TYPE,
//                           kArtificialDelayTime,
//                           kCobaltArtificialDelay,
//                           "ArtificialDelayTime",
//                           base::Milliseconds(10))
//   STARBOARD_FEATURE_PARAM(std::string,
//                           kCobaltDebugSetting,
//                           kCobaltVideoDebug,
//                           "CobaltDebugSetting",
//                           "standard")
// #endif // BUILDFLAG(IS_ANDROID) && (SB_API_VERSION >= 17)

#if BUILDFLAG(IS_ANDROID) && (SB_API_VERSION >= 17)
// By default, Cobalt restarts MediaCodec after stops/flushes during
// Reset()/Flush(). Set the following variable to > 0 to force it to
// wait during Reset()/Flush().
STARBOARD_FEATURE_PARAM(STARBOARD_FEATURE_PARAM_TIME_TYPE,
                        kFlushDelayUsec,
                        kVideoDecoderDelayUsecOverride,
                        "FlushDelayUsec",
                        base::Microseconds(0))
STARBOARD_FEATURE_PARAM(STARBOARD_FEATURE_PARAM_TIME_TYPE,
                        kResetDelayUsec,
                        kVideoDecoderDelayUsecOverride,
                        "ResetDelayUsec",
                        base::Microseconds(0))
#endif  // BUILDFLAG(IS_ANDROID) && (SB_API_VERSION >= 17)
FEATURE_PARAM_LIST_END
