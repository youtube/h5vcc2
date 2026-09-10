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
"""Generates or updates a junit xml report from a test log file."""

import argparse
import datetime
import pathlib
import re
import xml.etree.ElementTree as ET

RUN_MARKER = '[ RUN      ]'
END_MARKERS = (
    '[       OK ]',
    '[  FAILED  ]',
    '[  SKIPPED ]',
)


def _extract_crashes(
    log_path: pathlib.Path,) -> list[tuple[str, str, str, str]]:
  """Identifies crashed/failed tests and their log output from a gtest log.

  Returns:
    A list of tuples `(test_suite, test_name, error_message, log_output)`.
  """
  if not log_path.is_file():
    return []

  with log_path.open('r', encoding='utf-8', errors='replace') as f:
    text = f.read()

  crashes_map = {}

  # 1. Look for runner-formatted crash blocks: "[CRASH] <test_name>:"
  matches = list(re.finditer(r'\[CRASH\]\s*([^:\n]+):', text))
  for i, m in enumerate(matches):
    test_name = m.group(1).strip()
    start_pos = m.end()
    end_pos = matches[i + 1].start() if i + 1 < len(matches) else len(text)
    block = text[start_pos:end_pos]
    if i == len(matches) - 1:
      summary_idx = block.find('tests, listed below:')
      if summary_idx != -1:
        line_start = block.rfind('\n', 0, summary_idx)
        if line_start != -1:
          block = block[:line_start]
    crashes_map[test_name] = ('Test crashed', block.strip())

  # 2. Look for summary block: "[  FAILED  ] <N> tests, listed below:"
  lines = text.splitlines()
  in_summary = False
  for line in lines:
    if re.search(r'\[\s*FAILED\s*\]\s+\d+\s+tests?,\s+listed below:', line):
      in_summary = True
      continue
    if in_summary:
      m = re.search(r'\[\s*FAILED\s*\]\s+(\S+)(?:\s+\((CRASHED)\))?', line)
      if m:
        test_name = m.group(1)
        msg = 'Test crashed' if m.group(2) else 'Test failed'
        if test_name not in crashes_map:
          crashes_map[test_name] = (msg, msg)
      elif (re.search(r'^\s*$', line) or 'FAILED TESTS' in line or
            '*****' in line):
        in_summary = False

  # 3. Look for an unclosed RUN_MARKER at the end of the log
  for i, line in reversed(list(enumerate(lines))):
    if RUN_MARKER in line:
      log = '\n'.join(lines[i:])
      if any(marker in log for marker in END_MARKERS):
        break
      test_name = line[len(RUN_MARKER):].strip()
      if test_name not in crashes_map:
        crashes_map[test_name] = ('Test crashed', log)
      break

  results = []
  for test_name, (msg, log) in crashes_map.items():
    suite, name = (
        test_name.split('.', 1) if '.' in test_name else
        ('UnknownSuite', test_name))
    results.append((suite, name, msg, log))

  return results


def update_or_create_junit_xml(xml_path: pathlib.Path,
                               crashes: list[tuple[str, str, str, str]]) -> int:
  """Updates an existing JUnit XML file or creates a new one with crashed tests.

  Returns:
    The number of crashed/failed tests newly added or present in the XML.
  """
  if not crashes:
    return 0

  now = datetime.datetime.now(
      datetime.timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ')

  root = None
  tree = None
  if xml_path.is_file():
    try:
      tree = ET.parse(xml_path)
      root = tree.getroot()
    except (ET.ParseError, OSError):
      root = None
      tree = None

  if root is None or root.tag not in ('testsuites', 'testsuite'):
    root = ET.Element(
        'testsuites',
        tests='0',
        failures='0',
        disabled='0',
        errors='0',
        time='0',
    )
    tree = ET.ElementTree(root)
  elif root.tag == 'testsuite':
    wrapper = ET.Element('testsuites')
    wrapper.append(root)
    root = wrapper
    tree = ET.ElementTree(root)

  suites_dict = {s.attrib.get('name', ''): s for s in root.findall('testsuite')}
  existing_tests = set()
  for s_name, s_elem in suites_dict.items():
    for tc in s_elem.findall('testcase'):
      existing_tests.add(f"{s_name}.{tc.attrib.get('name', '')}")

  added = 0
  for suite, name, msg, log in crashes:
    full_name = f'{suite}.{name}'
    if full_name in existing_tests:
      continue
    if suite not in suites_dict:
      s_elem = ET.SubElement(
          root,
          'testsuite',
          name=suite,
          tests='0',
          failures='0',
          disabled='0',
          errors='0',
          time='0',
          timestamp=now,
      )
      suites_dict[suite] = s_elem
    else:
      s_elem = suites_dict[suite]

    tc = ET.SubElement(s_elem, 'testcase', name=name, classname=suite, time='0')
    tag_name = 'error' if 'crash' in msg.lower() else 'failure'
    err = ET.SubElement(tc, tag_name, message=msg)
    err.text = f' {log} '
    existing_tests.add(full_name)
    added += 1

  # Recalculate suite and root statistics
  total_tests = 0
  total_failures = 0
  total_errors = 0
  for s_elem in root.findall('testsuite'):
    num_tests = len(s_elem.findall('testcase'))
    num_failures = sum(1 for tc in s_elem.findall('testcase')
                       if tc.find('failure') is not None)
    num_errors = sum(
        1 for tc in s_elem.findall('testcase') if tc.find('error') is not None)
    s_elem.set('tests', str(num_tests))
    s_elem.set('failures', str(num_failures))
    s_elem.set('errors', str(num_errors))
    total_tests += num_tests
    total_failures += num_failures
    total_errors += num_errors

  root.set('tests', str(total_tests))
  root.set('failures', str(total_failures))
  root.set('errors', str(total_errors))

  xml_path.parent.mkdir(parents=True, exist_ok=True)
  tree.write(xml_path, encoding='utf-8', xml_declaration=True)
  return added


if __name__ == '__main__':

  def main():
    parser = argparse.ArgumentParser(
        description='Converts a gtest crash log to a JUnit XML report.')
    parser.add_argument(
        'log_path', type=pathlib.Path, help='Path to the input log file.')
    parser.add_argument(
        'xml_path',
        type=pathlib.Path,
        help='Path to the output XML report file.',
    )
    args = parser.parse_args()

    crashes = _extract_crashes(args.log_path)
    if crashes:
      update_or_create_junit_xml(args.xml_path, crashes)

  main()
