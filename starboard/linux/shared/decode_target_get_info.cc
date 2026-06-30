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

#include <string.h>

#include "starboard/common/memory.h"
#include "starboard/decode_target.h"
#include "starboard/linux/shared/decode_target_internal.h"

// TODO: Consider unifying this with that of android, since
// the only part that changes is the info struct size.
bool SbDecodeTargetGetInfo(SbDecodeTarget decode_target,
                           SbDecodeTargetInfo* out_info) {
  if (!starboard::MemoryIsZero(out_info, sizeof(*out_info))) {
    SB_DCHECK(false) << "out_info must be zeroed out.";
    return false;
  }

  memcpy(out_info, &decode_target->data->info, sizeof(*out_info));

  return true;
}
