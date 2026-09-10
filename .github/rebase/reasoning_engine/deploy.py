#!/usr/bin/env python3
# pylint: disable=wrong-import-position
"""Vertex AI Reasoning Engine Deployment and Lifecycle Management Script.

Deploys, updates, lists, and manages CobaltReasoningEngine instances on
Google Cloud Vertex AI Reasoning Engine service.
"""

import argparse
import os
import sys
from typing import Optional

# Ensure parent directory is in sys.path for proper package qualification
_PKG_DIR = os.path.dirname(os.path.abspath(__file__))
_REBASE_DIR = os.path.dirname(_PKG_DIR)
if _REBASE_DIR not in sys.path:
  sys.path.insert(0, _REBASE_DIR)

# Disable mTLS endpoint on Cloudtop to ensure clean Vertex AI transport.
os.environ["GOOGLE_API_USE_CLIENT_CERTIFICATE"] = "false"
os.environ["GOOGLE_API_USE_MTLS_ENDPOINT"] = "never"

import cloudpickle
import warnings

# Suppress google.auth UserWarning about ADC quota project on Cloudtop
warnings.filterwarnings("ignore", category=UserWarning, module="google.auth")

import vertexai
from vertexai.preview import reasoning_engines

import reasoning_engine.engine as engine_mod
from reasoning_engine.engine import CobaltReasoningEngine

# Register module to pickle by value so class and cached skills are
# self-contained in deployment artifact.
cloudpickle.register_pickle_by_value(engine_mod)


def _normalize_resource_name(resource_id: str, project_id: str,
                             location: str) -> str:
  """Formats raw resource ID into full Vertex AI resource path."""
  if resource_id.startswith("projects/"):
    return resource_id
  return (f"projects/{project_id}/locations/{location}/"
          f"reasoningEngines/{resource_id}")


def _get_effective_staging_bucket(
    staging_bucket: Optional[str],
    project_id: str,
) -> str:
  """Resolves or defaults the GCS staging bucket for Vertex AI deployment."""
  bucket = (
      staging_bucket or os.environ.get("GCS_STAGING_BUCKET") or
      f"gs://{project_id}-vertex-staging")
  if not bucket.startswith("gs://"):
    bucket = f"gs://{bucket}"
  return bucket


def deploy_reasoning_engine(
    project_id: str,
    location: str,
    *,
    staging_bucket: Optional[str] = None,
    display_name: str = "CobaltReasoningEngine",
    flash_model: str = "gemini-2.5-flash",
    pro_model: str = "gemini-2.5-pro",
) -> str:
  """Deploys a new CobaltReasoningEngine instance to Vertex AI."""
  effective_bucket = _get_effective_staging_bucket(staging_bucket, project_id)
  vertexai.init(
      project=project_id,
      location=location,
      staging_bucket=effective_bucket,
  )
  print(
      f"[deploy] Deploying \"{display_name}\" to Vertex AI "
      f"(Project: {project_id}, Region: {location}, "
      f"Bucket: {effective_bucket})...",
      file=sys.stderr,
  )

  engine_instance = CobaltReasoningEngine(
      project_id=project_id,
      location=location,
      flash_model=flash_model,
      pro_model=pro_model,
  )

  remote_app = reasoning_engines.ReasoningEngine.create(
      engine_instance,
      requirements=[
          "google-genai",
          "google-cloud-aiplatform[reasoningengine,langchain]",
          "google-cloud-storage",
      ],
      display_name=display_name,
      description=(
          "Cobalt Chromium Rebase Reasoning Engine for automated conflict "
          "resolution, GN healing, and compiler self-healing."),
  )
  resource_name = remote_app.resource_name
  print(
      "[deploy] [OK] Successfully deployed Reasoning Engine!",
      file=sys.stderr,
  )
  print(f"[deploy] Resource Name: {resource_name}", file=sys.stderr)
  return resource_name


def update_reasoning_engine(
    resource_id: str,
    project_id: str,
    location: str,
    *,
    staging_bucket: Optional[str] = None,
    display_name: str = "CobaltReasoningEngine",
    flash_model: str = "gemini-2.5-flash",
    pro_model: str = "gemini-2.5-pro",
):
  """Updates an existing Reasoning Engine instance on Vertex AI."""
  effective_bucket = _get_effective_staging_bucket(staging_bucket, project_id)
  vertexai.init(
      project=project_id,
      location=location,
      staging_bucket=effective_bucket,
  )
  resource_name = _normalize_resource_name(resource_id, project_id, location)

  print(
      f"[deploy] Updating Reasoning Engine \"{resource_name}\" "
      f"(Staging Bucket: {effective_bucket})...",
      file=sys.stderr,
  )
  engine_instance = CobaltReasoningEngine(
      project_id=project_id,
      location=location,
      flash_model=flash_model,
      pro_model=pro_model,
  )
  existing_engine = reasoning_engines.ReasoningEngine(resource_name)
  existing_engine.update(
      reasoning_engine=engine_instance,
      requirements=[
          "google-genai",
          "google-cloud-aiplatform[reasoningengine,langchain]",
          "google-cloud-storage",
      ],
      display_name=display_name,
  )
  print(
      f"[deploy] [OK] Successfully updated {resource_name}!",
      file=sys.stderr,
  )


