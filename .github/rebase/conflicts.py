#!/usr/bin/env python3
"""Unified AI-driven merge conflict resolver library for Cobalt Chromium rebase.

Provides ConflictResolver, which resolves merge conflicts across all files in
the repository (DEPS, C++, Java, GN, etc.) using Vertex AI Reasoning Engine,
validates Python AST on DEPS, and generates structured rebase summaries.
"""

import ast
import dataclasses
import os
import re
import subprocess
import sys
from typing import Any, Callable, Dict, List, Optional, Tuple
import warnings

from base_resolver import (
    BaseResolver,
    execute_local_tool,
    is_unmodified_third_party,
)
from token_usage import TokenUsage

# Suppress google.auth UserWarning about ADC quota project on Cloudtop
warnings.filterwarnings("ignore", category=UserWarning, module="google.auth")


@dataclasses.dataclass
class ConflictBlock:
  """Represents an extracted merge conflict chunk with surrounding context."""

  index: int
  start_line: int
  end_line: int
  raw_block: str
  ours_content: str
  base_content: Optional[str]
  theirs_content: str
  context_before: str
  context_after: str


@dataclasses.dataclass
class EscalationItem:
  """Records complex conflict blocks that require human review."""

  file_path: str
  block_index: int
  reason: str


def detect_language(file_path: str) -> str:
  """Detects programming language from file path/extension."""
  basename = os.path.basename(file_path)
  if basename == "DEPS":
    return "Python (Chromium DEPS)"

  ext = os.path.splitext(file_path)[1].lower()
  mapping = {
      ".cc": "C++",
      ".cpp": "C++",
      ".cxx": "C++",
      ".c": "C",
      ".h": "C/C++ Header",
      ".hh": "C/C++ Header",
      ".hpp": "C/C++ Header",
      ".inc": "C/C++ Header",
      ".mm": "Objective-C++",
      ".m": "Objective-C",
      ".java": "Java",
      ".tmpl": "Java / Build Template",
      ".gn": "GN Build File",
      ".gni": "GN Build File",
      ".py": "Python",
      ".spec": "Grit / Spec Config",
      ".json": "JSON",
      ".xml": "XML",
      ".grd": "Grit Resource",
      ".grdp": "Grit Resource",
  }
  return mapping.get(ext, "Source Code")


def sort_conflict_priority(file_path: str) -> Tuple[int, str]:
  """Prioritizes toolchain sync scripts and build configs before sources."""
  base = os.path.basename(file_path)
  norm = file_path.replace("\\", "/")
  if base == "DEPS":
    return 0, norm
  if "tools/clang/scripts" in norm or "tools/rust" in norm:
    return 1, norm
  if norm.endswith(".star"):
    return 2, norm
  if norm.endswith((".gn", ".gni")):
    return 3, norm
  if norm.endswith((".java", ".tmpl")):
    return 4, norm
  if norm.endswith((".h", ".hh", ".hpp", ".inc")):
    return 5, norm
  if norm.endswith((".cc", ".cpp", ".cxx", ".mm", ".c")):
    return 6, norm
  return 7, norm


