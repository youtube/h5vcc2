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
"""Cobalt DWARF Callstack Symbolizer.

This script parses and symbolizes hexadecimal program counters inside
in-process heap traces using local unstripped binaries and llvm-symbolizer.
"""

import argparse
import json
import os
import re
import subprocess
import sys


def find_repo_root():
  """Walks upwards to find the Cobalt repository root."""
  curr = os.getcwd()
  while curr and curr != "/":
    if os.path.exists(os.path.join(curr, "cobalt")) and os.path.exists(
        os.path.join(curr, "third_party")):
      return curr
    curr = os.path.dirname(curr)
  return None


def find_unstripped_library(repo_root):
  """Scans standard Cobalt build directories for libchrobalt.so."""
  standard_subpaths = [
      "out/android-arm_devel/lib.unstripped/libchrobalt.so",
      "out/android-arm_gold/lib.unstripped/libchrobalt.so",
      "out/android-arm_debug/lib.unstripped/libchrobalt.so",
      "out/android-arm64_devel/lib.unstripped/libchrobalt.so",
      "out/android-arm64_gold/lib.unstripped/libchrobalt.so",
  ]
  for subpath in standard_subpaths:
    full_path = os.path.join(repo_root, subpath)
    if os.path.exists(full_path):
      return full_path
  return None


