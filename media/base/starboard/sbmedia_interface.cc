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

#include "media/base/starboard/sbmedia_interface.h"

#include <atomic>
#include <limits>
#include <string_view>

#include "base/check.h"
#include "base/feature_list.h"
#include "base/logging.h"
#include "base/no_destructor.h"
#include "base/strings/string_util.h"
#include "media/base/media_switches.h"

namespace media {

namespace {

std::atomic<SbMediaInterface*> g_sbmedia_interface_for_testing{nullptr};

}  // namespace

int ExtractMimeIntParam(std::string_view mime, std::string_view key) {
  size_t pos = 0;
  while ((pos = mime.find(key, pos)) != std::string_view::npos) {
    if (pos == 0 || mime[pos - 1] == ';' ||
        base::IsAsciiWhitespace(mime[pos - 1])) {
      size_t eq_pos = pos + key.length();
      while (eq_pos < mime.length() && base::IsAsciiWhitespace(mime[eq_pos])) {
        eq_pos++;
      }
      if (eq_pos < mime.length() && mime[eq_pos] == '=') {
        size_t val_start = eq_pos + 1;
        while (val_start < mime.length() &&
               (base::IsAsciiWhitespace(mime[val_start]) ||
                mime[val_start] == '"')) {
          val_start++;
        }
        int val = 0;
        size_t val_end = val_start;
        while (val_end < mime.length() && base::IsAsciiDigit(mime[val_end])) {
          int digit = mime[val_end] - '0';
          if (val > (std::numeric_limits<int>::max() - digit) / 10) {
            return std::numeric_limits<int>::max();
          }
          val = val * 10 + digit;
          val_end++;
        }
        if (val_end > val_start) {
          return val;
        }
      }
    }
    pos += key.length();
  }
  return 0;
}

bool Exceeds720p(const char* mime) {
  if (!mime) {
    return false;
  }
  std::string_view mime_str(mime);
  int height = ExtractMimeIntParam(mime_str, "height");
  if (height > 720) {
    return true;
  }
  int width = ExtractMimeIntParam(mime_str, "width");
  if (width > 1280) {
    return true;
  }
  return false;
}

SbMediaSupportType DefaultSbMediaInterface::CanPlayMimeAndKeySystem(
    const char* mime,
    const char* key_system) const {
  if (base::FeatureList::IsEnabled(kCobaltCapResolutionTo720p) &&
      Exceeds720p(mime)) {
    // TODO(cobalt, b/557961321): enable with constrained network link.
    LOG(INFO) << "CobaltCapResolutionTo720p: format exceeds 720p cap: "
              << (mime ? mime : "(null)");
    return kSbMediaSupportTypeNotSupported;
  }
  return SbMediaCanPlayMimeAndKeySystem(mime, key_system);
}

bool DefaultSbMediaInterface::CanChangeType(const char* current_mime,
                                            const char* new_mime) const {
  return SbMediaCanChangeType(current_mime, new_mime);
}

int DefaultSbMediaInterface::GetAudioOutputCount() const {
  return SbMediaGetAudioOutputCount();
}

bool DefaultSbMediaInterface::GetAudioConfiguration(
    int output_index,
    SbMediaAudioConfiguration* out_configuration) const {
  DCHECK(out_configuration);
  if (!out_configuration) {
    return false;
  }
  return SbMediaGetAudioConfiguration(output_index, out_configuration);
}

int DefaultSbMediaInterface::GetBufferAllocationUnit() const {
  return SbMediaGetBufferAllocationUnit();
}

int DefaultSbMediaInterface::GetAudioBufferBudget() const {
  return SbMediaGetAudioBufferBudget();
}

int64_t DefaultSbMediaInterface::GetBufferGarbageCollectionDurationThreshold()
    const {
  return SbMediaGetBufferGarbageCollectionDurationThreshold();
}

int DefaultSbMediaInterface::GetInitialBufferCapacity() const {
  return SbMediaGetInitialBufferCapacity();
}

bool DefaultSbMediaInterface::IsBufferPoolAllocateOnDemand() const {
  return SbMediaIsBufferPoolAllocateOnDemand();
}

int DefaultSbMediaInterface::GetVideoBufferBudget(SbMediaVideoCodec codec,
                                                  int resolution_width,
                                                  int resolution_height,
                                                  int bits_per_pixel) const {
  return SbMediaGetVideoBufferBudget(codec, resolution_width, resolution_height,
                                     bits_per_pixel);
}

SbMediaInterface* GetSbMediaInterface() {
  SbMediaInterface* testing_interface =
      g_sbmedia_interface_for_testing.load(std::memory_order_acquire);
  if (testing_interface) {
    return testing_interface;
  }
  static base::NoDestructor<DefaultSbMediaInterface> default_interface;
  return default_interface.get();
}

void SetSbMediaInterfaceForTesting(SbMediaInterface* interface) {
  g_sbmedia_interface_for_testing.store(interface, std::memory_order_release);
}

}  // namespace media
