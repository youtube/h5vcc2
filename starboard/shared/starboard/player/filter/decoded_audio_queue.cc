// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Modifications Copyright 2017 The Cobalt Authors. All Rights Reserved.
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

#include "starboard/shared/starboard/player/filter/decoded_audio_queue.h"

#include <algorithm>

#include "starboard/common/check_op.h"
#include "starboard/common/log.h"
#include "starboard/media.h"

namespace starboard {

DecodedAudioQueue::DecodedAudioQueue() {
  Clear();
}
DecodedAudioQueue::~DecodedAudioQueue() {}

void DecodedAudioQueue::Clear() {
  buffers_.clear();
  current_buffer_ = buffers_.begin();
  current_buffer_offset_ = 0;
  frames_ = 0;
}

void DecodedAudioQueue::Append(DecodedAudio&& decoded_audio) {
  // Update the |frames_| counter since we have added frames.
  frames_ += decoded_audio.frames();
  SB_CHECK_GT(frames_, 0);  // make sure it doesn't overflow.

  // Add the buffer to the queue. Inserting into deque invalidates all
  // iterators, so point to the first buffer.
  buffers_.push_back(std::move(decoded_audio));
  current_buffer_ = buffers_.begin();
}

int DecodedAudioQueue::ReadFrames(int frames,
                                  int dest_frame_offset,
                                  DecodedAudio* dest) {
  SB_DCHECK_GE(dest->frames(), frames + dest_frame_offset);
  return InternalRead(frames, true, 0, dest_frame_offset, dest);
}

int DecodedAudioQueue::PeekFrames(int frames,
                                  int source_frame_offset,
                                  int dest_frame_offset,
                                  DecodedAudio* dest) {
  SB_DCHECK_GE(dest->frames(), frames);
  return InternalRead(frames, false, source_frame_offset, dest_frame_offset,
                      dest);
}

void DecodedAudioQueue::SeekFrames(int frames) {
  // Perform seek only if we have enough bytes in the queue.
  SB_CHECK_LE(frames, frames_);
  [[maybe_unused]] int taken = InternalRead(frames, true, 0, 0, NULL);
  SB_DCHECK_EQ(taken, frames);
}

int DecodedAudioQueue::InternalRead(int frames,
                                    bool advance_position,
                                    int source_frame_offset,
                                    int dest_frame_offset,
                                    DecodedAudio* dest) {
  // Counts how many frames are actually read from the buffer queue.
  int taken = 0;
  BufferQueue::iterator current_buffer = current_buffer_;
  int current_buffer_offset = current_buffer_offset_;

  int frames_to_skip = source_frame_offset;
  while (taken < frames) {
    // |current_buffer| is valid since the first time this buffer is appended
    // with data. Make sure there is data to be processed.
    if (current_buffer == buffers_.end()) {
      break;
    }

    const DecodedAudio& buffer = *current_buffer;

    int remaining_frames_in_buffer = buffer.frames() - current_buffer_offset;

    if (frames_to_skip > 0) {
      // If there are frames to skip, do it first. May need to skip into
      // subsequent buffers.
      int skipped = std::min(remaining_frames_in_buffer, frames_to_skip);
      current_buffer_offset += skipped;
      frames_to_skip -= skipped;
    } else {
      // Find the right amount to copy from the current buffer. We shall copy no
      // more than |frames| frames in total and each single step copies no more
      // than the current buffer size.
      int copied = std::min(frames - taken, remaining_frames_in_buffer);

      // if |dest| is NULL, there's no need to copy.
      if (dest) {
        SB_DCHECK_EQ(buffer.channels(), dest->channels());
        if (dest->sample_type() == kSbMediaAudioSampleTypeFloat32) {
          const float* source = reinterpret_cast<const float*>(buffer.data()) +
                                buffer.channels() * current_buffer_offset;
          float* destination = reinterpret_cast<float*>(dest->data()) +
                               dest->channels() * (dest_frame_offset + taken);
          memcpy(destination, source, copied * dest->channels() * 4);
        } else {
          const int16_t* source =
              reinterpret_cast<const int16_t*>(buffer.data()) +
              buffer.channels() * current_buffer_offset;
          int16_t* destination = reinterpret_cast<int16_t*>(dest->data()) +
                                 dest->channels() * (dest_frame_offset + taken);
          memcpy(destination, source, copied * dest->channels() * 2);
        }
      }

      // Increase total number of frames copied, which regulates when to end
      // this loop.
      taken += copied;

      // We have read |copied| frames from the current buffer. Advance the
      // offset.
      current_buffer_offset += copied;
    }

    // Has the buffer has been consumed?
    if (current_buffer_offset == buffer.frames()) {
      // If we are at the last buffer, no more data to be copied, so stop.
      BufferQueue::iterator next = current_buffer + 1;
      if (next == buffers_.end()) {
        break;
      }

      // Advances the iterator.
      current_buffer = next;
      current_buffer_offset = 0;
    }
  }

  if (advance_position) {
    // Update the appropriate values since |taken| frames have been copied out.
    frames_ -= taken;
    SB_DCHECK_GE(frames_, 0);
    SB_DCHECK(current_buffer_ != buffers_.end() || frames_ == 0);

    // Remove any buffers before the current buffer as there is no going
    // backwards.
    buffers_.erase(buffers_.begin(), current_buffer);
    current_buffer_ = buffers_.begin();
    current_buffer_offset_ = current_buffer_offset;
  }

  return taken;
}

}  // namespace starboard
