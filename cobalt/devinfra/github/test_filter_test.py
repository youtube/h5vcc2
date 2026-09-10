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
"""Tests for cobalt.devinfra.github.test_filter."""

import io
import os
import shutil
import sys
import tempfile
import unittest
from unittest import mock

_REPO_ROOT = os.path.abspath(
    os.path.join(os.path.dirname(__file__), '..', '..', '..'))
if _REPO_ROOT not in sys.path:
  sys.path.insert(0, _REPO_ROOT)

# pylint: disable=wrong-import-position
from cobalt.devinfra.github.test_filter import get_gtest_filter
from cobalt.devinfra.github.test_filter import main
# pylint: enable=wrong-import-position


class TestGetGtestFilter(unittest.TestCase):
  """Tests for get_gtest_filter."""

  def setUp(self):
    self.temp_dir = tempfile.mkdtemp()

  def tearDown(self):
    shutil.rmtree(self.temp_dir)

  def _write_filter_text(self, filename: str, content: str) -> str:
    filepath = os.path.join(self.temp_dir, filename)
    with open(filepath, 'w', encoding='utf-8', newline='') as f:
      f.write(content)
    return filepath

  def test_file_not_found(self):
    self.assertEqual(get_gtest_filter(self.temp_dir, 'nonexistent'), '*')

  def test_negative_filter_only(self):
    content = """# Comments are ignored
-Suite.Test1
# Another comment
-Suite.Test2
"""
    self._write_filter_text('my_target.filter', content)
    self.assertEqual(
        get_gtest_filter(self.temp_dir, 'my_target'),
        '-Suite.Test1:Suite.Test2')

  def test_positive_filter_only(self):
    content = """Suite.Test1
Suite.Test2
"""
    self._write_filter_text('my_target.filter', content)
    self.assertEqual(
        get_gtest_filter(self.temp_dir, 'my_target'), 'Suite.Test1:Suite.Test2')

  def test_explicit_plus_positive_filter(self):
    content = """+Suite.Test1
+Suite.Test2
"""
    self._write_filter_text('my_target.filter', content)
    self.assertEqual(
        get_gtest_filter(self.temp_dir, 'my_target'), 'Suite.Test1:Suite.Test2')

  def test_mixed_positive_and_negative(self):
    content = """# Run Test1 but exclude Test2
Suite.Test1
-Suite.Test2
"""
    self._write_filter_text('my_target.filter', content)
    self.assertEqual(
        get_gtest_filter(self.temp_dir, 'my_target'), 'Suite.Test1-Suite.Test2')

  def test_inline_comments_and_whitespace(self):
    content = """
    # Leading whitespace comment
    Suite.Test1  # inline comment
    -Suite.Test2 # inline comment 2

"""
    self._write_filter_text('my_target.filter', content)
    self.assertEqual(
        get_gtest_filter(self.temp_dir, 'my_target'), 'Suite.Test1-Suite.Test2')

  def test_double_slash_raises_value_error(self):
    content = """// Invalid comment style
-Suite.Test1
"""
    self._write_filter_text('my_target.filter', content)
    with self.assertRaises(ValueError):
      get_gtest_filter(self.temp_dir, 'my_target')

  def test_sharding(self):
    self._write_filter_text('my_target.filter', 'Suite.GlobalTest\n')
    self._write_filter_text('my_target_0.filter', 'Suite.Shard0Test\n')
    self._write_filter_text('my_target_1.filter', '-*\n')

    self.assertEqual(
        get_gtest_filter(self.temp_dir, 'my_target', shard_index=0),
        'Suite.Shard0Test')
    self.assertEqual(
        get_gtest_filter(self.temp_dir, 'my_target', shard_index='0'),
        'Suite.Shard0Test')
    self.assertEqual(
        get_gtest_filter(self.temp_dir, 'my_target', shard_index=1), '-*')
    self.assertEqual(
        get_gtest_filter(self.temp_dir, 'my_target', shard_index=2),
        'Suite.GlobalTest')
    self.assertEqual(
        get_gtest_filter(self.temp_dir, 'my_target'), 'Suite.GlobalTest')

  def test_wildcard_skip(self):
    self._write_filter_text('my_target.filter', '-*\n')
    self.assertEqual(get_gtest_filter(self.temp_dir, 'my_target'), '-*')

  def test_empty_filter_file(self):
    self._write_filter_text('my_target.filter', '# Only comments\n\n')
    self.assertEqual(get_gtest_filter(self.temp_dir, 'my_target'), '*')

  def test_empty_shard_index_uses_global_filter(self):
    self._write_filter_text('my_target.filter', 'Suite.GlobalTest\n')
    self.assertEqual(
        get_gtest_filter(self.temp_dir, 'my_target', shard_index=''),
        'Suite.GlobalTest')

  def test_colon_prefixed_target_name(self):
    self._write_filter_text('my_target.filter', 'Suite.TargetTest\n')
    self.assertEqual(
        get_gtest_filter(self.temp_dir, 'base:my_target'), 'Suite.TargetTest')
    self.assertEqual(
        get_gtest_filter(self.temp_dir, 'starboard/nplb:my_target'),
        'Suite.TargetTest')


class TestCli(unittest.TestCase):
  """Tests for CLI entry point in main()."""

  def setUp(self):
    self.temp_dir = tempfile.mkdtemp()

  def tearDown(self):
    shutil.rmtree(self.temp_dir)

  def test_cli(self):
    filter_file = os.path.join(self.temp_dir, 'target.filter')
    with open(filter_file, 'w', encoding='utf-8', newline='') as f:
      f.write('Suite.Test1\n')

    with mock.patch('sys.stdout', new_callable=io.StringIO) as mock_stdout:
      exit_code = main(['--filter-dir', self.temp_dir, '--target', 'target'])
      self.assertEqual(exit_code, 0)
      self.assertEqual(mock_stdout.getvalue().strip(), 'Suite.Test1')

  def test_cli_with_shard(self):
    shard_file = os.path.join(self.temp_dir, 'target_1.filter')
    with open(shard_file, 'w', encoding='utf-8', newline='') as f:
      f.write('Suite.Shard1Test\n')

    with mock.patch('sys.stdout', new_callable=io.StringIO) as mock_stdout:
      exit_code = main(
          ['--filter-dir', self.temp_dir, '--target', 'target', '--shard', '1'])
      self.assertEqual(exit_code, 0)
      self.assertEqual(mock_stdout.getvalue().strip(), 'Suite.Shard1Test')

  def test_cli_file_not_found_outputs_wildcard(self):
    with mock.patch('sys.stdout', new_callable=io.StringIO) as mock_stdout:
      exit_code = main(
          ['--filter-dir', self.temp_dir, '--target', 'nonexistent'])
      self.assertEqual(exit_code, 0)
      self.assertEqual(mock_stdout.getvalue().strip(), '*')


if __name__ == '__main__':
  unittest.main()
