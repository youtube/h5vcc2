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

#include "starboard_cdm.h"

#include <stddef.h>

#include <memory>
#include <utility>
#include <vector>

#include "base/logging.h"
#include "build/build_config.h"
#include "media/media_buildflags.h"
#include "starboard/common/drm.h"

namespace media {

namespace {

const char* GetInitDataTypeName(EmeInitDataType type) {
  switch (type) {
    case EmeInitDataType::WEBM:
      return "webm";
    case EmeInitDataType::CENC:
      return "cenc";
    case EmeInitDataType::KEYIDS:
      return "keyids";
#if BUILDFLAG(IS_IOS_TVOS) && BUILDFLAG(USE_STARBOARD_MEDIA)
    case EmeInitDataType::SINF:
      return "sinf";
    case EmeInitDataType::SKD:
      return "skd";
    case EmeInitDataType::FAIRPLAY:
      return "fairplay";
#endif  // BUILDFLAG(IS_IOS_TVOS) && BUILDFLAG(USE_STARBOARD_MEDIA)
    case EmeInitDataType::UNKNOWN:
      return "unknown";
  }
  NOTREACHED() << "Unexpected EmeInitDataType";
}

std::ostream& operator<<(std::ostream& os, EmeInitDataType type) {
  return os << GetInitDataTypeName(type);
}

CdmMessageType SbDrmSessionRequestTypeToMediaMessageType(
    SbDrmSessionRequestType type) {
  switch (type) {
    case kSbDrmSessionRequestTypeLicenseRequest:
      return CdmMessageType::LICENSE_REQUEST;
    case kSbDrmSessionRequestTypeLicenseRenewal:
      return CdmMessageType::LICENSE_RENEWAL;
    case kSbDrmSessionRequestTypeLicenseRelease:
      return CdmMessageType::LICENSE_RELEASE;
    case kSbDrmSessionRequestTypeIndividualizationRequest:
      return CdmMessageType::INDIVIDUALIZATION_REQUEST;
  }

  NOTREACHED() << "Unexpected SbDrmSessionRequestType " << type;
}

CdmKeyInformation::KeyStatus ToCdmKeyStatus(SbDrmKeyStatus status) {
  switch (status) {
    case kSbDrmKeyStatusUsable:
      return CdmKeyInformation::KeyStatus::USABLE;
    case kSbDrmKeyStatusExpired:
      return CdmKeyInformation::KeyStatus::EXPIRED;
    case kSbDrmKeyStatusReleased:
      return CdmKeyInformation::KeyStatus::RELEASED;
    case kSbDrmKeyStatusRestricted:
      return CdmKeyInformation::KeyStatus::OUTPUT_RESTRICTED;
    case kSbDrmKeyStatusDownscaled:
      return CdmKeyInformation::KeyStatus::OUTPUT_DOWNSCALED;
    case kSbDrmKeyStatusPending:
      return CdmKeyInformation::KeyStatus::KEY_STATUS_PENDING;
    case kSbDrmKeyStatusError:
      return CdmKeyInformation::KeyStatus::INTERNAL_ERROR;
  }
  NOTREACHED() << "Unexpected SbDrmKeyStatus " << status;
}

std::string BytesToString(const void* data, int length) {
  return std::string(static_cast<const char*>(data), length);
}

}  // namespace

StarboardCdm::StarboardCdm(
    const CdmConfig& cdm_config,
    const SessionMessageCB& message_cb,
    const SessionClosedCB& closed_cb,
    const SessionKeysChangeCB& keys_change_cb,
    const SessionExpirationUpdateCB& expiration_update_cb)
    : task_runner_(base::SequencedTaskRunner::GetCurrentDefault()),
      sb_drm_(SbDrmCreateSystem(cdm_config.key_system.c_str(),
                                this,
                                OnSessionUpdateRequestGeneratedFunc,
                                OnSessionUpdatedFunc,
                                OnSessionKeyStatusesChangedFunc,
                                OnServerCertificateUpdatedFunc,
                                OnSessionClosedFunc)),
      message_cb_{message_cb},
      closed_cb_{closed_cb},
      keys_change_cb_{keys_change_cb},
      expiration_update_cb_{expiration_update_cb} {
  DCHECK(message_cb_);
  DCHECK(closed_cb_);
  DCHECK(keys_change_cb_);
  DCHECK(expiration_update_cb_);

  std::string key_system = cdm_config.key_system;
  if (SbDrmSystemIsValid(sb_drm_)) {
    LOG(INFO) << "Successfully created SbDrmSystem (" << sb_drm_
              << "), key system: " << key_system << ".";
  } else {
    LOG(INFO) << "Failed to create SbDrmSystem, key system: " << key_system;
  }
}

StarboardCdm::~StarboardCdm() {
  if (SbDrmSystemIsValid(sb_drm_)) {
    LOG(INFO) << "Starboard CDM destructor. Destroying SbDrm";
    SbDrmDestroySystem(sb_drm_);
  } else {
    LOG(WARNING) << "Attempting to close invalid SbDrmSystem";
  }
}

void StarboardCdm::SetServerCertificate(
    const std::vector<uint8_t>& certificate,
    std::unique_ptr<SimpleCdmPromise> promise) {
  DCHECK(task_runner_->RunsTasksInCurrentSequence());
  LOG(INFO) << "StarboardCdm - Set server cert - size:" << certificate.size();

  if (!SbDrmIsServerCertificateUpdatable(sb_drm_)) {
    LOG(WARNING)
        << "Trying to update cert, but DRM system does not support it.";
    promise->reject(CdmPromise::Exception::NOT_SUPPORTED_ERROR, 0,
                    "DRM system doesn't support updating server certificate.");
    return;
  }

  int ticket = next_ticket_++;
  if (!SbDrmTicketIsValid(ticket)) {
    LOG(ERROR) << "Updating server with invalid ticket";
    return;
  }

  uint32_t promise_id = promises_.SavePromise(std::move(promise));

  ticket_to_server_certificate_updated_map_.insert(
      std::make_pair(ticket, promise_id));

  SbDrmUpdateServerCertificate(sb_drm_, ticket, certificate.data(),
                               certificate.size());
}

void StarboardCdm::CreateSessionAndGenerateRequest(
    CdmSessionType session_type,
    EmeInitDataType init_data_type,
    const std::vector<uint8_t>& init_data,
    std::unique_ptr<NewSessionCdmPromise> promise) {
  DCHECK(task_runner_->RunsTasksInCurrentSequence());

  uint32_t promise_id = promises_.SavePromise(std::move(promise));

  SessionUpdateRequest session_update_request;
  session_update_request.promise_id = promise_id;
  session_update_request.session_type = session_type;

  int ticket = next_ticket_++;
  if (!SbDrmTicketIsValid(ticket)) {
    LOG(ERROR) << "Generate session update request with invalid ticket";
    return;
  }

  ticket_to_session_update_request_map_.insert(
      std::make_pair(ticket, std::move(session_update_request)));

  LOG(INFO) << "Generate session update request of drm system (" << sb_drm_
            << "), type: " << init_data_type
            << ", init data size: " << init_data.size()
            << ", ticket: " << ticket;

  SbDrmGenerateSessionUpdateRequest(sb_drm_, ticket,
                                    GetInitDataTypeName(init_data_type),
                                    init_data.data(), init_data.size());
}

void StarboardCdm::LoadSession(CdmSessionType session_type,
                               const std::string& session_id,
                               std::unique_ptr<NewSessionCdmPromise> promise) {
  LOG(INFO) << "Starboard CDM - Load Session - NOT IMPL";
  promise->reject(CdmPromise::Exception::INVALID_STATE_ERROR, 0,
                  "SbDrm doesn't implement Load Session.");
  return;
}

void StarboardCdm::UpdateSession(const std::string& session_id,
                                 const std::vector<uint8_t>& response,
                                 std::unique_ptr<SimpleCdmPromise> promise) {
  DCHECK(task_runner_->RunsTasksInCurrentSequence());

  auto it = std::find(session_list_.begin(), session_list_.end(), session_id);
  if (it == session_list_.end()) {
    LOG(WARNING) << "Update session - " << session_id
                 << " - session doesn't exist";
    promise->reject(CdmPromise::Exception::INVALID_STATE_ERROR, 0,
                    "Session doesn't exist.");
    return;
  }

  // Caller should NOT pass in an empty response.
  DCHECK(!response.empty());

  uint32_t promise_id = promises_.SavePromise(std::move(promise));

  // Store the context of the call.
  SessionUpdate session_update;
  session_update.promise_id = promise_id;
  int ticket = next_ticket_++;

  if (!SbDrmTicketIsValid(ticket)) {
    LOG(WARNING) << "Update session with invalid ticket";
    return;
  }

  ticket_to_session_update_map_.insert(std::make_pair(ticket, session_update));

  LOG(INFO) << "Update session of drm system (" << sb_drm_
            << "), response length: " << response.size()
            << ", ticket: " << ticket << ", session id: " << session_id;

  SbDrmUpdateSession(sb_drm_, ticket, response.data(), response.size(),
                     session_id.c_str(), session_id.size());
}

// Runs the parallel steps from https://w3c.github.io/encrypted-media/#close.
void StarboardCdm::CloseSession(const std::string& session_id,
                                std::unique_ptr<SimpleCdmPromise> promise) {
  DCHECK(task_runner_->RunsTasksInCurrentSequence());
  LOG(INFO) << "StarboardCdm - Close session:" << session_id;

  auto it = std::find(session_list_.begin(), session_list_.end(), session_id);
  if (it == session_list_.end()) {
    LOG(WARNING) << "Close session - " << session_id
                 << " - session doesn't exist";
    promise->resolve();
    return;
  }

  SbDrmCloseSession(sb_drm_, session_id.c_str(), session_id.size());

  session_list_.erase(it);
  closed_cb_.Run(session_id, media::CdmSessionClosedReason::kClose);
  promise->resolve();
}

// Runs the parallel steps from https://w3c.github.io/encrypted-media/#remove.
void StarboardCdm::RemoveSession(const std::string& session_id,
                                 std::unique_ptr<SimpleCdmPromise> promise) {
  DCHECK(task_runner_->RunsTasksInCurrentSequence());
  LOG(INFO) << "Starboard CDM - Remove Session - NOT IMPL";
  promise->reject(CdmPromise::Exception::INVALID_STATE_ERROR, 0,
                  "SbDrm doesn't implement License Release.");
  return;
}

CdmContext* StarboardCdm::GetCdmContext() {
  return this;
}

SbDrmSystem StarboardCdm::GetSbDrmSystem() {
  return sb_drm_;
}

bool StarboardCdm::HasValidSbDrm() {
  return SbDrmSystemIsValid(sb_drm_);
}

std::unique_ptr<CallbackRegistration> StarboardCdm::RegisterEventCB(
    EventCB event_cb) {
  return event_callbacks_.Register(std::move(event_cb));
}

void StarboardCdm::OnSessionUpdateRequestGenerated(
    SessionTicketAndOptionalId ticket_and_optional_id,
    SbDrmStatus status,
    SbDrmSessionRequestType type,
    const std::string& error_message,
    std::vector<uint8_t> message,
    int message_size) {
  DCHECK(task_runner_->RunsTasksInCurrentSequence());

  int ticket = ticket_and_optional_id.ticket;
  const std::optional<std::string>& session_id = ticket_and_optional_id.id;

  LOG(INFO) << "Receiving session update request notification from drm system ("
            << sb_drm_ << "), status: " << status << ", type: " << type
            << ", ticket_and_session_id: " << ticket_and_optional_id.ToString()
            << ", message size: " << message.size() << ", error message: "
            << (error_message.empty() ? "n/a" : error_message);

  if (SbDrmTicketIsValid(ticket)) {
    // Called back as a result of |SbDrmGenerateSessionUpdateRequest|.

    // Restore the context of |GenerateSessionUpdateRequest|.
    auto session_update_request_iterator =
        ticket_to_session_update_request_map_.find(ticket);
    if (session_update_request_iterator ==
        ticket_to_session_update_request_map_.end()) {
      LOG(INFO) << "Unknown session update request ticket: " << ticket << ".";
      return;
    }

    auto session_request = std::move(session_update_request_iterator->second);

    if (session_id) {
      session_list_.push_back(session_id.value());
      promises_.ResolvePromise(session_request.promise_id, session_id.value());
    } else {
      // Failure during request generation.
      LOG(INFO) << "Calling session update request callback on drm system ("
                << sb_drm_ << "), status: " << status
                << ", error message: " << error_message;
      promises_.RejectPromise(session_request.promise_id,
                              CdmPromise::Exception::INVALID_STATE_ERROR, 0,
                              "Failure during request generation.");
    }

    ticket_to_session_update_request_map_.erase(
        session_update_request_iterator);

  } else {
    // Called back spontaneously by the underlying DRM system.
    // Spontaneous calls must refer to a valid session.
    if (!session_id.has_value()) {
      LOG(ERROR) << "SbDrmSessionUpdateRequestFunc() should not be called "
                    "with both invalid ticket and null session id.";
      NOTREACHED();
      return;
    }
  }

  auto session_iterator =
      std::find(session_list_.begin(), session_list_.end(), session_id.value());
  if (session_iterator == session_list_.end()) {
    LOG(ERROR) << "Unknown session id: " << session_id.value() << ".";
    return;
  }

  LOG(INFO) << "Scheduling session message callback.";
  task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(message_cb_, session_id.value(),
                     SbDrmSessionRequestTypeToMediaMessageType(type), message));
}

