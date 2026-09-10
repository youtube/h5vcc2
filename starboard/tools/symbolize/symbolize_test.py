#!/usr/bin/env python3
# Copyright 2026 The Cobalt Authors. All Rights Reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
"""Unit and integration tests for symbolize.py."""

# pylint: disable=protected-access,wrong-import-position,consider-using-with

import io
import os
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

_SRC_DIR = os.path.abspath(
    os.path.join(os.path.dirname(__file__), os.pardir, os.pardir, os.pardir))
if _SRC_DIR not in sys.path:
  sys.path.insert(0, _SRC_DIR)

from starboard.tools import paths
from starboard.tools.symbolize import symbolize

_CLANG = os.path.join(paths.REPOSITORY_ROOT, 'third_party', 'llvm-build',
                      'Release+Asserts', 'bin', 'clang')
_LLVM_NM = os.path.join(paths.REPOSITORY_ROOT, 'third_party', 'llvm-build',
                        'Release+Asserts', 'bin', 'llvm-nm')
_SYMBOLIZER = os.path.join(paths.REPOSITORY_ROOT, 'third_party', 'llvm-build',
                           'Release+Asserts', 'bin', 'llvm-symbolizer')
_TESTDATA_DIR = os.path.join(os.path.dirname(__file__), 'testdata')


class _FakeSymbolizerRunner:
  """Mock runner to supply deterministic responses without spawning LLVM."""

  def __init__(self, responses=None, default_response=None):
    self._responses = responses or {}
    self._default_response = default_response
    self.calls = []
    self.closed = False

  def __enter__(self):
    return self

  def __exit__(self, exc_type, exc_val, exc_tb):
    self.close()

  def close(self):
    self.closed = True

  def symbolize(self, offset):
    self.calls.append(offset)
    if offset in self._responses:
      return self._responses[offset]
    return self._default_response