def extract_git_context(repo_path: str,) -> Tuple[str, Dict[str, str]]:
  """Extracts branch, rebase commits, and merge base information."""
  ctx_lines = []
  meta: Dict[str, str] = {
      "branch": "Unknown",
      "upstream": "Unknown",
      "head_sha": "Unknown",
      "onto_sha": "Unknown",
      "merge_base": "Unknown",
  }

  def _run_git(args: List[str]) -> str:
    try:
      res = subprocess.run(
          ["git"] + args,
          cwd=repo_path,
          capture_output=True,
          text=True,
          errors="replace",
          check=False,
      )
      return res.stdout.strip()
    except (OSError, subprocess.SubprocessError):
      return ""

  branch = _run_git(["branch", "--show-current"])
  if branch:
    meta["branch"] = branch
    ctx_lines.append(f"Current Branch: {branch}")

  head_sha = _run_git(["rev-parse", "--short", "HEAD"])
  if head_sha:
    meta["head_sha"] = head_sha
    ctx_lines.append(f"HEAD Commit: {head_sha}")

  git_dir = _run_git(["rev-parse", "--git-dir"])
  if git_dir:
    abs_git = (
        os.path.join(repo_path, git_dir)
        if not os.path.isabs(git_dir) else git_dir)
    onto_file = os.path.join(abs_git, "rebase-merge", "onto")
    head_name_file = os.path.join(abs_git, "rebase-merge", "head-name")
    if os.path.isfile(onto_file):
      with open(onto_file, "r", encoding="utf-8") as f:
        onto_sha = f.read().strip()[:10]
        meta["onto_sha"] = onto_sha
        ctx_lines.append(f"Rebase Onto Commit: {onto_sha}")
    if os.path.isfile(head_name_file):
      with open(head_name_file, "r", encoding="utf-8") as f:
        meta["upstream"] = f.read().strip()

  merge_base = _run_git(["merge-base", "HEAD", "HEAD@{u}"])
  if merge_base:
    base_sha = merge_base[:10]
    meta["merge_base"] = base_sha
    ctx_lines.append(f"Merge Base: {base_sha}")

  return "\n".join(ctx_lines), meta


def is_ignored_conflict_path(rel_path: str) -> bool:
  """Excludes test suites, rebase tooling, and build output dirs."""
  norm = rel_path.replace("\\", "/")
  return (norm.startswith("out/") or norm.startswith(".github/rebase/") or
          norm.startswith("results/"))


def find_all_conflicted_files(repo_path: str) -> List[str]:
  """Finds all files in git working tree that have unmerged conflict markers."""
  conflicted = []
  try:
    res = subprocess.run(
        ["git", "diff", "--name-only", "--diff-filter=U"],
        cwd=repo_path,
        capture_output=True,
        text=True,
        errors="replace",
        check=False,
    )
    if res.returncode == 0 and res.stdout.strip():
      conflicted = [
          l.strip()
          for l in res.stdout.splitlines()
          if l.strip() and not is_ignored_conflict_path(l.strip())
      ]
  except (OSError, subprocess.SubprocessError):
    pass

  # Fallback: scan for <<<<<<< HEAD markers
  if not conflicted:
    try:
      res2 = subprocess.run(
          ["git", "grep", "-l", "-I", "^<<<<<<<", "--", "."],
          cwd=repo_path,
          capture_output=True,
          text=True,
          errors="replace",
          check=False,
      )
      if res2.returncode == 0 and res2.stdout.strip():
        conflicted = [
            l.strip()
            for l in res2.stdout.splitlines()
            if l.strip() and not is_ignored_conflict_path(l.strip())
        ]
    except (OSError, subprocess.SubprocessError):
      pass

  conflicted.sort(key=lambda p: sort_conflict_priority(os.path.basename(p)))
  return conflicted


def extract_conflict_blocks(
    content: str,
    context_lines: int = 15,
) -> List[ConflictBlock]:
  """Extracts all diff3 or normal merge conflict blocks from file text."""
  lines = content.splitlines(keepends=True)
  blocks = []
  i = 0
  idx = 1

  while i < len(lines):
    if lines[i].startswith("<<<<<<<"):
      start = i
      ours = []
      base = []
      theirs = []
      has_base = False
      in_base = False
      in_theirs = False
      i += 1
      while i < len(lines):
        if lines[i].startswith("|||||||"):
          has_base = True
          in_base = True
          in_theirs = False
          i += 1
          continue
        if lines[i].startswith("======="):
          in_base = False
          in_theirs = True
          i += 1
          continue
        if lines[i].startswith(">>>>>>>"):
          end = i
          raw_blk = "".join(lines[start:end + 1])
          ctx_before = "".join(lines[max(0, start - context_lines):start])
          ctx_after = "".join(lines[end +
                                    1:min(len(lines), end + 1 + context_lines)])
          blocks.append(
              ConflictBlock(
                  index=idx,
                  start_line=start + 1,
                  end_line=end + 1,
                  raw_block=raw_blk,
                  ours_content="".join(ours),
                  base_content="".join(base) if has_base else None,
                  theirs_content="".join(theirs),
                  context_before=ctx_before,
                  context_after=ctx_after,
              ))
          idx += 1
          i += 1
          break

        if in_theirs:
          theirs.append(lines[i])
        elif in_base:
          base.append(lines[i])
        else:
          ours.append(lines[i])
        i += 1
    else:
      i += 1
  return blocks


