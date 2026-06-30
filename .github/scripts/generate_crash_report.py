#!/usr/bin/env python3
# Copyright 2025 The Cobalt Authors. All Rights Reserved.
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
"""Generates a fake junit xml report from a log file (if available)."""

import html
import datetime
import pathlib
import argparse
from typing import Optional, Tuple

RUN_MARKER = '[ RUN      ]'
END_MARKERS = (
    '[       OK ]',
    '[  FAILED  ]',
    '[  SKIPPED ]',
)


def _extract_crash(log_path: pathlib.Path) -> Optional[Tuple[str, str, str]]:
  """
  Identifies the crashed test and its log output from a gtest log file.
  A crashed test will have a run marker but no end marker.

  Returns:
    A tuple `(test_suite, test_name, log_output_for_crashed_test)` or
    `None` if no crash is detected.
  """
  if not log_path.is_file():
    return None
  with log_path.open('r', encoding='utf-8', errors='replace') as f:
    lines = f.readlines()

  for i, line in reversed(list(enumerate(lines))):
    if RUN_MARKER in line:
      log = ''.join(lines[i:])
      # If the test crashed there are no end markers.
      if any(marker in log for marker in END_MARKERS):
        break
      test_name = line[len(RUN_MARKER):].strip()
      suite, name = test_name.split(
          '.', 1) if '.' in test_name else ('UnknownSuite', test_name)
      return suite, name, log
  return None


def write_junit_xml(xml_path: pathlib.Path, suite: str, name: str, log: str):
  # pylint: disable=line-too-long
  now = datetime.datetime.now(datetime.timezone.utc)
  with xml_path.open('w', encoding='utf-8') as f:
    f.write(f"""<?xml version="1.0" encoding="UTF-8"?>
<testsuites tests="1" failures="0" disabled="0" errors="1" time="0">
  <testsuite name="{html.escape(suite)}" tests="1" failures="0" disabled="0" errors="1" time="0" timestamp="{now.strftime('%Y-%m-%dT%H:%M:%SZ')}">
    <testcase name="{html.escape(name)}" classname="{html.escape(suite)}" time="0">
      <error message="Test crashed">
        <![CDATA[ {log} ]]>
      </error>
    </testcase>
  </testsuite>
</testsuites>
""")


if __name__ == '__main__':

  def main():
    parser = argparse.ArgumentParser(
        description='Converts a gtest crash log to a JUnit XML report.')
    parser.add_argument(
        'log_path', type=pathlib.Path, help='Path to the input log file.')
    parser.add_argument(
        'xml_path',
        type=pathlib.Path,
        help='Path to the output XML report file.')
    args = parser.parse_args()

    crash_info = _extract_crash(args.log_path)
    if crash_info:
      args.xml_path.parent.mkdir(parents=True, exist_ok=True)
      write_junit_xml(args.xml_path, *crash_info)

  main()