class SymbolizeUnitTests(unittest.TestCase):
  """Tests parsing, regexes, offset arithmetic, and streams in isolation."""

  def test_rdk_syslog_prefix_preservation(self):
    line = ('[2026-09-01 01:28:54:229 PDT] Sep 01 08:28:53 AmlogicFirebolt '
            'YouTube[2001]: \t<unknown> [0x10e2de2]\n')
    runner = _FakeSymbolizerRunner({
        str(0x10e2de2): [
            'cobalt::CobaltBrowserMainParts::PreCreateThreads()',
            'cobalt_browser_main_parts.cc:316'
        ]
    })
    in_stream = io.StringIO(line)
    out_stream = io.StringIO()
    symbolize._Symbolize(
        in_stream=in_stream,
        out_stream=out_stream,
        runner=runner,
        base_address='0')
    expected = ('[2026-09-01 01:28:54:229 PDT] Sep 01 08:28:53 AmlogicFirebolt '
                'YouTube[2001]: \t0x10e2de2 '
                '[cobalt::CobaltBrowserMainParts::PreCreateThreads()]\n')
    self.assertEqual(out_stream.getvalue(), expected)

  def test_android_logcat_prefix_and_tombstone(self):
    logcat_line = (
        '09-08 17:27:44.416  8169  8169 E chromium: #00 pc 0x03bef795 '
        '/data/app/dev.cobalt.coat/lib/arm/libchrobalt.so\n')
    tombstone_line = (
        '09-08 17:27:46.387  8283  8283 F DEBUG   :       #00 pc 01115620  '
        '/data/app/dev.cobalt.coat/lib/arm/libchrobalt.so (BuildId: 3abe1846)\n'
    )
    runner = _FakeSymbolizerRunner({
        str(0x3bef795): [
            'base::debug::StackTrace::StackTrace()', 'stack_trace.cc:255'
        ],
        str(0x1115620): [
            'cobalt::CobaltBrowserMainParts::PreCreateThreads()',
            'cobalt_browser_main_parts.cc:319'
        ]
    })
    in_stream = io.StringIO(logcat_line + tombstone_line)
    out_stream = io.StringIO()
    symbolize._Symbolize(
        in_stream=in_stream,
        out_stream=out_stream,
        runner=runner,
        base_address='0')
    output = out_stream.getvalue().splitlines(keepends=True)
    self.assertEqual(len(output), 2)
    self.assertIn(
        '09-08 17:27:44.416  8169  8169 E chromium: #00 pc 0x3bef795 in '
        'base::debug::StackTrace::StackTrace() stack_trace.cc:255 '
        '(/data/app/dev.cobalt.coat/lib/arm/libchrobalt.so)\n', output[0])
    self.assertIn(
        '09-08 17:27:46.387  8283  8283 F DEBUG   :       #00 pc 0x1115620 in '
        'cobalt::CobaltBrowserMainParts::PreCreateThreads() '
        'cobalt_browser_main_parts.cc:319 '
        '(/data/app/dev.cobalt.coat/lib/arm/libchrobalt.so '
        '(BuildId: 3abe1846))\n', output[1])

  def test_asan_format(self):
    line = '    #1 0x7fdc59bbaa6b  (<unknown module>)\n'
    runner = _FakeSymbolizerRunner(
        {str(0x7fdc59bbaa6b): ['asan_detected_leak()', 'asan.cc:42']})
    in_stream = io.StringIO(line)
    out_stream = io.StringIO()
    symbolize._Symbolize(
        in_stream=in_stream,
        out_stream=out_stream,
        runner=runner,
        base_address='0')
    self.assertEqual(
        out_stream.getvalue(),
        '    #1 0x7fdc59bbaa6b in asan_detected_leak() asan.cc:42\n')

  def test_gdb_format(self):
    line = '    #1  0x742a51b6 in ?? () from /lib/libcobalt.so\n'
    runner = _FakeSymbolizerRunner(
        {str(0x742a51b6): ['gdb_resolved_func()', 'gdb.cc:10']})
    in_stream = io.StringIO(line)
    out_stream = io.StringIO()
    symbolize._Symbolize(
        in_stream=in_stream,
        out_stream=out_stream,
        runner=runner,
        base_address='0')
    self.assertEqual(out_stream.getvalue(),
                     '    #1 0x742a51b6 in gdb_resolved_func() gdb.cc:10\n')

  def test_raw_format(self):
    line = '0x7efcdf1fd52b\n'
    runner = _FakeSymbolizerRunner(
        {str(0x7efcdf1fd52b): ['raw_symbol()', 'raw.cc:99']})
    in_stream = io.StringIO(line)
    out_stream = io.StringIO()
    symbolize._Symbolize(
        in_stream=in_stream,
        out_stream=out_stream,
        runner=runner,
        base_address='0')
    self.assertEqual(out_stream.getvalue(),
                     '0x7efcdf1fd52b raw_symbol() in raw.cc:99\n')

  def test_unresolved_question_marks_left_unmodified(self):
    line = '        <unknown> [0x12345]\n'
    runner = _FakeSymbolizerRunner({str(0x12345): ['??', '??:0']})
    in_stream = io.StringIO(line)
    out_stream = io.StringIO()
    symbolize._Symbolize(
        in_stream=in_stream,
        out_stream=out_stream,
        runner=runner,
        base_address='0')
    self.assertEqual(out_stream.getvalue(), line)

  def test_single_line_response_no_index_error(self):
    line = '        <unknown> [0x12345]\n'
    runner = _FakeSymbolizerRunner({str(0x12345): ['only_func_name']})
    in_stream = io.StringIO(line)
    out_stream = io.StringIO()
    symbolize._Symbolize(
        in_stream=in_stream,
        out_stream=out_stream,
        runner=runner,
        base_address='0')
    self.assertEqual(out_stream.getvalue(),
                     '        0x12345 [only_func_name]\n')

  def test_offset_arithmetic_relative_vs_absolute(self):
    base_addr = '0x7f0000000000'
    rel_line = '        <unknown> [0x1000]\n'
    abs_line = '        <unknown> [0x7f0000002000]\n'
    runner = _FakeSymbolizerRunner({
        str(0x1000): ['func_rel()'],
        str(0x2000): ['func_abs()']
    })
    in_stream = io.StringIO(rel_line + abs_line)
    out_stream = io.StringIO()
    symbolize._Symbolize(
        in_stream=in_stream,
        out_stream=out_stream,
        runner=runner,
        base_address=base_addr)
    self.assertIn(str(0x1000), runner.calls)
    self.assertIn(str(0x2000), runner.calls)

  def test_symbolizer_runner_dead_pipe_recovery(self):
    """Verifies that if the symbolizer process dies, it safely closes."""
    runner = symbolize._SymbolizerRunner(
        library='/nonexistent/lib.so', symbolizer_path='/bin/false')
    res = runner.symbolize('4096')
    self.assertIsNone(res)
    self.assertIsNone(runner._proc)

  def test_symbolizer_runner_caching(self):
    runner = symbolize._SymbolizerRunner(library='/nonexistent/lib.so')
    runner._cache['1234'] = ['cached_func()', 'file.cc:1']
    self.assertEqual(runner.symbolize('1234'), ['cached_func()', 'file.cc:1'])
    self.assertIsNone(runner._proc)

  def test_golden_collected_files_process_cleanly(self):
    """Verifies all collected hardware and platform logs parse cleanly."""
    for filename in [
        'rdk_syslog_stack.log', 'rdk_device_live.log',
        'evergreen_linux_stack.log', 'android_chromecast_stack.log'
    ]:
      log_path = os.path.join(_TESTDATA_DIR, filename)
      if os.path.exists(log_path):
        runner = _FakeSymbolizerRunner(
            default_response=['mock_resolved_symbol()', 'file.cc:1'])
        out_stream = io.StringIO()
        with open(log_path, 'r', encoding='utf-8') as f:
          symbolize._Symbolize(
              in_stream=f,
              out_stream=out_stream,
              runner=runner,
              base_address='0')
        self.assertGreater(len(out_stream.getvalue()), 0)
        self.assertGreater(len(runner.calls), 0)
        self.assertIn('mock_resolved_symbol', out_stream.getvalue())

  def test_symbolize_raises_on_missing_file(self):
    with self.assertRaises(ValueError) as ctx:
      symbolize._Symbolize(
          filename='/nonexistent/trace/file.log',
          runner=_FakeSymbolizerRunner())
    self.assertIn('File not found', str(ctx.exception))

  def test_symbolize_raises_on_missing_library(self):
    with self.assertRaises(ValueError) as ctx:
      symbolize._Symbolize(
          in_stream=io.StringIO('test'), library='/nonexistent/lib.so')
    self.assertIn('Library not found', str(ctx.exception))

  def test_symbolize_reads_from_file_path(self):
    with tempfile.NamedTemporaryFile(mode='w+', encoding='utf-8') as f:
      f.write('        <unknown> [0x1000]\n')
      f.flush()
      runner = _FakeSymbolizerRunner(
          {str(0x1000): ['func_from_file()', 'file.cc:10']})
      out_stream = io.StringIO()
      symbolize._Symbolize(
          filename=f.name,
          out_stream=out_stream,
          runner=runner,
          base_address='0')
      self.assertIn('func_from_file()', out_stream.getvalue())

  def test_main_cli_argument_parsing(self):
    with tempfile.NamedTemporaryFile(mode='w+', encoding='utf-8') as f_trace, \
         tempfile.NamedTemporaryFile(mode='w+', encoding='utf-8') as f_lib:
      test_args = [
          'symbolize.py', '-f', f_trace.name, '-l', f_lib.name, '0x1000'
      ]
      with mock.patch('sys.argv', test_args), \
           mock.patch.object(symbolize, '_Symbolize') as mock_symbolize, \
           mock.patch('os.path.exists', return_value=True):
        symbolize.main()
        mock_symbolize.assert_called_once_with(f_trace.name, f_lib.name,
                                               '0x1000')

  def test_main_cli_raises_when_llvm_symbolizer_missing(self):
    test_args = ['symbolize.py', '-f', '/tmp/trace.log', '-l', '/tmp/lib.so']
    with mock.patch('sys.argv', test_args), \
         mock.patch('os.path.exists', return_value=False):
      with self.assertRaises(ValueError) as ctx:
        symbolize.main()
      self.assertIn('Please update', str(ctx.exception))

  def test_symbolizer_runner_negative_offset(self):
    runner = symbolize._SymbolizerRunner(library='/nonexistent/lib.so')
    self.assertIsNone(runner.symbolize('-1'))
    self.assertIsNone(runner.symbolize('-4096'))

  def test_symbolizer_runner_context_manager(self):
    runner = symbolize._SymbolizerRunner(library='/nonexistent/lib.so')
    mock_proc = mock.MagicMock()
    with runner:
      runner._proc = mock_proc
    mock_proc.stdin.close.assert_called_once()
    mock_proc.stdout.close.assert_called_once()
    mock_proc.wait.assert_called_once()
    self.assertIsNone(runner._proc)

  def test_symbolizer_runner_exception_during_write_closes_proc(self):
    runner = symbolize._SymbolizerRunner(library='/nonexistent/lib.so')
    mock_proc = mock.MagicMock()
    mock_proc.stdin.write.side_effect = BrokenPipeError('Broken pipe')
    runner._proc = mock_proc
    result = runner.symbolize('100')
    self.assertIsNone(result)
    self.assertIsNone(runner._proc)
    mock_proc.wait.assert_called_once()

  def test_symbolizer_runner_close_handles_exceptions_gracefully(self):
    runner = symbolize._SymbolizerRunner(library='/nonexistent/lib.so')
    mock_proc = mock.MagicMock()
    mock_proc.stdin.close.side_effect = OSError('Stream error')
    runner._proc = mock_proc
    runner.close()
    self.assertIsNone(runner._proc)
    mock_proc.stdout.close.assert_called_once()
    mock_proc.wait.assert_called_once()

  def test_symbolizer_runner_popen_failure_returns_none(self):
    runner = symbolize._SymbolizerRunner(
        library='/nonexistent/lib.so',
        symbolizer_path='/nonexistent/path/to/llvm-symbolizer')
    with mock.patch(
        'subprocess.Popen', side_effect=FileNotFoundError('No such file')):
      result = runner.symbolize('100')
      self.assertIsNone(result)
      self.assertIsNone(runner._proc)

  def test_unresolved_all_formats_left_unmodified(self):
    lines = ('    #1 0x7fdc59bbaa6b  (<unknown module>)\n'
             '#00 pc 0x03bef795 /lib/libcobalt.so\n'
             '    #1  0x742a51b6 in ?? () from /lib/libcobalt.so\n')
    runner = _FakeSymbolizerRunner(default_response=['??', '??:0'])
    in_stream = io.StringIO(lines)
    out_stream = io.StringIO()
    symbolize._Symbolize(
        in_stream=in_stream,
        out_stream=out_stream,
        runner=runner,
        base_address='0')
    self.assertEqual(out_stream.getvalue(), lines)

  def test_raw_format_includes_unresolved_symbols(self):
    line = '0x7efcdf1fd52b\n'
    runner = _FakeSymbolizerRunner({str(0x7efcdf1fd52b): ['??', '??:0']})
    in_stream = io.StringIO(line)
    out_stream = io.StringIO()
    symbolize._Symbolize(
        in_stream=in_stream,
        out_stream=out_stream,
        runner=runner,
        base_address='0')
    self.assertEqual(out_stream.getvalue(), '0x7efcdf1fd52b ??\n')

  def test_single_line_response_all_formats(self):
    input_text = ('    #1 0x7fdc59bbaa6b  (<unknown module>)\n'
                  '#00 pc 0x03bef795 /lib/libcobalt.so\n'
                  '    #1  0x742a51b6 in ?? () from /lib/libcobalt.so\n'
                  '0x7efcdf1fd52b\n')
    runner = _FakeSymbolizerRunner({
        str(0x7fdc59bbaa6b): ['asan_func'],
        str(0x3bef795): ['android_func'],
        str(0x742a51b6): ['gdb_func'],
        str(0x7efcdf1fd52b): ['raw_func']
    })
    in_stream = io.StringIO(input_text)
    out_stream = io.StringIO()
    symbolize._Symbolize(
        in_stream=in_stream,
        out_stream=out_stream,
        runner=runner,
        base_address='0')
    output = out_stream.getvalue().splitlines()
    self.assertIn('asan_func', output[0])
    self.assertIn('android_func', output[1])
    self.assertIn('gdb_func', output[2])
    self.assertIn('raw_func', output[3])

  def test_cobalt_format_with_existing_symbol_name(self):
    line = '        SbEventHandle [0x9c17b5]\n'
    runner = _FakeSymbolizerRunner(
        {str(0x9c17b5): ['NewSymbolName()', 'event.cc:50']})
    in_stream = io.StringIO(line)
    out_stream = io.StringIO()
    symbolize._Symbolize(
        in_stream=in_stream,
        out_stream=out_stream,
        runner=runner,
        base_address='0')
    self.assertEqual(out_stream.getvalue(),
                     '        0x9c17b5 [NewSymbolName()]\n')

  def test_non_matching_lines_passed_through_untouched(self):
    lines = ('\n'
             '[0909/002302.313880:INFO:thread.cc(154)] Thread started\n'
             'Check failed: g_sb_event_func.\n'
             'Some arbitrary console output\n')
    runner = _FakeSymbolizerRunner()
    in_stream = io.StringIO(lines)
    out_stream = io.StringIO()
    symbolize._Symbolize(
        in_stream=in_stream,
        out_stream=out_stream,
        runner=runner,
        base_address='0')
    self.assertEqual(out_stream.getvalue(), lines)
    self.assertEqual(len(runner.calls), 0)

  def test_hex_casing_variations(self):
    lines = ('    #1 0x7FDC59BBAA6B  (<unknown module>)\n'
             '#00 pc 0x03BEF795 /lib/libcobalt.so\n'
             '        <unknown> [0x10E2DE2]\n'
             '0x7EFCDF1FD52B\n')
    runner = _FakeSymbolizerRunner({
        str(0x7fdc59bbaa6b): ['asan_fn()'],
        str(0x3bef795): ['android_fn()'],
        str(0x10e2de2): ['cobalt_fn()'],
        str(0x7efcdf1fd52b): ['raw_fn()']
    })
    in_stream = io.StringIO(lines)
    out_stream = io.StringIO()
    symbolize._Symbolize(
        in_stream=in_stream,
        out_stream=out_stream,
        runner=runner,
        base_address='0')
    output = out_stream.getvalue()
    self.assertIn('asan_fn()', output)
    self.assertIn('android_fn()', output)
    self.assertIn('cobalt_fn()', output)
    self.assertIn('raw_fn()', output)


