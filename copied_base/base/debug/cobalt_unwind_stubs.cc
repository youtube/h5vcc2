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

#include <stdint.h>
#include <unwind.h>

extern "C" {

_Unwind_Reason_Code _Unwind_Backtrace(_Unwind_Trace_Fn /*trace*/,
                                      void* /*trace_argument*/) {
  return _URC_END_OF_STACK;
}

_Unwind_VRS_Result _Unwind_VRS_Get(
    struct _Unwind_Context* /*context*/,
    _Unwind_VRS_RegClass /*regclass*/,
    uint32_t /*regno*/,
    _Unwind_VRS_DataRepresentation /*representation*/,
    void* /*valuep*/) {
  return _UVRSR_FAILED;
}

}  // extern "C"
