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

#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/notreached.h"
#include "cobalt/browser/h5vcc_system/h5vcc_system_impl_base.h"
#include "cobalt/configuration/configuration.h"
#include "starboard/common/system_property.h"
#include "starboard/extension/low_memory_kill.h"
#include "starboard/system.h"
#include "starboard/window.h"

namespace h5vcc_system {

namespace {

using cobalt::configuration::Configuration;

h5vcc_system::mojom::UserOnExitStrategy GetUserOnExitStrategyInternal() {
  switch (Configuration::GetInstance()->CobaltUserOnExitStrategy()) {
    case Configuration::UserOnExitStrategy::kClose:
      return h5vcc_system::mojom::UserOnExitStrategy::kClose;
    case Configuration::UserOnExitStrategy::kMinimize:
      return h5vcc_system::mojom::UserOnExitStrategy::kMinimize;
    case Configuration::UserOnExitStrategy::kNoExit:
      return h5vcc_system::mojom::UserOnExitStrategy::kNoExit;
  }
  NOTREACHED();
}

std::string GetAdvertisingIdShared() {
  std::string advertising_id;
  advertising_id =
      starboard::GetSystemPropertyString(kSbSystemPropertyAdvertisingId);
  DLOG_IF(INFO, advertising_id.empty())
      << "Failed to get kSbSystemPropertyAdvertisingId.";
  return advertising_id;
}

bool GetLimitAdTrackingShared() {
  bool limit_ad_tracking = false;
  std::string result =
      starboard::GetSystemPropertyString(kSbSystemPropertyLimitAdTracking);
  if (result.empty()) {
    DLOG(INFO) << "Failed to get kSbSystemPropertyLimitAdTracking.";
  } else {
    limit_ad_tracking = std::atoi(result.c_str());
  }
  return limit_ad_tracking;
}

std::string GetTrackingAuthorizationStatusShared() {
  return "NOT_SUPPORTED";
}

bool GetWasLowMemoryKilledShared() {
  const auto* low_memory_kill_extension =
      static_cast<const StarboardExtensionLowMemoryKillApi*>(
          SbSystemGetExtension(kStarboardExtensionLowMemoryKillName));
  if (!low_memory_kill_extension || !low_memory_kill_extension->name ||
      strcmp(low_memory_kill_extension->name,
             kStarboardExtensionLowMemoryKillName) != 0 ||
      low_memory_kill_extension->version < 1 ||
      !low_memory_kill_extension->WasLowMemoryKilled) {
    return false;
  }
  return low_memory_kill_extension->WasLowMemoryKilled();
}

}  // namespace

void H5vccSystemImpl::GetAdvertisingId(GetAdvertisingIdCallback callback) {
  CHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  std::move(callback).Run(GetAdvertisingIdShared());
}

void H5vccSystemImpl::GetAdvertisingIdSync(
    GetAdvertisingIdSyncCallback callback) {
  CHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  std::move(callback).Run(GetAdvertisingIdShared());
}

void H5vccSystemImpl::GetLimitAdTracking(GetLimitAdTrackingCallback callback) {
  CHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  std::move(callback).Run(GetLimitAdTrackingShared());
}

void H5vccSystemImpl::GetLimitAdTrackingSync(
    GetLimitAdTrackingSyncCallback callback) {
  CHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  std::move(callback).Run(GetLimitAdTrackingShared());
}

void H5vccSystemImpl::GetTrackingAuthorizationStatus(
    GetTrackingAuthorizationStatusCallback callback) {
  CHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  std::move(callback).Run(GetTrackingAuthorizationStatusShared());
}

void H5vccSystemImpl::GetTrackingAuthorizationStatusSync(
    GetTrackingAuthorizationStatusSyncCallback callback) {
  CHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  std::move(callback).Run(GetTrackingAuthorizationStatusShared());
}

void H5vccSystemImpl::RequestTrackingAuthorization(
    RequestTrackingAuthorizationCallback callback) {
  CHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  std::move(callback).Run(false);
}

void H5vccSystemImpl::GetFriendlyName(GetFriendlyNameCallback callback) {
  CHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  std::string friendly_name =
      starboard::GetSystemPropertyString(kSbSystemPropertyFriendlyName);
  DLOG_IF(INFO, friendly_name.empty())
      << "Failed to get kSbSystemPropertyFriendlyName.";
  std::move(callback).Run(friendly_name);
}

void H5vccSystemImpl::GetScreenDiagonal(GetScreenDiagonalCallback callback) {
  CHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  SbWindow window = GetPrimarySbWindow();
  DLOG_IF(ERROR, !SbWindowIsValid(window))
      << "GetScreenDiagonal: Invalid primary window.";
  float diagonal =
      SbWindowIsValid(window) ? SbWindowGetDiagonalSizeInInches(window) : 0.0f;
  std::move(callback).Run(static_cast<double>(diagonal));
}

void H5vccSystemImpl::GetUserOnExitStrategy(
    GetUserOnExitStrategyCallback callback) {
  std::move(callback).Run(GetUserOnExitStrategyInternal());
}

void H5vccSystemImpl::HideSplashScreen() {}

void H5vccSystemImpl::GetWasLowMemoryKilled(
    GetWasLowMemoryKilledCallback callback) {
  CHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  std::move(callback).Run(GetWasLowMemoryKilledShared());
}

void H5vccSystemImpl::PerformExitStrategy() {
  auto strategy = GetUserOnExitStrategyInternal();
  switch (strategy) {
    case h5vcc_system::mojom::UserOnExitStrategy::kClose:
      SbSystemRequestStop(/*error_level=*/0);
      break;
    case h5vcc_system::mojom::UserOnExitStrategy::kMinimize:
      SbSystemRequestConceal();
      break;
    case h5vcc_system::mojom::UserOnExitStrategy::kNoExit:
      return;
  }
}

}  // namespace h5vcc_system