def clean_output(text: str) -> str:
  """Strips markdown code fences if model wrapped response in ```."""
  stripped = text.strip()
  match = re.search(r"^```(?:[a-zA-Z0-9_\+\-]+)?\s*\n(.*?)\n```$", stripped,
                    re.DOTALL)
  if match:
    return match.group(1)
  if stripped.startswith("```") and stripped.endswith("```"):
    lines = stripped.splitlines()
    if len(lines) >= 2:
      return "\n".join(lines[1:-1])
  return text.rstrip()


def resolve_file_conflicts(
    file_path: str,
    repo_path: str,
    git_context: str,
    *,
    engine: Optional[Any] = None,
    token_tracker: Optional[TokenUsage] = None,
    escalations: Optional[List[EscalationItem]] = None,
    max_tool_rounds: int = 5,
    mock_mode: bool = False,
) -> bool:
  """Resolves all conflict markers in a specific file."""
  if not os.path.isfile(file_path):
    return False

  with open(file_path, "r", encoding="utf-8", errors="replace") as f:
    content = f.read()

  blocks = extract_conflict_blocks(content)
  if not blocks:
    return True

  lang = detect_language(file_path)
  rel_path = os.path.relpath(file_path, repo_path)

  # 1. Fast-path: Check if this is an unmodified third_party file
  if is_unmodified_third_party(file_path, repo_path):
    print(
        f"\n[resolve_conflicts] Fast-path: {rel_path} is unmodified "
        f"third_party. Resolving {len(blocks)} conflict(s) with "
        "upstream (theirs)...",
        file=sys.stderr,
    )
    for block in blocks:
      content = content.replace(block.raw_block, block.theirs_content, 1)
    with open(file_path, "w", encoding="utf-8") as f:
      f.write(content)
    print(
        f"  [OK] Resolved all {len(blocks)} block(s) in {rel_path} "
        "using upstream.",
        file=sys.stderr,
    )
    return True

  print(
      f"\n[resolve_conflicts] Resolving {len(blocks)} conflict(s) in "
      f"{rel_path} ({lang})...",
      file=sys.stderr,
  )

  for block in blocks:
    print(
        f"  - Block #{block.index} (lines {block.start_line}-{block.end_line}, "
        f"{len(block.raw_block.splitlines())} lines)...",
        file=sys.stderr,
    )

    resolved_code: Optional[str] = None
    investigation_history = ""
    for attempt in range(2):
      use_pro = attempt > 0
      if use_pro:
        print(
            f"    [PRO_RETRY] Retrying Block #{block.index} with Pro model...",
            file=sys.stderr,
        )
      for _ in range(max_tool_rounds):
        if mock_mode:
          resolved_code = block.theirs_content
          if token_tracker:
            token_tracker.add(50, 20, 70, "mock")
          break

        if engine is None:
          raise ValueError(
              "ReasoningEngine client required for conflict resolution.")

        try:
          res = engine.resolve_conflict(
              file_path=rel_path,
              language=lang,
              raw_conflict=block.raw_block,
              context_before=block.context_before,
              context_after=block.context_after,
              git_context=git_context,
              investigation_history=investigation_history,
              use_pro=use_pro,
          )
        except Exception as e:  # pylint: disable=broad-exception-caught
          print(f"    [FAIL] Reasoning Engine Error: {e}", file=sys.stderr)
          break

        raw_replacement = ""
        if isinstance(res, dict):
          raw_replacement = res.get("replacement", "")
          model_used = res.get("model_used", "flash")
          if token_tracker:
            token_tracker.add(100, 30, 130, model_used)
        elif isinstance(res, str):
          raw_replacement = res

        tool_match = re.search(r"^(TOOL_[A-Z_]+:.*)$", raw_replacement.strip(),
                               re.MULTILINE)
        if tool_match:
          tool_cmd = tool_match.group(1).strip()
          print(f"    [TOOL_USE] Model requested: {tool_cmd}", file=sys.stderr)
          tool_output = execute_local_tool(tool_cmd, repo_path)
          investigation_history += (
              f"\n\nTool Call: `{tool_cmd}`\nResult:\n```\n{tool_output}\n```")
          continue

        resolved_code = clean_output(raw_replacement)
        break

      if resolved_code is not None and "<<<<<<<" not in resolved_code:
        break

    if resolved_code is None or "<<<<<<<" in resolved_code:
      print(
          f"    [ESCALATE] Block #{block.index} could not be cleanly resolved.",
          file=sys.stderr,
      )
      if escalations is not None:
        escalations.append(
            EscalationItem(rel_path, block.index,
                           "Unresolved conflict markers"))
      return False

    content = content.replace(block.raw_block, resolved_code, 1)
    print(f"    [OK] Resolved Block #{block.index}", file=sys.stderr)

  if os.path.basename(file_path) == "DEPS":
    try:
      ast.parse(content)
      print("  [OK] DEPS Python AST syntax validated.", file=sys.stderr)
    except SyntaxError as e:
      print(f"  [FAIL] DEPS AST Syntax Error: {e}", file=sys.stderr)
      if escalations is not None:
        escalations.append(
            EscalationItem(rel_path, 0, f"DEPS AST Syntax Error: {e}"))
      return False

  with open(file_path, "w", encoding="utf-8") as f:
    f.write(content)
  print(
      f"[resolve_conflicts] Successfully processed {rel_path}.",
      file=sys.stderr,
  )
  return True