def main():
  parser = argparse.ArgumentParser(
      description=(
          "Cobalt DWARF Callstack Symbolizer.\n"
          "Resolves raw program counters inside in-process heap traces "
          "using local unstripped binaries."))
  parser.add_argument(
      "trace_path",
      help="Path to the JSON trace file to symbolize (e.g. /tmp/c26_raw.json)")
  parser.add_argument(
      "-l",
      "--lib_path",
      help=("Path to the unstripped libchrobalt.so. If omitted, "
            "will auto-detect relative to the Cobalt repository root."))
  parser.add_argument(
      "-s",
      "--symbolizer_path",
      help=("Path to llvm-symbolizer. If omitted, will auto-detect "
            "inside the toolchain, falling back to system PATH."))

  args = parser.parse_args()

  # 1. Validate Trace Path
  trace_path = os.path.abspath(args.trace_path)
  if not os.path.exists(trace_path):
    print(f"Error: Trace file not found at: {trace_path}")
    sys.exit(1)

  # 2. Discover Repo Root and Auto-detect Library Path
  repo_root = find_repo_root()
  lib_path = args.lib_path

  if not lib_path:
    if repo_root:
      lib_path = find_unstripped_library(repo_root)
      if lib_path:
        rel_lib_path = os.path.relpath(lib_path, repo_root)
        print(f"💡 Auto-detected unstripped library in build folder: "
              f"{rel_lib_path}")
      else:
        print("Error: Could not find an unstripped libchrobalt.so "
              "in standard 'out/' directories.")
        print("Please compile a build or specify the path explicitly "
              "using the '-l' / '--lib_path' argument.")
        sys.exit(1)
    else:
      print("Error: Running outside of a Cobalt repository directory, "
            "and no library path was provided.")
      print("Please run this script inside your Cobalt workspace, "
            "or specify the library path using '-l'.")
      sys.exit(1)
  else:
    lib_path = os.path.abspath(lib_path)
    if not os.path.exists(lib_path):
      print(f"Error: Specified library not found at: {lib_path}")
      sys.exit(1)

  # 3. Auto-detect Toolchain llvm-symbolizer
  symbolizer_path = args.symbolizer_path
  if not symbolizer_path:
    toolchain_path = None
    if repo_root:
      toolchain_path = os.path.join(
          repo_root,
          "third_party/llvm-build/Release+Asserts/bin/llvm-symbolizer")

    if toolchain_path and os.path.exists(toolchain_path):
      symbolizer_path = toolchain_path
      rel_sym_path = os.path.relpath(symbolizer_path, repo_root)
      print(f"💡 Using toolchain prebuilt symbolizer: {rel_sym_path}")
    else:
      symbolizer_path = "llvm-symbolizer"
      print("💡 Toolchain symbolizer not found. "
            "Falling back to system 'llvm-symbolizer'.")
  else:
    if symbolizer_path != "llvm-symbolizer" and not os.path.exists(
        symbolizer_path):
      print(f"Warning: Specified symbolizer not found at {symbolizer_path}."
            f" Falling back to system 'llvm-symbolizer'.")
      symbolizer_path = "llvm-symbolizer"

  # 4. Present Execution Profile
  print("============================================================")
  print("🚀 RUNNING COBALT HEAP SYMBOLIZER")
  print(f"   📁 Trace File:       {trace_path}")
  print(f"   ⚙️  Unstripped Lib:   {lib_path}")
  print(f"   🛠️  LLVM Symbolizer:  {symbolizer_path}")
  print("============================================================")

  print("Loading trace file...")
  with open(trace_path, "r", encoding="utf-8") as f:
    trace_string = f.read()

  # Step 5: Find the base load address of libchrobalt.so
  print("Extracting libchrobalt.so memory mapping from trace...")
  regex_pattern = (r"\\\"?mf\\\"?:\s*\\\"?([^\\\"]*libchrobalt.so)\\\"?,"
                   r"\s*\\\"?pf\\\"?:\s*5,"
                   r"\s*\\\"?sa\\\"?:\s*\\\"?([0-9a-fA-F]+)\\\"?,"
                   r"\s*\\\"?sz\\\"?:\s*\\\"?([0-9a-fA-F]+)\\\"?")
  match = re.search(regex_pattern, trace_string)

  if not match:
    relaxed_pattern = (r"libchrobalt.so.*?pf.*?5.*?sa.*?([0-9a-fA-F]+)"
                       r".*?sz.*?([0-9a-fA-F]+)")
    match = re.search(relaxed_pattern, trace_string)

  if not match:
    print("Error: Could not find libchrobalt.so executable mapping in trace!")
    sys.exit(1)

  if match.lastindex == 3:
    base_address_hex = match.group(2)
    size_hex = match.group(3)
  else:
    base_address_hex = match.group(1)
    size_hex = match.group(2)

  base_address = int(base_address_hex, 16)
  size = int(size_hex, 16)

  print(f"🎉 Found libchrobalt.so base load address: 0x{base_address_hex} "
        f"(Size: 0x{size_hex} bytes)")

  trace_data = json.loads(trace_string)

  # Step 6: Locate heaps_v2 and extract all PC strings across all snapshots
  events = [
      x for x in (trace_data if isinstance(trace_data, list) else trace_data
                  .get("traceEvents", []))
      if x.get("name") == "periodic_interval"
  ]

  entry_map = {}
  pc_count = 0

  for e in events:
    dumps = e.get("args", {}).get("dumps", {})
    temp = json.loads(dumps) if isinstance(dumps, str) else dumps
    if isinstance(temp, dict) and "heaps_v2" in temp:
      heaps = temp["heaps_v2"]
      strings_table = heaps.get("maps", {}).get("strings", [])
      for entry in strings_table:
        s = entry.get("string", "")
        if s.startswith("pc:"):
          pc_hex = s[3:]
          if pc_hex not in entry_map:
            entry_map[pc_hex] = []
          entry_map[pc_hex].append(entry)
          pc_count += 1

  print(f"Found {len(entry_map)} unique raw program counters "
        f"({pc_count} total occurrences) across all snapshots.")

  if not entry_map:
    print("No program counters to resolve!")
    sys.exit(0)

  # Step 7: Resolve PCs in bulk using llvm-symbolizer
  print("Resolving C++ symbols...")
  offsets = []
  offset_to_entries = {}

  for pc_hex, entries in entry_map.items():
    pc_val = int(pc_hex, 16)
    if base_address <= pc_val < base_address + size:
      offset = pc_val - base_address
      offset_str = f"0x{offset:x}"
      offsets.append(offset_str)
      offset_to_entries[offset_str] = entries

  print(f"Resolving {len(offsets)} offsets belonging to libchrobalt.so...")

  stdout = ""
  if offsets:
    cmd = [symbolizer_path, "--demangle", "--no-inlines", f"--obj={lib_path}"
          ] + offsets
    with subprocess.Popen(
        cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        text=True) as process:
      stdout, stderr = process.communicate()

    if process.returncode != 0:
      print(f"Error running llvm-symbolizer: {stderr}")
      sys.exit(1)

  lines = stdout.split("\n")

  resolved_count = 0
  for i in range(len(offsets)):
    func = lines[3 * i].strip()
    loc = lines[3 * i + 1].strip()

    if func == "??" or not func:
      symbol = f"Unresolved [offset: {offsets[i]}]"
    else:
      if "net/" in loc:
        loc = "net/" + loc.split("net/")[1]
      elif "base/" in loc:
        loc = "base/" + loc.split("base/")[1]
      elif "cobalt/" in loc:
        loc = "cobalt/" + loc.split("cobalt/")[1]
      elif "third_party/" in loc:
        loc = "third_party/" + loc.split("third_party/")[1]

      symbol = f"{func} ({loc})"
      resolved_count += 1

    offset_key = offsets[i]
    for entry in offset_to_entries[offset_key]:
      entry["string"] = symbol

  print(f"🎉 Successfully resolved {resolved_count} "
        f"out of {len(offsets)} C++ symbols!")

  # Step 8: Save back to disk in-place
  print("Saving symbolized trace...")
  with open(trace_path, "w", encoding="utf-8") as f:
    json.dump(trace_data, f)

  # Step 9: Self-Verification Phase
  print("\nVerifying symbolized trace mapping...")
  last_heaps = None
  for e in reversed(events):
    dumps = e.get("args", {}).get("dumps", {})
    temp = json.loads(dumps) if isinstance(dumps, str) else dumps
    if isinstance(temp, dict) and "heaps_v2" in temp:
      last_heaps = temp["heaps_v2"]
      break

  if last_heaps:
    strings_table = last_heaps.get("maps", {}).get("strings", [])
    pcs = [
        s["string"]
        for s in strings_table
        if "string" in s and s["string"].startswith("pc:")
    ]
    resolved = [
        s["string"]
        for s in strings_table
        if "string" in s and not s["string"].startswith("pc:")
    ]
    print("   📊 Verification Stats (Latest Snapshot):")
    print(f"      • Total Strings in Maps:    {len(strings_table)}")
    print(f"      • Fully Symbolized C++:     {len(resolved)}")
    print(f"      • Unresolved System PCs:    {len(pcs)}")
    print("   Sample Resolved C++ Symbols:")
    for s in resolved[:10]:
      print(f"      - {s}")
    print()

  print("============================================================")
  print("🎉 SYMBOLIZATION COMPLETELY SUCCESSFUL!")
  print(f"   📁 Output Path: {trace_path}")
  print("============================================================")


if __name__ == "__main__":
  main()
