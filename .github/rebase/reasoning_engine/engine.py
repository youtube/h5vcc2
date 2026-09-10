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
"""Vertex AI Reasoning Engine Service for Cobalt Chromium Rebase.

This module is packaged and deployed to Google Cloud Vertex AI to execute
remote reasoning, prompt construction, memory retrieval, and LLM calls.
Local resolvers communicate with this hosted service via ReasoningEngineClient.
"""

import json
import os
import re
import sys
import time
from typing import Any, Dict, List, Optional
import warnings

from google import genai
from google.cloud import storage
from google.genai import types

# Suppress google.genai informational warnings about AFC on direct model calls
warnings.filterwarnings("ignore", message=".*automatic function calling.*")
warnings.filterwarnings("ignore", category=UserWarning, module="google.genai")

# Disable mTLS endpoint on Cloudtop to ensure clean Vertex AI transport.
os.environ["GOOGLE_API_USE_CLIENT_CERTIFICATE"] = "false"
os.environ["GOOGLE_API_USE_MTLS_ENDPOINT"] = "never"

SKILLS_DIR = os.path.join(
    os.path.dirname(os.path.abspath(__file__)),
    "skills",
)
_SKILL_CACHE: Dict[str, str] = {}


def load_skill(skill_name: str, skills_dir: Optional[str] = None) -> str:
  """Loads and caches domain instructions from a skill markdown file."""
  base_dir = skills_dir or SKILLS_DIR
  fname = skill_name if skill_name.endswith(".md") else f"{skill_name}.md"
  cache_key = os.path.join(base_dir, fname)

  if cache_key in _SKILL_CACHE:
    return _SKILL_CACHE[cache_key]

  if not os.path.isfile(cache_key):
    raise FileNotFoundError(
        f"Required skill file not found: {cache_key}. "
        "Ensure skills directory is present and contains valid .md skills.")

  with open(cache_key, "r", encoding="utf-8") as f:
    content = f.read().strip()
    if content.startswith("---"):
      parts = content.split("---", 2)
      if len(parts) >= 3:
        content = parts[2].strip()
    _SKILL_CACHE[cache_key] = content
    return content


