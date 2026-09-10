#!/usr/bin/env python3
"""Interactive Chat CLI for Cobalt Chromium Rebase Reasoning Engine.

Allows engineers to converse directly with Vertex AI Gemini models pre-loaded
with Cobalt rebase skills, GN build diagnostics, and compiler healing guides.

Usage:
  python3 .github/rebase/reasoning_engine/chat.py
  python3 .github/rebase/reasoning_engine/chat.py --model gemini-2.5-pro
  python3 .github/rebase/reasoning_engine/chat.py --prompt "Explain NavThrottle"
"""

import argparse
import os
import readline  # pylint: disable=unused-import
import sys
from typing import Dict, List, Optional

# Ensure reasoning_engine package can be imported
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PARENT_DIR = os.path.dirname(SCRIPT_DIR)
if SCRIPT_DIR not in sys.path:
  sys.path.insert(0, SCRIPT_DIR)
if PARENT_DIR not in sys.path:
  sys.path.insert(0, PARENT_DIR)

# pylint: disable=wrong-import-position
from reasoning_engine.engine import CobaltReasoningEngine


def load_latest_failure(repo_root: Optional[str] = None) -> str:
  """Retrieves the latest failure description from logs."""
  root = repo_root or os.path.abspath(
      os.path.join(
          os.path.dirname(os.path.abspath(__file__)), "..", "..", ".."))
  log_file = os.path.join(root, "out", "rebase_pipeline.log")
  if os.path.isfile(log_file):
    try:
      with open(log_file, "r", encoding="utf-8", errors="replace") as f:
        lines = f.readlines()
      err_block = []
      for line in reversed(lines[-300:]):
        sline = line.strip()
        if (sline.startswith(("ERROR at //", "[FAIL]", "FAILED:", "Error:",
                              "Traceback (most recent call last):")) or
            "Can't include this header" in sline):
          err_block.insert(0, sline)
          if len(err_block) >= 20:
            break
        elif err_block:
          err_block.insert(0, sline)
          if len(err_block) >= 20:
            break
      if err_block:
        return "\n".join(err_block)
      tail_lines = [line.strip() for line in lines[-25:] if line.strip()]
      return "\n".join(tail_lines)
    except OSError:
      pass
  return "No recent failure records found."


def print_banner(
    project_id: str,
    location: str,
    model: str,
    mode: str,
    latest_failure: str = "",
):
  """Prints welcome banner and active configuration."""
  print("=" * 80)
  print(" COBALT REBASE REASONING ENGINE - INTERACTIVE CHAT")
  print("=" * 80)
  print(f"  - GCP Project  : {project_id}")
  print(f"  - Vertex Region: {location}")
  print(f"  - Model        : {model}")
  print(f"  - Active Mode  : {mode.upper()} (Skills: {mode}_healing / rebase)")
  if latest_failure and "No recent" not in latest_failure:
    first_line = latest_failure.splitlines()[0][:70]
    print(f"  - Failure Memo : {first_line}")
  print("-" * 80)
  print(" Commands: /mode <gn|compiler|conflict|rebase>  /model <flash|pro>")
  print("           /file <path>   /failure   /clear   /help   /exit")
  print("=" * 80)
  print()


def print_help():
  """Prints interactive command guide."""
  print("\n--- Available Commands ---")
  print("  /mode <rebase|gn|compiler|conflict> : Switch active domain skill")
  print("  /model <gemini-2.5-flash|pro>        : Switch Vertex AI model")
  print("  /file <relative_filepath>            : Attach file to prompt")
  print("  /failure                             : Show latest failure memory")
  print("  /clear                               : Clear conversation history")
  print("  /exit, /quit                         : Exit chat session\n")