void StarboardCdm::OnSessionUpdated(int ticket,
                                    SbDrmStatus status,
                                    const std::string& error_message) {
  DCHECK(task_runner_->RunsTasksInCurrentSequence());
  LOG(INFO) << "Receiving session updated notification from drm system ("
            << sb_drm_ << "), status: " << status << ", ticket: " << ticket
            << ", error message: "
            << (error_message.empty() ? "n/a" : error_message);

  // Restore the context of |UpdateSession|.
  TicketToSessionUpdateMap::iterator session_update_iterator =
      ticket_to_session_update_map_.find(ticket);
  if (session_update_iterator == ticket_to_session_update_map_.end()) {
    LOG(ERROR) << "Unknown session update ticket: " << ticket << ".";
    return;
  }
  const SessionUpdate& session_update = session_update_iterator->second;

  // Interpret the result.
  if (status == kSbDrmStatusSuccess) {
    promises_.ResolvePromise(session_update.promise_id);
  } else {
    promises_.RejectPromise(session_update.promise_id,
                            CdmPromise::Exception::INVALID_STATE_ERROR, 0,
                            "Unable to create CDM.");
  }

  // TODO(cobalt, b/378957649) Check for removed session ids
  // Sweep the context of |UpdateSession|.
  ticket_to_session_update_map_.erase(session_update_iterator);
}

