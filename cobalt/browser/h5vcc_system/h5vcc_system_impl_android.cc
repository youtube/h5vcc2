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
#include "base/notimplemented.h"
#include "cobalt/browser/h5vcc_system/h5vcc_system_impl_base.h"
#include "starboard/android/shared/starboard_bridge.h"

using ::starboard::StarboardBridge;

namespace h5vcc_system {

namespace {

std::string GetAdvertisingIdShared() {
  std::string advertising_id;
  JNIEnv* env = base::android::AttachCurrentThread();
  StarboardBridge* starboard_bridge = StarboardBridge::GetInstance();
  advertising_id = starboard_bridge->GetAdvertisingId(env);
  return advertising_id;
}

bool GetLimitAdTrackingShared() {
  bool limit_ad_tracking = false;
  JNIEnv* env = base::android::AttachCurrentThread();
  StarboardBridge* starboard_bridge = StarboardBridge::GetInstance();
  limit_ad_tracking = starboard_bridge->GetLimitAdTracking(env);
  return limit_ad_tracking;
}

std::string GetTrackingAuthorizationStatusShared() {
  // TODO: b/395650827 - Connect to Starboard extension.
  NOTIMPLEMENTED();
  return "NOT_SUPPORTED";
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
  // TODO: b/395650827 - Connect to Starboard extension.
  NOTIMPLEMENTED();
  std::move(callback).Run(false);
}

void H5vccSystemImpl::GetFriendlyName(GetFriendlyNameCallback callback) {
  CHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  JNIEnv* env = base::android::AttachCurrentThread();
  std::move(callback).Run(StarboardBridge::GetInstance()->GetFriendlyName(env));
}

void H5vccSystemImpl::GetScreenDiagonal(GetScreenDiagonalCallback callback) {
  CHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  JNIEnv* env = base::android::AttachCurrentThread();
  std::move(callback).Run(
      StarboardBridge::GetInstance()->GetScreenDiagonal(env));
}

void H5vccSystemImpl::GetUserOnExitStrategy(
    GetUserOnExitStrategyCallback callback) {
  std::move(callback).Run(h5vcc_system::mojom::UserOnExitStrategy::kMinimize);
}

void H5vccSystemImpl::PerformExitStrategy() {
  JNIEnv* env = base::android::AttachCurrentThread();
  StarboardBridge* starboard_bridge = StarboardBridge::GetInstance();
  starboard_bridge->RequestSuspend(env);
}

void H5vccSystemImpl::HideSplashScreen() {
  LOG(INFO) << "H5vccSystem HideSplashScreen.";
  JNIEnv* env = base::android::AttachCurrentThread();
  StarboardBridge::GetInstance()->HideSplashScreen(env);
}

void H5vccSystemImpl::GetWasLowMemoryKilled(
    GetWasLowMemoryKilledCallback callback) {
  CHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  // Stubbed.
  std::move(callback).Run(false);
}

}  // namespace h5vcc_system
