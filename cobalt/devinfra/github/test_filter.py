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
"""Utilities for reading and evaluating Cobalt test filters."""

import argparse
import os
import sys
from typing import List, Optional, Sequence, Tuple, Union


def parse_filter_file(filter_file: str) -> Tuple[List[str], List[str]]:
  """Parses a buildbot .filter file into positive and negative filter lists.

  In .filter files:
  - Empty lines and lines starting with '#' are ignored.
  - Lines starting with '//' raise a ValueError.
  - Lines starting with '-' are treated as negative (failing/excluded) filters.
  - Lines starting with '+' or bare test names are treated as positive filters.

  Args:
    filter_file: Path to the filter file.

  Returns:
    A tuple of (positive_filters, negative_filters).
  """
  if not os.path.exists(filter_file):
    return [], []

  positive: List[str] = []
  negative: List[str] = []
  with open(filter_file, 'r', encoding='utf-8') as f:
    for line_num, raw_line in enumerate(f, start=1):
      line = raw_line.rstrip('\r\n')
      hash_pos = line.find('#')
      if hash_pos != -1:
        line = line[:hash_pos]
      trimmed = line.strip()
      if trimmed.startswith('//'):
        raise ValueError(
            f'Line {line_num} in {filter_file} starts with //, use # for'
            ' comments.')
      if not trimmed:
        continue
      if trimmed.startswith('-'):
        negative.append(trimmed[1:].strip())
      elif trimmed.startswith('+'):
        positive.append(trimmed[1:].strip())
      else:
        positive.append(trimmed)

  return positive, negative


def format_gtest_filter(positive: Sequence[str],
                        negative: Sequence[str]) -> str:
  """Formats positive and negative filter lists into a standard gtest_filter."""
  pos_str = ':'.join(filter(None, positive))
  neg_str = ':'.join(filter(None, negative))

  if pos_str and neg_str:
    return f'{pos_str}-{neg_str}'
  if pos_str:
    return pos_str
  if neg_str:
    return f'-{neg_str}'
  return '*'


def find_filter_file(
    filter_dir: str,
    target_name: str,
    shard_index: Optional[Union[int, str]] = None,
) -> Optional[str]:
  """Locates the .filter file for a target and optional shard."""
  if target_name:
    target_name = target_name.split(':')[-1]

  if shard_index is not None and str(shard_index) != '':
    shard_file = os.path.join(filter_dir, f'{target_name}_{shard_index}.filter')
    if os.path.exists(shard_file):
      return shard_file

  target_file = os.path.join(filter_dir, f'{target_name}.filter')
  if os.path.exists(target_file):
    return target_file

  return None


def get_gtest_filter(
    filter_dir: str,
    target_name: str,
    shard_index: Optional[Union[int, str]] = None,
) -> str:
  """Retrieves gtest filters for a given target and optional shard.

  Looks for a shard-specific filter file first (if shard_index is provided),
  then falls back to the target filter file.

  Args:
    filter_dir: Directory containing filter files.
    target_name: The name of the gtest target.
    shard_index: Optional shard index (e.g. 0, '0').

  Returns:
    A string containing the gtest filter (e.g. '*', '-*', 'TestA:TestB',
    '-TestA:TestB', 'TestA-TestB').
  """
  filter_file = find_filter_file(filter_dir, target_name, shard_index)
  if not filter_file:
    return '*'

  positive, negative = parse_filter_file(filter_file)
  return format_gtest_filter(positive, negative)


def main(argv: Optional[Sequence[str]] = None) -> int:
  """Main CLI entry point."""
  parser = argparse.ArgumentParser(description='Cobalt test filter utility.')
  parser.add_argument(
      '--filter-dir',
      required=True,
      help='Directory containing test filter files.')
  parser.add_argument(
      '--target', required=True, help='Name of the test target.')
  parser.add_argument('--shard', default=None, help='Optional shard index.')

  args = parser.parse_args(argv)
  print(get_gtest_filter(args.filter_dir, args.target, args.shard))
  return 0


if __name__ == '__main__':
  sys.exit(main())