def list_reasoning_engines(project_id: str, location: str):
  """Lists all active Reasoning Engine instances in the project/region."""
  vertexai.init(project=project_id, location=location)
  print(
      f"[deploy] Listing Reasoning Engines in {project_id}/{location}...",
      file=sys.stderr,
  )
  engines = reasoning_engines.ReasoningEngine.list()
  if not engines:
    print("  No Reasoning Engine instances found in this region.")
    return

  for idx, eng in enumerate(engines, 1):
    print(f"  {idx:2d}. Display Name:  {eng.display_name}")
    print(f"      Resource Name: {eng.resource_name}")
    print(f"      Create Time:   {eng.create_time}")
    print(f"      Update Time:   {eng.update_time}")


def delete_reasoning_engine(resource_id: str, project_id: str, location: str):
  """Deletes a Reasoning Engine instance on Vertex AI."""
  vertexai.init(project=project_id, location=location)
  resource_name = _normalize_resource_name(resource_id, project_id, location)
  print(
      f"[deploy] Deleting Reasoning Engine \"{resource_name}\"...",
      file=sys.stderr,
  )
  engine = reasoning_engines.ReasoningEngine(resource_name)
  engine.delete()
  print(
      f"[deploy] [OK] Successfully deleted {resource_name}!",
      file=sys.stderr,
  )


def main():
  """CLI entry point for Vertex AI Reasoning Engine lifecycle."""
  parser = argparse.ArgumentParser(
      description="Deploy and manage Vertex AI Reasoning Engine for Cobalt.")
  parser.add_argument(
      "action",
      choices=["deploy", "update", "list", "delete"],
      help="Action to perform (deploy, update, list, delete).",
  )
  parser.add_argument(
      "--resource-id",
      default=None,
      help="Reasoning Engine Resource ID (required for update/delete).",
  )
  parser.add_argument(
      "--project-id",
      default=os.environ.get("GCP_PROJECT") or
      os.environ.get("GOOGLE_CLOUD_PROJECT"),
      help="GCP Project ID for Vertex AI.",
  )
  parser.add_argument(
      "--location",
      default=os.environ.get("GCP_LOCATION", "us-central1"),
      help="Vertex AI Region (default: us-central1).",
  )
  parser.add_argument(
      "--staging-bucket",
      default=os.environ.get("GCS_STAGING_BUCKET"),
      help="GCS Staging Bucket (gs://bucket_name) for deployment artifacts.",
  )
  parser.add_argument(
      "--display-name",
      default="CobaltReasoningEngine",
      help="Display name for the deployed Reasoning Engine.",
  )
  parser.add_argument(
      "--flash-model",
      default=os.environ.get("GEMINI_MODEL", "gemini-2.5-flash"),
      help="Default Flash model.",
  )
  parser.add_argument(
      "--pro-model",
      default="gemini-2.5-pro",
      help="Default Pro model for complex escalations.",
  )
  args = parser.parse_args()

  if not args.project_id:
    print(
        "[ERROR] GCP Project ID required. Set $GCP_PROJECT or "
        "pass --project-id.",
        file=sys.stderr,
    )
    sys.exit(1)

  if args.action == "deploy":
    deploy_reasoning_engine(
        args.project_id,
        args.location,
        staging_bucket=args.staging_bucket,
        display_name=args.display_name,
        flash_model=args.flash_model,
        pro_model=args.pro_model,
    )
  elif args.action == "update":
    if not args.resource_id:
      print("[ERROR] --resource-id required for update.", file=sys.stderr)
      sys.exit(1)
    update_reasoning_engine(
        args.resource_id,
        args.project_id,
        args.location,
        staging_bucket=args.staging_bucket,
        display_name=args.display_name,
        flash_model=args.flash_model,
        pro_model=args.pro_model,
    )
  elif args.action == "list":
    list_reasoning_engines(
        args.project_id,
        args.location,
    )
  elif args.action == "delete":
    if not args.resource_id:
      print("[ERROR] --resource-id required for delete.", file=sys.stderr)
      sys.exit(1)
    delete_reasoning_engine(
        args.resource_id,
        args.project_id,
        args.location,
    )


if __name__ == "__main__":
  main()
