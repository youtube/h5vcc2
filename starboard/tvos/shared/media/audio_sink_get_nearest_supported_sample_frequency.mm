// Copyright 2017 The Cobalt Authors. All Rights Reserved.
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

#include "starboard/audio_sink.h"

int SbAudioSinkGetNearestSupportedSampleFrequency(int sampling_frequency_hz) {
  if (sampling_frequency_hz < 8000) {
    return 8000;
  }
  if (sampling_frequency_hz > 48000) {
    return 48000;
  }
  // TODO: Retrieve the nearest supported frequency from AudioManager.
  return sampling_frequency_hz;
}
