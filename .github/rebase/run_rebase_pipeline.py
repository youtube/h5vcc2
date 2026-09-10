#!/usr/bin/env python3
"""End-to-end automated Cobalt Chromium rebase pipeline runner.

Executes all rebase phases in sequence:
  Phase 1: Conflict Resolution (prioritizing DEPS & toolchain build files first,
           then GN build configs, then C++/Java source files).
  Phase 2: Toolchain & Dependency Sync (gclient sync -D).
  Phase 3: GN Build Generation & Verification (cobalt/build/gn.py).
  Phase 4: autoninja Compiler Self-Healing Loop (up to 100 iterations).
  Phase 5: Comprehensive MXXX_rebase_summary.md generation with metrics.
"""

import argparse
import os
import sys
import time
from typing import List
import warnings

from autoninja import AutoninjaResolver
from base_resolver import write_rebase_report
from conflicts import ConflictResolver
from gclient_sync import GClientSyncResolver
from gn_gen import GNGenResolver
from engine_client import ReasoningEngineClient

# Suppress google.auth UserWarning about ADC quota project on Cloudtop
warnings.filterwarnings("ignore", category=UserWarning, module="google.auth")


def build_arg_parser() -> argparse.ArgumentParser:
  """Constructs the CLI argument parser for the rebase pipeline."""
  parser = argparse.ArgumentParser(
      description="Automated Cobalt Chromium Rebase Pipeline Runner.")
  parser.add_argument(
      "--repo-path",
      default=os.path.expanduser("~/cobalt/src"),
      help="Path to Cobalt src repository.",
  )
  parser.add_argument(
      "--cobalt-root",
      default=os.path.expanduser("~/cobalt"),
      help="Path to Cobalt workspace root.",
  )
  parser.add_argument(
      "--platform",
      default="android-arm",
      help="Platform for cobalt/build/gn.py (e.g. android-arm, linux-x64x11)",
  )
  parser.add_argument(
      "--build-type",
      default="devel",
      help="Build type for cobalt/build/gn.py (default: devel)",
  )
  parser.add_argument(
      "--target",
      default="cobalt",
      help="Target executable to build (default: cobalt)",
  )
  parser.add_argument(
      "--project-id",
      default=os.environ.get("GCP_PROJECT") or
      os.environ.get("GOOGLE_CLOUD_PROJECT"),
      help="GCP Project ID for Vertex AI Reasoning Engine.",
  )
  parser.add_argument(
      "--location",
      default=os.environ.get("GCP_LOCATION", "global"),
      help="Vertex AI Region (default: global).",
  )
  parser.add_argument(
      "--reasoning-engine-id",
      default=os.environ.get("REASONING_ENGINE_ID") or
      os.environ.get("REASONING_ENGINE_RESOURCE_ID"),
      help="Hosted Vertex AI Reasoning Engine resource ID or full name.",
  )
  parser.add_argument(
      "--skills-dir",
      default=None,
      help=(
          "Directory path containing declarative rebase skills markdown files."
      ),
  )
  parser.add_argument(
      "--model",
      default=os.environ.get("GEMINI_MODEL"),
      help="Optional Gemini model override (e.g. gemini-3.7-flash).",
  )
  parser.add_argument(
      "--skip-conflicts",
      action="store_true",
      help="Skip Phase 1 (Conflict resolution).",
  )
  parser.add_argument(
      "--skip-sync",
      action="store_true",
      help="Skip Phase 2 (gclient sync -D).",
  )
  parser.add_argument(
      "--skip-gn",
      action="store_true",
      help="Skip Phase 3 (cobalt/build/gn.py).",
  )
  parser.add_argument(
      "--skip-build",
      action="store_true",
      help="Skip Phase 4 (autoninja compiler loop).",
  )
  parser.add_argument(
      "--max-gn-iterations",
      type=int,
      default=50,
      help="Max GN self-healing iterations (default: 50)",
  )
  parser.add_argument(
      "--max-build-iterations",
      type=int,
      default=60,
      help="Max autoninja compiler self-healing iterations (default: 60)",
  )
  parser.add_argument(
      "--gcs-memory-uri",
      default=os.environ.get("GCS_MEMORY_URI"),
      help=(
          "Optional GCS bucket URI (gs://bucket/path) to sync knowledge bank."),
  )
  return parser


