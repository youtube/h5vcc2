"""Tests for the simple JUnit parser."""
#!/usr/bin/env python3
#
# Copyright 2025 The Cobalt Authors. All Rights Reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

import json
import logging
import shutil
import tempfile
import unittest
from unittest.mock import mock_open, patch

from cobalt.tools.junit_mini_parser import find_failing_tests, main, TestFailure


class TestFindFailingTests(unittest.TestCase):
  """Test cases for find_failing_tests in junit_mini_parser."""

  def test_no_failing_tests(self):
    xml_content = """
    <testsuites>
      <testsuite name="TestSuite1">
        <testcase name="test_success_1"/>
        <testcase name="test_success_2"/>
      </testsuite>
    </testsuites>
    """
    with patch('builtins.open', mock_open(read_data=xml_content)):
      failing_tests, num_parsed = find_failing_tests(['dummy_file.xml'])
      self.assertEqual(failing_tests, {})
      self.assertEqual(num_parsed, 1)

  def test_single_failing_test(self):
    xml_content = """
    <testsuites>
      <testsuite name="TestSuiteA">
        <testcase name="test_pass"/>
        <testcase name="test_fail">
          <failure message="Assertion failed">...</failure>
        </testcase>
      </testsuite>
    </testsuites>
    """
    with patch('builtins.open', mock_open(read_data=xml_content)):
      failing_tests, num_parsed = find_failing_tests(['report.xml'])
      expected = {
          'report.xml': [
              TestFailure(
                  name='TestSuiteA.test_fail', message='Assertion failed\n...')
          ]
      }
      self.assertEqual(failing_tests, expected)
      self.assertEqual(num_parsed, 1)

  def test_directory_as_xml_input(self):
    temp_dir = tempfile.mkdtemp()
    try:
      failing_tests, num_parsed = find_failing_tests([temp_dir])
      self.assertEqual(failing_tests, {})
      self.assertEqual(num_parsed, 0)
    finally:
      shutil.rmtree(temp_dir)

  def test_multiple_failing_tests_in_single_file(self):
    xml_content = """
    <testsuites>
      <testsuite name="UnitTests">
        <testcase name="test_one_passed"/>
        <testcase name="test_two_failed">
          <failure message="Something went wrong">...</failure>
        </testcase>
        <testcase name="test_three_errored">
          <error message="An exception occurred">...</error>
        </testcase>
        <testcase name="test_four_passed"/>
      </testsuite>
    </testsuites>
    """
    with patch('builtins.open', mock_open(read_data=xml_content)):
      failing_tests, num_parsed = find_failing_tests(['results.xml'])
      expected = {
          'results.xml': [
              TestFailure(
                  name='UnitTests.test_two_failed',
                  message='Something went wrong\n...'),
              TestFailure(
                  name='UnitTests.test_three_errored',
                  message='An exception occurred\n...')
          ]
      }
      self.assertEqual(failing_tests, expected)
      self.assertEqual(num_parsed, 1)

  def test_failing_tests_in_multiple_files(self):
    xml_content1 = """
    <testsuites>
      <testsuite name="ModuleA">
        <testcase name="test_alpha_ok"/>
        <testcase name="test_beta_fail">
          <failure message="Failure A">...</failure>
        </testcase>
      </testsuite>
    </testsuites>
    """
    xml_content2 = """
    <testsuites>
      <testsuite name="ModuleB">
        <testcase name="test_gamma_error">
          <error message="Error B">...</error>
        </testcase>
        <testcase name="test_delta_ok"/>
      </testsuite>
    </testsuites>
    """
    with patch(
        'builtins.open',
        side_effect=[
            mock_open(read_data=xml_content1).return_value,
            mock_open(read_data=xml_content2).return_value
        ]):
      failing_tests, num_parsed = find_failing_tests(
          ['report1.xml', 'report2.xml'])
      expected = {
          'report1.xml': [
              TestFailure(
                  name='ModuleA.test_beta_fail', message='Failure A\n...')
          ],
          'report2.xml': [
              TestFailure(
                  name='ModuleB.test_gamma_error', message='Error B\n...')
          ]
      }
      self.assertEqual(failing_tests, expected)
      self.assertEqual(num_parsed, 2)

  def test_empty_testsuite_name(self):
    xml_content = """
    <testsuites>
      <testsuite>
        <testcase name="test_one_failed">
          <failure message="Fail msg">...</failure>
        </testcase>
      </testsuite>
    </testsuites>
    """
    with patch('builtins.open', mock_open(read_data=xml_content)):
      failing_tests, num_parsed = find_failing_tests(['empty_name.xml'])
      expected = {
          'empty_name.xml': [
              TestFailure(
                  name='empty_name.xml.test_one_failed',
                  message='Fail msg\n...')
          ]
      }
      self.assertEqual(failing_tests, expected)
      self.assertEqual(num_parsed, 1)


