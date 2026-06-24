"""Cobalt Differential Heap Profile Analyzer.

This script parses and compares DWARF C++ symbolized JSON heap traces,
groups allocations by browser subsystems, and outputs a differential report.
"""

import argparse
import json
import os
import sys


def parse_trace(trace_path):
  """Parses a symbolized JSON trace and returns structured allocations."""
  if not os.path.exists(trace_path):
    print(f"Error: File not found: {trace_path}")
    sys.exit(1)

  print(f"Parsing trace: {os.path.basename(trace_path)}...")
  with open(trace_path, "r", encoding="utf-8") as f:
    try:
      data = json.load(f)
    except json.JSONDecodeError:
      f.seek(0)
      data = json.loads(f.read())

  events = data if isinstance(data, list) else data.get("traceEvents", [])

  # Find the last periodic_interval event (steady state!)
  periodic_events = [e for e in events if e.get("name") == "periodic_interval"]
  if not periodic_events:
    print(f"Error: No periodic_interval memory dumps found in {trace_path}!")
    sys.exit(1)

  # Find the latest periodic_interval event that contains heaps_v2 data
  last_event = None
  dumps_dict = None
  for e in reversed(periodic_events):
    dumps = e.get("args", {}).get("dumps", {})
    temp_dict = json.loads(dumps) if isinstance(dumps, str) else dumps
    if isinstance(temp_dict, dict) and "heaps_v2" in temp_dict:
      last_event = e
      dumps_dict = temp_dict
      break

  if not last_event:
    print(f"Error: No heaps_v2 data found in ANY memory dump of {trace_path}!")
    sys.exit(1)

  heaps_v2 = dumps_dict.get("heaps_v2", {})

  maps = heaps_v2.get("maps", {})
  nodes_list = maps.get("nodes", [])
  strings_list = maps.get("strings", [])

  # Index strings by ID
  strings_dict = {
      s["id"]: s["string"] for s in strings_list if "id" in s and "string" in s
  }
  # Index nodes by ID
  nodes_dict = {n["id"]: n for n in nodes_list if "id" in n}

  allocators = heaps_v2.get("allocators", {})

  allocations = []

  for alloc_name, alloc_data in allocators.items():
    counts = alloc_data.get("counts", [])
    sizes = alloc_data.get("sizes", [])
    nodes = alloc_data.get("nodes", [])

    for i in range(len(nodes)):
      node_id = nodes[i]
      size = sizes[i]
      count = counts[i]

      # Reconstruct the callstack
      callstack = []
      curr_id = node_id
      while curr_id is not None:
        node = nodes_dict.get(curr_id)
        if not node:
          break
        name_sid = node.get("name_sid")
        symbol = strings_dict.get(name_sid, f"pc:{name_sid}")

        # Filter out internal profiler tracking/interception frames
        if not any(x in symbol for x in [
            "PoissonAllocationSampler",
            "DispatcherImpl",
            "PartitionAllocatorAllocationHook",
            "AllocFn",
            "AllocAlignedFn",
            "__wrap_posix_memalign",
            "__wrap_malloc",
            "__wrap_memalign",
            "__wrap_realloc",
            "__wrap_calloc",
        ]):
          callstack.append(symbol)
        curr_id = node.get("parent")

      # Reverse so that the root frame is first
      callstack.reverse()

      allocations.append({
          "allocator": alloc_name,
          "size": size,
          "count": count,
          "callstack": callstack,
      })

  return allocations


def attribute_subsystem(callstack):
  """Attributes a callstack to a browser subsystem."""
  for frame in reversed(callstack):
    if "v8::internal" in frame or "v8::" in frame:
      return "V8 Javascript Engine"
    if "PartitionAlloc" in frame or "blink::" in frame or "WTF::" in frame:
      return "Blink DOM & Rendering"
    if "Sk" in frame or "Gr" in frame or "Skia" in frame:
      return "Skia Graphics & Textures"
    if "mojo" in frame or "IPC::" in frame:
      return "Mojo & IPC Channels"
    if "cobalt::" in frame:
      return "Cobalt Browser Shell"
    if "net::" in frame:
      return "Network Stack"
  return "Other Native / libc malloc"


def format_size(bytes_val):
  """Formats raw bytes to human readable sizes."""
  sign = "-" if bytes_val < 0 else ""
  bytes_val = abs(bytes_val)
  if bytes_val >= 1024 * 1024:
    return f"{sign}{bytes_val / (1024*1024):.2f} MB"
  elif bytes_val >= 1024:
    return f"{sign}{bytes_val / 1024:.2f} KB"
  else:
    return f"{sign}{bytes_val} Bytes"


