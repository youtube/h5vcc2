#!/usr/bin/env python3
# Copyright 2021 The Cobalt Authors. All Rights Reserved.
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
Tests for the gn.py script.
"""

import gn
import os
import sys
import unittest
from unittest import mock

# pylint: disable=inconsistent-quotes


class GnTest(unittest.TestCase):

  def setUp(self):
    self.original_argv = sys.argv
    self.original_cwd = os.getcwd()
    self.test_root_path = os.path.abspath(
        os.path.join(os.path.dirname(__file__), os.pardir, os.pardir))
    os.chdir(self.test_root_path)

  def tearDown(self):
    sys.argv = self.original_argv
    os.chdir(self.original_cwd)

  @mock.patch('pathlib.Path.mkdir')
  @mock.patch('os.path.exists', return_value=False)
  @mock.patch('os.rename')
  @mock.patch('builtins.open', new_callable=mock.mock_open)
  @mock.patch('subprocess.check_call')
  def test_coverage_flag(  # pylint: disable=too-many-positional-arguments
      self, mock_check_call, mock_open, mock_rename, mock_exists, mock_mkdir):
    del mock_rename, mock_exists  # unused
    platform = 'android-x86'
    build_type = 'devel'
    expected_out_dir = f'out/{platform}_{build_type}'
    expected_args_content_part = (
        'use_remoteexec = true # Set by gn.py\n'
        'use_siso = true # Set by gn.py\n'
        'build_type = "devel" # Set by gn.py\n'
        'symbol_level = 1 # Set by gn.py\n'
        'is_debug = false # Set by gn.py\n'
        'import("//cobalt/build/configs/android-x86/args.gn")\n'
        'import("//cobalt/build/configs/coverage.gn") # Set by gn.py\n')

    sys.argv = [
        'gn.py',
        '-p',
        platform,
        '-c',
        build_type,
        '--coverage',
    ]

    gn.main()

    # Verify output directory creation
    mock_mkdir.assert_called_once_with(parents=True, exist_ok=True)

    # Verify args.gn content
    mock_open.assert_called_once()
    handle = mock_open()
    actual_content = "".join(
        [call.args[0] for call in handle.write.call_args_list])
    self.assertIn(expected_args_content_part, actual_content)

    # Verify gn gen command
    mock_check_call.assert_called_once_with([
        'gn',
        'gen',
        expected_out_dir,
        '--check',
    ])

  @mock.patch('pathlib.Path.mkdir')
  @mock.patch('os.path.exists', return_value=False)
  @mock.patch('os.rename')
  @mock.patch('builtins.open', new_callable=mock.mock_open)
  @mock.patch('subprocess.check_call')
  def test_rdk_platform_build_types(  # pylint: disable=too-many-positional-arguments
      self, mock_check_call, mock_open, mock_rename, mock_exists, mock_mkdir):
    del mock_rename, mock_exists  # unused
    platform = 'evergreen-arm-hardfp-rdk'
    build_type = 'qa'
    expected_out_dir = f'out/{platform}_{build_type}'
    expected_args_content_part = (
        'build_type = "qa" # Set by gn.py\n'
        'symbol_level = 1 # Set by gn.py\n'
        'is_official_build = true # Set by gn.py\n'
        'exclude_unwind_tables = false # Set by gn.py\n'
        'arm_use_thumb = false # Set by gn.py\n'
        'enable_profiling = true # Set by gn.py\n'
        'import("//cobalt/build/configs/evergreen-arm-hardfp-rdk/args.gn")\n')

    sys.argv = [
        'gn.py',
        '-p',
        platform,
        '-c',
        build_type,
    ]

    gn.main()

    mock_mkdir.assert_called_once_with(parents=True, exist_ok=True)
    mock_open.assert_called_once()
    handle = mock_open()
    actual_content = "".join(
        [call.args[0] for call in handle.write.call_args_list])
    self.assertIn(expected_args_content_part, actual_content)

    mock_check_call.assert_called_once_with([
        'gn',
        'gen',
        expected_out_dir,
        '--check',
    ])

  @mock.patch('builtins.open', new_callable=mock.mock_open)
  def test_write_build_args_backward_compatibility(self, mock_open):
    # Verify write_build_args can be called positionally without platform
    # argument.
    gn.write_build_args('/tmp/args.gn',
                        'cobalt/build/configs/android-x86/args.gn', 'devel',
                        True, False, False)
    handle = mock_open()
    actual_content = "".join(
        [call.args[0] for call in handle.write.call_args_list])
    self.assertIn('use_remoteexec = true # Set by gn.py\n', actual_content)
    self.assertIn('build_type = "devel" # Set by gn.py\n', actual_content)
    self.assertIn('import("//cobalt/build/configs/android-x86/args.gn")\n',
                  actual_content)

  @mock.patch('pathlib.Path.mkdir')
  @mock.patch('os.path.exists', return_value=False)
  @mock.patch('os.rename')
  @mock.patch('builtins.open', new_callable=mock.mock_open)
  @mock.patch('subprocess.check_call')
  def test_no_coverage_flag(  # pylint: disable=too-many-positional-arguments
      self, mock_check_call, mock_open, mock_rename, mock_exists, mock_mkdir):
    del mock_rename, mock_exists  # unused
    platform = 'android-x86'
    build_type = 'devel'
    expected_out_dir = f'out/{platform}_{build_type}'
    unexpected_args_content_part = (
        'import("//cobalt/build/configs/coverage.gn") # Set by gn.py\n')

    sys.argv = [
        'gn.py',
        '-p',
        platform,
        '-c',
        build_type,
    ]

    gn.main()

    # Verify output directory creation
    mock_mkdir.assert_called_once_with(parents=True, exist_ok=True)

    # Verify args.gn content
    mock_open.assert_called_once()
    handle = mock_open()
    actual_content = "".join(
        [call.args[0] for call in handle.write.call_args_list])
    self.assertNotIn(unexpected_args_content_part, actual_content)

    # Verify gn gen command
    mock_check_call.assert_called_once_with([
        'gn',
        'gen',
        expected_out_dir,
        '--check',
    ])


if __name__ == '__main__':
  unittest.main()
