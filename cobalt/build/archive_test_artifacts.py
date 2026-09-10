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
"""Creates test artifacts tar with runtime dependencies."""

import argparse
import contextlib
import os
import shutil
import subprocess
import sys
import tempfile
from typing import Optional

# Path prefixes and extensions that contain files we don't need to run tests on
# devices.
_EXCLUDE_DIRS_DEFAULT = [
    './exe.unstripped/', './lib.unstripped/', 'obj/', 'lib.java/',
    '../../third_party/jdk/', '../../net/tools/testserver/',
    '../../third_party/pywebsocket3/'
]

_EXCLUDE_DIRS_JUNIT = [
    './exe.unstripped/',
    './lib.unstripped/',
]

_EXCLUDE_EXTENSIONS = ('.map',)


def _find_strip_tool(source_dir: str) -> Optional[str]:
  """Locates llvm-strip or system strip tool."""
  llvm_strip = os.path.join(
      source_dir, 'third_party/llvm-build/Release+Asserts/bin/llvm-strip')
  if os.path.exists(llvm_strip):
    return llvm_strip
  return shutil.which('llvm-strip') or shutil.which('strip')


def _copy_or_strip(deps: set[str], src_dir: str, dest_dir: str,
                   strip_tool: str):
  """Copies and strips dependencies from src_dir into dest_dir."""
  for dep in deps:
    src = os.path.join(src_dir, dep)
    if not os.path.exists(src):
      continue
    dest = os.path.join(dest_dir, dep)
    os.makedirs(os.path.dirname(dest), exist_ok=True)
    is_bin = os.path.isfile(src) and (src.endswith('.so') or
                                      os.access(src, os.X_OK))
    if os.path.isdir(src):
      shutil.copytree(src, dest, dirs_exist_ok=True)
    elif is_bin and subprocess.run(
        [strip_tool, '--strip-unneeded', '-o', dest, src],
        check=False).returncode == 0:
      continue
    else:
      shutil.copy2(src, dest)


def _make_tar(archive_path: str, compression: str, compression_level: int,
              file_lists: list[tuple[str, str]]):
  """Creates the tar file. Uses tar command instead of tarfile for performance.
  """
  if compression == 'gz':
    compression_flag = f'gzip -{compression_level}'
  elif compression == 'xz':
    compression_flag = f'xz -T0 -{compression_level}'
  elif compression == 'zstd':
    compression_flag = f'zstd -T0 -{compression_level}'
  else:
    raise ValueError(f'Unsupported compression: {compression}')
  tar_cmd = [
      'tar', '--owner=0', '--group=0', '--numeric-owner', '-I',
      compression_flag, '-cvf', archive_path
  ]
  tmp_files = []
  for file_list, base_dir in file_lists:
    if not file_list:
      continue
    # Create temporary file to hold file list to not blow out the commandline.
    # It will get cleaned up via implicit close.
    # pylint: disable=consider-using-with
    tmp_file = tempfile.NamedTemporaryFile(mode='w', encoding='utf-8')
    tmp_files.append(tmp_file)
    tmp_file.write('\n'.join(sorted(file_list)))
    tmp_file.flush()
    # Change the tar working directory and add the file list.
    # Use absolute path for base_dir to avoid issues with cumulative -C flags.
    tar_cmd += ['-C', os.path.abspath(base_dir), '-T', tmp_file.name]

  print(f'Running `{" ".join(tar_cmd)}`')  # pylint: disable=inconsistent-quotes
  subprocess.check_call(tar_cmd)

  archive_size = f'{os.path.getsize(archive_path) / 1024 / 1024:.2f} MB'
  print(f'Created {os.path.basename(archive_path)} ({archive_size})')


def _handle_browsertests(
    source_dir: str,
    out_dir: str,
    destination_dir: str,
    compression: str,
    archive_name: str = 'cobalt_browsertests_deps',
):
  # Handle cobalt_browsertests using the specialized script.
  collect_script = os.path.join(source_dir, 'cobalt', 'testing',
                                'browser_tests', 'tools',
                                'collect_test_artifacts.py')
  output_name = f'{archive_name}.tar.{compression}'
  cmd = [
      sys.executable, collect_script, out_dir, '-o', output_name,
      '--output_dir', destination_dir, '--compression', compression
  ]
  print(f"Running: {' '.join(cmd)}")
  subprocess.check_call(cmd)


