#!/usr/bin/env python3
"""AI-driven gclient sync and toolchain self-healing resolver library.

Provides GClientSyncResolver, which executes `gclient sync -D`, automatically
recovers with `--force --reset` on dirty submodule state, extracts diagnostic
traces, prompts Gemini via Vertex AI Reasoning Engine to heal DEPS syntax
and revision errors, and validates Python AST before resuming sync.
"""

import ast
import dataclasses
import os
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

# Keywords that mark the beginning of an error or traceback in gclient sync
# output. Covers standard Python exception names (since gclient and DEPS are
# evaluated in Python) as well as tool-level error phrases.
SYNC_ERROR_KEYWORDS = (
    "error:",
    "traceback",
    "failed to",
    "syntaxerror",
    "syntax error",
    "keyerror",
    "key error",
    "attributeerror",
    "attribute error",
    "exception",
    "cannot find",
    "conflict",
    "fatal:",
)


@dataclasses.dataclass
class GClientSyncDiagnostic:
  """Represents a gclient sync error diagnostic."""

  error_message: str
  raw_output: str
  diagnostic_trace: str


def extract_sync_diagnostic_trace(output: str) -> str:
  """Extracts relevant error and traceback lines from gclient sync output."""
  lines = output.splitlines()
  error_lines = []
  capture = False
  for line in lines:
    if any(kw in line.lower() for kw in SYNC_ERROR_KEYWORDS):
      capture = True
    if capture:
      error_lines.append(line)
      if len(error_lines) > 35:
        break
  if error_lines:
    return "\n".join(error_lines)
  return "\n".join(lines[-25:] if len(lines) > 25 else lines)


class GClientSyncResolver(BaseResolver):
  """Self-healing resolver for gclient sync and DEPS dependency failures."""

  def __init__(
      self,
      repo_path: str,
      *,
      engine: Optional[Any] = None,
      flags: Optional[List[str]] = None,
      max_iterations: int = 10,
      on_patch_applied_fn: Optional[Callable[[List[str]], None]] = None,
  ):
    super().__init__(
        repo_path=repo_path,
        engine=engine,
        max_iterations=max_iterations,
        on_patch_applied_fn=on_patch_applied_fn,
    )
    self.flags = flags if flags is not None else ["-D"]

  @property
  def name(self) -> str:
    return "Phase 2 (Toolchain & Dependency Sync)"

  def run_command(self, iteration: int) -> Tuple[bool, str, str]:
    del iteration  # Unused in standard sync command execution
    clean_env = get_clean_build_env()
    cmd = ["gclient", "sync"] + self.flags
    cmd_str = " ".join(cmd)
    print(
        f"\n[gclient_sync] Executing: {cmd_str} in {self.repo_path}",
        file=sys.stderr,
    )
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
      if proc.returncode == 0:
        return True, combined_output, ""

      # Auto-recover with --force --reset
      print(
          f"[WARNING] gclient sync returned {proc.returncode}. "
          "Retrying with --force --reset...",
          file=sys.stderr,
      )
      retry_cmd = ["gclient", "sync"] + self.flags + ["--force", "--reset"]
      proc_retry = subprocess.run(
          retry_cmd,
          cwd=self.repo_path,
          capture_output=True,
          text=True,
          env=clean_env,
          check=False,
      )
      retry_output = f"{proc_retry.stdout}\n{proc_retry.stderr}"
      return proc_retry.returncode == 0, retry_output, ""
    except Exception as e:  # pylint: disable=broad-exception-caught
      return False, f"Subprocess execution failed: {e}", ""

  def extract_diagnostics(self, build_output: str,
                          siso_output: str) -> List[Any]:
    del siso_output  # Unused in gclient sync
    diag_trace = extract_sync_diagnostic_trace(build_output)
    first_line = diag_trace.splitlines()[0] if diag_trace else "Sync Error"
    return [
        GClientSyncDiagnostic(
            error_message=first_line,
            raw_output=build_output,
            diagnostic_trace=diag_trace,
        )
    ]

  def resolve_diagnostic(
      self,
      diagnostic: Any,
      history_records: List[Dict[str, Any]],
      use_pro: bool,
  ) -> Tuple[str, str, str]:
    del history_records  # Unused for single DEPS resolution
    if not isinstance(diagnostic, GClientSyncDiagnostic):
      return "", self.model, "DEPS"

    deps_path = os.path.join(self.repo_path, "DEPS")
    if not os.path.isfile(deps_path):
      return "", self.model, "DEPS"

    try:
      with open(deps_path, "r", encoding="utf-8", errors="replace") as f:
        current_deps = f.read()
    except OSError:
      return "", self.model, "DEPS"

    instruction = (
        f"gclient sync failed with the following error output:\n"
        f"--------------------------------------------------\n"
        f"{diagnostic.diagnostic_trace}\n"
        f"--------------------------------------------------\n\n"
        "Provide a concrete SEARCH/REPLACE patch for DEPS to fix the sync "
        "error.")

    res = self.reasoning_engine.resolve_conflict(
        file_path="DEPS",
        language="Python",
        raw_conflict=current_deps,
        instruction=instruction,
        use_pro=use_pro,
    )
    raw_patch = res.get("patch", "")
    model_used = res.get("model_used", self.model)

    # If the model returned full replacement content, wrap it as SEARCH/REPLACE
    if "<<<<<<< SEARCH" not in raw_patch and "=======" not in raw_patch:
      try:
        ast.parse(raw_patch)
        patch = (f"FILE: DEPS\n"
                 f"<<<<<<< SEARCH\n"
                 f"{current_deps}\n"
                 f"=======\n"
                 f"{raw_patch}\n"
                 f">>>>>>> REPLACE")
        return patch, model_used, "DEPS"
      except SyntaxError:
        pass

    return raw_patch, model_used, "DEPS"
