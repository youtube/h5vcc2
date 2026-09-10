// Copyright 2018 The Cobalt Authors. All Rights Reserved.
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

// Module Overview: Extended header for starboard/player.h on tvos
//
// Defines additional interface for URL-based SbPlayer on tvos.

#ifndef STARBOARD_TVOS_SHARED_MEDIA_URL_PLAYER_H_
#define STARBOARD_TVOS_SHARED_MEDIA_URL_PLAYER_H_

#include "starboard/player.h"

#ifdef __cplusplus
extern "C" {
#endif

// --- Types -----------------------------------------------------------------

// Additional SbPlayerError enums for URL-based player. Can be cast into
// SbPlayerError safely.
enum SbUrlPlayerError {
  kSbUrlPlayerErrorNetwork = kSbPlayerErrorMax,
  kSbUrlPlayerErrorSrcNotSupported,
};

// Extra player info for URL-based player.
typedef struct SbUrlPlayerExtraInfo {
  // The position of the buffer head, as precisely as possible, in microseconds.
  int64_t buffer_start_timestamp;

  // The known duration of the currently playing media buffer, in microseconds.
  int64_t buffer_duration;
} SbUrlPlayerExtraInfo;

// Callback for encrypted media init data encountered in media data.
typedef void (*SbPlayerEncryptedMediaInitDataEncounteredCB)(
    SbPlayer player,
    void* context,
    const char* init_data_type,
    const unsigned char* init_data,
    unsigned int init_data_length);

// --- Functions -------------------------------------------------------------

// Creates a URL-based SbPlayer that will be displayed on |window| for the
// specified URL |url|, acquiring all resources needed to operate it, and
// returning an opaque handle to it. The expectation is that a new player will
// be created and destroyed for every playback.
//
// In many ways this function is similar to SbPlayerCreate, but it is missing
// the input arguments related to the configuration and format of the audio and
// video stream, as well as the DRM system. The DRM system for a player created
// with SbUrlPlayerCreateWithUrl can be set after creation using
// SbUrlPlayerSetDrmSystem. Because the DRM system is not available at the time
// of SbPlayerCreateWithUrl, it takes in a callback,
// |encrypted_media_init_data_encountered_cb|, which is run when encrypted media
// initial data is encountered.
SbPlayer SbUrlPlayerCreate(const char* url,
                           SbWindow window,
                           SbPlayerStatusFunc player_status_func,
                           SbPlayerEncryptedMediaInitDataEncounteredCB
                               encrypted_media_init_data_encountered_cb,
                           SbPlayerErrorFunc player_error_func,
                           void* context);

// Sets the DRM system of a running URL-based SbPlayer created with
// SbUrlPlayerCreate. This may only be run once for a given URL-based
// SbPlayer.
void SbUrlPlayerSetDrmSystem(SbPlayer player, SbDrmSystem drm_system);

// Returns true if the given URL player output mode is supported by
// the platform.  If this function returns true, it is okay to call
// SbPlayerCreate() with the given |output_mode|.
bool SbUrlPlayerOutputModeSupported(SbPlayerOutputMode output_mode);

// Specified function to get extra information of an URL-based player. Writes
// URL-based player related information into |out_url_player_info|. This
// function may be called very frequently and is expected to be inexpensive.
//
// |player|: The URL-based player about which information is being retrieved.
// |out_url_player_info|: The url player related information.
void SbUrlPlayerGetExtraInfo(SbPlayer player,
                             SbUrlPlayerExtraInfo* out_url_player_info);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // STARBOARD_TVOS_SHARED_MEDIA_URL_PLAYER_H_
