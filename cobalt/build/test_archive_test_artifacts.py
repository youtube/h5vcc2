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
"""Tests for the archive_test_artifacts script."""

import os
import shutil
import tempfile
import unittest
from unittest import mock

import archive_test_artifacts


class TestArchiveTestArtifacts(unittest.TestCase):
  """Unit tests for archive collection logic."""

  def setUp(self):
    self.test_dir = tempfile.mkdtemp()
    self.old_cwd = os.getcwd()
    os.chdir(self.test_dir)
    # Match the likely real-world scenario where out is inside src
    self.source_dir = os.path.join(self.test_dir, 'src')
    self.out_dir = os.path.join(self.source_dir, 'out')
    self.dest_dir = os.path.join(self.test_dir, 'dest')
    os.makedirs(self.source_dir)
    os.makedirs(self.out_dir)
    os.makedirs(self.dest_dir)

  def tearDown(self):
    os.chdir(self.old_cwd)
    shutil.rmtree(self.test_dir)

  @mock.patch('tempfile.NamedTemporaryFile')
  @mock.patch('subprocess.check_call')
  def test_make_tar(self, mock_call, mock_temp):
    archive_path = os.path.join(self.dest_dir, 'test.tar.gz')
    file_lists = [
        (['file2', 'file1'], self.source_dir),
        (['file3'], self.out_dir),
    ]

    # Mock the temporary file objects
    mock_file1 = mock.MagicMock()
    mock_file1.name = '/tmp/fake_list1'
    mock_file2 = mock.MagicMock()
    mock_file2.name = '/tmp/fake_list2'
    mock_temp.side_effect = [mock_file1, mock_file2]

    # Mock getsize to avoid error when checking created file
    with mock.patch('os.path.getsize', return_value=1024):
      # pylint: disable=protected-access
      archive_test_artifacts._make_tar(archive_path, 'gz', 1, file_lists)

    self.assertTrue(mock_call.called)
    tar_cmd = mock_call.call_args[0][0]
    self.assertIn('--owner=0', tar_cmd)
    self.assertIn('--group=0', tar_cmd)
    self.assertIn('--numeric-owner', tar_cmd)

    # Verify file list content (sorted and newline-separated)
    mock_file1.write.assert_called_with('file1\nfile2')
    mock_file2.write.assert_called_with('file3')

  @mock.patch('archive_test_artifacts._make_tar')
  def test_create_archive_linux_style(self, mock_make_tar):
    # Setup mock runtime_deps
    target_name = 'my_test'
    deps_file = os.path.join(self.out_dir, f'{target_name}.runtime_deps')
    with open(deps_file, 'w', encoding='utf-8') as f:
      f.write('base_unittests\n')
      f.write('../../cobalt/test/data/file.txt\n')
      f.write('obj/some_file.o\n')  # Should be excluded

    archive_test_artifacts.create_archive(
        targets=['cobalt/test:my_test'],
        source_dir=self.source_dir,
        out_dir=self.out_dir,
        destination_dir=self.dest_dir,
        archive_per_target=False,
        use_android_deps_path=False,
        compression='gz',
        compression_level=1,
        flatten_deps=False)

    self.assertTrue(mock_make_tar.called)
    file_list = mock_make_tar.call_args[0][3][0][0]

    # os.path.relpath(os.path.join(out_dir, 'base_unittests'))
    # Since out_dir is absolute and we are in test_dir:
    # it will be 'src/out/base_unittests'
    self.assertIn(
        os.path.relpath(
            os.path.join(self.out_dir, 'base_unittests'),
            start=self.source_dir), file_list)
    self.assertIn('cobalt/test/data/file.txt', file_list)

  @mock.patch('archive_test_artifacts._make_tar')
  def test_create_archive_android_style(self, mock_make_tar):
    # Android style: flatten_deps=True, archive_per_target=True
    target_name = 'my_test'
    deps_file = os.path.join(self.out_dir, f'{target_name}.runtime_deps')
    with open(deps_file, 'w', encoding='utf-8') as f:
      f.write('base_unittests\n')
      f.write('../../cobalt/test/data/file.txt\n')

    archive_test_artifacts.create_archive(
        targets=['cobalt/test:my_test'],
        source_dir=self.source_dir,
        out_dir=self.out_dir,
        destination_dir=self.dest_dir,
        archive_per_target=True,
        use_android_deps_path=False,
        compression='gz',
        compression_level=1,
        flatten_deps=True)

    self.assertTrue(mock_make_tar.called)
    file_lists = mock_make_tar.call_args[0][3]

    out_deps = file_lists[0][0]
    src_deps = file_lists[1][0]

    # base_unittests goes to out_deps because it doesn't start with ../../
    self.assertIn('base_unittests', out_deps)
    # ../../cobalt/test/data/file.txt starts with ../../
    self.assertIn('cobalt/test/data/file.txt', src_deps)

  @mock.patch('os.path.getsize', return_value=1024)
  @mock.patch('subprocess.check_call')
  def test_create_archive_browsertests_redirection(self, mock_call,
                                                   unused_mock_getsize):
    # Setup mock runtime_deps for another target to ensure
    # it still runs if mixed
    target_name = 'my_test'
    deps_file = os.path.join(self.out_dir, f'{target_name}.runtime_deps')
    with open(deps_file, 'w', encoding='utf-8') as f:
      f.write('base_unittests\n')

    archive_test_artifacts.create_archive(
        targets=[
            'cobalt/test:my_test',
            'cobalt/testing/browser_tests:cobalt_browsertests'
        ],
        source_dir=self.source_dir,
        out_dir=self.out_dir,
        destination_dir=self.dest_dir,
        archive_per_target=False,
        use_android_deps_path=False,
        compression='gz',
        compression_level=1,
        flatten_deps=False)

    # Verify specialized script was called
    # The first call should be to collect_test_artifacts.py
    # The second call (from _make_tar) should be to tar
    self.assertEqual(mock_call.call_count, 2)

    collect_cmd = mock_call.call_args_list[0][0][0]
    self.assertIn('collect_test_artifacts.py', collect_cmd[1])
    self.assertIn(self.out_dir, collect_cmd)
    self.assertIn('cobalt_browsertests_deps.tar.gz', collect_cmd)
    self.assertIn('--compression', collect_cmd)
    self.assertIn('gz', collect_cmd)

    tar_cmd = mock_call.call_args_list[1][0][0]
    self.assertIn('tar', tar_cmd)

  @mock.patch('archive_test_artifacts._make_tar')
  def test_create_archive_relative_paths(self, mock_make_tar):
    # Regression test for relative paths.
    # We are already in self.test_dir (temp). Use relative paths to src and out.
    rel_source = 'src'
    rel_out = 'src/out'
    target_name = 'rel_test'
    deps_file = os.path.join(self.out_dir, f'{target_name}.runtime_deps')
    with open(deps_file, 'w', encoding='utf-8') as f:
      f.write('binary\n')
      f.write('../../data.txt\n')

    archive_test_artifacts.create_archive(
        targets=[f'path:{target_name}'],
        source_dir=rel_source,
        out_dir=rel_out,
        destination_dir='dest',
        archive_per_target=True,
        use_android_deps_path=False,
        compression='gz',
        compression_level=1,
        flatten_deps=True)

    self.assertTrue(mock_make_tar.called)
    file_lists = mock_make_tar.call_args[0][3]

    # Verify that the base_dirs passed to _make_tar are now absolute
    # (or at least resolved correctly relative to current cwd)
    out_base_dir = file_lists[0][1]
    src_base_dir = file_lists[1][1]

    # In our script, we now call os.path.abspath(base_dir) inside _make_tar.
    # To test that the logic in create_archive correctly passes the relative
    # paths which then get converted, we'll check that create_archive
    # didn't crash and passed the expected relative roots.
    self.assertEqual(out_base_dir, rel_out)
    self.assertEqual(src_base_dir, rel_source)

  def test_find_strip_tool(self):
    # Test discovery of llvm-strip under source_dir
    fake_llvm_strip = os.path.join(self.source_dir, 'third_party', 'llvm-build',
                                   'Release+Asserts', 'bin', 'llvm-strip')
    os.makedirs(os.path.dirname(fake_llvm_strip), exist_ok=True)
    with open(fake_llvm_strip, 'w', encoding='utf-8') as f:
      f.write('fake-strip')
    # pylint: disable=protected-access
    found = archive_test_artifacts._find_strip_tool(self.source_dir)
    self.assertEqual(found, fake_llvm_strip)

  @mock.patch(
      'archive_test_artifacts._find_strip_tool',
      return_value='/fake/llvm-strip')
  @mock.patch('subprocess.run')
  @mock.patch('archive_test_artifacts._make_tar')
  def test_create_archive_with_strip(self, mock_make_tar, mock_run,
                                     mock_strip_tool):
    del mock_strip_tool
    target_name = 'stripped_target'
    deps_file = os.path.join(self.out_dir, f'{target_name}.runtime_deps')
    with open(deps_file, 'w', encoding='utf-8') as f:
      f.write('libsample.so\n')

    # Create dummy .so file
    sample_so = os.path.join(self.out_dir, 'libsample.so')
    with open(sample_so, 'w', encoding='utf-8') as f:
      f.write('dummy elf binary content')

    archive_test_artifacts.create_archive(
        targets=[f'starboard:{target_name}'],
        source_dir=self.source_dir,
        out_dir=self.out_dir,
        destination_dir=self.dest_dir,
        archive_per_target=True,
        use_android_deps_path=False,
        compression='gz',
        compression_level=1,
        flatten_deps=True,
        strip_binaries=True)

    # Verify llvm-strip was invoked with --strip-unneeded
    self.assertTrue(mock_run.called)
    strip_cmd = mock_run.call_args[0][0]
    self.assertEqual(strip_cmd[0], '/fake/llvm-strip')
    self.assertEqual(strip_cmd[1], '--strip-unneeded')
    self.assertEqual(strip_cmd[2], '-o')
    self.assertEqual(strip_cmd[4], sample_so)
    self.assertTrue(mock_make_tar.called)

  @mock.patch(
      'archive_test_artifacts._find_strip_tool',
      return_value='/fake/llvm-strip')
  @mock.patch('subprocess.run')
  @mock.patch('archive_test_artifacts._make_tar')
  def test_create_archive_without_strip(self, mock_make_tar, mock_run,
                                        mock_strip_tool):
    del mock_strip_tool
    target_name = 'unstripped_target'
    deps_file = os.path.join(self.out_dir, f'{target_name}.runtime_deps')
    with open(deps_file, 'w', encoding='utf-8') as f:
      f.write('libsample.so\n')

    sample_so = os.path.join(self.out_dir, 'libsample.so')
    with open(sample_so, 'w', encoding='utf-8') as f:
      f.write('dummy elf binary content')

    archive_test_artifacts.create_archive(
        targets=[f'starboard:{target_name}'],
        source_dir=self.source_dir,
        out_dir=self.out_dir,
        destination_dir=self.dest_dir,
        archive_per_target=True,
        use_android_deps_path=False,
        compression='gz',
        compression_level=1,
        flatten_deps=True,
        strip_binaries=False)

    # Verify strip was NOT invoked
    self.assertFalse(mock_run.called)
    self.assertTrue(mock_make_tar.called)

  @mock.patch(
      'archive_test_artifacts._find_strip_tool',
      return_value='/fake/llvm-strip')
  @mock.patch('subprocess.run')
  @mock.patch('archive_test_artifacts._make_tar')
  def test_create_archive_linux_style_with_strip(self, mock_make_tar, mock_run,
                                                 mock_strip_tool):
    del mock_strip_tool
    target_name = 'my_test'
    deps_file = os.path.join(self.out_dir, f'{target_name}.runtime_deps')
    with open(deps_file, 'w', encoding='utf-8') as f:
      f.write('base_unittests\n')
      f.write('../../cobalt/test/data/file.txt\n')

    binary_path = os.path.join(self.out_dir, 'base_unittests')
    with open(binary_path, 'w', encoding='utf-8') as f:
      f.write('dummy elf binary content')
    os.chmod(binary_path, 0o755)

    data_path = os.path.join(self.source_dir, 'cobalt', 'test', 'data',
                             'file.txt')
    os.makedirs(os.path.dirname(data_path), exist_ok=True)
    with open(data_path, 'w', encoding='utf-8') as f:
      f.write('dummy test data')

    mock_run.return_value.returncode = 0

    archive_test_artifacts.create_archive(
        targets=['cobalt/test:my_test'],
        source_dir=self.source_dir,
        out_dir=self.out_dir,
        destination_dir=self.dest_dir,
        archive_per_target=False,
        use_android_deps_path=False,
        compression='gz',
        compression_level=1,
        flatten_deps=False,
        strip_binaries=True)

    self.assertTrue(mock_run.called)
    strip_cmd = mock_run.call_args[0][0]
    self.assertEqual(strip_cmd[0], '/fake/llvm-strip')
    self.assertEqual(strip_cmd[1], '--strip-unneeded')
    self.assertEqual(strip_cmd[2], '-o')
    self.assertEqual(strip_cmd[4], os.path.abspath(binary_path))

    self.assertTrue(mock_make_tar.called)
    file_lists = mock_make_tar.call_args[0][3]
    self.assertEqual(len(file_lists), 2)

    stripped_list, staged_dir = file_lists[0]
    unstripped_list, src_dir = file_lists[1]

    rel_binary = os.path.relpath(binary_path, start=self.source_dir)
    self.assertIn(rel_binary, stripped_list)
    self.assertIn('stripped_host_', staged_dir)

    self.assertIn('cobalt/test/data/file.txt', unstripped_list)
    self.assertEqual(src_dir, self.source_dir)

  @mock.patch(
      'subprocess.run', side_effect=OSError('strip tool not executable'))
  def test_strip_binary_oserror_handled(self, mock_run):
    del mock_run
    binary_path = os.path.join(self.out_dir, 'sample_binary')
    with open(binary_path, 'w', encoding='utf-8') as f:
      f.write('binary content')
    os.chmod(binary_path, 0o755)

    dest_path = os.path.join(self.test_dir, 'staged', 'sample_binary')
    # pylint: disable=protected-access
    result = archive_test_artifacts._strip_binary(binary_path, dest_path,
                                                  '/fake/llvm-strip')
    self.assertFalse(result)
    self.assertFalse(os.path.exists(dest_path))

  @mock.patch('archive_test_artifacts._make_tar')
  def test_create_archive_linux_style_per_target(self, mock_make_tar):
    target_name = 'my_test'
    deps_file = os.path.join(self.out_dir, f'{target_name}.runtime_deps')
    with open(deps_file, 'w', encoding='utf-8') as f:
      f.write('base_unittests\n')
      f.write('../../cobalt/test/data/file.txt\n')

    archive_test_artifacts.create_archive(
        targets=['cobalt/test:my_test'],
        source_dir=self.source_dir,
        out_dir=self.out_dir,
        destination_dir=self.dest_dir,
        archive_per_target=True,
        use_android_deps_path=False,
        compression='gz',
        compression_level=1,
        flatten_deps=False,
        strip_binaries=False)

    self.assertTrue(mock_make_tar.called)
    archive_path = mock_make_tar.call_args[0][0]
    self.assertTrue(archive_path.endswith('my_test_deps.tar.gz'))

    file_lists = mock_make_tar.call_args[0][3]
    self.assertEqual(len(file_lists), 1)
    target_deps, src_dir = file_lists[0]
    self.assertEqual(src_dir, self.source_dir)
    self.assertIn(
        os.path.relpath(
            os.path.join(self.out_dir, 'base_unittests'),
            start=self.source_dir), target_deps)
    self.assertIn('cobalt/test/data/file.txt', target_deps)

  @mock.patch(
      'archive_test_artifacts._find_strip_tool',
      return_value='/fake/llvm-strip')
  @mock.patch('subprocess.run')
  @mock.patch('archive_test_artifacts._make_tar')
  def test_create_archive_linux_style_per_target_with_strip(
      self, mock_make_tar, mock_run, mock_strip_tool):
    del mock_strip_tool
    target_name = 'my_test'
    deps_file = os.path.join(self.out_dir, f'{target_name}.runtime_deps')
    with open(deps_file, 'w', encoding='utf-8') as f:
      f.write('base_unittests\n')
      f.write('../../cobalt/test/data/file.txt\n')

    binary_path = os.path.join(self.out_dir, 'base_unittests')
    with open(binary_path, 'w', encoding='utf-8') as f:
      f.write('dummy elf binary content')
    os.chmod(binary_path, 0o755)

    data_path = os.path.join(self.source_dir, 'cobalt', 'test', 'data',
                             'file.txt')
    os.makedirs(os.path.dirname(data_path), exist_ok=True)
    with open(data_path, 'w', encoding='utf-8') as f:
      f.write('dummy test data')

    mock_run.return_value.returncode = 0

    archive_test_artifacts.create_archive(
        targets=['cobalt/test:my_test'],
        source_dir=self.source_dir,
        out_dir=self.out_dir,
        destination_dir=self.dest_dir,
        archive_per_target=True,
        use_android_deps_path=False,
        compression='gz',
        compression_level=1,
        flatten_deps=False,
        strip_binaries=True)

    self.assertTrue(mock_run.called)
    strip_cmd = mock_run.call_args[0][0]
    self.assertEqual(strip_cmd[0], '/fake/llvm-strip')
    self.assertEqual(strip_cmd[1], '--strip-unneeded')
    self.assertEqual(strip_cmd[2], '-o')
    self.assertEqual(strip_cmd[4], os.path.abspath(binary_path))

    self.assertTrue(mock_make_tar.called)
    archive_path = mock_make_tar.call_args[0][0]
    self.assertTrue(archive_path.endswith('my_test_deps.tar.gz'))

    file_lists = mock_make_tar.call_args[0][3]
    self.assertEqual(len(file_lists), 2)

    stripped_list, staged_dir = file_lists[0]
    unstripped_list, src_dir = file_lists[1]

    rel_binary = os.path.relpath(binary_path, start=self.source_dir)
    self.assertIn(rel_binary, stripped_list)
    self.assertIn('stripped_host_', staged_dir)

    self.assertIn('cobalt/test/data/file.txt', unstripped_list)
    self.assertEqual(src_dir, self.source_dir)


if __name__ == '__main__':
  unittest.main()
