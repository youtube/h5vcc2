#!/usr/bin/env python3
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
"""
This script orchestrates the code coverage process by:
1. Running gn.py with the --coverage flag to generate build arguments.
2. Running the code_coverage_tool.py to perform the coverage scan.
"""

import argparse
from dataclasses import dataclass
import json
import logging
import os
import subprocess
import sys

from cobalt.devinfra.github.test_filter import get_gtest_filter

logging.basicConfig(level=logging.INFO, format='[%(levelname)s] %(message)s')

PLATFORM_MAP = {
    'android-x86': 'android-arm',
    'android-x64': 'android-arm64',
}

GN_PY_REL_PATH = os.path.join('cobalt', 'build', 'gn.py')
COVERAGE_PY_REL_PATH = os.path.join('tools', 'code_coverage', 'coverage.py')
LLVM_RELEASE_ASSERTS_REL_PATH = os.path.join('third_party', 'llvm-build',
                                             'Release+Asserts')
LLVM_BIN_REL_PATH = os.path.join(LLVM_RELEASE_ASSERTS_REL_PATH, 'bin')
TEST_TARGETS_REL_PATH = os.path.join('cobalt', 'build', 'testing', 'targets')


@dataclass
class CoverageConfig:
  """Configuration for running code coverage."""
  src_root: str
  coverage_py_path: str
  llvm_bin_dir: str
  build_dir: str
  discovery_platform: str
  output_dir: str
  filters: list = None


def discover_targets(test_targets_dir, platform):
  """
  Discover test targets for a given platform by scanning the build testing
  targets directory.
  """
  discovered_targets = []
  logging.info('Scanning for targets in: %s', test_targets_dir)
  target_file = os.path.join(test_targets_dir, platform, 'test_targets.json')
  logging.info('Checking for target file: %s', target_file)
  if os.path.exists(target_file):
    with open(target_file, 'r', encoding='utf-8') as f:
      data = json.load(f)
      # The schema is a list of dicts, which pairs each target with its
      # executable (if defined).
      for entry in data:
        if isinstance(entry, dict) and 'target' in entry:
          discovered_targets.append(entry['target'])
  return discovered_targets


def run_command(command):
  """
  Run a command and handle errors.
  """
  logging.info('Running command: %s', ' '.join(command))
  try:
    subprocess.check_call(command)
    return True
  except subprocess.CalledProcessError as e:
    logging.error('Error running command: %s', e)
    return False


def parse_args():
  parser = argparse.ArgumentParser(
      description='Run the end-to-end code coverage process.')

  # Arguments for gn.py
  parser.add_argument(
      '-p',
      '--platform',
      required=True,
      help='The platform to build for (e.g., android-x86).')

  # Arguments for code_coverage_tool.py
  parser.add_argument(
      'targets',
      nargs='*',
      help='The test targets to run. If not specified, targets are '
      'auto-discovered.')
  parser.add_argument(
      '-o',
      '--output-dir',
      required=True,
      help='Output directory for generated artifacts.')
  parser.add_argument(
      '-f',
      '--filters',
      action='append',
      help='Directories or files to get code coverage for.')
  parser.add_argument(
      '--src-root',
      default=os.path.abspath(
          os.path.join(os.path.dirname(__file__), '..', '..', '..')),
      help='Absolute path to the root of the checkout.')

  return parser.parse_args()


def is_target_skipped(src_root, target, discovery_platform):
  """Check if a target should be skipped based on its filter file."""
  filter_dir = os.path.join(src_root, 'cobalt', 'testing', 'filters',
                            discovery_platform)
  filter_val = get_gtest_filter(filter_dir, target)
  return filter_val == '-*'


def run_coverage_for_target(config, target):
  """Run code coverage for a single target."""
  logging.info('--- Running coverage for target: %s ---', target)
  sanitized_target = target.replace(':', '_').replace('/', '_')
  target_output_dir = os.path.join(config.output_dir, sanitized_target)
  os.makedirs(target_output_dir, exist_ok=True)

  executable_name = target.split(':')[-1]
  command = os.path.join(config.build_dir, executable_name)

  # Add the test filters to the command.
  filter_dir = os.path.join(config.src_root, 'cobalt', 'testing', 'filters',
                            config.discovery_platform)
  filter_val = get_gtest_filter(filter_dir, target)
  if filter_val and filter_val != '*':
    command += f' --gtest_filter={filter_val}'

  coverage_command = [
      sys.executable,
      config.coverage_py_path,
      '--coverage-tools-dir',
      config.llvm_bin_dir,
      '-b',
      config.build_dir,
      '-o',
      target_output_dir,
      '-c',
      command,
  ]
  if config.filters:
    for f in config.filters:
      coverage_command.extend(['-f', f])
  coverage_command.extend(['--format=lcov', executable_name])

  return run_command(coverage_command)