void StarboardCdm::OnSessionKeyStatusChanged(const std::string& session_id,
                                             CdmKeysInfo keys_info,
                                             bool has_additional_usable_key) {
  DCHECK(task_runner_->RunsTasksInCurrentSequence());
  LOG(INFO) << "Receiving session key status changed notification from drm"
            << " system (" << sb_drm_ << "), session id: " << session_id
            << ", number of key ids: " << keys_info.size()
            << ", has_additional_usable_key: "
            << (has_additional_usable_key ? "true" : "false");

  // Find the session by ID.
  auto session_iterator =
      std::find(session_list_.begin(), session_list_.end(), session_id);
  if (session_iterator == session_list_.end()) {
    LOG(ERROR) << "Unknown session id: " << session_id << ".";
    return;
  }

  task_runner_->PostTask(FROM_HERE, base::BindOnce(keys_change_cb_, session_id,
                                                   has_additional_usable_key,
                                                   std::move(keys_info)));
}

void StarboardCdm::OnServerCertificateUpdated(
    int ticket,
    SbDrmStatus status,
    const std::string& error_message) {
  DCHECK(task_runner_->RunsTasksInCurrentSequence());
  LOG(INFO) << "Receiving server certificate updated notification from drm"
            << " system (" << sb_drm_ << "), ticket: " << ticket
            << ", status: " << status << ", error message: " << error_message;

  auto iter = ticket_to_server_certificate_updated_map_.find(ticket);
  if (iter == ticket_to_server_certificate_updated_map_.end()) {
    LOG(ERROR) << "Unknown ticket: " << ticket << ".";
    return;
  }

  if (status == kSbDrmStatusSuccess) {
    promises_.ResolvePromise(iter->second);
  } else {
    promises_.RejectPromise(iter->second,
                            CdmPromise::Exception::INVALID_STATE_ERROR, 0,
                            "Failure to update Certificate.");
  }

  ticket_to_server_certificate_updated_map_.erase(iter);
}

