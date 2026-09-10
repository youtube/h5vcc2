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
"""Tool to trim Cobalt NPLB .dmp test assets to a specified duration."""

import argparse
import hashlib
import struct

_VIDEO_INFO_FORMAT = '<iiiiiiiiiii10fffiiii12f'
_VIDEO_INFO_SIZE = struct.calcsize(_VIDEO_INFO_FORMAT)


def TrimDmpFile(input_path, output_path, max_duration_sec=5.0):
  """Trims a Cobalt DMP file to a maximum duration in seconds.

  Args:
    input_path: Path to the source DMP file.
    output_path: Path to write the trimmed DMP file.
    max_duration_sec: Maximum duration in seconds to keep.

  Returns:
    A dictionary containing trimming statistics (original and new sizes,
    frame counts, durations, and output SHA1).
  """
  max_duration_us = int(max_duration_sec * 1_000_000)
  with open(input_path, 'rb') as f:
    data = f.read()

  if len(data) < 8:
    raise ValueError(f'File {input_path} is too small to be a valid DMP file.')

  offset = 0
  bom, ver = struct.unpack_from('<II', data, offset)
  offset += 8

  if bom != 0x76543210 or ver != 0x00001000:
    raise ValueError(
        f'Invalid DMP header in {input_path}: BOM={hex(bom)}, Ver={hex(ver)}')

  out_records = [data[:8]]  # Header BOM + Version
  orig_records = 0
  kept_records = 0
  max_orig_ts = 0
  max_kept_ts = 0

  while offset < len(data):
    rec_start = offset
    rec_type = struct.unpack_from('<I', data, offset)[0]
    offset += 4

    if rec_type == 0x61636667:  # acfg (audio config)
      codec = struct.unpack_from('<i', data, offset)[0]
      offset += 4
      if codec != 0:
        _, _, _, _, _, _, _, asc_size = struct.unpack_from(
            '<iHHIIHHH', data, offset)
        offset += struct.calcsize('<iHHIIHHH') + asc_size
      out_records.append(data[rec_start:offset])
    elif rec_type == 0x76636667:  # vcfg (video config)
      offset += 4
      out_records.append(data[rec_start:offset])
    elif rec_type == 0x61646174:  # adat (audio data)
      ts, drm_pres = struct.unpack_from('<qi', data, offset)
      offset += 12
      if drm_pres != 0:
        iv_size = struct.unpack_from('<i', data, offset)[0]
        offset += 4 + iv_size
        id_size = struct.unpack_from('<i', data, offset)[0]
        offset += 4 + id_size
        sub_cnt = struct.unpack_from('<i', data, offset)[0]
        offset += 4 + sub_cnt * 8
      d_size = struct.unpack_from('<I', data, offset)[0]
      offset += 4 + d_size
      _, _, _, _, _, _, _, asc_size = struct.unpack_from(
          '<iHHIIHHH', data, offset)
      offset += struct.calcsize('<iHHIIHHH') + asc_size
      orig_records += 1
      max_orig_ts = max(max_orig_ts, ts)
      if ts <= max_duration_us:
        out_records.append(data[rec_start:offset])
        kept_records += 1
        max_kept_ts = max(max_kept_ts, ts)
    elif rec_type == 0x76646174:  # vdat (video data)
      ts, drm_pres = struct.unpack_from('<qi', data, offset)
      offset += 12
      if drm_pres != 0:
        iv_size = struct.unpack_from('<i', data, offset)[0]
        offset += 4 + iv_size
        id_size = struct.unpack_from('<i', data, offset)[0]
        offset += 4 + id_size
        sub_cnt = struct.unpack_from('<i', data, offset)[0]
        offset += 4 + sub_cnt * 8
      d_size = struct.unpack_from('<I', data, offset)[0]
      offset += 4 + d_size
      offset += _VIDEO_INFO_SIZE
      orig_records += 1
      max_orig_ts = max(max_orig_ts, ts)
      if ts <= max_duration_us:
        out_records.append(data[rec_start:offset])
        kept_records += 1
        max_kept_ts = max(max_kept_ts, ts)
    else:
      raise ValueError(
          f'Unknown record type {hex(rec_type)} at byte offset {rec_start}')

  out_data = b''.join(out_records)
  with open(output_path, 'wb') as f:
    f.write(out_data)

  sha1 = hashlib.sha1(out_data).hexdigest()
  return {
      'orig_size': len(data),
      'new_size': len(out_data),
      'orig_records': orig_records,
      'kept_records': kept_records,
      'orig_duration_s': max_orig_ts / 1e6,
      'new_duration_s': max_kept_ts / 1e6,
      'sha1': sha1,
  }


def main():
  parser = argparse.ArgumentParser(description='Trim Cobalt DMP test files.')
  parser.add_argument('input', help='Input .dmp file')
  parser.add_argument('output', help='Output trimmed .dmp file')
  parser.add_argument(
      '--duration',
      type=float,
      default=5.0,
      help='Max duration in seconds (default: 5.0)')
  args = parser.parse_args()

  res = TrimDmpFile(args.input, args.output, args.duration)
  reduction = 100.0 * (1.0 - res['new_size'] / res['orig_size'])
  orig_mb = res['orig_size'] / (1024 * 1024)
  new_mb = res['new_size'] / (1024 * 1024)
  print(f'Trimmed {args.input} -> {args.output}')
  print(f'  Size: {orig_mb:.2f} MB -> {new_mb:.2f} MB ({reduction:.1f}% off)')
  print(f"  Duration: {res['orig_duration_s']:.2f}s -> "
        f"{res['new_duration_s']:.2f}s")
  print(f"  Frames: {res['orig_records']} -> {res['kept_records']}")
  print(f"  SHA1: {res['sha1']}")


if __name__ == '__main__':
  main()