def _find_deps_file(*, target: str, target_name: str, target_path: str,
                    out_dir: str, use_android_deps_path: bool,
                    is_junit_test: bool):
  """Checks possible search paths for the runtime deps files for a target."""
  search_paths = []

  # If generated with the starboard_toolchain, the deps_file will be at
  # starboard/.
  for prefix in ('', 'starboard'):
    if use_android_deps_path:
      search_paths.append(
          os.path.join(out_dir, prefix, 'gen.runtime', target_path,
                       f'{target_name}__test_runner_script.runtime_deps'))
    else:
      search_paths.append(
          os.path.join(out_dir, prefix, f'{target_name}.runtime_deps'))

  if is_junit_test:
    # Fallback for robolectric tests which don't append '__test_runner_script'
    search_paths.extend([
        os.path.join(out_dir, 'gen.runtime', target_path,
                     f'{target_name}.runtime_deps')
    ])

  found_path = None
  for search_path in search_paths:
    print(f'Checking for deps_file for {target} in {search_path}.')
    if os.path.exists(search_path):
      found_path = search_path
      break

  if found_path:
    return found_path
  else:
    raise FileNotFoundError(f'Runtime deps file not found for {target}')


def create_archive(
    *,
    targets: list[str],
    source_dir: str,
    out_dir: str,
    destination_dir: str,
    archive_per_target: bool,
    use_android_deps_path: bool,
    compression: str,
    compression_level: int,
    flatten_deps: bool,
    strip_binaries: bool = False,
):
  """Main logic. Collects runtime dependencies for each target."""
  os.makedirs(destination_dir, exist_ok=True)
  strip_tool = _find_strip_tool(source_dir) if strip_binaries else None
  if strip_binaries:
    if strip_tool:
      print(f'Stripping test binaries using: {strip_tool}')
    else:
      print('Warning: --strip requested but no strip tool found.')

  combined_deps = set()
  for target in targets:
    # TODO(b/483460300): Unify unittest and browsertest packaging
    if target.endswith(':cobalt_browsertests'):
      if not use_android_deps_path:
        _handle_browsertests(source_dir, out_dir, destination_dir, compression)
        # If this was the only target, we are done.
        if len(targets) == 1:
          return
        continue
      else:
        # Generate host runner archive and lightweight device archive
        _handle_browsertests(
            source_dir,
            out_dir,
            destination_dir,
            compression,
            archive_name='cobalt_browsertests_host_deps')

    # Junit tests have some exceptions to normal packaging steps.
    is_junit_test = 'junit' in target
    target_path, target_name = target.split(':')
    target_path = target_path.lstrip('/')
    # Paths are configured in test.gni:
    # https://github.com/youtube/cobalt/blob/main/testing/test.gni
    deps_file = _find_deps_file(
        target=target,
        target_name=target_name,
        target_path=target_path,
        out_dir=out_dir,
        use_android_deps_path=use_android_deps_path,
        is_junit_test=is_junit_test)

    with open(deps_file, 'r', encoding='utf-8') as runtime_deps_file:
      # The paths in the runtime_deps files are relative to the out folder.
      # Android tests expects files both in the out and source root folders
      # to be in the working directory of the archive whereas Linux tests
      # expect it relative to the binary.
      tar_root = '.' if flatten_deps else out_dir
      target_deps = set()
      target_src_root_deps = set()

      # Add test_targets.json to archive so that test runners know what to run.
      test_targets_json = os.path.join(out_dir, 'test_targets.json')
      if os.path.exists(test_targets_json):
        target_deps.add(os.path.join(tar_root, 'test_targets.json'))

      # Add JUnit wrapper scripts if they exist (special case for these
      # since they are missing from the runtime deps file).
      # TODO(b/524712602): This special case handling could be removed with GN
      # refactoring of the Robolectric binary.
      if is_junit_test:
        print(f'Adding Junit-specific test runner scripts for {target}.')
        junit_wrapper = os.path.join('bin', f'run_{target_name}')
        junit_helper = os.path.join('bin', 'helper', target_name)
        if os.path.exists(os.path.join(out_dir, junit_wrapper)):
          target_deps.add(os.path.join(tar_root, junit_wrapper))
        if os.path.exists(os.path.join(out_dir, junit_helper)):
          target_deps.add(os.path.join(tar_root, junit_helper))

      exclude_dirs = _EXCLUDE_DIRS_DEFAULT
      if is_junit_test:
        exclude_dirs = _EXCLUDE_DIRS_JUNIT
      elif use_android_deps_path and target_name == 'nplb_loader':
        # TODO(crbug.com/532068409): remove the test data exclusion and figure
        # out a workaround for the increase in size.
        exclude_dirs = _EXCLUDE_DIRS_DEFAULT + [
            'test/starboard/shared/starboard/player/'
        ]

      raw_lines = [line.strip() for line in runtime_deps_file if line.strip()]
      has_uncompressed_so = any(l.endswith('.so') for l in raw_lines)

      for line in raw_lines:
        if any(line.startswith(path) for path in exclude_dirs):
          continue

        if line.endswith(_EXCLUDE_EXTENSIONS):
          continue

        # Skip redundant compressed libraries if uncompressed .so is included.
        if has_uncompressed_so and (line.endswith('.lz4') or
                                    line.endswith('.zst')):
          continue

        if flatten_deps and line.startswith('../../'):
          target_src_root_deps.add(line[6:])
        else:
          # Rebase all files to be relative to their respective root (source or
          # out dir) to be able to flatten them below. Chromium test runners
          # have access to the source directory in '../..' which ours (ODTs
          # especially) do not.
          rel_path = os.path.relpath(os.path.join(tar_root, line))
          target_deps.add(rel_path)

      # Optionally strip binaries into staged temp directory before archiving.
      with (tempfile.TemporaryDirectory(prefix='stripped_deps_') if strip_tool
            else contextlib.nullcontext(out_dir)) as staged_out_dir:
        if strip_tool:
          _copy_or_strip(target_deps, out_dir, staged_out_dir, strip_tool)

        combined_deps |= target_deps

        if archive_per_target:
          output_path = os.path.join(destination_dir,
                                     f'{target_name}_deps.tar.{compression}')
          if flatten_deps:
            _make_tar(
                output_path,
                compression,
                compression_level,
                [(target_deps, staged_out_dir),
                 (target_src_root_deps, source_dir)],
            )
          else:
            raise ValueError('Unsupported configuration.')

  # Linux tests and deps are all bundled into a single tar file.
  if not archive_per_target:
    output_path = os.path.join(destination_dir,
                               f'test_artifacts.tar.{compression}')
    _make_tar(
        output_path,
        compression,
        compression_level,
        [(combined_deps, source_dir)],
    )