def generate_report(base_allocs, target_allocs, output_md_path):
  """Generates a markdown comparison report."""
  print("Aggregating allocations by subsystem...")

  # 1. Aggregate by subsystem
  base_subs = {}
  if base_allocs:
    for alloc in base_allocs:
      sub = attribute_subsystem(alloc["callstack"])
      base_subs[sub] = base_subs.get(sub, 0) + alloc["size"]

  target_subs = {}
  for alloc in target_allocs:
    sub = attribute_subsystem(alloc["callstack"])
    target_subs[sub] = target_subs.get(sub, 0) + alloc["size"]

  # 2. Aggregate by unique callstacks
  def get_stack_key(alloc):
    joined_stack = " -> ".join(alloc["callstack"][-5:])
    return alloc["allocator"] + "::" + joined_stack

  base_stacks = {}
  if base_allocs:
    for alloc in base_allocs:
      key = get_stack_key(alloc)
      base_stacks[key] = base_stacks.get(key, 0) + alloc["size"]

  target_stacks = {}
  for alloc in target_allocs:
    key = get_stack_key(alloc)
    target_stacks[key] = target_stacks.get(key, 0) + alloc["size"]

  # Write Markdown Report
  with open(output_md_path, "w", encoding="utf-8") as f:
    f.write("# 📊 Cobalt Native Heap Memory Diagnosis Report\n\n")

    if base_allocs:
      f.write("> [!NOTE]\n")
      f.write("> This is a **differential heap comparison report** "
              "analyzing the memory increase between Cobalt 26 (Base) "
              "and Cobalt 27 (Target).\n\n")
    else:
      f.write("> [!NOTE]\n")
      f.write("> This is a **single-profile heap diagnostic report** "
              "analyzing the memory footprint of Cobalt 27.\n\n")

    # Section 1: Subsystem Breakdown Table
    f.write("## 1. Subsystem Memory Breakdown\n")
    f.write("This table attributes every in-process PartitionAlloc, V8, "
            "and malloc allocation to its corresponding browser subsystem.\n\n")

    if base_allocs:
      f.write("| Subsystem | Cobalt 26 (Base) | Cobalt 27 (Target) | "
              "Delta Size | Delta % |\n")
      f.write("| :--- | :--- | :--- | :--- | :--- |\n")

      all_subs = sorted(
          list(set(base_subs.keys()) | set(target_subs.keys())),
          key=lambda x: target_subs.get(x, 0) - base_subs.get(x, 0),
          reverse=True,
      )
      for sub in all_subs:
        b_size = base_subs.get(sub, 0)
        t_size = target_subs.get(sub, 0)
        delta = t_size - b_size
        delta_pct = (delta / b_size * 100) if b_size > 0 else 100.0
        pct_str = (f"{delta_pct:+.1f}%" if b_size > 0 else "New Subsystem")
        f.write(f"| **{sub}** | {format_size(b_size)} | "
                f"{format_size(t_size)} | **{format_size(delta)}** | "
                f"{pct_str} |\n")
    else:
      f.write("| Subsystem | Allocated Size | Percentage |\n")
      f.write("| :--- | :--- | :--- |\n")
      total_size = sum(target_subs.values())
      for sub, size in sorted(
          target_subs.items(), key=lambda x: x[1], reverse=True):
        pct = (size / total_size * 100) if total_size > 0 else 0
        f.write(f"| **{sub}** | {format_size(size)} | {pct:.1f}% |\n")
    f.write("\n")

    # Section 2: Top Allocation Hotspots
    if base_allocs:
      f.write("## 2. Top Memory Regressions (Delta Comparison)\n")
      f.write("These are the individual C++ callstacks that saw the "
              "largest increase in memory allocations between Cobalt 26 "
              "and 27.\n\n")
      f.write("| Rank | Allocator | C++ Allocation Callstack (Leaf Frames) "
              "| C26 Size | C27 Size | Delta Size |\n")
      f.write("| :--- | :--- | :--- | :--- | :--- | :--- |\n")

      diff_stacks = []
      all_keys = set(base_stacks.keys()) | set(target_stacks.keys())
      for key in all_keys:
        b_size = base_stacks.get(key, 0)
        t_size = target_stacks.get(key, 0)
        diff_stacks.append((key, b_size, t_size, t_size - b_size))

      # Sort by delta descending
      diff_stacks.sort(key=lambda x: x[3], reverse=True)

      rank = 1
      for key, b_size, t_size, delta in diff_stacks[:30]:
        if delta <= 0:
          continue
        parts = key.split("::")
        allocator = parts[0]
        callstack_str = "::".join(parts[1:])
        callstack_formatted = callstack_str.replace(" -> ", "<br>↳ ")
        f.write(f"| {rank} | `{allocator}` | `{callstack_formatted}` | "
                f"{format_size(b_size)} | {format_size(t_size)} | "
                f"**{format_size(delta)}** |\n")
        rank += 1
    else:
      f.write("## 2. Top 30 Allocation Hotspots (Cobalt 27)\n")
      f.write("These are the individual C++ callstacks responsible for "
              "the largest memory allocations in Cobalt 27.\n\n")
      f.write("| Rank | Allocator | C++ Allocation Callstack (Leaf "
              "Frames) | Size |\n")
      f.write("| :--- | :--- | :--- | :--- |\n")

      sorted_stacks = sorted(
          target_stacks.items(), key=lambda x: x[1], reverse=True)
      rank = 1
      for key, size in sorted_stacks[:30]:
        parts = key.split("::")
        allocator = parts[0]
        callstack_str = "::".join(parts[1:])
        callstack_formatted = callstack_str.replace(" -> ", "<br>↳ ")
        f.write(f"| {rank} | `{allocator}` | `{callstack_formatted}` | "
                f"**{format_size(size)}** |\n")
        rank += 1

  print(f"Report successfully generated and saved to: {output_md_path}")


def main():
  """Main runner function."""
  parser = argparse.ArgumentParser(
      description="Cobalt Differential Heap Profile Analyzer")
  parser.add_argument("target", help="Path to Cobalt 27 symbolized JSON trace")
  parser.add_argument(
      "--base", help="Optional path to Cobalt 26 symbolized JSON trace")
  parser.add_argument(
      "-o",
      "--output",
      required=True,
      help="Path to output markdown report file",
  )

  args = parser.parse_args()

  target_allocs = parse_trace(args.target)
  base_allocs = parse_trace(args.base) if args.base else None

  generate_report(base_allocs, target_allocs, args.output)


if __name__ == "__main__":
  main()