def run_pipeline(args: argparse.Namespace) -> int:
  """Executes the end-to-end multi-phase Cobalt rebase pipeline."""
  rebase_dir = os.path.dirname(os.path.abspath(__file__))
  out_dir = f"{args.platform}_{args.build_type}"
  effective_target = args.target
  if effective_target == "cobalt" and args.platform.startswith("android"):
    effective_target = "cobalt_apk"

  start_time = time.time()
  print("=" * 80, file=sys.stderr)
  print(
      "[START] STARTING AUTOMATED COBALT CHROMIUM REBASE PIPELINE",
      file=sys.stderr,
  )
  if args.reasoning_engine_id:
    print(f"  - Reasoning Engine: {args.reasoning_engine_id}", file=sys.stderr)
  else:
    effective_model = args.model or "gemini-2.5-flash"
    print(f"  - Model:      {effective_model}", file=sys.stderr)
  print(f"  - Platform:   {args.platform}", file=sys.stderr)
  print(f"  - Config:     {args.build_type}", file=sys.stderr)
  print(f"  - Out Dir:    out/{out_dir}", file=sys.stderr)
  print(f"  - Target:     {effective_target}", file=sys.stderr)
  if args.gcs_memory_uri:
    print(f"  - GCS Memory: {args.gcs_memory_uri}", file=sys.stderr)
  print("=" * 80, file=sys.stderr)

  # -------------------------------------------------------------------------
  # REASONING ENGINE & RESOLVER SETUP
  # -------------------------------------------------------------------------
  reasoning_engine = ReasoningEngineClient(
      resource_id=args.reasoning_engine_id,
      project_id=args.project_id,
      location=args.location,
  )

  # Phase 1: Conflict Resolver
  conflict_resolver = ConflictResolver(
      repo_path=args.repo_path,
      engine=reasoning_engine,
      skip_sync=True,  # Phase 2 handles gclient sync
  )

  # Phase 2: Shared Sync Resolver
  sync_resolver = GClientSyncResolver(
      repo_path=args.repo_path,
      engine=reasoning_engine,
      max_iterations=10,
  )

  def on_gn_patch_applied(modified_files: List[str]) -> None:
    """Triggered if GN healing touches DEPS or other dependency files."""
    if any(os.path.basename(f) == "DEPS" for f in modified_files):
      print(
          "[Phase 3] DEPS was modified by GN fix. Re-running gclient sync...",
          file=sys.stderr,
      )
      sync_resolver.run_resolution_loop()

  # Phase 3: Shared GN Resolver
  gn_resolver = GNGenResolver(
      repo_path=args.repo_path,
      platform=args.platform,
      build_type=args.build_type,
      gn_check=True,
      max_iterations=args.max_gn_iterations,
      engine=reasoning_engine,
      on_patch_applied_fn=on_gn_patch_applied,
  )

  def on_build_patch_applied(modified_files: List[str]) -> None:
    """Triggered if compiler loop touches DEPS or GN build files."""
    if any(os.path.basename(f) == "DEPS" for f in modified_files):
      print(
          "[Phase 4] DEPS modified by compiler fix. Re-running gclient sync...",
          file=sys.stderr,
      )
      sync_resolver.run_resolution_loop()

    if any(f.endswith((".gn", ".gni", ".star")) for f in modified_files):
      print(
          "[Phase 4] Build files modified. Re-running GN generation...",
          file=sys.stderr,
      )
      gn_resolver.run_resolution_loop()

  # Phase 4: autoninja Compiler Resolver
  autoninja_resolver = AutoninjaResolver(
      repo_path=args.repo_path,
      out_dir=out_dir,
      target=effective_target,
      max_iterations=args.max_build_iterations,
      engine=reasoning_engine,
      on_patch_applied_fn=on_build_patch_applied,
  )

  def write_report(status_str: str) -> str:
    return write_rebase_report(
        rebase_dir=rebase_dir,
        platform=args.platform,
        build_type=args.build_type,
        target=effective_target,
        model=args.model,
        status=status_str,
        elapsed_seconds=time.time() - start_time,
        repo_path=args.repo_path,
    )

  # -------------------------------------------------------------------------
  # PHASE 1: Unified Conflict Resolution (DEPS + Source)
  # -------------------------------------------------------------------------
  if not args.skip_conflicts:
    print("\n" + "=" * 80, file=sys.stderr)
    print(
        "[PHASE] PHASE 1: Unified Conflict Resolution (DEPS + Source)",
        file=sys.stderr,
    )
    print("=" * 80, file=sys.stderr)
    conflict_ok = conflict_resolver.run_resolution_loop()
    if not conflict_ok:
      print(
          "[FAIL] Phase 1 Conflict Resolution failed.",
          file=sys.stderr,
      )
      write_report("FAILED (Phase 1: Conflict Resolution)")
      return 1
    print("[OK] Phase 1 Completed Successfully.", file=sys.stderr)

  # -------------------------------------------------------------------------
  # PHASE 2: Toolchain & Dependency Sync: gclient sync -D
  # -------------------------------------------------------------------------
  if not getattr(args, "skip_sync", False):
    print("\n" + "=" * 80, file=sys.stderr)
    print(
        "[PHASE] PHASE 2: Toolchain & Dependency Sync (gclient sync -D)",
        file=sys.stderr,
    )
    print("=" * 80, file=sys.stderr)
    sync_ok = sync_resolver.run_resolution_loop()
    if not sync_ok:
      print(
          "[FAIL] Phase 2 Toolchain Sync failed.",
          file=sys.stderr,
      )
      write_report("FAILED (Phase 2: gclient sync)")
      return 1
    print("[OK] Phase 2 Completed Successfully.", file=sys.stderr)

  # -------------------------------------------------------------------------
  # PHASE 3: GN Generation & Header Verification (cobalt/build/gn.py)
  # -------------------------------------------------------------------------
  if not args.skip_gn:
    print("\n" + "=" * 80, file=sys.stderr)
    print(
        "[PHASE] PHASE 3: GN Build Generation (cobalt/build/gn.py)",
        file=sys.stderr,
    )
    print("=" * 80, file=sys.stderr)
    gn_ok = gn_resolver.run_resolution_loop()
    if not gn_ok:
      print(
          "[FAIL] Phase 3 GN Generation & Header Verification failed.",
          file=sys.stderr,
      )
      write_report("FAILED (Phase 3: GN Generation)")
      return 1
    print("[OK] Phase 3 Completed Successfully.", file=sys.stderr)

  # -------------------------------------------------------------------------
  # PHASE 4: autoninja Compiler Self-Healing Loop
  # -------------------------------------------------------------------------
  if not args.skip_build:
    print("\n" + "=" * 80, file=sys.stderr)
    print(
        f"[PHASE] PHASE 4: autoninja Compiler Loop "
        f"(Target: {effective_target})",
        file=sys.stderr,
    )
    print("=" * 80, file=sys.stderr)
    build_ok = autoninja_resolver.run_resolution_loop()
    if not build_ok:
      print(
          "[FAIL] Phase 4 Compiler Feedback Loop failed.",
          file=sys.stderr,
      )
      write_report("FAILED (Phase 4: Compiler Loop)")
      return 1
    print("[OK] Phase 4 Completed Successfully.", file=sys.stderr)

  elapsed = time.time() - start_time
  summary_path = write_report("SUCCESS (All Phases Complete)")

  print("\n" + "=" * 80, file=sys.stderr)
  print(
      f"[SUCCESS] PIPELINE COMPLETED CLEANLY in {elapsed:.1f}s!",
      file=sys.stderr,
  )
  print(
      f"[REPORT] Summary Report: {summary_path}",
      file=sys.stderr,
  )
  print("=" * 80, file=sys.stderr)
  return 0


def main():
  """Main CLI entry point for Cobalt rebase pipeline."""
  parser = build_arg_parser()
  args = parser.parse_args()
  sys.exit(run_pipeline(args))


if __name__ == "__main__":
  main()
