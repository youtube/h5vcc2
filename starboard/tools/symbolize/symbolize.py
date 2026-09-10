#!/usr/bin/env python

# Copyright 2019 The Cobalt Authors. All Rights Reserved.
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
"""Lightweight utility to simplify resolving stack traces and crashes.

This tool supports three different formats for crashes and stack traces, but can
easily be expanded for addition cases. Examples of current formats are as
follows:

  Address Sanitizer
    #1 0x7fdc59bbaa6b  (<unknown module>)

  Cobalt
    <unknown> [0x7efcdf1fd52b]

  Raw
    0x7efcdf1fd52b

  GDB
    #1  0x742a51b6 in ?? () from /home/pi/content/app/cobalt/lib/libcobalt.so


The results of the symbolizer will only be included if it was able to find the
name of the symbol, and it does not appear to be malformed. The only exception
is when the line was matched with the |_RAW| regular expression in which case it
will always output the results of the symbolizer.
"""

import argparse
import os
import re
import subprocess
import sys

_SRC_DIR = os.path.abspath(
    os.path.join(os.path.dirname(__file__), os.pardir, os.pardir, os.pardir))
if _SRC_DIR not in sys.path:
  sys.path.insert(0, _SRC_DIR)

from starboard.tools import paths  # pylint: disable=wrong-import-position

_SYMBOLIZER = os.path.join(paths.REPOSITORY_ROOT, 'third_party', 'llvm-build',
                           'Release+Asserts', 'bin', 'llvm-symbolizer')

_RE_ASAN = re.compile(
    r'^(.*?)(#[0-9]{1,3})\s+(0x[a-fA-F0-9]+)\s+\(<unknown\s+module>\)')
_RE_ANDROID = re.compile(
    r'^(.*?)(#[0-9]{1,3})\s+pc\s+(?:0x)?([a-fA-F0-9]+)\s+(.*)$')
_RE_COBALT = re.compile(
    r'^(.*?(?:\s|\t|^))(<unknown>|[^\s\[\]]+(?:\(.*?\))?)\s+'
    r'\[(0x[0-9a-fA-F]+)\]\s*$')
_RE_RAW = re.compile(r'^(0x[a-fA-F0-9]+)$')
_RE_GDB = re.compile(r'^(.*?)(#[0-9]{1,3})\s+(0x[a-fA-F0-9]+)\s*')


class _SymbolizerRunner:
  """Runs llvm-symbolizer in interactive mode with caching for fast lookups.

  Lifetime and Ownership:
    Typically managed as a context manager using a `with` statement to ensure
    the underlying subprocess is properly closed.

  Threading Model:
    This class is not thread-safe and is thread-affine. It should only be
    accessed from a single thread.
  """

  def __init__(self, library, symbolizer_path=None):
    self._library = library
    self._symbolizer_path = symbolizer_path or _SYMBOLIZER
    self._proc = None
    self._cache = {}

  def __enter__(self):
    return self

  def __exit__(self, exc_type, exc_val, exc_tb):
    self.close()

  def close(self):
    if self._proc:
      if self._proc.stdin:
        try:
          self._proc.stdin.close()
        except Exception:  # pylint: disable=broad-except
          pass
      if self._proc.stdout:
        try:
          self._proc.stdout.close()
        except Exception:  # pylint: disable=broad-except
          pass
      try:
        self._proc.wait()
      except Exception:  # pylint: disable=broad-except
        pass
      self._proc = None

  def symbolize(self, offset):
    """Resolves an offset using llvm-symbolizer."""
    if int(offset) < 0:
      return None
    if offset in self._cache:
      return self._cache[offset]
    try:
      if self._proc is None:
        self._proc = subprocess.Popen(  # pylint: disable=consider-using-with
            [self._symbolizer_path, '-e', self._library, '-f'],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            text=True,
            encoding='utf-8',
            errors='replace')
      self._proc.stdin.write(f'{offset}\n')
      self._proc.stdin.flush()
      lines = []
      while True:
        line = self._proc.stdout.readline()
        if not line:
          self.close()
          break
        if line == '\n':
          break
        lines.append(line.rstrip('\r\n'))
      if lines:
        self._cache[offset] = lines
        return lines
    except Exception:  # pylint: disable=broad-except
      self.close()
    return None