class ConflictResolver(BaseResolver):
  """Self-healing resolver for git merge conflicts (DEPS & source files)."""

  def __init__(
      self,
      repo_path: str,
      *,
      engine: Optional[Any] = None,
      max_iterations: int = 5,
      files: Optional[List[str]] = None,
      skip_sync: bool = False,
      on_patch_applied_fn: Optional[Callable[[List[str]], None]] = None,
  ):
    super().__init__(
        repo_path=repo_path,
        engine=engine,
        max_iterations=max_iterations,
        on_patch_applied_fn=on_patch_applied_fn,
    )
    self.explicit_files = files
    self.skip_sync = skip_sync
    self.git_context, self.git_meta = extract_git_context(self.repo_path)
    self.token_tracker = TokenUsage()
    self.escalations: List[EscalationItem] = []
    self.resolved_list: List[str] = []

  @property
  def name(self) -> str:
    return "Phase 1 (Conflict Resolution)"

  def run_command(self, iteration: int) -> Tuple[bool, str, str]:
    del iteration  # Unused in conflict resolution
    # Check git status for conflict markers
    if self.explicit_files:
      target_files = [
          os.path.abspath(f) if os.path.isabs(f) else os.path.join(
              self.repo_path, f) for f in self.explicit_files
      ]
    else:
      rel_files = find_all_conflicted_files(self.repo_path)
      target_files = [os.path.join(self.repo_path, f) for f in rel_files]

    remaining: List[str] = []
    for tf in target_files:
      if not os.path.isfile(tf):
        continue
      try:
        with open(tf, "r", encoding="utf-8", errors="replace") as f:
          if extract_conflict_blocks(f.read()):
            remaining.append(os.path.relpath(tf, self.repo_path))
      except OSError:
        pass

    if not remaining:
      return True, "No conflict markers remaining in repository.", ""

    msg = f"Found {len(remaining)} conflicted file(s): " + ", ".join(remaining)
    return False, msg, ""

  def extract_diagnostics(self, build_output: str,
                          siso_output: str) -> List[Any]:
    del build_output, siso_output
    if self.explicit_files:
      target_files = [
          os.path.abspath(f) if os.path.isabs(f) else os.path.join(
              self.repo_path, f) for f in self.explicit_files
      ]
    else:
      rel_files = find_all_conflicted_files(self.repo_path)
      target_files = [os.path.join(self.repo_path, f) for f in rel_files]

    remaining: List[str] = []
    for tf in target_files:
      if not os.path.isfile(tf):
        continue
      try:
        with open(tf, "r", encoding="utf-8", errors="replace") as f:
          if extract_conflict_blocks(f.read()):
            remaining.append(tf)
      except OSError:
        pass
    return remaining

  def resolve_diagnostic(
      self,
      diagnostic: Any,
      history_records: List[Dict[str, Any]],
      use_pro: bool,
  ) -> Tuple[str, str, str]:
    del history_records, use_pro
    tf = str(diagnostic)
    rel = os.path.relpath(tf, self.repo_path)
    ok = resolve_file_conflicts(
        file_path=tf,
        repo_path=self.repo_path,
        git_context=self.git_context,
        token_tracker=self.token_tracker,
        escalations=self.escalations,
        engine=self.reasoning_engine,
    )
    if ok:
      self.resolved_list.append(tf)
      return f"# Conflict resolved cleanly in {rel}", self.model, rel
    return "", self.model, rel

  def run_resolution_loop(self) -> bool:
    """Resolves all conflicted files in the repository."""
    if self.explicit_files:
      target_files = [
          os.path.abspath(f) if os.path.isabs(f) else os.path.join(
              self.repo_path, f) for f in self.explicit_files
      ]
    else:
      rel_files = find_all_conflicted_files(self.repo_path)
      target_files = [os.path.join(self.repo_path, f) for f in rel_files]

    if not target_files:
      print(
          "[resolve_conflicts] No conflicted files found in repository.",
          file=sys.stderr,
      )
      return True

    print("=" * 70, file=sys.stderr)
    print(
        f"[resolve_conflicts] FOUND {len(target_files)} CONFLICTED FILE(S):",
        file=sys.stderr,
    )
    for idx, tf in enumerate(target_files, 1):
      rel = os.path.relpath(tf, self.repo_path)
      lang = detect_language(tf)
      print(f"  {idx:2d}. {rel} [{lang}]", file=sys.stderr)
    print("=" * 70, file=sys.stderr)

    deps_resolved = False
    for tf in target_files:
      if not os.path.isfile(tf):
        continue

      if resolve_file_conflicts(
          file_path=tf,
          repo_path=self.repo_path,
          git_context=self.git_context,
          token_tracker=self.token_tracker,
          escalations=self.escalations,
          engine=self.reasoning_engine,
      ):
        self.resolved_list.append(tf)
        if os.path.basename(tf) == "DEPS":
          deps_resolved = True
      else:
        print(f"[WARNING] Issues detected in: {tf}", file=sys.stderr)

    # If DEPS was resolved and callback hook provided, trigger callback
    if deps_resolved and self.on_patch_applied_fn:
      self.on_patch_applied_fn(["DEPS"])

    print("\n" + "=" * 70, file=sys.stderr)
    print(
        f"[resolve_conflicts] ALL {len(self.resolved_list)} CONFLICTED FILES "
        "PROCESSED!",
        file=sys.stderr,
    )
    if self.escalations:
      print(
          f"  - [WARNING] Escalations Flagged: {len(self.escalations)} "
          "block(s) require human review",
          file=sys.stderr,
      )
    print(
        f"  - Total Tokens:   {self.token_tracker.total_tokens:,}",
        file=sys.stderr,
    )
    print(f"  - Total AI Calls: {self.token_tracker.calls}", file=sys.stderr)
    print("=" * 70, file=sys.stderr)

    return len(self.resolved_list) == len(target_files)
