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

#ifndef MEDIA_BASE_STARBOARD_SBMEDIA_INTERFACE_H_
#define MEDIA_BASE_STARBOARD_SBMEDIA_INTERFACE_H_

#include <stdint.h>

#include <string_view>

#include "media/base/media_export.h"
#include "starboard/media.h"

namespace media {

// SbMediaInterface abstracts the Starboard media C APIs to facilitate testing
// and mocking of media features.
//
// Lifetime and ownership:
// The production instance is a global singleton that is created on demand and
// lives for the lifetime of the process. Custom test implementations can be
// injected via SetSbMediaInterfaceForTesting().
//
// Threading model:
// Implementations must be thread-safe and callable from any thread.
class MEDIA_EXPORT SbMediaInterface {
 public:
  SbMediaInterface(const SbMediaInterface&) = delete;
  SbMediaInterface& operator=(const SbMediaInterface&) = delete;

  virtual ~SbMediaInterface() = default;

  virtual SbMediaSupportType CanPlayMimeAndKeySystem(
      const char* mime,
      const char* key_system) const = 0;
  virtual bool CanChangeType(const char* current_mime,
                             const char* new_mime) const = 0;
  virtual int GetAudioOutputCount() const = 0;
  virtual bool GetAudioConfiguration(
      int output_index,
      SbMediaAudioConfiguration* out_configuration) const = 0;
  virtual int GetBufferAllocationUnit() const = 0;
  virtual int GetAudioBufferBudget() const = 0;
  virtual int64_t GetBufferGarbageCollectionDurationThreshold() const = 0;
  virtual int GetInitialBufferCapacity() const = 0;
  virtual bool IsBufferPoolAllocateOnDemand() const = 0;
  virtual int GetVideoBufferBudget(SbMediaVideoCodec codec,
                                   int resolution_width,
                                   int resolution_height,
                                   int bits_per_pixel) const = 0;

 protected:
  SbMediaInterface() = default;
};

// DefaultSbMediaInterface is the production implementation of SbMediaInterface
// that forwards all calls directly to the corresponding Starboard C functions.
//
// Lifetime and ownership:
// Managed as a base::NoDestructor static singleton in GetSbMediaInterface().
//
// Threading model:
// Thread-safe as it delegates to thread-safe Starboard C APIs.
class MEDIA_EXPORT DefaultSbMediaInterface final : public SbMediaInterface {
 public:
  SbMediaSupportType CanPlayMimeAndKeySystem(
      const char* mime,
      const char* key_system) const override;
  bool CanChangeType(const char* current_mime,
                     const char* new_mime) const override;
  int GetAudioOutputCount() const override;
  bool GetAudioConfiguration(
      int output_index,
      SbMediaAudioConfiguration* out_configuration) const override;
  int GetBufferAllocationUnit() const override;
  int GetAudioBufferBudget() const override;
  int64_t GetBufferGarbageCollectionDurationThreshold() const override;
  int GetInitialBufferCapacity() const override;
  bool IsBufferPoolAllocateOnDemand() const override;
  int GetVideoBufferBudget(SbMediaVideoCodec codec,
                           int resolution_width,
                           int resolution_height,
                           int bits_per_pixel) const override;
};

// Returns a pointer to the global SbMediaInterface instance.
// By default, this returns a DefaultSbMediaInterface instance.
MEDIA_EXPORT SbMediaInterface* GetSbMediaInterface();

// Sets a custom SbMediaInterface for testing.
MEDIA_EXPORT void SetSbMediaInterfaceForTesting(SbMediaInterface* interface);

// Parses an integer parameter value from a MIME string (e.g. "width",
// "height").
MEDIA_EXPORT int ExtractMimeIntParam(std::string_view mime,
                                     std::string_view key);

// Returns true if the MIME string specifies width > 1280 or height > 720.
MEDIA_EXPORT bool Exceeds720p(const char* mime);

}  // namespace media

#endif  // MEDIA_BASE_STARBOARD_SBMEDIA_INTERFACE_H_