def main():
  """Main entry point for interactive chat CLI."""
  parser = argparse.ArgumentParser(
      description="Interactive Chat with Cobalt Reasoning Engine")
  parser.add_argument(
      "--project-id",
      default=os.environ.get("GCP_PROJECT") or
      os.environ.get("GOOGLE_CLOUD_PROJECT", "lxn-test"),
      help="GCP Project ID for Vertex AI",
  )
  parser.add_argument(
      "--location",
      default=os.environ.get("GCP_LOCATION", "global"),
      help="Vertex AI region (default: global)",
  )
  parser.add_argument(
      "--model",
      default=os.environ.get("GEMINI_MODEL", "gemini-2.5-flash"),
      help="Default Gemini model (default: gemini-2.5-flash)",
  )
  parser.add_argument(
      "--mode",
      choices=["rebase", "gn", "compiler", "conflict"],
      default="rebase",
      help="Domain skill mode (default: rebase)",
  )
  parser.add_argument(
      "--prompt",
      default="",
      help="One-shot prompt to evaluate without entering interactive REPL",
  )
  args = parser.parse_args()

  engine = CobaltReasoningEngine(
      project_id=args.project_id,
      location=args.location,
      flash_model=args.model,
      pro_model="gemini-2.5-pro",
  )

  current_mode = args.mode
  current_model = args.model
  history: List[Dict[str, str]] = []
  latest_failure = load_latest_failure()

  # One-shot mode
  if args.prompt:
    use_pro = "pro" in current_model.lower()
    prompt_with_context = args.prompt
    if latest_failure and "No recent" not in latest_failure:
      prompt_with_context = (
          f"[Latest Pipeline Failure Memory]\n{latest_failure}\n\n"
          f"[User Query]\n{args.prompt}")
    res = engine.chat(
        prompt_with_context,
        history=[],
        mode=current_mode,
        use_pro=use_pro,
    )
    if res.get("status") == "SUCCESS":
      print(res.get("response", ""))
      sys.exit(0)
    else:
      err_msg = res.get("response")
      print(f"Error: {err_msg}", file=sys.stderr)
      sys.exit(1)

  # Interactive REPL mode
  print_banner(
      args.project_id,
      args.location,
      current_model,
      current_mode,
      latest_failure,
  )

  while True:
    try:
      user_input = input(f"[{current_mode}|{current_model}] >>> ").strip()
      if not user_input:
        continue

      if user_input in ("/exit", "/quit", "exit", "quit"):
        print("Exiting Cobalt Reasoning Engine. Goodbye!")
        break

      if user_input == "/help":
        print_help()
        continue

      if user_input == "/clear":
        history.clear()
        print("[OK] Conversation history cleared.\n")
        continue

      if user_input == "/failure":
        print(f"\n--- Latest Failure Memory ---\n{latest_failure}\n")
        user_input = (
            "Analyze the latest pipeline failure above. Explain why it "
            "occurred and suggest how to resolve it.")

      elif user_input.startswith("/model"):
        parts = user_input.split(maxsplit=1)
        if len(parts) > 1:
          target_model = parts[1].strip()
          if target_model in ("flash", "gemini-3.7-flash", "3.7"):
            current_model = "gemini-3.7-flash"
          elif target_model in ("pro", "gemini-2.5-pro"):
            current_model = "gemini-2.5-pro"
          else:
            current_model = target_model
          print(f"[OK] Model switched to: {current_model}\n")
        else:
          print(f"Current model: {current_model}")
        continue

      elif user_input.startswith("/mode"):
        parts = user_input.split(maxsplit=1)
        if len(parts) > 1 and parts[1].strip() in (
            "rebase",
            "gn",
            "compiler",
            "conflict",
        ):
          current_mode = parts[1].strip()
          print(f"[OK] Domain mode switched to: {current_mode.upper()}\n")
        else:
          print("Valid modes: rebase, gn, compiler, conflict")
        continue

      elif user_input.startswith("/file"):
        parts = user_input.split(maxsplit=1)
        if len(parts) > 1:
          file_rel = parts[1].strip()
          if os.path.isfile(file_rel):
            with open(file_rel, "r", encoding="utf-8", errors="replace") as f:
              content = f.read()
            user_input = (
                f"File: {file_rel}\n```\n{content}\n```\n\n"
                "Please analyze this file and its relevance to Cobalt rebase.")
            print(f"[OK] Attached file: {file_rel} ({len(content)} bytes)")
          else:
            print(f"[ERROR] File not found: {file_rel}")
            continue

      print("\n[Thinking...]")
      use_pro = "pro" in current_model.lower()
      res = engine.chat(
          user_input,
          history=history,
          mode=current_mode,
          use_pro=use_pro,
          failure_memory=latest_failure,
      )

      if res.get("status") == "SUCCESS":
        answer = res.get("response", "")
        print(f"\n{answer}\n")
        history.append({"role": "user", "content": user_input})
        history.append({"role": "model", "content": answer})
      else:
        err_resp = res.get("response")
        print(f"\n[ERROR] Request failed: {err_resp}\n")

    except (KeyboardInterrupt, EOFError):
      print("\nExiting Cobalt Reasoning Engine. Goodbye!")
      break


if __name__ == "__main__":
  main()
