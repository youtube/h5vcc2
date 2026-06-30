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

#include "media/base/starboard/ipc_param_traits.h"

#include <type_traits>
#include <variant>

#include "ipc/ipc_message_utils.h"

namespace IPC {

using media::ExperimentalFeatures;

static_assert(
    std::is_same_v<std::variant_alternative_t<0, ExperimentalFeatures::Value>,
                   int64_t>,
    "Index 0 of ExperimentalFeatures::Value must be int64_t");
static_assert(
    std::is_same_v<std::variant_alternative_t<1, ExperimentalFeatures::Value>,
                   std::string>,
    "Index 1 of ExperimentalFeatures::Value must be std::string");

void ParamTraits<ExperimentalFeatures::Value>::Write(
    base::Pickle* m,
    const ExperimentalFeatures::Value& p) {
  m->WriteInt(static_cast<int>(p.index()));
  std::visit([m](const auto& val) { WriteParam(m, val); }, p);
}

bool ParamTraits<ExperimentalFeatures::Value>::Read(
    const base::Pickle* m,
    base::PickleIterator* iter,
    ExperimentalFeatures::Value* r) {
  int index = 0;
  if (!iter->ReadInt(&index)) {
    return false;
  }
  if (index == 0) {
    int64_t val;
    if (!ReadParam(m, iter, &val)) {
      return false;
    }
    *r = val;
    return true;
  }
  if (index == 1) {
    std::string val;
    if (!ReadParam(m, iter, &val)) {
      return false;
    }
    *r = std::move(val);
    return true;
  }
  return false;
}

void ParamTraits<ExperimentalFeatures::Value>::Log(
    const ExperimentalFeatures::Value& p,
    std::string* l) {
  l->append("<ExperimentalFeatures::Value>");
}

void ParamTraits<ExperimentalFeatures>::Write(base::Pickle* m,
                                              const ExperimentalFeatures& p) {
  WriteParam(m, p.settings());
}

bool ParamTraits<ExperimentalFeatures>::Read(const base::Pickle* m,
                                             base::PickleIterator* iter,
                                             ExperimentalFeatures* r) {
  ExperimentalFeatures::Map settings;
  if (!ReadParam(m, iter, &settings)) {
    return false;
  }
  *r = ExperimentalFeatures(std::move(settings));
  return true;
}

void ParamTraits<ExperimentalFeatures>::Log(const ExperimentalFeatures& p,
                                            std::string* l) {
  l->append("<ExperimentalFeatures>");
}

}  // namespace IPC
