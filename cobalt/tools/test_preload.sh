#!/bin/bash
# Copyright 2026 The Cobalt Authors. All Rights Reserved.
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

# Integration test for Cobalt Preload on Linux.
# Verifies that Cobalt starts in a hidden state and reveals on SIGCONT.

source cobalt/tools/test_common.sh

PORT=9223
LOG_FILE="preload_run.log"

RED='\033[0;31m'
GREEN='\033[0;32m'
NC='\033[0m'

function log {
  echo -e "${GREEN}[TEST] $(date +'%H:%M:%S') - $1${NC}"
}

parse_args "test_preload.sh" "$@"
check_running_processes "test_preload.sh"
run_build_if_needed

log "Starting cobalt_loader in preload mode with DevTools on port $PORT..."
rm -f $LOG_FILE

$EXECUTABLE --preload --remote-debugging-port=$PORT --no-sandbox --v=1 > $LOG_FILE 2>&1 &
COBALT_PID=$!

log "Launched PID: $COBALT_PID. Waiting for DevTools..."
sleep 5

log "Verifying initial preloaded state (Hidden & Not Focused)..."
# In preload mode, the app should be hidden and not have focus.
bash cobalt/tools/wait_for_state.sh "document.visibilityState" "hidden" $PORT 120 || exit 1
bash cobalt/tools/wait_for_state.sh "document.hasFocus()" "False" $PORT 120 || exit 1
bash cobalt/tools/wait_for_state.sh "window.location.search.includes('launch=preload')" "True" $PORT 120 || exit 1

log "Sending SIGCONT to reveal application..."
kill -SIGCONT $COBALT_PID

log "Verifying revealed state (Visible & Focused)..."
bash cobalt/tools/wait_for_state.sh "document.visibilityState" "visible" $PORT 120 || exit 1
bash cobalt/tools/wait_for_state.sh "document.hasFocus()" "True" $PORT 120 || exit 1

log "Sending SIGPWR (STOP)..."
kill -SIGPWR $COBALT_PID
sleep 5

# Wait for process to exit
log "Waiting for Cobalt to exit..."
wait $COBALT_PID 2>/dev/null
EXIT_CODE=$?

log "Cobalt exited with code: $EXIT_CODE"

if kill -0 $COBALT_PID 2>/dev/null; then
  STAT=$(ps -p $COBALT_PID -o stat= | tr -d ' ')
  if [[ "$STAT" != "Z"* ]]; then
    echo "FAILURE: Cobalt (PID: $COBALT_PID) did not stop after SIGPWR. State: $STAT"
    echo "[TEST] Cleaning up Cobalt (PID: $COBALT_PID)..."
    kill -9 $COBALT_PID
    exit 1
  fi
fi

if [ $EXIT_CODE -ne 0 ] && [ $EXIT_CODE -ne 143 ]; then
  # 143 is SIGTERM exit code, which is acceptable for SIGPWR/SIGTERM shutdown.
  echo -e "${RED}FAILURE: Cobalt exited with non-zero code $EXIT_CODE${NC}"
  exit 1
fi

log "SUCCESS: Preload and reveal verified!"

exit 0
