#!/usr/bin/env python3
# Copyright 2026 The Cobalt Authors. All Rights. Reserved.
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
Tests for the run_coverage.py script.
"""

import argparse
import os
import sys
import unittest
from unittest import mock
from cobalt.tools.code_coverage import run_coverage


class RunCoverageTest(unittest.TestCase):
  """
  Tests for the end-to-end code coverage script.
  """

  @mock.patch('logging.info')
  @mock.patch('logging.error')
  @mock.patch(
      'os.listdir',
      return_value=[
          'bin', 'cr_build_revision', 'lib', 'llvmobjdump_build_revision'
      ])
  @mock.patch('os.path.isdir', return_value=True)
  @mock.patch('os.makedirs')
  @mock.patch(
      'cobalt.tools.code_coverage.run_coverage.run_command', return_value=True)
  @mock.patch('argparse.ArgumentParser')
  def test_run_coverage_success_with_targets(  # pylint: disable=too-many-positional-arguments
      self, mock_arg_parser, mock_run_command, mock_makedirs, mock_isdir,
      mock_listdir, mock_logging_error, mock_logging_info):
    """
    Tests successful execution when targets are provided as arguments.
    """
    del mock_isdir, mock_listdir  # Unused argument.
    del mock_logging_error, mock_logging_info  # Unused argument.
    platform = 'android-x86'
    output_dir = 'out/coverage_report'
    filters = 'cobalt/browser'
    target = 'cobalt:unittests'
    sanitized_target = 'cobalt_unittests'
    executable_name = 'unittests'
    build_dir = f'out/{platform}_devel'
    expected_command = os.path.join(build_dir, executable_name)
    target_output_dir = os.path.join(output_dir, sanitized_target)

    mock_parser = mock_arg_parser.return_value
    mock_parser.parse_args.return_value = argparse.Namespace(
        platform=platform,
        output_dir=output_dir,
        targets=[target],
        filters=[filters],
        src_root='/test/src/root')

    result = run_coverage.main()

    mock_makedirs.assert_called_once_with(target_output_dir, exist_ok=True)
    expected_gn_command = [
        sys.executable, '/test/src/root/cobalt/build/gn.py', '-p', platform,
        '--coverage'
    ]
    expected_build_command = ['autoninja', '-C', build_dir, executable_name]
    expected_coverage_command = [
        sys.executable,
        '/test/src/root/tools/code_coverage/coverage.py',
        '--coverage-tools-dir',
        '/test/src/root/third_party/llvm-build/Release+Asserts/bin',
        '-b',
        build_dir,
        '-o',
        target_output_dir,
        '-c',
        expected_command,
        '-f',
        filters,
        '--format=lcov',
        executable_name,
    ]
    mock_run_command.assert_has_calls([
        mock.call(expected_gn_command),
        mock.call(expected_build_command),
        mock.call(expected_coverage_command)
    ])
    self.assertEqual(result, 0)

  @mock.patch('logging.info')
  @mock.patch('logging.error')
  @mock.patch(
      'os.listdir',
      return_value=[
          'bin', 'cr_build_revision', 'lib', 'llvmobjdump_build_revision'
      ])
  @mock.patch('os.path.isdir', return_value=True)
  @mock.patch('os.makedirs')
  @mock.patch(
      'cobalt.tools.code_coverage.run_coverage.discover_targets',
      return_value=['target1', 'target2'])
  @mock.patch(
      'cobalt.tools.code_coverage.run_coverage.run_command', return_value=True)
  @mock.patch('argparse.ArgumentParser')
  def test_run_coverage_success_no_targets(  # pylint: disable=too-many-positional-arguments
      self, mock_arg_parser, mock_run_command, mock_discover_targets,
      mock_makedirs, mock_isdir, mock_listdir, mock_logging_error,
      mock_logging_info):
    """
    Tests successful execution when targets are auto-discovered.
    """
    del mock_makedirs, mock_isdir  # Unused argument.
    del mock_listdir, mock_logging_error, mock_logging_info  # Unused argument.
    platform = 'android-x86'
    output_dir = 'out/coverage_report'
    build_dir = f'out/{platform}_devel'

    mock_parser = mock_arg_parser.return_value
    mock_parser.parse_args.return_value = argparse.Namespace(
        platform=platform,
        output_dir=output_dir,
        targets=[],
        filters=None,
        src_root='/test/src/root')

    result = run_coverage.main()

    mock_discover_targets.assert_called_once_with(
        '/test/src/root/cobalt/build/testing/targets', 'android-arm')
    expected_gn_command = [
        sys.executable, '/test/src/root/cobalt/build/gn.py', '-p', platform,
        '--coverage'
    ]
    expected_build_command = [
        'autoninja', '-C', build_dir, 'target1', 'target2'
    ]
    expected_calls = [
        mock.call(expected_gn_command),
        mock.call(expected_build_command)
    ]
    for target in ['target1', 'target2']:
      target_output_dir = os.path.join(output_dir, target)
      expected_command = os.path.join(build_dir, target)
      expected_coverage_command = [
          sys.executable,
          '/test/src/root/tools/code_coverage/coverage.py',
          '--coverage-tools-dir',
          '/test/src/root/third_party/llvm-build/Release+Asserts/bin',
          '-b',
          build_dir,
          '-o',
          target_output_dir,
          '-c',
          expected_command,
          '--format=lcov',
          target,
      ]
      expected_calls.append(mock.call(expected_coverage_command))

    mock_run_command.assert_has_calls(expected_calls)
    self.assertEqual(result, 0)

  @mock.patch('logging.info')
  @mock.patch('logging.error')
  @mock.patch(
      'os.listdir',
      return_value=[
          'bin', 'cr_build_revision', 'lib', 'llvmobjdump_build_revision'
      ])
  @mock.patch('os.path.isdir', return_value=True)
  @mock.patch(
      'cobalt.tools.code_coverage.run_coverage.discover_targets',
      return_value=[])
  @mock.patch('argparse.ArgumentParser')
  def test_no_targets_found(  # pylint: disable=too-many-positional-arguments
      self, mock_arg_parser, mock_discover_targets, mock_isdir, mock_listdir,
      mock_logging_error, mock_logging_info):
    """
    Tests that the script exits gracefully if no targets are found.
    """
    del mock_isdir, mock_listdir, mock_logging_info
    platform = 'android-x86'
    mock_parser = mock_arg_parser.return_value
    mock_parser.parse_args.return_value = argparse.Namespace(
        platform=platform,
        output_dir='out/report',
        targets=[],
        filters=None,
        src_root='/test/src/root')

    result = run_coverage.main()

    mock_discover_targets.assert_called_once_with(
        '/test/src/root/cobalt/build/testing/targets', 'android-arm')
    mock_logging_error.assert_any_call('No test targets found for platform %s.',
                                       platform)
    self.assertEqual(result, 1)

  @mock.patch('logging.info')
  @mock.patch('logging.error')
  @mock.patch(
      'os.listdir',
      return_value=[
          'bin', 'cr_build_revision', 'lib', 'llvmobjdump_build_revision'
      ])
  @mock.patch('os.path.isdir', return_value=True)
  @mock.patch(
      'cobalt.tools.code_coverage.run_coverage.discover_targets',
      return_value=['target1'])
  @mock.patch(
      'cobalt.tools.code_coverage.run_coverage.run_command', return_value=False)
  @mock.patch('argparse.ArgumentParser')
  def test_gn_py_failure(  # pylint: disable=too-many-positional-arguments
      self, mock_arg_parser, mock_run_command, mock_discover_targets,
      mock_isdir, mock_listdir, mock_logging_error, mock_logging_info):
    """
    Tests that the script exits gracefully if gn.py fails.
    """
    del mock_isdir, mock_listdir, mock_logging_info  # Unused arguments.
    platform = 'android-x86'
    mock_parser = mock_arg_parser.return_value
    mock_parser.parse_args.return_value = argparse.Namespace(
        platform=platform,
        output_dir='out/report',
        targets=[],
        filters=None,
        src_root='/test/src/root')

    result = run_coverage.main()

    mock_discover_targets.assert_called_once_with(
        '/test/src/root/cobalt/build/testing/targets', 'android-arm')
    mock_run_command.assert_called_once()
    mock_logging_error.assert_any_call('Error running gn.py')
    self.assertEqual(result, 1)

  @mock.patch('logging.info')
  @mock.patch('logging.error')
  @mock.patch(
      'os.listdir',
      return_value=[
          'bin', 'cr_build_revision', 'lib', 'llvmobjdump_build_revision'
      ])
  @mock.patch('os.path.isdir', return_value=True)
  @mock.patch('os.makedirs')
  @mock.patch(
      'cobalt.tools.code_coverage.run_coverage.run_command',
      side_effect=[True, True, False])
  @mock.patch('argparse.ArgumentParser')
  def test_coverage_tool_failure(  # pylint: disable=too-many-positional-arguments
      self, mock_arg_parser, mock_run_command, mock_makedirs, mock_isdir,
      mock_listdir, mock_logging_error, mock_logging_info):
    """
    Tests that the script exits gracefully if code_coverage_tool.py fails.
    """
    del mock_makedirs, mock_isdir  # Unused argument.
    del mock_listdir, mock_logging_info  # Unused argument.
    target = 'target'
    mock_parser = mock_arg_parser.return_value
    mock_parser.parse_args.return_value = argparse.Namespace(
        platform='android-x86',
        output_dir='out/report',
        targets=[target],
        filters=None,
        src_root='/test/src/root')

    result = run_coverage.main()

    self.assertEqual(mock_run_command.call_count, 3)
    mock_logging_error.assert_any_call(
        'Code coverage process completed with some errors.')
    self.assertEqual(result, 0)  # Should continue to next target

  @mock.patch('os.path.exists')
  @mock.patch('builtins.open', new_callable=mock.mock_open)
  def test_discover_targets_android_platforms(self, mock_open, mock_exists):
    """
    Tests that discover_targets correctly finds test_targets.json
    for Android platforms.
    """
    # Simulate the existence of test_targets.json for
    # android-arm and android-arm64
    mock_exists.side_effect = lambda path: (
        'android-arm/test_targets.json' in path or
        'android-arm64/test_targets.json' in path)

    # Mock the content of test_targets.json for android-arm
    mock_arm_json_content = ('[{"target": "android_arm_target1"}, '
                             '{"target": "android_arm_target2"}]')
    # Mock the content of test_targets.json for android-arm64
    mock_arm64_json_content = ('[{"target": "android_arm64_target1"}, '
                               '{"target": "android_arm64_target2"}]')

    def open_side_effect(file_path, *args, **kwargs):
      del args, kwargs  # unused
      if 'android-arm/test_targets.json' in file_path:
        mock_file = mock.mock_open(read_data=mock_arm_json_content).return_value
      elif 'android-arm64/test_targets.json' in file_path:
        mock_file = mock.mock_open(
            read_data=mock_arm64_json_content).return_value
      else:
        mock_file = mock.DEFAULT
      return mock_file

    mock_open.side_effect = open_side_effect

    test_targets_dir = '/test/src/root/cobalt/build/testing/targets'

    # Test for android-arm (remapped from android-x86)
    targets_arm = run_coverage.discover_targets(test_targets_dir, 'android-arm')
    self.assertEqual(targets_arm,
                     ['android_arm_target1', 'android_arm_target2'])
    mock_exists.assert_any_call(
        os.path.join(test_targets_dir, 'android-arm', 'test_targets.json'))

    # Test for android-arm64 (remapped from android-x64)
    targets_arm64 = run_coverage.discover_targets(test_targets_dir,
                                                  'android-arm64')
    self.assertEqual(targets_arm64,
                     ['android_arm64_target1', 'android_arm64_target2'])
    mock_exists.assert_any_call(
        os.path.join(test_targets_dir, 'android-arm64', 'test_targets.json'))

  @mock.patch('logging.info')
  @mock.patch('logging.error')
  @mock.patch(
      'os.listdir',
      return_value=[
          'bin', 'cr_build_revision', 'lib', 'llvmobjdump_build_revision'
      ])
  @mock.patch('os.path.isdir', return_value=True)
  @mock.patch(
      'cobalt.tools.code_coverage.run_coverage.discover_targets',
      return_value=['target1', 'target2'])
  @mock.patch(
      'cobalt.tools.code_coverage.run_coverage.run_command', return_value=True)
  @mock.patch('argparse.ArgumentParser')
  def test_platform_reuse(  # pylint: disable=too-many-positional-arguments
      self, mock_arg_parser, mock_run_command, mock_discover_targets,
      mock_isdir, mock_listdir, mock_logging_error, mock_logging_info):
    """
    Tests that android-x86 and android-x64 platforms reuse android-arm and
    android-arm64 test targets respectively.
    """
    del mock_run_command, mock_isdir  # Unused argument.
    del mock_listdir, mock_logging_error, mock_logging_info  # Unused argument.
    # Test android-x86 reuses android-arm
    mock_parser = mock_arg_parser.return_value
    mock_parser.parse_args.return_value = argparse.Namespace(
        platform='android-x86',
        output_dir='out/report',
        targets=[],
        filters=None,
        src_root='/test/src/root')
    run_coverage.main()
    mock_discover_targets.assert_called_with(
        '/test/src/root/cobalt/build/testing/targets', 'android-arm')

    # Test android-x64 reuses android-arm64
    mock_parser.parse_args.return_value = argparse.Namespace(
        platform='android-x64',
        output_dir='out/report',
        targets=[],
        filters=None,
        src_root='/test/src/root')
    run_coverage.main()
    mock_discover_targets.assert_called_with(
        '/test/src/root/cobalt/build/testing/targets', 'android-arm64')

  @mock.patch('logging.info')
  @mock.patch('logging.error')
  @mock.patch(
      'os.listdir',
      return_value=[
          'bin', 'cr_build_revision', 'lib', 'llvmobjdump_build_revision'
      ])
  @mock.patch('os.path.isdir', return_value=True)
  @mock.patch('os.makedirs')
  @mock.patch('os.path.exists', return_value=True)
  @mock.patch('builtins.open', new_callable=mock.mock_open)
  @mock.patch(
      'cobalt.tools.code_coverage.run_coverage.run_command', return_value=True)
  @mock.patch('argparse.ArgumentParser')
  def test_run_coverage_with_test_filters(  # pylint: disable=too-many-positional-arguments
      self, mock_arg_parser, mock_run_command, mock_open, mock_exists,
      mock_makedirs, mock_isdir, mock_listdir, mock_logging_error,
      mock_logging_info):
    """
    Tests successful execution when test filters are applied.
    """
    del mock_exists, mock_isdir  # Unused arguments.
    del mock_listdir, mock_logging_error, mock_logging_info  # Unused arguments.
    platform = 'android-x86'
    output_dir = 'out/coverage_report'
    target = 'cobalt:unittests'
    sanitized_target = 'cobalt_unittests'
    executable_name = 'unittests'
    build_dir = f'out/{platform}_devel'
    base_command = os.path.join(build_dir, executable_name)
    target_output_dir = os.path.join(output_dir, sanitized_target)
    filter_file_path = os.path.join('/test/src/root', 'cobalt', 'testing',
                                    'filters', 'android-arm',
                                    f'{executable_name}.filter')
    mock_filter_content = '-Test.Fails\n-Test.Crashes\n'
    mock_open.side_effect = lambda path, *args, **kwargs: mock.mock_open(
        read_data=mock_filter_content
    ).return_value if path == filter_file_path else mock.DEFAULT

    mock_parser = mock_arg_parser.return_value
    mock_parser.parse_args.return_value = argparse.Namespace(
        platform=platform,
        output_dir=output_dir,
        targets=[target],
        filters=None,
        src_root='/test/src/root')

    result = run_coverage.main()

    mock_makedirs.assert_called_once_with(target_output_dir, exist_ok=True)
    expected_gn_command = [
        sys.executable, '/test/src/root/cobalt/build/gn.py', '-p', platform,
        '--coverage'
    ]
    expected_build_command = ['autoninja', '-C', build_dir, executable_name]
    expected_coverage_command = [
        sys.executable,
        '/test/src/root/tools/code_coverage/coverage.py',
        '--coverage-tools-dir',
        '/test/src/root/third_party/llvm-build/Release+Asserts/bin',
        '-b',
        build_dir,
        '-o',
        target_output_dir,
        '-c',
        f'{base_command} --gtest_filter=-Test.Fails:Test.Crashes',
        '--format=lcov',
        executable_name,
    ]
    mock_run_command.assert_has_calls([
        mock.call(expected_gn_command),
        mock.call(expected_build_command),
        mock.call(expected_coverage_command)
    ])
    self.assertEqual(result, 0)

  @mock.patch('logging.info')
  @mock.patch('logging.error')
  @mock.patch(
      'os.listdir',
      return_value=[
          'bin', 'cr_build_revision', 'lib', 'llvmobjdump_build_revision'
      ])
  @mock.patch('os.path.isdir', return_value=True)
  @mock.patch(
      'cobalt.tools.code_coverage.run_coverage.run_command', return_value=True)
  @mock.patch('argparse.ArgumentParser')
  def test_run_coverage_skips_fully_filtered_target(  # pylint: disable=too-many-positional-arguments
      self, mock_arg_parser, mock_run_command, mock_isdir, mock_listdir,
      mock_logging_error, mock_logging_info):
    """
    Tests that a target is skipped if all its tests are filtered out.
    """
    del mock_isdir, mock_listdir  # Unused arguments.
    del mock_logging_error, mock_logging_info  # Unused arguments.
    platform = 'android-x86'
    target = 'cobalt:unittests'

    mock_parser = mock_arg_parser.return_value
    mock_parser.parse_args.return_value = argparse.Namespace(
        platform=platform,
        output_dir='out/report',
        targets=[target],
        filters=None,
        src_root='/test/src/root')

    # Patch at the source where 'main' will find it.
    with mock.patch(
        'cobalt.tools.code_coverage.run_coverage.is_target_skipped',
        return_value=True):
      result = run_coverage.main()

    # NOTHING should run because the only target was filtered out before any
    # external commands.
    self.assertEqual(mock_run_command.call_count, 0)
    self.assertEqual(result, 0)

  @mock.patch('logging.info')
  @mock.patch('logging.error')
  @mock.patch('os.path.isdir', return_value=False)
  @mock.patch('argparse.ArgumentParser')
  def test_llvm_dir_missing(self, mock_arg_parser, mock_isdir,
                            mock_logging_error, mock_logging_info):
    """
    Tests that the script exits with an error if the LLVM directory is missing.
    """
    del mock_isdir  # Unused argument.
    mock_parser = mock_arg_parser.return_value
    mock_parser.parse_args.return_value = argparse.Namespace(
        platform='android-x86',
        output_dir='out/report',
        targets=[],
        filters=None,
        src_root='/test/src/root')
    result = run_coverage.main()
    self.assertEqual(result, 1)
    mock_logging_error.assert_any_call(
        'LLVM build directory not found at %s',
        '/test/src/root/third_party/llvm-build/Release+Asserts')
    mock_logging_info.assert_any_call(
        'Please run `gclient sync --no-history -r $(git rev-parse @)` '
        'to install them.')

  @mock.patch('logging.info')
  @mock.patch('logging.error')
  @mock.patch('os.path.isdir', return_value=True)
  @mock.patch('os.listdir', return_value=['bin'])  # Incomplete entries
  @mock.patch('argparse.ArgumentParser')
  def test_llvm_dir_incomplete(  # pylint: disable=too-many-positional-arguments
      self, mock_arg_parser, mock_listdir, mock_isdir, mock_logging_error,
      mock_logging_info):
    """
    Tests that the script exits with an error if the
    LLVM directory is incomplete.
    """
    del mock_isdir, mock_listdir  # Unused argument.
    mock_parser = mock_arg_parser.return_value
    mock_parser.parse_args.return_value = argparse.Namespace(
        platform='android-x86',
        output_dir='out/report',
        targets=[],
        filters=None,
        src_root='/test/src/root')
    result = run_coverage.main()
    self.assertEqual(result, 1)
    mock_logging_error.assert_any_call(
        'The LLVM build directory %s is incomplete.',
        '/test/src/root/third_party/llvm-build/Release+Asserts')
    mock_logging_info.assert_any_call(
        'Please run `gclient sync --no-history -r $(git rev-parse @)` '
        'to ensure a complete installation.')
