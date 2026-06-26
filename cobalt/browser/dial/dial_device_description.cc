// Copyright 2026 The Cobalt Authors. All Rights Reserved.
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

#include "cobalt/browser/dial/dial_device_description.h"

#include <algorithm>
#include <array>
#include <cstdint>

#include "base/containers/span.h"
#include "base/files/file.h"
#include "base/files/file_path.h"
#include "base/logging.h"
#include "base/rand_util.h"
#include "base/strings/stringprintf.h"
#include "base/task/current_thread.h"
#include "base/uuid.h"
#include "crypto/hash.h"
#include "starboard/common/system_property.h"
#include "starboard/configuration_constants.h"
#include "starboard/system.h"

namespace in_app_dial {

namespace {

// From C25:
// https://github.com/youtube/cobalt/blob/b8a6647a9aa54ffc55fe13f1e77a3d73dd30eea1/cobalt/network/dial/dial_system_config_starboard.cc#L24
// In C25 this file contained the random bytes used to generate a shorter UUID.
// In Chrobalt it is repurposed to contain
static constexpr const char kInAppDialUuidFilename[] = "upnp_udn";

// From C25:
// https://github.com/youtube/cobalt/blob/b8a6647a9aa54ffc55fe13f1e77a3d73dd30eea1/cobalt/network/dial/dial_system_config.cc#L36
// TODO: b/521275198 - This is not really a secret.
static constexpr char kSecret[] = "v=8FpigqfcvlM";

// From C25:
// https://github.com/youtube/cobalt/blob/b8a6647a9aa54ffc55fe13f1e77a3d73dd30eea1/cobalt/network/dial/dial_http_server.cc#L41
static constexpr const char kDdXmlTemplate[] =
    "<?xml version=\"1.0\"?>"
    "<root"
    " xmlns=\"urn:schemas-upnp-org:device-1-0\""
    " xmlns:r=\"urn:restful-tv-org:schemas:upnp-dd\">"
    "<specVersion>"
    "<major>1</major>"
    "<minor>0</minor>"
    "</specVersion>"
    "<device>"
    "<deviceType>urn:schemas-upnp-org:device:tvdevice:1</deviceType>"
    "<friendlyName>%s</friendlyName>"
    "<manufacturer>%s</manufacturer>"
    "<modelName>%s</modelName>"
    "<UDN>uuid:%s</UDN>"
    "</device>"
    "</root>";

std::array<uint8_t, base::Uuid::kGuidV4InputLength> GenerateUuidBytes() {
  std::array<uint8_t, base::Uuid::kGuidV4InputLength> uuid_bytes;
  base::RandBytes(uuid_bytes);
  return uuid_bytes;
}

// Attempts to read and return base::Uuid::kGuidV4InputLength bytes from a cache
// file. If that fails, generate a new sequence of bytes, write that to the
// cache and return it.
std::array<uint8_t, base::Uuid::kGuidV4InputLength> GetOrGenerateUuidBytes() {
  std::array<uint8_t, base::Uuid::kGuidV4InputLength> uuid_bytes;

  std::vector<char> cache_dir(kSbFileMaxPath + 1, 0);
  if (!SbSystemGetPath(kSbSystemPathCacheDirectory, cache_dir.data(),
                       kSbFileMaxPath)) {
    LOG(WARNING) << __func__ << " SbSystemGetPath() failed";
    return GenerateUuidBytes();
  }

  const auto cache_path =
      base::FilePath(cache_dir.data()).AppendASCII(kInAppDialUuidFilename);
  auto cache_file = base::File(cache_path, base::File::FLAG_OPEN_ALWAYS |
                                               base::File::FLAG_READ |
                                               base::File::FLAG_WRITE);
  if (!cache_file.IsValid()) {
    LOG(WARNING) << __func__ << " Unable to open or create path " << cache_path;
    return GenerateUuidBytes();
  }

  if (cache_file.ReadAndCheck(0, uuid_bytes)) {
    return uuid_bytes;
  }

  uuid_bytes = GenerateUuidBytes();
  cache_file.SetLength(0);
  if (!cache_file.WriteAndCheck(0, uuid_bytes)) {
    LOG(WARNING) << __func__ << " Unable to store device UUID to "
                 << cache_path;
  }

  return uuid_bytes;
}

// Returns a DIAL UUID following the same format as C25:
// https://github.com/youtube/cobalt/blob/b8a6647a9aa54ffc55fe13f1e77a3d73dd30eea1/cobalt/network/dial/dial_system_config.cc#L58
//
// TODO: b/521275198 - It is not clear if this is required or we can just use
// base::Uuid directly.
std::string GetOrCreateFormattedUuid() {
  crypto::hash::Hasher sha1_hasher(crypto::hash::HashKind::kSha1);
  sha1_hasher.Update(kSecret);
  sha1_hasher.Update(GetOrGenerateUuidBytes());
  std::array<uint8_t, crypto::hash::kSha1Size> sha1_digest;
  sha1_hasher.Finish(sha1_digest);

  // Now format the uuid as xxxxxxxx-yyyy-zzzzzzzz.
  // SHA-1 has a digest of 160 bits = 20bytes.
  // For full representation we need 40 hex chars, but we reduce
  // it down to 20 hex chars and then print it out.
  std::array<uint8_t, crypto::hash::kSha1Size / 2> reduced_digest;
  std::copy(sha1_digest.begin(), sha1_digest.begin() + reduced_digest.size(),
            reduced_digest.begin());
  static_assert(reduced_digest.size() * 2 == sha1_digest.size());
  // Use base::as_byte_span() for bounds checking.
  base::span<uint8_t> sha1_digest_view(sha1_digest);
  for (size_t i = 0; i < reduced_digest.size(); ++i) {
    reduced_digest[i] ^= sha1_digest_view[reduced_digest.size() + i];
  }

  return base::StringPrintf(
      "%02x%02x%02x%02x-%02x%02x-%02x%02x%02x%02x", reduced_digest[0],
      reduced_digest[1], reduced_digest[2], reduced_digest[3],
      reduced_digest[4], reduced_digest[5], reduced_digest[6],
      reduced_digest[7], reduced_digest[8], reduced_digest[9]);
}

}  // namespace

DialDeviceDescription::DialDeviceDescription()
    : DialDeviceDescription(
          GetOrCreateFormattedUuid(),
          starboard::GetSystemPropertyString(kSbSystemPropertyFriendlyName),
          starboard::GetSystemPropertyString(kSbSystemPropertyManufacturerName),
          starboard::GetSystemPropertyString(kSbSystemPropertyModelName)) {}

DialDeviceDescription::DialDeviceDescription(std::string_view uuid,
                                             std::string_view friendly_name,
                                             std::string_view manufacturer_name,
                                             std::string_view model_name)
    : formatted_uuid_(uuid),
      dd_xml_(base::StringPrintf(kDdXmlTemplate,
                                 friendly_name,
                                 manufacturer_name,
                                 model_name,
                                 uuid)) {
  CHECK(base::CurrentIOThread::IsSet());
  // 20 hex characters + 2 hyphens.
  CHECK_EQ(formatted_uuid_.size(), 22U);
}

DialDeviceDescription::~DialDeviceDescription() = default;

}  // namespace in_app_dial
