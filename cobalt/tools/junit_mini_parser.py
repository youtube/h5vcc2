"""A simple util that prints failing tests from JUnit xml using only the Python
standard library."""
#!/usr/bin/env python3
#
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

import argparse
from typing import Dict, List, Tuple
import collections
import dataclasses
import json
import logging
import os
import sys
import xml.etree.ElementTree


@dataclasses.dataclass
class TestFailure:
  name: str
  message: str


def find_failing_tests(
    junit_xml_files: List[str]) -> Tuple[Dict[str, List[TestFailure]], int]:
  """Parses a list of JUnit XML files to find failing test cases.

  Args:
    junit_xml_files (list): A list of paths to JUnit XML files.

  Returns:
    A tuple of:
      - A map of test target -> list of TestFailure objects.
      - The number of XML files successfully parsed.
  """
  failing_tests = collections.defaultdict(list)
  num_parsed = 0
  for filename in junit_xml_files:
    try:
      tree = xml.etree.ElementTree.parse(filename)
    except (xml.etree.ElementTree.ParseError, OSError) as e:
      logging.error('Failed to parse %s: %s', filename, e)
      continue

    num_parsed += 1
    root = tree.getroot()
    for testsuite in root.findall('testsuite'):
      suite_name = testsuite.get('name', os.path.basename(filename))
      for testcase in testsuite.findall('testcase'):
        test_name = testcase.get('name')
        failures = testcase.findall('failure')
        errors = testcase.findall('error')
        if failures or errors:
          message = '\n'.join(
              case.attrib.get('message', '').strip() + '\n' +
              (case.text or '').strip() for case in failures + errors)
          rel_path = os.path.relpath(filename)
          failing_tests[rel_path].append(
              TestFailure(name=f'{suite_name}.{test_name}', message=message))
  return failing_tests, num_parsed


def main(argv: List[str]) -> int:
  """Main entry point.

  Args:
    argv (list): Command line arguments.

  Returns:
    Exit code.
  """
  parser = argparse.ArgumentParser(
      description='JUnit XML parser and filter generator.')
  parser.add_argument(
      '--mode',
      choices=['print', 'generate-filter'],
      default='print',
      help='Mode: print failing tests, or generate a retry filter JSON file.')
  parser.add_argument(
      '--target',
      help='Name of the test target (required in generate-filter mode).')
  parser.add_argument(
      '--output-dir',
      help='Directory to save the generated filter JSON (required in '
      'generate-filter mode).')
  parser.add_argument('xml_files', nargs='*', help='JUnit XML files to parse.')

  args = parser.parse_args(argv)

  if args.mode == 'generate-filter':
    if not args.target:
      parser.error('--target is required in generate-filter mode.')
    if not args.output_dir:
      parser.error('--output-dir is required in generate-filter mode.')

    failing_tests, num_parsed = find_failing_tests(args.xml_files)
    if num_parsed != len(args.xml_files) or num_parsed == 0:
      logging.warning(
          'Some or all JUnit XML files failed to parse or no files were'
          ' provided. Skipping retry filter generation.')
      return 0

    # Collect all unique failed tests.
    failed_test_names = set()
    for file_failures in failing_tests.values():
      for failure in file_failures:
        failed_test_names.add(failure.name)

    failed_test_names = sorted(list(failed_test_names))

    filter_data = {
        'failing_tests': [] if failed_test_names else ['*'],
        'tests_to_run': failed_test_names,
    }

    filter_file = os.path.join(args.output_dir, f'{args.target}_filter.json')
    os.makedirs(os.path.dirname(filter_file), exist_ok=True)
    try:
      with open(filter_file, 'w', encoding='utf-8') as f:
        json.dump(filter_data, f, indent=2)
      logging.info('Generated retry filter for %s at %s', args.target,
                   filter_file)
    except IOError as e:
      logging.error('Failed to write filter file %s: %s', filter_file, e)
      return 1
    return 0

  # Print mode
  failing_tests, num_parsed = find_failing_tests(args.xml_files)

  if failing_tests:
    logging.info('Failing Tests:')
    for target, test_status in sorted(failing_tests.items()):
      logging.info('%s', target)
      for failure in sorted(test_status, key=lambda x: x.name):
        logging.info('[  FAILED  ] %s', failure.name)
        if failure.message:
          logging.info('%s', failure.message)
      logging.info('')  # Blank line between targets
    return 1

  if args.xml_files and num_parsed > 0:
    logging.info('No failing tests found in the test results.')
  return 0


if __name__ == '__main__':
  logging.basicConfig(level=logging.INFO, format='%(message)s')
  sys.exit(main(sys.argv[1:]))