void StarboardCdm::OnSessionClosed(const std::string& session_id) {
  DCHECK(task_runner_->RunsTasksInCurrentSequence());
  LOG(INFO) << "OnSessionClosed for session id:" << session_id;

  // Find the session by ID.
  auto session_iterator =
      std::find(session_list_.begin(), session_list_.end(), session_id);
  if (session_iterator == session_list_.end()) {
    LOG(ERROR) << "Unknown session id: " << session_id << ".";
    return;
  }

  closed_cb_.Run(session_id, media::CdmSessionClosedReason::kClose);
  session_list_.erase(session_iterator);
}

// static
void StarboardCdm::OnSessionUpdateRequestGeneratedFunc(
    SbDrmSystem sb_drm,
    void* context,
    int ticket,
    SbDrmStatus status,
    SbDrmSessionRequestType type,
    const char* error_message,
    const void* session_id,
    int session_id_size,
    const void* content,
    int content_size,
    const char* url) {
  DCHECK(context);
  StarboardCdm* cdm = static_cast<StarboardCdm*>(context);
  DCHECK_EQ(sb_drm, cdm->sb_drm_);

  const uint8_t* begin = static_cast<const uint8_t*>(content);
  const uint8_t* end = begin + content_size;
  const std::vector<uint8_t> content_copy(begin, end);

  cdm->task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(
          &StarboardCdm::OnSessionUpdateRequestGenerated,
          cdm->weak_factory_.GetWeakPtr(),
          session_id == nullptr
              ? SessionTicketAndOptionalId(ticket)
              : SessionTicketAndOptionalId(
                    ticket, BytesToString(session_id, session_id_size)),
          status, type, error_message ? std::string(error_message) : "",
          std::move(content_copy), content_size));
}