def main():
  """
  Main function to run the end-to-end code coverage process.
  """
  args = parse_args()

  src_root = args.src_root
  gn_py_path = os.path.join(src_root, GN_PY_REL_PATH)
  coverage_py_path = os.path.join(src_root, COVERAGE_PY_REL_PATH)
  llvm_release_asserts_dir = os.path.join(src_root,
                                          LLVM_RELEASE_ASSERTS_REL_PATH)
  llvm_bin_dir = os.path.join(src_root, LLVM_BIN_REL_PATH)
  test_targets_dir = os.path.join(src_root, TEST_TARGETS_REL_PATH)

  expected_entries = [
      'bin', 'cr_build_revision', 'lib', 'llvmobjdump_build_revision'
  ]
  if not os.path.isdir(llvm_release_asserts_dir):
    logging.error('LLVM build directory not found at %s',
                  llvm_release_asserts_dir)
    logging.info('Please run `gclient sync --no-history -r $(git rev-parse @)` '
                 'to install them.')
    return 1

  actual_entries = os.listdir(llvm_release_asserts_dir)
  missing_entries = [
      entry for entry in expected_entries if entry not in actual_entries
  ]
  if missing_entries:
    logging.error('The LLVM build directory %s is incomplete.',
                  llvm_release_asserts_dir)
    logging.error('Missing entries: %s', ', '.join(missing_entries))
    logging.info('Please run `gclient sync --no-history -r $(git rev-parse @)` '
                 'to ensure a complete installation.')
    return 1

  discovery_platform = PLATFORM_MAP.get(args.platform, args.platform)

  targets = args.targets
  if not targets:
    logging.info('No targets specified, auto-discovering targets...')
    targets = discover_targets(test_targets_dir, discovery_platform)
    if not targets:
      logging.error('No test targets found for platform %s.', args.platform)
      return 1

  # Filter out targets that have all tests skipped via filter files.
  # Also explicitly skip blink_unittests as they have linking
  # issues in coverage builds. Also skip perftests as they are not
  # suitable for coverage.
  # TODO(b/475289841): Remove blink_unittest and
  # possibly blink_perftest filters.
  targets = [
      t for t in targets
      if not is_target_skipped(src_root, t, discovery_platform) and
      'blink_unittests' not in t and 'perftests' not in t
  ]

  if not targets:
    logging.info('All targets were filtered out.')
    return 0

  logging.info('Final target list: %s', ', '.join(targets))

  # 1. Run gn.py with --coverage
  build_dir = os.path.join('out', f'{args.platform}_devel')
  gn_command = [sys.executable, gn_py_path, '-p', args.platform, '--coverage']
  if not run_command(gn_command):
    logging.error('Error running gn.py')
    return 1

  # 2. Build all targets
  logging.info('Building targets: %s', ', '.join(targets))
  build_command = ['autoninja', '-C', build_dir
                  ] + [target.split(':')[-1] for target in targets]
  if not run_command(build_command):
    logging.error('Error building targets')
    return 1

  config = CoverageConfig(
      src_root=src_root,
      coverage_py_path=coverage_py_path,
      llvm_bin_dir=llvm_bin_dir,
      build_dir=build_dir,
      discovery_platform=discovery_platform,
      output_dir=args.output_dir,
      filters=args.filters)

  # 3. Run code_coverage_tool.py for each target
  # TODO(b/382508397): Implement parallelization using multiple Android devices.
  # This would involve:
  # 1. Accepting a list of device serials via a command-line argument.
  # 2. Validating that the provided serials are active and available.
  # 3. Distributing the test targets evenly across the available devices.
  # 4. Running the tests for each subset serially on its assigned device.
  # 5. Aggregating the resulting LCOV files for the final report.
  logging.info('Running coverage for %d targets...', len(targets))
  results = []
  for target in targets:
    results.append(run_coverage_for_target(config, target))

  if all(results):
    logging.info('Code coverage process completed successfully.')
  else:
    logging.error('Code coverage process completed with some errors.')

  return 0


if __name__ == '__main__':
  sys.exit(main())