# pylint: disable=too-many-arguments,too-many-positional-arguments
def _Symbolize(filename=None,
               library=None,
               base_address='0',
               in_stream=None,
               out_stream=None,
               runner=None):
  """Attempts to resolve memory addresses within the file specified.

  This function iterates through lines of a stack trace. When a line is found
  that matches one of our regular expressions it will stop and invoke
  llvm-symbolizer with the offset of the symbol and the library specified. The
  results are verified and the output formatted to match whichever crash-style
  is being used.

  Args:
    filename:     The path to the file containing the stack trace.
    library:      The path to the library that is believed to have the symbol.
    base_address: The base address of the library when it was loaded and
      crashed, typically found in the logs.
    in_stream:    Optional input iterable/stream of lines.
    out_stream:   Optional output stream (defaults to sys.stdout).
    runner:       Optional _SymbolizerRunner instance.
  """
  if in_stream is None:
    if not filename or not os.path.exists(filename):
      raise ValueError(f'File not found: {filename}.')
  if runner is None:
    if not library or not os.path.exists(library):
      raise ValueError(f'Library not found: {library}.')

  out = out_stream if out_stream is not None else sys.stdout
  base = int(base_address, 0) if base_address else 0

  def _ProcessLines(lines, active_runner):
    for line in lines:
      # Address Sanitizer
      match = _RE_ASAN.match(line)
      if match:
        addr = int(match.group(3), 0)
        offset = addr if addr < base else addr - base
        results = active_runner.symbolize(str(offset))
        if results and '?' not in results[0]:
          file_line = (f' {results[1]}'
                       if len(results) > 1 and '?' not in results[1] else '')
          out.write(f'{match.group(1)}{match.group(2)} {hex(offset)} in '
                    f'{results[0]}{file_line}\n')
          continue
      # Android
      match = _RE_ANDROID.match(line)
      if match:
        addr = int(match.group(3), 16)
        offset = addr if addr < base else addr - base
        results = active_runner.symbolize(str(offset))
        if results and '?' not in results[0]:
          file_line = (f' {results[1]}'
                       if len(results) > 1 and '?' not in results[1] else '')
          out.write(f'{match.group(1)}{match.group(2)} pc {hex(offset)} in '
                    f'{results[0]}{file_line} ({match.group(4)})\n')
          continue
      # Cobalt
      match = _RE_COBALT.match(line)
      if match:
        addr = int(match.group(3), 0)
        offset = addr if addr < base else addr - base
        results = active_runner.symbolize(str(offset))
        if results and '?' not in results[0]:
          prefix = match.group(1) or '        '
          out.write(f'{prefix}{hex(offset)} [{results[0]}]\n')
          continue
      # Raw
      match = _RE_RAW.match(line)
      if match:
        addr = int(match.group(1), 0)
        offset = addr if addr < base else addr - base
        results = active_runner.symbolize(str(offset))
        if results:
          file_line = (f' in {results[1]}'
                       if len(results) > 1 and '?' not in results[1] else '')
          out.write(f'{hex(offset)} {results[0]}{file_line}\n')
          continue
      # GDB
      match = _RE_GDB.match(line)
      if match:
        addr = int(match.group(3), 0)
        offset = addr if addr < base else addr - base
        results = active_runner.symbolize(str(offset))
        if results and '?' not in results[0]:
          file_line = (f' {results[1]}'
                       if len(results) > 1 and '?' not in results[1] else '')
          out.write(f'{match.group(1)}{match.group(2)} {hex(offset)} in '
                    f'{results[0]}{file_line}\n')
          continue

      out.write(line)

  active_runner = runner if runner is not None else _SymbolizerRunner(library)
  try:
    if in_stream is not None:
      _ProcessLines(in_stream, active_runner)
    else:
      with open(filename, encoding='utf-8') as f:
        _ProcessLines(f, active_runner)
  finally:
    if runner is None:
      active_runner.close()


def main():
  arg_parser = argparse.ArgumentParser()
  arg_parser.add_argument(
      '-f',
      '--filename',
      required=True,
      help='The path to the file that contains the stack traces, crashes, or '
      'raw addresses.')
  arg_parser.add_argument(
      '-l',
      '--library',
      required=True,
      help='The path to the library that is believed to contain the addresses.')
  arg_parser.add_argument(
      'base_address',
      type=str,
      nargs='?',
      default='0',
      help='The base address of the library.')
  args, _ = arg_parser.parse_known_args()

  if not os.path.exists(_SYMBOLIZER):
    raise ValueError(
        f'Please update {__file__} with a valid llvm-symbolizer path.')

  base_address = (
      args.base_address[0]
      if isinstance(args.base_address, list) else args.base_address)
  return _Symbolize(args.filename, args.library, base_address)


if __name__ == '__main__':
  sys.exit(main())
