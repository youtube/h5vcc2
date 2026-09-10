#!/usr/bin/env python3
"""AI-driven GN build generation and self-healing resolver library.

Provides GNGenResolver, which executes `cobalt/build/gn.py`, parses GN
tracebacks and missing dependency graphs, prompts Gemini via Vertex AI
Reasoning Engine, applies patches with third-party guards, and verifies
clean GN manifest generation with header checks.
"""

import dataclasses
import os
import re
import subprocess
import sys
from typing import Any, Callable, Dict, List, Optional, Tuple
import warnings

from base_resolver import (
    BaseResolver,
    get_clean_build_env,
)
# Suppress google.auth UserWarning about ADC quota project on Cloudtop
warnings.filterwarnings("ignore", category=UserWarning, module="google.auth")


@dataclasses.dataclass
class GNDiagnostic:
  """Represents a GN build error diagnostic parsed from gn gen output."""

  error_message: str
  raw_output: str
  target_files: Dict[str, Optional[int]]
  is_structural_break: bool


def extract_gn_target_files(
    output: str,
    repo_path: str,
) -> Dict[str, Optional[int]]:
  """Extracts all referenced GN build files and line numbers from output."""
  unique_gn_files: Dict[str, Optional[int]] = {}

  # Priority 0: Dependency Cycle -> prioritize locally modified BUILD.gn files
  if "Dependency cycle:" in output:
    try:
      proc = subprocess.run(
          ["git", "status", "--porcelain", "--", "*.gn", "*.gni"],
          cwd=repo_path,
          capture_output=True,
          text=True,
          check=False,
      )
      for line in proc.stdout.splitlines():
        f_rel = line.strip().split()[-1]
        if f_rel.endswith((".gn", ".gni")):
          abs_f = os.path.join(repo_path, f_rel)
          if os.path.isfile(abs_f):
            unique_gn_files[abs_f] = None
    except (OSError, subprocess.SubprocessError):
      pass

  # 1. Target definitions: "The target: //dir:target" (e.g. missing sources)
  target_def_matches = re.findall(
      r"The target:\s*(?:\n\s*)?//([a-zA-Z0-9_/\.\-]+):",
      output,
  )
  for t_dir in target_def_matches:
    gn_path = os.path.join(repo_path, t_dir, "BUILD.gn")
    if os.path.isfile(gn_path) and gn_path not in unique_gn_files:
      unique_gn_files[gn_path] = None

  # 2. Caller targets missing dependency: "dependency of //dir:target"
  caller_matches = re.findall(
      r"dependency of\s*(?:\n\s*)?//([a-zA-Z0-9_/\.\-]+):",
      output,
  )
  for c_dir in caller_matches:
    gn_path = os.path.join(repo_path, c_dir, "BUILD.gn")
    if os.path.isfile(gn_path) and gn_path not in unique_gn_files:
      unique_gn_files[gn_path] = None

  # 3. Universal scan: Match ANY //path/to/file.gn[i] with optional line number
  all_gn_matches = re.findall(
      r"//([a-zA-Z0-9_/\.\-]+\.gn[i]?)(?::(\d+))?",
      output,
  )
  deferred_gn_files: Dict[str, Optional[int]] = {}
  for f, line_str in all_gn_matches:
    full_p = os.path.join(repo_path, f) if not os.path.isabs(f) else f
    if os.path.isfile(full_p) and full_p not in unique_gn_files:
      target_line = int(line_str) if line_str else None
      if f.endswith("BUILDCONFIG.gn") and ("Source file not found" in output or
                                           "The target:" in output):
        deferred_gn_files[full_p] = target_line
      else:
        unique_gn_files[full_p] = target_line

  # 4. Resolve GN targets: "target(s): //dir:target" or "needs //dir:target"
  target_matches = re.findall(
      r"(?:target(?:\(s\))?:\s+|needs\s+)//([a-zA-Z0-9_/\.\-]+):",
      output,
  )
  for t_dir in target_matches:
    gn_path = os.path.join(repo_path, t_dir, "BUILD.gn")
    if os.path.isfile(gn_path) and gn_path not in unique_gn_files:
      unique_gn_files[gn_path] = None

  # 5. Resolve source files and BUILD.gn: "ERROR at //path/to/file.cc:line"
  src_pattern = re.findall(
      r"ERROR at //([a-zA-Z0-9_/\.\-]+\.(?:cc|h|mm|cpp|c))(?::(\d+))?",
      output,
  )
  for s_rel, line_str in src_pattern:
    s_abs = os.path.join(repo_path, s_rel)
    if os.path.isfile(s_abs) and s_abs not in unique_gn_files:
      unique_gn_files[s_abs] = int(line_str) if line_str else None
    s_dir = os.path.dirname(s_rel)
    gn_path = os.path.join(repo_path, s_dir, "BUILD.gn")
    if os.path.isfile(gn_path) and gn_path not in unique_gn_files:
      unique_gn_files[gn_path] = None

  for p, line_no in deferred_gn_files.items():
    if p not in unique_gn_files:
      unique_gn_files[p] = line_no

  return unique_gn_files


