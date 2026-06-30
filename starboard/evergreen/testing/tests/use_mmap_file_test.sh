#!/bin/bash
#
# Copyright 2022 The Cobalt Authors. All Rights Reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# Unset the previous test's name and runner function.
unset TEST_NAME
unset TEST_FILE
unset -f run_test

TEST_NAME="UseMemoryMappedFile"
TEST_FILE="test.html"

function run_test() {
  if [[ "${SYSTEM_IMAGE_EXTENSION}" == ".lz4" ]]; then
    echo " MemoryMappedFile extension incompatible with compression, skipping"
    return 2
  fi

  clear_storage

  cycle_cobalt "file:///tests/${TEST_FILE}?channel=test" "${TEST_NAME}.2.log" "Using memory mapped file for the program header" "--loader_use_mmap_file --use_uncompressed_updates"

  if [[ $? -ne 0 ]]; then
    log "error" "Failed to run system installation"
    return 1
  fi

  cycle_cobalt "file:///tests/${TEST_FILE}?channel=test" "${TEST_NAME}.2.log" "Using memory mapped file for the program header" "--loader_use_mmap_file --evergreen_lite"

  if [[ $? -ne 0 ]]; then
    log "error" "Failed to run system installation"
    return 1
  fi
  return 0
}