class CobaltReasoningEngine:
  """Hosted Vertex AI Reasoning Engine service for Cobalt rebase tasks."""

  def __init__(
      self,
      *,
      project_id: Optional[str] = None,
      location: str = "us-central1",
      flash_model: Optional[str] = None,
      pro_model: str = "gemini-2.5-pro",
      skills_dir: Optional[str] = None,
      gcs_memory_uri: Optional[str] = None,
  ):
    self.project_id = (
        project_id or os.environ.get("GCP_PROJECT") or
        os.environ.get("GOOGLE_CLOUD_PROJECT"))
    self.location = location
    self.flash_model = flash_model or "gemini-2.5-flash"
    self.pro_model = pro_model
    self.skills_dir = skills_dir or SKILLS_DIR
    self.gcs_memory_uri = (
        gcs_memory_uri or os.environ.get("GCS_MEMORY_URI") or
        (f"gs://{self.project_id}/rebase_memory/knowledge_bank.json"
         if self.project_id else None))
    self.memory_cache: Optional[List[Dict[str, Any]]] = None
    self.storage_client: Any = None
    self.skill_cache: Dict[str, str] = {
        "cobalt_rebase":
            load_skill("cobalt_rebase", self.skills_dir),
        "conflict_resolution":
            load_skill("conflict_resolution", self.skills_dir),
        "gn_healing":
            load_skill("gn_healing", self.skills_dir),
        "compiler_healing":
            load_skill("compiler_healing", self.skills_dir),
    }
    self.client: Optional[genai.Client] = None

  def set_up(self):
    """Initializes Google GenAI client in remote Vertex AI container."""
    self.client = genai.Client(
        vertexai=True,
        project=self.project_id,
        location=self.location,
    )

  def _get_storage_client(self) -> Any:
    """Lazy loads Google Cloud Storage client in cloud or local environment."""
    if self.storage_client is None:
      try:
        self.storage_client = storage.Client(project=self.project_id)
      except Exception:  # pylint: disable=broad-exception-caught
        self.storage_client = None
    return self.storage_client

  def _load_memory(self) -> List[Dict[str, Any]]:
    """Loads knowledge memory from GCS or returns cached memory."""
    if self.memory_cache is not None:
      return self.memory_cache
    self.memory_cache = []
    if not self.gcs_memory_uri or not self.gcs_memory_uri.startswith("gs://"):
      return self.memory_cache

    client = self._get_storage_client()
    if client is None:
      return self.memory_cache

    try:
      path_part = self.gcs_memory_uri[5:]
      if "/" not in path_part:
        return self.memory_cache
      bucket_name, blob_name = path_part.split("/", 1)
      bucket = client.bucket(bucket_name)
      blob = bucket.blob(blob_name)
      if blob.exists():
        data = blob.download_as_text(encoding="utf-8")
        loaded = json.loads(data)
        if isinstance(loaded, list):
          self.memory_cache = loaded
    except Exception as e:  # pylint: disable=broad-exception-caught
      print(
          f"  [REASONING_ENGINE] Notice: Could not read memory from GCS: {e}",
          file=sys.stderr,
      )
    return self.memory_cache

  def get_past_experience(self, query: str, max_items: int = 3) -> str:
    """Retrieves relevant past fixes from knowledge bank."""
    memory = self._load_memory()
    if not memory:
      return ""

    q_lower = query.lower()
    q_words = set(re.findall(r"\w+", q_lower))
    scored = []
    for item in memory:
      desc = item.get("issue_description", "").lower()
      target = item.get("target_file", "").lower()
      combined = f"{desc} {target}"
      score = sum(1 for w in q_words if w in combined)
      if score > 0:
        scored.append((score, item))

    scored.sort(key=lambda x: x[0], reverse=True)
    top_items = [item for _, item in scored[:max_items]]
    if not top_items:
      return ""

    blocks = []
    for idx, item in enumerate(top_items, 1):
      desc = item.get("issue_description", "")
      sol = item.get("solution_diff", "")
      tf = item.get("target_file", "")
      target_line = f"Target File: {tf}\n" if tf else ""
      blocks.append(f"Example #{idx}:\n{target_line}Issue: {desc}\nFix:\n{sol}")
    return "\n\n".join(blocks)

  def record_successful_fix(
      self,
      issue_description: str,
      solution_diff: str,
      target_file: str = "",
  ) -> bool:
    """Records a verified fix into GCS knowledge memory bank."""
    memory = self._load_memory()
    for item in memory:
      if item.get("solution_diff") == solution_diff:
        return True

    record = {
        "timestamp": time.time(),
        "target_file": target_file,
        "issue_description": issue_description,
        "solution_diff": solution_diff,
    }
    memory.append(record)
    self.memory_cache = memory

    if self.gcs_memory_uri and self.gcs_memory_uri.startswith("gs://"):
      client = self._get_storage_client()
      if client:
        try:
          path_part = self.gcs_memory_uri[5:]
          if "/" in path_part:
            bucket_name, blob_name = path_part.split("/", 1)
            bucket = client.bucket(bucket_name)
            blob = bucket.blob(blob_name)
            blob.upload_from_string(
                json.dumps(memory, indent=2),
                content_type="application/json",
            )
            return True
        except Exception as e:  # pylint: disable=broad-exception-caught
          print(
              "  [REASONING_ENGINE] Warning: Could not write memory to GCS: "
              f"{e}",
              file=sys.stderr,
          )
    return True

  def _get_client(self) -> genai.Client:
    """Returns active client instance, initializing if needed."""
    if self.client is None:
      self.set_up()
    return self.client

  def _get_skill(self, name: str) -> str:
    """Retrieves skill instructions from cache or disk."""
    if name in self.skill_cache:
      return self.skill_cache[name]
    return load_skill(name, self.skills_dir)

  def _generate_content_with_retry(
      self,
      model: str,
      contents: Any,
      config: types.GenerateContentConfig,
      *,
      max_retries: int = 5,
      initial_backoff: float = 4.0,
  ) -> Optional[types.GenerateContentResponse]:
    """Calls generate_content with exponential backoff for 429/503 errors."""
    client = self._get_client()
    backoff = initial_backoff
    for attempt in range(1, max_retries + 1):
      try:
        return client.models.generate_content(
            model=model,
            contents=contents,
            config=config,
        )
      except Exception as e:  # pylint: disable=broad-exception-caught
        error_msg = str(e)
        if any(
            code in error_msg
            for code in ("429", "ResourceExhausted", "503", "Unavailable")):
          if attempt == max_retries:
            print(
                f"  [REASONING_ENGINE] Rate limit exceeded ({model}) after "
                f"{attempt} attempts: {e}",
                file=sys.stderr,
            )
            return None
          print(
              f"  [REASONING_ENGINE] Rate limit/busy ({model}) on attempt "
              f"{attempt}. Retrying in {backoff:.1f}s...",
              file=sys.stderr,
          )
          time.sleep(backoff)
          backoff *= 2.0
        else:
          print(
              f"  [REASONING_ENGINE] API error ({model}): {e}",
              file=sys.stderr,
          )
          if attempt == max_retries:
            return None
          time.sleep(backoff)
          backoff *= 1.5
    return None

  def _create_generation_config(
      self,
      chosen_model: str,
      system_instruction: str,
      temperature: float = 0.1,
  ) -> types.GenerateContentConfig:
    """Constructs model-specific GenerateContentConfig."""
    if "gemini-3." in chosen_model or "thinking" in chosen_model.lower():
      return types.GenerateContentConfig(
          system_instruction=system_instruction,
          thinking_config=types.ThinkingConfig(thinking_budget=1024),
      )
    return types.GenerateContentConfig(
        system_instruction=system_instruction,
        temperature=temperature,
    )

  def generate_content(
      self,
      contents: Any,
      system_instruction: str = "",
      *,
      model: Optional[str] = None,
      temperature: float = 0.1,
  ) -> Optional[types.GenerateContentResponse]:
    """Generates content via Vertex AI with automatic rate-limit retries."""
    chosen_model = model or self.flash_model
    config = self._create_generation_config(
        chosen_model, system_instruction, temperature=temperature)
    return self._generate_content_with_retry(
        model=chosen_model,
        contents=contents,
        config=config,
    )

  def query(self, action: str = "resolve_conflict", **kwargs) -> Dict[str, Any]:
    """Primary query dispatcher for Vertex AI Reasoning Engine."""
    if action == "resolve_conflict":
      return self.resolve_conflict(**kwargs)
    if action in ("heal_gn", "heal_gn_error"):
      return self.heal_gn_error(**kwargs)
    if action in ("heal_compiler", "heal_compiler_break",
                  "heal_compiler_error"):
      return self.heal_compiler_error(**kwargs)
    if action in ("record_successful_fix", "record_fix"):
      return {"success": self.record_successful_fix(**kwargs)}
    if action in ("get_past_experience", "load_past_experience"):
      return {"experience": self.get_past_experience(**kwargs)}
    if action == "chat":
      return self.chat(**kwargs)
    raise ValueError(f"Unknown Reasoning Engine action: {action}")

  def resolve_conflict(
      self,
      file_path: str,
      language: str,
      raw_conflict: str,
      *,
      context_before: str = "",
      context_after: str = "",
      git_context: str = "",
      past_experience: str = "",
      investigation_history: str = "",
      instruction: str = "",
      use_pro: bool = False,
  ) -> Dict[str, Any]:
    """Resolves source/DEPS merge conflicts on Vertex AI."""
    chosen_model = self.pro_model if use_pro else self.flash_model
    rebase_skill = self._get_skill("cobalt_rebase")
    conflict_skill = self._get_skill("conflict_resolution")

    effective_past = (
        past_experience or
        self.get_past_experience(f"{file_path} {raw_conflict}"))
    past_lessons_section = (
        f"--- Past Successful Lessons ---\n{effective_past}\n\n"
        if effective_past else "")
    investigation_section = (
        f"--- Investigation Tool Results ---\n{investigation_history}\n\n"
        if investigation_history else "")

    sys_inst = (
        f"You are an expert Chromium and Cobalt engineer ({language}).\n\n"
        f"--- General Rebase Guidelines ---\n{rebase_skill}\n\n"
        f"--- Conflict Resolution Skill ---\n{conflict_skill}\n")
    task_inst = instruction or (
        "Task: Resolve this specific merge conflict. Preserve all Cobalt "
        "macros (#if BUILDFLAG(USE_STARBOARD_MEDIA), #if BUILDFLAG(IS_COBALT), "
        "#if defined(STARBOARD)), custom variables (checkout_cobalt_internal, "
        "checkout_copybara), platform shims, and Cobalt runtime behavior "
        "while adopting upstream updates.\nReturn ONLY the exact replacement "
        "code for the block.")

    prompt = (f"Target File: {file_path} ({language})\n"
              f"{git_context}\n\n"
              f"{past_lessons_section}"
              f"Context before conflict:\n{context_before}\n\n"
              f"Conflicted Block to Resolve:\n{raw_conflict}\n\n"
              f"Context after conflict:\n{context_after}\n\n"
              f"{investigation_section}"
              f"{task_inst}")

    resp = self._generate_content_with_retry(
        model=chosen_model,
        contents=prompt,
        config=self._create_generation_config(chosen_model, sys_inst),
    )
    return {
        "status": "SUCCESS" if resp and resp.text else "ERROR",
        "replacement": resp.text.strip() if resp and resp.text else "",
        "model_used": chosen_model,
    }

  def heal_gn_error(
      self,
      error_trace: str,
      file_context: str = "",
      *,
      attempt_history: str = "",
      past_experience: str = "",
      investigation_history: str = "",
      use_pro: bool = False,
  ) -> Dict[str, Any]:
    """Diagnoses and fixes GN generation errors on Vertex AI."""
    chosen_model = self.pro_model if use_pro else self.flash_model
    rebase_skill = self._get_skill("cobalt_rebase")
    gn_skill = self._get_skill("gn_healing")

    effective_past = (
        past_experience or self.get_past_experience(f"gn {error_trace}"))
    past_lessons_section = (f"Past Successful Lessons:\n{effective_past}\n\n"
                            if effective_past else "")
    investigation_section = (
        f"--- Investigation Tool Results ---\n{investigation_history}\n\n"
        if investigation_history else "")

    sys_inst = ("You are an expert Chromium and Cobalt GN build engineer.\n\n"
                f"--- General Rebase Guidelines ---\n{rebase_skill}\n\n"
                f"--- GN Healing Skill ---\n{gn_skill}\n")
    prompt = (f"GN Build Error:\n--------------------\n{error_trace}\n"
              "--------------------\n\n"
              f"{investigation_section}"
              f"{past_lessons_section}"
              f"Prior Attempt History:\n{attempt_history}\n\n"
              f"Relevant File Definitions:\n{file_context}\n\n"
              "Instructions:\n"
              "- If you need to inspect files or search paths, output a single "
              "TOOL_ command (e.g. `TOOL_READ_FILE: <path> <start>-<end>` or "
              "`TOOL_FIND_FILE: <pattern>`).\n"
              "- Otherwise output the final SEARCH / REPLACE block:\n"
              "FILE: <relative_filepath>\n"
              "<<<<<<< SEARCH\n"
              "<exact lines to replace>\n"
              "=======\n"
              "<fixed replacement lines>\n"
              ">>>>>>> REPLACE")
    resp = self._generate_content_with_retry(
        model=chosen_model,
        contents=prompt,
        config=self._create_generation_config(chosen_model, sys_inst),
    )
    return {
        "status": "SUCCESS" if resp and resp.text else "ERROR",
        "patch": resp.text.strip() if resp and resp.text else "",
        "model_used": chosen_model,
    }

  def heal_compiler_error(
      self,
      target: str = "",
      diagnostics: str = "",
      source_contexts: str = "",
      *,
      error_trace: str = "",
      file_context: str = "",
      target_file: str = "",
      history: str = "",
      past_experience: str = "",
      investigation_history: str = "",
      use_pro: bool = False,
  ) -> Dict[str, Any]:
    """Diagnoses and repairs C++/Java compilation errors on Vertex AI."""
    eff_target = target or target_file or "cobalt"
    eff_diag = diagnostics or error_trace
    eff_ctx = source_contexts or file_context
    eff_inv = investigation_history or history

    chosen_model = self.pro_model if use_pro else self.flash_model
    rebase_skill = self._get_skill("cobalt_rebase")
    compiler_skill = self._get_skill("compiler_healing")

    effective_past = (
        past_experience or self.get_past_experience(f"{eff_target} {eff_diag}"))
    past_lessons_section = (
        f"Past Successful Rebase Lessons:\n{effective_past}\n\n"
        if effective_past else "")
    investigation_section = (
        f"--- Investigation Tool Results ---\n{eff_inv}\n\n" if eff_inv else "")

    is_gn_target = (
        eff_target.endswith((".gn", ".gni")) or target_file.endswith(
            (".gn", ".gni")))
    gn_skill_text = self._get_skill("gn_healing") if is_gn_target else ""
    gn_skill_section = (f"\n\n--- GN Healing Skill ---\n{gn_skill_text}"
                        if is_gn_target else "")

    sys_inst = ("You are an expert Chromium and Cobalt systems engineer.\n\n"
                f"--- General Rebase Guidelines ---\n{rebase_skill}\n\n"
                f"--- Compiler Healing Skill ---\n{compiler_skill}"
                f"{gn_skill_section}\n")
    prompt = (f"autoninja build for \"{eff_target}\" failed.\n\n"
              f"Compiler Diagnostics:\n--------------------\n{eff_diag}\n"
              "--------------------\n\n"
              f"{investigation_section}"
              f"{past_lessons_section}"
              f"Offending Source Code Excerpts:\n{eff_ctx}\n\n"
              "Instructions:\n"
              "- When encountering undeclared identifiers, unknown types, or "
              "incomplete types, DO NOT guess namespaces. Use TOOL_GREP to "
              "find the defining header in Chromium:\n"
              "  TOOL_GREP: <symbol>\n"
              "- If you need to inspect referencing BUILD.gn files or read "
              "headers, output a single TOOL_ command (e.g. `TOOL_READ_FILE: "
              "<path> <start>-<end>` or `TOOL_FIND_FILE: <pattern>`).\n"
              "- Otherwise output the final SEARCH / REPLACE or DELETE block:\n"
              "  * To replace / add include:\n"
              "  FILE: <relative_filepath>\n"
              "  <<<<<<< SEARCH\n"
              "  <exact lines to replace>\n"
              "  =======\n"
              "  <fixed replacement lines>\n"
              "  >>>>>>> REPLACE\n"
              "  * To delete obsolete code:\n"
              "  FILE: <relative_filepath>\n"
              "  <<<<<<< DELETE\n"
              "  <exact lines to delete>\n"
              "  >>>>>>> DELETE")
    resp = self._generate_content_with_retry(
        model=chosen_model,
        contents=prompt,
        config=self._create_generation_config(chosen_model, sys_inst),
    )
    return {
        "status": "SUCCESS" if resp and resp.text else "ERROR",
        "patch": resp.text.strip() if resp and resp.text else "",
        "model_used": chosen_model,
    }

  def chat(
      self,
      message: str,
      history: Optional[List[Dict[str, str]]] = None,
      *,
      mode: str = "rebase",
      use_pro: bool = False,
      failure_memory: str = "",
  ) -> Dict[str, Any]:
    """Interactive conversational interface to Reasoning Engine."""
    chosen_model = self.pro_model if use_pro else self.flash_model
    skill_map = {
        "rebase": "cobalt_rebase",
        "gn": "gn_healing",
        "compiler": "compiler_healing",
        "conflict": "conflict_resolution",
    }
    skill_key = skill_map.get(mode, "cobalt_rebase")
    skill_doc = self._get_skill(skill_key)
    rebase_doc = self._get_skill("cobalt_rebase")

    failure_section = (
        f"\n--- Latest Pipeline Failure State ---\n{failure_memory}\n"
        if failure_memory and "No recent" not in failure_memory else "")

    sys_inst = (
        f"You are the Cobalt Chromium Rebase Reasoning Engine ({mode} mode).\n"
        "You assist engineers in analyzing rebase conflicts, GN build breaks, "
        "toolchain issues, and compiler failures.\n\n"
        f"--- Base Instructions ---\n{rebase_doc}\n\n"
        f"--- Active Domain Skill ({mode}) ---\n{skill_doc}\n"
        f"{failure_section}")

    contents: List[Any] = []
    if history:
      for turn in history:
        contents.append(
            types.Content(
                role=turn.get("role", "user"),
                parts=[types.Part.from_text(text=turn.get("content", ""))],
            ))
    contents.append(
        types.Content(
            role="user",
            parts=[types.Part.from_text(text=message)],
        ))

    resp = self._generate_content_with_retry(
        model=chosen_model,
        contents=contents,
        config=self._create_generation_config(
            chosen_model, sys_inst, temperature=0.2),
    )
    return {
        "status": "SUCCESS" if resp and resp.text else "ERROR",
        "response": resp.text.strip() if resp and resp.text else "",
        "model_used": chosen_model,
    }
