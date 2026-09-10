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

#import <AdSupport/AdSupport.h>
#import <AppTrackingTransparency/ATTrackingManager.h>
#import <AppTrackingTransparency/AppTrackingTransparency.h>
#import <UIKit/UIKit.h>

#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/functional/callback_helpers.h"
#include "base/notimplemented.h"
#include "base/notreached.h"
#include "base/strings/sys_string_conversions.h"
#include "base/task/bind_post_task.h"
#include "cobalt/browser/h5vcc_system/h5vcc_system_impl_base.h"
#include "cobalt/common/cobalt_thread_checker.h"
#include "starboard/system.h"

namespace h5vcc_system {

namespace {

void GetAdvertisingIdShared(
    H5vccSystemImpl::GetAdvertisingIdCallback callback) {
  NSString* advertising_id =
      [[ASIdentifierManager sharedManager].advertisingIdentifier UUIDString];
  std::move(callback).Run(base::SysNSStringToUTF8(advertising_id));
}

bool GetLimitAdTrackingShared() {
  return [ATTrackingManager trackingAuthorizationStatus] !=
         ATTrackingManagerAuthorizationStatusAuthorized;
}

std::string GetTrackingAuthorizationStatusShared() {
  ATTrackingManagerAuthorizationStatus status =
      [ATTrackingManager trackingAuthorizationStatus];
  switch (status) {
    case ATTrackingManagerAuthorizationStatusAuthorized:
      return std::string("AUTHORIZED");
    case ATTrackingManagerAuthorizationStatusDenied:
      return std::string("DENIED");
    case ATTrackingManagerAuthorizationStatusRestricted:
      return std::string("RESTRICTED");
    case ATTrackingManagerAuthorizationStatusNotDetermined:
      return std::string("NOT_DETERMINED");
  }
  NOTREACHED();
}

}  // namespace

void H5vccSystemImpl::GetAdvertisingId(GetAdvertisingIdCallback callback) {
  CHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  GetAdvertisingIdShared(std::move(callback));
}

void H5vccSystemImpl::GetAdvertisingIdSync(
    GetAdvertisingIdSyncCallback callback) {
  CHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  GetAdvertisingIdShared(std::move(callback));
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
  auto completion = base::CallbackToBlock(base::BindPostTask(
      base::SequencedTaskRunner::GetCurrentDefault(),
      base::IgnoreArgs<ATTrackingManagerAuthorizationStatus>(base::BindOnce(
          [](RequestTrackingAuthorizationCallback callback) {
            std::move(callback).Run(
                /*is_tracking_authorization_supported=*/true);
          },
          std::move(callback)))));
  [ATTrackingManager
      requestTrackingAuthorizationWithCompletionHandler:completion];
}

void H5vccSystemImpl::GetFriendlyName(GetFriendlyNameCallback callback) {
  CHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  std::move(callback).Run(
      base::SysNSStringToUTF8([UIDevice currentDevice].name));
}

void H5vccSystemImpl::GetScreenDiagonal(GetScreenDiagonalCallback callback) {
  CHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  // This was not implemented in Cobalt 25 (which linked against the stub
  // returning 0.0), and we are not yet sure if or how we will implement it in
  // Cobalt 27.
  std::move(callback).Run(0.0);
}

void H5vccSystemImpl::GetUserOnExitStrategy(
    GetUserOnExitStrategyCallback callback) {
  std::move(callback).Run(h5vcc_system::mojom::UserOnExitStrategy::kMinimize);
}

void H5vccSystemImpl::HideSplashScreen() {}

void H5vccSystemImpl::GetWasLowMemoryKilled(
    GetWasLowMemoryKilledCallback callback) {
  CHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  // Stubbed.
  std::move(callback).Run(false);
}

void H5vccSystemImpl::PerformExitStrategy() {
  SbSystemRequestConceal();
}

}  // namespace h5vcc_system
