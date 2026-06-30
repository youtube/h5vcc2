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

#ifndef MEDIA_BASE_STARBOARD_IPC_PARAM_TRAITS_H_
#define MEDIA_BASE_STARBOARD_IPC_PARAM_TRAITS_H_

#include "base/pickle.h"
#include "ipc/ipc_param_traits.h"
#include "media/base/media_export.h"
#include "media/base/starboard/experimental_features.h"

namespace IPC {

template <>
struct MEDIA_EXPORT ParamTraits<media::ExperimentalFeatures::Value> {
  static void Write(base::Pickle* m,
                    const media::ExperimentalFeatures::Value& p);
  static bool Read(const base::Pickle* m,
                   base::PickleIterator* iter,
                   media::ExperimentalFeatures::Value* r);
  static void Log(const media::ExperimentalFeatures::Value& p, std::string* l);
};

template <>
struct MEDIA_EXPORT ParamTraits<media::ExperimentalFeatures> {
  static void Write(base::Pickle* m, const media::ExperimentalFeatures& p);
  static bool Read(const base::Pickle* m,
                   base::PickleIterator* iter,
                   media::ExperimentalFeatures* r);
  static void Log(const media::ExperimentalFeatures& p, std::string* l);
};

}  // namespace IPC

#endif  // MEDIA_BASE_STARBOARD_IPC_PARAM_TRAITS_H_