def main():
  parser = argparse.ArgumentParser()
  parser.add_argument(
      '-s', '--source-dir', required=True, help='The path to the source root.')
  parser.add_argument(
      '-o',
      '--out-dir',
      required=True,
      help='The path to where the build artifacts are stored.')
  parser.add_argument(
      '-d',
      '--destination-dir',
      required=True,
      help='The output directory. For linux test_artifacts.tar.gz is created '
      'with all test artifacts, else a `<target_name>_deps.tar.gz` is '
      'created for each target passed.')
  parser.add_argument(
      '-t',
      '--targets',
      required=True,
      type=lambda arg: arg.split(','),
      help='The targets to package, comma-separated. Must be fully qualified, '
      'e.g. path/to:target_name,other/path/to:target_name.')
  parser.add_argument(
      '--archive-per-target',
      action='store_true',
      help='Create a separate .tar.gz archive for each build target.')
  parser.add_argument(
      '--use-android-deps-path',
      action='store_true',
      help='Look for .runtime_deps files in the Android-specific path.')
  parser.add_argument(
      '--strip',
      action='store_true',
      help='Strip shared libraries and executables before packaging.')
  parser.add_argument(
      '--compression',
      choices=['xz', 'gz', 'zstd'],
      default='zstd',
      help='The compression algorithm to use.')
  parser.add_argument(
      '--compression-level',
      type=int,
      default=1,
      help='The compression level to use.')
  parser.add_argument(
      '--flatten-deps',
      action='store_true',
      help='Pass this argument to archive files from the source and out '
      'directories both at the root of the deps archive.')
  args = parser.parse_args()

  if args.flatten_deps != args.archive_per_target:
    raise ValueError('Unsupported configuration.')

  create_archive(
      targets=args.targets,
      source_dir=args.source_dir,
      out_dir=args.out_dir,
      destination_dir=args.destination_dir,
      archive_per_target=args.archive_per_target,
      use_android_deps_path=args.use_android_deps_path,
      compression=args.compression,
      compression_level=args.compression_level,
      flatten_deps=args.flatten_deps,
      strip_binaries=args.strip)


if __name__ == '__main__':
  main()