class SymbolizeIntegrationTests(unittest.TestCase):
  """Integration test compiling a synthetic C shared library on the fly."""

  temp_dir = None
  so_path = None
  symbols = {}

  @classmethod
  def setUpClass(cls):
    if not (os.path.exists(_CLANG) and os.path.exists(_LLVM_NM) and
            os.path.exists(_SYMBOLIZER)):
      return

    cls.temp_dir = tempfile.TemporaryDirectory()
    c_path = os.path.join(cls.temp_dir.name, 'fixture.c')
    cls.so_path = os.path.join(cls.temp_dir.name, 'fixture.so')

    with open(c_path, 'w', encoding='utf-8') as f:
      f.write("""
        int fixture_func_alpha(int a) {
          return a + 42;
        }
        int fixture_func_beta(int b) {
          return fixture_func_alpha(b) * 2;
        }
      """)

    subprocess.check_call(
        [_CLANG, '-shared', '-fPIC', '-g', '-O0', c_path, '-o', cls.so_path])

    nm_out = subprocess.check_output([_LLVM_NM, '-n', cls.so_path], text=True)
    cls.symbols = {}
    for line in nm_out.splitlines():
      parts = line.split()
      if len(parts) >= 3 and parts[2].startswith('fixture_func_'):
        cls.symbols[parts[2]] = hex(int(parts[0], 16))

  @classmethod
  def tearDownClass(cls):
    if cls.temp_dir:
      cls.temp_dir.cleanup()

  def setUp(self):
    if not (os.path.exists(_CLANG) and os.path.exists(_LLVM_NM) and
            os.path.exists(_SYMBOLIZER)):
      self.skipTest('Hermetic LLVM toolchain binaries not found in repository.')

  def test_real_llvm_symbolizer_end_to_end(self):
    alpha_offset = self.symbols['fixture_func_alpha']
    beta_offset = self.symbols['fixture_func_beta']

    input_text = (
        f'Sep 01 08:28:53 Host App[100]: \t<unknown> [{alpha_offset}]\n'
        f'09-08 17:27:44.416  8169  8169 E chromium: #01 pc {beta_offset} '
        f'{self.so_path}\n')

    in_stream = io.StringIO(input_text)
    out_stream = io.StringIO()

    symbolize._Symbolize(
        library=self.so_path,
        base_address='0',
        in_stream=in_stream,
        out_stream=out_stream)

    output = out_stream.getvalue()
    self.assertIn('Sep 01 08:28:53 Host App[100]:', output)
    self.assertIn('fixture_func_alpha', output)
    self.assertIn('fixture_func_beta', output)
    self.assertIn('fixture.c', output)

  def test_real_llvm_symbolizer_with_file_and_base_address(self):
    alpha_offset = int(self.symbols['fixture_func_alpha'], 16)
    base_addr = 0x100000
    loaded_addr = hex(base_addr + alpha_offset)

    with tempfile.NamedTemporaryFile(mode='w+', encoding='utf-8') as f:
      f.write(f'        <unknown> [{loaded_addr}]\n')
      f.flush()
      out_stream = io.StringIO()
      symbolize._Symbolize(
          filename=f.name,
          library=self.so_path,
          base_address=hex(base_addr),
          out_stream=out_stream)
      self.assertIn('fixture_func_alpha', out_stream.getvalue())


if __name__ == '__main__':
  unittest.main()
