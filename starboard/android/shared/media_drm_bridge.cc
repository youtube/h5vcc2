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

#include "starboard/android/shared/media_drm_bridge.h"

#include <jni.h>

#include <cstring>
#include <vector>

#include "base/android/jni_array.h"
#include "base/android/jni_string.h"
#include "base/memory/raw_ref.h"
#include "starboard/common/check_op.h"
#include "starboard/common/log.h"
#include "third_party/jni_zero/jni_zero.h"

// Must come after all headers that specialize FromJniType() / ToJniType().
#include "cobalt/android/jni_headers/MediaDrmBridge_jni.h"

namespace starboard {
namespace {

using base::android::ConvertJavaStringToUTF8;
using base::android::ConvertUTF8ToJavaString;
using base::android::JavaByteArrayToString;
using base::android::ToJavaByteArray;
using jni_zero::AttachCurrentThread;
using jni_zero::JavaParamRef;
using jni_zero::ScopedJavaLocalRef;

using DrmOperationResult = MediaDrmBridge::OperationResult;

// Using all capital names to be consistent with other Android media statuses.
// They are defined in the same order as in their Java counterparts.  Their
// values should be kept in consistent with their Java counterparts defined in
// android.media.MediaDrm.KeyStatus.
enum class MediaDrmKeyStatus : jint {
  kExpired = 1,
  kInternalError = 4,
  kOutputNotAllowed = 2,
  kPending = 3,
  kUsable = 0,
};

// They must have the same values as defined in MediaDrm.KeyRequest.
enum class RequestType : jint {
  kInitial = 0,
  kRenewal = 1,
  kRelease = 2,
};

SbDrmSessionRequestType ToSbDrmSessionRequestType(RequestType request_type) {
  switch (request_type) {
    case RequestType::kInitial:
      return kSbDrmSessionRequestTypeLicenseRequest;
    case RequestType::kRenewal:
      return kSbDrmSessionRequestTypeLicenseRenewal;
    case RequestType::kRelease:
      return kSbDrmSessionRequestTypeLicenseRelease;
    default:
      SB_NOTREACHED() << "Unknown key request type: "
                      << static_cast<jint>(request_type);
      return kSbDrmSessionRequestTypeLicenseRequest;
  }
}

// Converts a Java MediaDrm.KeyStatus status code to the equivalent
// SbDrmKeyStatus. The |status_code| is obtained via JNI from a Java KeyStatus
// object's getStatusCode() method.
SbDrmKeyStatus ToSbDrmKeyStatus(jint status_code) {
  switch (static_cast<MediaDrmKeyStatus>(status_code)) {
    case MediaDrmKeyStatus::kExpired:
      return kSbDrmKeyStatusExpired;
    case MediaDrmKeyStatus::kInternalError:
      return kSbDrmKeyStatusError;
    case MediaDrmKeyStatus::kOutputNotAllowed:
      return kSbDrmKeyStatusRestricted;
    case MediaDrmKeyStatus::kPending:
      return kSbDrmKeyStatusPending;
    case MediaDrmKeyStatus::kUsable:
      return kSbDrmKeyStatusUsable;
    default:
      SB_NOTREACHED() << "Unknown status=" << status_code;
      return kSbDrmKeyStatusError;
  }
}

std::string JavaByteArrayToString(
    JNIEnv* env,
    const jni_zero::JavaRef<jbyteArray>& j_byte_array) {
  std::string out;
  JavaByteArrayToString(env, j_byte_array, &out);
  return out;
}

ScopedJavaLocalRef<jbyteArray> ToScopedJavaByteArray(JNIEnv* env,
                                                     std::string_view data) {
  return ToJavaByteArray(env, reinterpret_cast<const uint8_t*>(data.data()),
                         data.size());
}

DrmOperationResult ToOperationResult(
    JNIEnv* env,
    const ScopedJavaLocalRef<jobject>& result) {
  auto status = static_cast<DrmOperationStatus>(
      Java_OperationResult_getStatusCode(env, result));
  // Sanitize status.
  switch (status) {
    case DRM_OPERATION_STATUS_SUCCESS:
    case DRM_OPERATION_STATUS_OPERATION_FAILED:
    case DRM_OPERATION_STATUS_NOT_PROVISIONED:
      break;
    default:
      SB_NOTREACHED() << "Unknown status " << static_cast<int>(status);
      status = DRM_OPERATION_STATUS_OPERATION_FAILED;
  }
  return {
      status,
      ConvertJavaStringToUTF8(
          Java_OperationResult_getErrorMessage(env, result)),
  };
}
}  // namespace

// static
std::unique_ptr<MediaDrmBridge> MediaDrmBridge::Create(
    base::raw_ref<MediaDrmBridge::Host> host,
    std::string_view key_system,
    bool enable_app_provisioning) {
  auto bridge =
      std::make_unique<MediaDrmBridge>(PassKey<MediaDrmBridge>(), host);

  if (!bridge->Initialize(key_system, enable_app_provisioning)) {
    return nullptr;
  }
  SB_LOG(INFO) << "Created MediaDrmBridge.";
  return bridge;
}

MediaDrmBridge::MediaDrmBridge(PassKey<MediaDrmBridge>,
                               base::raw_ref<MediaDrmBridge::Host> host)
    : host_(host) {}

MediaDrmBridge::~MediaDrmBridge() {
  if (!j_media_drm_bridge_.is_null()) {
    JNIEnv* env = AttachCurrentThread();
    Java_MediaDrmBridge_destroy(env, j_media_drm_bridge_);
    if (jni_zero::ClearException(env)) {
      SB_LOG(WARNING) << "Ignored JNI exception during MediaDrmBridge destroy in teardown.";
    }
  }
}

void MediaDrmBridge::CreateSession(int ticket,
                                   std::string_view init_data,
                                   std::string_view mime) const {
  JNIEnv* env = AttachCurrentThread();

  JniIntWrapper j_ticket = static_cast<jint>(ticket);
  auto j_init_data = ToScopedJavaByteArray(env, init_data);
  auto j_mime = ScopedJavaLocalRef(ConvertUTF8ToJavaString(env, mime));

  Java_MediaDrmBridge_createSession(env, j_media_drm_bridge_, j_ticket,
                                    j_init_data, j_mime);
}

DrmOperationResult MediaDrmBridge::CreateSessionWithAppProvisioning(
    int ticket,
    std::string_view init_data,
    std::string_view mime) const {
  JNIEnv* env = AttachCurrentThread();

  jint j_ticket = static_cast<jint>(ticket);
  auto j_init_data = ToScopedJavaByteArray(env, init_data);
  auto j_mime = ScopedJavaLocalRef(ConvertUTF8ToJavaString(env, mime));

  return ToOperationResult(
      env, Java_MediaDrmBridge_createSessionWithAppProvisioning(
               env, j_media_drm_bridge_, j_ticket, j_init_data, j_mime));
}

std::string MediaDrmBridge::GenerateProvisionRequest() const {
  JNIEnv* env = AttachCurrentThread();
  ScopedJavaLocalRef<jbyteArray> j_provision_request =
      Java_MediaDrmBridge_generateProvisionRequest(env, j_media_drm_bridge_);
  return JavaByteArrayToString(env, j_provision_request);
}

DrmOperationResult MediaDrmBridge::ProvideProvisionResponse(
    std::string_view response) const {
  JNIEnv* env = AttachCurrentThread();
  return ToOperationResult(
      env, Java_MediaDrmBridge_provideProvisionResponse(
               env, j_media_drm_bridge_, ToScopedJavaByteArray(env, response)));
}

DrmOperationResult MediaDrmBridge::UpdateSession(
    int ticket,
    std::string_view key,
    std::string_view session_id) const {
  JNIEnv* env = AttachCurrentThread();

  auto j_session_id = ToScopedJavaByteArray(env, session_id);
  auto j_response = ToScopedJavaByteArray(env, key);

  return ToOperationResult(
      env, Java_MediaDrmBridge_updateSession(env, j_media_drm_bridge_, ticket,
                                             j_session_id, j_response));
}

void MediaDrmBridge::CloseSession(std::string_view session_id) const {
  JNIEnv* env = AttachCurrentThread();

  auto j_session_id = ToScopedJavaByteArray(env, session_id);

  Java_MediaDrmBridge_closeSession(env, j_media_drm_bridge_, j_session_id);
  if (jni_zero::ClearException(env)) {
    SB_LOG(WARNING) << "Ignored JNI exception during MediaDrmBridge closeSession in teardown.";
  }
}

const void* MediaDrmBridge::GetMetrics(int* size) {
  JNIEnv* env = AttachCurrentThread();

  ScopedJavaLocalRef<jbyteArray> j_metrics =
      Java_MediaDrmBridge_getMetricsInBase64(env, j_media_drm_bridge_);

  if (j_metrics.is_null()) {
    *size = 0;
    return nullptr;
  }

  base::android::JavaByteArrayToByteVector(env, j_metrics, &metrics_);
  *size = static_cast<int>(metrics_.size());

  return metrics_.data();
}

bool MediaDrmBridge::CreateMediaCryptoSession() {
  bool result = Java_MediaDrmBridge_createMediaCryptoSession(
      AttachCurrentThread(), j_media_drm_bridge_);
  if (!result && !j_media_crypto_.is_null()) {
    j_media_crypto_.Reset();
    return false;
  }

  return true;
}

void MediaDrmBridge::OnSessionMessage(
    JNIEnv* env,
    jint ticket,
    const JavaParamRef<jbyteArray>& session_id,
    jint request_type,
    const JavaParamRef<jbyteArray>& message) {
  host_->OnSessionUpdate(
      ticket, ToSbDrmSessionRequestType(static_cast<RequestType>(request_type)),
      JavaByteArrayToString(env, session_id),
      JavaByteArrayToString(env, message));
}

void MediaDrmBridge::OnKeyStatusChange(
    JNIEnv* env,
    const JavaParamRef<jbyteArray>& session_id,
    const std::vector<DrmKeyStatusInfo>& key_statuses) {
  std::string session_id_bytes = JavaByteArrayToString(env, session_id);

  size_t length = key_statuses.size();
  std::vector<SbDrmKeyId> drm_key_ids(length);
  std::vector<SbDrmKeyStatus> drm_key_statuses(length);

  for (size_t i = 0; i < length; ++i) {
    const auto& key_status = key_statuses[i];

    SB_CHECK_LE(key_status.key_id.size(), sizeof(drm_key_ids[i].identifier));
    if (!key_status.key_id.empty()) {
      memcpy(drm_key_ids[i].identifier, key_status.key_id.data(),
             key_status.key_id.size());
    }
    drm_key_ids[i].identifier_size = key_status.key_id.size();
    drm_key_statuses[i] = key_status.status;
  }

  host_->OnKeyStatusChange(session_id_bytes, drm_key_ids, drm_key_statuses);
}

// static
bool MediaDrmBridge::IsWidevineSupported(JNIEnv* env) {
  return Java_MediaDrmBridge_isWidevineCryptoSchemeSupported(env) == JNI_TRUE;
}

// static
bool MediaDrmBridge::IsCbcsSupported(JNIEnv* env) {
  return Java_MediaDrmBridge_isCbcsSchemeSupported(env) == JNI_TRUE;
}

bool MediaDrmBridge::Initialize(std::string_view key_system,
                                bool enable_app_provisioning) {
  JNIEnv* env = AttachCurrentThread();

  ScopedJavaLocalRef<jstring> j_key_system(
      ConvertUTF8ToJavaString(env, key_system));
  ScopedJavaLocalRef<jobject> j_media_drm_bridge(
      Java_MediaDrmBridge_create(env, j_key_system, enable_app_provisioning,
                                 reinterpret_cast<jlong>(this)));

  if (j_media_drm_bridge.is_null()) {
    SB_LOG(ERROR) << "Failed to create MediaDrmBridge.";
    return false;
  }

  ScopedJavaLocalRef<jobject> j_media_crypto(
      Java_MediaDrmBridge_getMediaCrypto(env, j_media_drm_bridge));

  if (j_media_crypto.is_null()) {
    SB_LOG(ERROR) << "Failed to create MediaCrypto.";
    return false;
  }

  j_media_drm_bridge_.Reset(j_media_drm_bridge);
  j_media_crypto_.Reset(j_media_crypto);

  return true;
}

std::ostream& operator<<(std::ostream& os, DrmOperationStatus status) {
  switch (status) {
    case DRM_OPERATION_STATUS_SUCCESS:
      return os << "success";
    case DRM_OPERATION_STATUS_OPERATION_FAILED:
      return os << "operation-failed";
    case DRM_OPERATION_STATUS_NOT_PROVISIONED:
      return os << "not-provisioned";
    default:
      SB_NOTREACHED();
      return os << "unknown-status";
  }
}

std::ostream& operator<<(std::ostream& os, const DrmOperationResult& result) {
  os << "{status:" << result.status;
  if (!result.ok()) {
    os << ", error_message: "
       << (result.error_message.empty() ? "(empty)" : result.error_message);
  }
  return os << "}";
}

}  // namespace starboard

namespace jni_zero {
template <>
starboard::DrmKeyStatusInfo FromJniType<starboard::DrmKeyStatusInfo>(
    JNIEnv* env,
    const JavaRef<jobject>& j_key_status) {
  ScopedJavaLocalRef<jbyteArray> j_key_id =
      starboard::Java_KeyStatus_getKeyId(env, j_key_status);
  std::vector<uint8_t> key_id_bytes;
  if (j_key_id) {
    base::android::JavaByteArrayToByteVector(env, j_key_id, &key_id_bytes);
  }
  SbDrmKeyStatus status = starboard::ToSbDrmKeyStatus(
      starboard::Java_KeyStatus_getStatusCode(env, j_key_status));
  return starboard::DrmKeyStatusInfo{std::move(key_id_bytes), status};
}
}  // namespace jni_zero