class TestMain(unittest.TestCase):
  """Test cases for main function in junit_mini_parser."""

  def setUp(self):
    logging.disable(logging.CRITICAL)

  def tearDown(self):
    logging.disable(logging.NOTSET)

  @patch('cobalt.tools.junit_mini_parser.find_failing_tests')
  def test_main_success(self, mock_find):
    mock_find.return_value = ({}, 1)
    self.assertEqual(main(['results.xml']), 0)

  @patch('cobalt.tools.junit_mini_parser.find_failing_tests')
  def test_main_failure(self, mock_find):
    mock_find.return_value = ({
        'results.xml': [TestFailure(name='Test.fail', message='msg')]
    }, 1)
    self.assertEqual(main(['results.xml']), 1)

  def test_main_no_files(self):
    self.assertEqual(main([]), 0)

  @patch('cobalt.tools.junit_mini_parser.find_failing_tests')
  @patch('os.makedirs')
  @patch('builtins.open', new_callable=mock_open)
  def test_generate_filter_failures(self, mock_file, mock_makedirs, mock_find):
    mock_find.return_value = ({
        'results1.xml': [
            TestFailure(name='SuiteA.test1', message='msg1'),
            TestFailure(name='SuiteA.test2', message='msg2')
        ],
        'results2.xml': [TestFailure(name='SuiteB.test3', message='msg3')]
    }, 2)

    argv = [
        '--mode', 'generate-filter', '--target', 'my_target', '--output-dir',
        '/tmp/output', 'results1.xml', 'results2.xml'
    ]
    self.assertEqual(main(argv), 0)

    mock_makedirs.assert_called_once_with('/tmp/output', exist_ok=True)
    mock_file.assert_called_once_with(
        '/tmp/output/my_target_filter.json', 'w', encoding='utf-8')

    # Check what was written.
    handle = mock_file()
    written_data = ''.join(call.args[0] for call in handle.write.call_args_list)
    parsed_json = json.loads(written_data)
    self.assertEqual(parsed_json['failing_tests'], [])
    self.assertEqual(parsed_json['tests_to_run'],
                     ['SuiteA.test1', 'SuiteA.test2', 'SuiteB.test3'])

  @patch('cobalt.tools.junit_mini_parser.find_failing_tests')
  @patch('os.makedirs')
  @patch('builtins.open', new_callable=mock_open)
  def test_generate_filter_no_failures(self, mock_file, mock_makedirs,
                                       mock_find):
    mock_find.return_value = ({}, 1)
    argv = [
        '--mode', 'generate-filter', '--target', 'my_target', '--output-dir',
        '/tmp/output', 'results1.xml'
    ]
    self.assertEqual(main(argv), 0)

    mock_makedirs.assert_called_once_with('/tmp/output', exist_ok=True)
    mock_file.assert_called_once_with(
        '/tmp/output/my_target_filter.json', 'w', encoding='utf-8')

    handle = mock_file()
    written_data = ''.join(call.args[0] for call in handle.write.call_args_list)
    parsed_json = json.loads(written_data)
    self.assertEqual(parsed_json['failing_tests'], ['*'])
    self.assertEqual(parsed_json['tests_to_run'], [])

  def test_generate_filter_missing_target(self):
    argv = [
        '--mode', 'generate-filter', '--output-dir', '/tmp/output',
        'results1.xml'
    ]
    with self.assertRaises(SystemExit):
      main(argv)

  def test_generate_filter_missing_output_dir(self):
    argv = [
        '--mode', 'generate-filter', '--target', 'my_target', 'results1.xml'
    ]
    with self.assertRaises(SystemExit):
      main(argv)

  @patch('cobalt.tools.junit_mini_parser.find_failing_tests')
  @patch('os.makedirs')
  @patch('builtins.open', side_effect=IOError('Mocked write error'))
  def test_generate_filter_io_error(self, mock_file, mock_makedirs, mock_find):
    mock_find.return_value = ({}, 1)
    argv = [
        '--mode', 'generate-filter', '--target', 'my_target', '--output-dir',
        '/tmp/output', 'results1.xml'
    ]
    self.assertEqual(main(argv), 1)

    mock_makedirs.assert_called_once_with('/tmp/output', exist_ok=True)
    mock_file.assert_called_once_with(
        '/tmp/output/my_target_filter.json', 'w', encoding='utf-8')

  @patch('cobalt.tools.junit_mini_parser.find_failing_tests')
  @patch('os.makedirs')
  @patch('builtins.open', new_callable=mock_open)
  def test_generate_filter_parse_error(self, mock_file, mock_makedirs,
                                       mock_find):
    mock_find.return_value = ({}, 1)
    argv = [
        '--mode', 'generate-filter', '--target', 'my_target', '--output-dir',
        '/tmp/output', 'results1.xml', 'results2.xml'
    ]
    self.assertEqual(main(argv), 0)
    mock_makedirs.assert_not_called()
    mock_file.assert_not_called()

  @patch('cobalt.tools.junit_mini_parser.find_failing_tests')
  @patch('os.makedirs')
  @patch('builtins.open', new_callable=mock_open)
  def test_generate_filter_no_files_parsed(self, mock_file, mock_makedirs,
                                           mock_find):
    mock_find.return_value = ({}, 0)
    argv = [
        '--mode', 'generate-filter', '--target', 'my_target', '--output-dir',
        '/tmp/output', 'results1.xml'
    ]
    self.assertEqual(main(argv), 0)
    mock_makedirs.assert_not_called()
    mock_file.assert_not_called()


if __name__ == '__main__':
  unittest.main()