class GNGenResolver(BaseResolver):
  """Self-healing resolver for cobalt/build/gn.py generation failures."""

  def __init__(
      self,
      repo_path: str,
      platform: str,
      build_type: str,
      *,
      gn_check: bool = True,
      engine: Optional[Any] = None,
      max_iterations: int = 50,
      on_patch_applied_fn: Optional[Callable[[List[str]], None]] = None,
  ):
    super().__init__(
        repo_path=repo_path,
        engine=engine,
        max_iterations=max_iterations,
        on_patch_applied_fn=on_patch_applied_fn,
    )
    self.platform = platform
    self.build_type = build_type
    self.gn_check = gn_check

  @property
  def name(self) -> str:
    return "Phase 3 (GN Generation)"

  def run_command(self, iteration: int) -> Tuple[bool, str, str]:
    del iteration  # Unused in standard GN command execution
    gn_script = os.path.join(self.repo_path, "cobalt", "build", "gn.py")
    cmd = [
        sys.executable,
        gn_script,
        "-p",
        self.platform,
        "-C",
        self.build_type,
    ]
    if self.gn_check:
      cmd.append("--check")

    cmd_str = " ".join(cmd)
    print(
        f"\n[gn_gen] Executing: {cmd_str} in {self.repo_path}",
        file=sys.stderr,
    )
    clean_env = get_clean_build_env()
    try:
      proc = subprocess.run(
          cmd,
          cwd=self.repo_path,
          capture_output=True,
          text=True,
          env=clean_env,
          check=False,
      )
      combined_output = f"{proc.stdout}\n{proc.stderr}"
      return proc.returncode == 0, combined_output, ""
    except Exception as e:  # pylint: disable=broad-exception-caught
      return False, f"Subprocess execution failed: {e}", ""

  def extract_diagnostics(self, build_output: str,
                          siso_output: str) -> List[Any]:
    del siso_output  # Unused in GN generation
    stripped = build_output.strip()
    error_summary = stripped.splitlines()[0] if stripped else "GN Error"
    target_files = extract_gn_target_files(build_output, self.repo_path)
    is_structural = any(kw in build_output.lower() for kw in (
        "unexpected token",
        "expecting assignment",
        "syntax error",
    ))
    return [
        GNDiagnostic(
            error_message=error_summary,
            raw_output=build_output,
            target_files=target_files,
            is_structural_break=is_structural,
        )
    ]

  def resolve_diagnostic(
      self,
      diagnostic: Any,
      history_records: List[Dict[str, Any]],
      use_pro: bool,
  ) -> Tuple[str, str, str]:
    if not isinstance(diagnostic, GNDiagnostic):
      return "", self.model, ""

    # Fast-path: Automatically remove stray conflict marker lines
    for gnf, target_line in diagnostic.target_files.items():
      stray_res = self.check_and_clean_stray_marker(gnf, target_line)
      if stray_res is not None:
        return stray_res

    send_full_file = use_pro or diagnostic.is_structural_break
    file_contexts = []
    primary_target = ""

    for gnf, target_line in list(diagnostic.target_files.items())[:3]:
      if not primary_target:
        primary_target = gnf
      try:
        with open(gnf, "r", encoding="utf-8", errors="replace") as gf:
          file_content = gf.read()
        rel_f = os.path.relpath(gnf, self.repo_path)
        lang = "gn" if gnf.endswith((".gn", ".gni")) else "cpp"
        if (send_full_file or target_line is None or
            len(file_content.splitlines()) <= 800):
          file_contexts.append(
              f"### Full File: {rel_f}\n```{lang}\n{file_content}\n```")
        else:
          lines = file_content.splitlines(keepends=True)
          start_idx = max(0, target_line - 150)
          end_idx = min(len(lines), target_line + 150)
          snippet = "".join(lines[start_idx:end_idx])
          file_contexts.append(
              f"### File: {rel_f} (Lines {start_idx + 1}-{end_idx})\n"
              f"```{lang}\n{snippet}\n```")
      except OSError:
        pass

    history_items = []
    investigation_items = []
    for h in history_records[-6:]:
      it = str(h.get("iteration", ""))
      hf = h.get("file", "")
      he = h.get("error", "")
      if it.startswith("Tool-"):
        investigation_items.append(
            f"Tool Call: `{hf}`\nResult:\n```\n{he}\n```")
      else:
        history_items.append(f"- Iteration {it}: Modified {hf} to fix \"{he}\"")
    history_str = "\n".join(history_items)
    investigation_str = "\n\n".join(investigation_items)

    anti_oscillation_note = ""
    if "Source file not found" in diagnostic.raw_output:
      anti_oscillation_note = (
          "\n\nCRITICAL INSTRUCTION: If a target references source files "
          "that do not exist on disk at either relative path, DO NOT toggle or "
          "guess relative paths back and forth.\n"
          "If the tool/test was removed upstream and no matching "
          "source file exists, DELETE the defunct target definition from "
          "BUILD.gn using a <<<<<<< DELETE block.")

    res = self.reasoning_engine.heal_gn_error(
        error_trace=f"{diagnostic.raw_output[:32768]}{anti_oscillation_note}",
        file_context="\n\n".join(file_contexts),
        attempt_history=history_str,
        investigation_history=investigation_str,
        use_pro=use_pro,
    )
    patch = res.get("patch", "")
    model_used = res.get("model_used", self.model)
    rel_target = (
        os.path.relpath(primary_target, self.repo_path)
        if primary_target else f"{self.platform}_{self.build_type}")
    return patch, model_used, rel_target