// static
void StarboardCdm::OnSessionUpdatedFunc(SbDrmSystem sb_drm,
                                        void* context,
                                        int ticket,
                                        SbDrmStatus status,
                                        const char* error_message,
                                        const void* session_id,
                                        int session_id_size) {
  DCHECK(context);
  StarboardCdm* cdm = static_cast<StarboardCdm*>(context);
  DCHECK_EQ(sb_drm, cdm->sb_drm_);

  cdm->task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(&StarboardCdm::OnSessionUpdated,
                     cdm->weak_factory_.GetWeakPtr(), ticket, status,
                     error_message ? std::string(error_message) : ""));
}

// static
void StarboardCdm::OnSessionKeyStatusesChangedFunc(
    SbDrmSystem sb_drm,
    void* context,
    const void* session_id,
    int session_id_size,
    int number_of_keys,
    const SbDrmKeyId* key_ids,
    const SbDrmKeyStatus* key_statuses) {
  DCHECK(context);
  StarboardCdm* cdm = static_cast<StarboardCdm*>(context);
  DCHECK_EQ(sb_drm, cdm->sb_drm_);

  DCHECK(session_id != NULL);

  bool has_additional_usable_key = false;
  CdmKeysInfo keys_info;

  for (int i = 0; i < number_of_keys; ++i) {
    const char* identifier =
        reinterpret_cast<const char*>(key_ids[i].identifier);
    std::string key_id(identifier, identifier + key_ids[i].identifier_size);
    CdmKeyInformation::KeyStatus status = ToCdmKeyStatus(key_statuses[i]);
    has_additional_usable_key |= (status == CdmKeyInformation::USABLE);

    keys_info.emplace_back(
        std::make_unique<CdmKeyInformation>(key_id, status, 0));
  }

  cdm->task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(&StarboardCdm::OnSessionKeyStatusChanged,
                     cdm->weak_factory_.GetWeakPtr(),
                     BytesToString(session_id, session_id_size),
                     std::move(keys_info), has_additional_usable_key));
}

// static
void StarboardCdm::OnServerCertificateUpdatedFunc(SbDrmSystem sb_drm,
                                                  void* context,
                                                  int ticket,
                                                  SbDrmStatus status,
                                                  const char* error_message) {
  DCHECK(context);
  StarboardCdm* cdm = static_cast<StarboardCdm*>(context);
  DCHECK_EQ(sb_drm, cdm->sb_drm_);

  cdm->task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(&StarboardCdm::OnServerCertificateUpdated,
                     cdm->weak_factory_.GetWeakPtr(), ticket, status,
                     error_message ? std::string(error_message) : ""));
}

// static
void StarboardCdm::OnSessionClosedFunc(SbDrmSystem sb_drm,
                                       void* context,
                                       const void* session_id,
                                       int session_id_size) {
  DCHECK(context);
  StarboardCdm* cdm = static_cast<StarboardCdm*>(context);
  DCHECK_EQ(sb_drm, cdm->sb_drm_);

  DCHECK(session_id != NULL);

  cdm->task_runner_->PostTask(
      FROM_HERE, base::BindOnce(&StarboardCdm::OnSessionClosed,
                                cdm->weak_factory_.GetWeakPtr(),
                                BytesToString(session_id, session_id_size)));
}

StarboardCdm::SessionTicketAndOptionalId::SessionTicketAndOptionalId(int ticket)
    : ticket(ticket), id(std::nullopt) {}

StarboardCdm::SessionTicketAndOptionalId::SessionTicketAndOptionalId(
    int ticket,
    const std::string& session_id)
    : ticket(ticket), id(session_id) {}

std::string StarboardCdm::SessionTicketAndOptionalId::ToString() const {
  return "{ticket: " +
         (ticket == kSbDrmTicketInvalid ? "kSbDrmTicketInvalid"
                                        : std::to_string(ticket)) +
         ", id: " + id.value_or("n/a") + "}";
}
}  // namespace media
