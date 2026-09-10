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
"""Local client agent for communicating with hosted Reasoning Engine."""

import dataclasses
import os
import sys
import time
from typing import Any, Dict, List, Optional

import vertexai
from vertexai.preview import reasoning_engines


@dataclasses.dataclass
class ModelResponseWrapper:
  """Lightweight wrapper mimicking generate_content response."""
  text: str
  usage_metadata: Optional[Any] = None


class ReasoningEngineClient:
  """Client proxy that dispatches rebase tasks to hosted Reasoning Engine."""

  def __init__(
      self,
      resource_id: Optional[str] = None,
      *,
      project_id: Optional[str] = None,
      location: str = "us-central1",
      flash_model: str = "gemini-2.5-flash",
      pro_model: str = "gemini-2.5-pro",
      max_connect_retries: int = 3,
      max_query_retries: int = 5,
  ):
    self.resource_id = (
        resource_id or os.environ.get("REASONING_ENGINE_ID") or
        os.environ.get("REASONING_ENGINE_RESOURCE_ID"))
    self.project_id = (
        project_id or os.environ.get("GCP_PROJECT") or
        os.environ.get("GOOGLE_CLOUD_PROJECT"))
    self.location = location
    self.flash_model = flash_model
    self.pro_model = pro_model
    self.max_connect_retries = max_connect_retries
    self.max_query_retries = max_query_retries
    self._remote_engine: Any = None

    if not self.resource_id:
      raise ValueError(
          "ReasoningEngineClient requires a valid Reasoning Engine resource "
          "ID (e.g. '--reasoning-engine-id 7153081251950231552').")

  def _get_remote_engine(self) -> Any:
    """Connects to the hosted Vertex AI Reasoning Engine with retries."""
    if self._remote_engine is not None:
      return self._remote_engine

    res_name = self.resource_id
    loc = self.location
    if res_name.startswith("projects/"):
      if "/locations/" in res_name:
        loc = res_name.split("/locations/", 1)[1].split("/", 1)[0]
    else:
      res_loc = "us-central1" if self.location == "global" else self.location
      res_name = (f"projects/{self.project_id}/locations/{res_loc}/"
                  f"reasoningEngines/{self.resource_id}")
      loc = res_loc

    backoff = 2.0
    for attempt in range(1, self.max_connect_retries + 1):
      try:
        vertexai.init(project=self.project_id, location=loc)
        print(
            "  [REASONING_ENGINE_CLIENT] Connecting to hosted Reasoning "
            f"Engine: {res_name}",
            file=sys.stderr,
        )
        self._remote_engine = reasoning_engines.ReasoningEngine(res_name)
        return self._remote_engine
      except Exception as e:  # pylint: disable=broad-exception-caught
        if attempt == self.max_connect_retries:
          raise RuntimeError(
              f"Failed to connect to hosted Reasoning Engine '{res_name}' "
              f"after {attempt} attempt(s): {e}") from e
        print(
            "  [REASONING_ENGINE_CLIENT] Warning: Connection attempt "
            f"{attempt}/{self.max_connect_retries} failed: {e}. "
            f"Retrying in {backoff:.1f}s...",
            file=sys.stderr,
        )
        time.sleep(backoff)
        backoff *= 1.5

    raise RuntimeError(f"Could not connect to Reasoning Engine: {res_name}")

  def query(self, action: str, **kwargs) -> Any:
    """Dispatches query to hosted Reasoning Engine with exponential backoff."""
    engine = self._get_remote_engine()
    backoff = 2.0
    last_error: Optional[Exception] = None

    for attempt in range(1, self.max_query_retries + 1):
      try:
        return engine.query(action=action, **kwargs)
      except Exception as e:  # pylint: disable=broad-exception-caught
        last_error = e
        if attempt == self.max_query_retries:
          print(
              "  [REASONING_ENGINE_CLIENT] Error querying hosted Reasoning "
              f"Engine after {attempt} attempt(s): {e}",
              file=sys.stderr,
          )
          raise RuntimeError("Hosted Reasoning Engine query failed after "
                             f"{attempt} attempts: {e}") from e

        print(
            "  [REASONING_ENGINE_CLIENT] Notice: Remote query failed "
            f"(attempt {attempt}/{self.max_query_retries}): {e}. "
            f"Retrying in {backoff:.1f}s...",
            file=sys.stderr,
        )
        time.sleep(backoff)
        backoff *= 1.5

    raise RuntimeError(
        f"Hosted Reasoning Engine query failed: {last_error}") from last_error

  def generate_content(
      self,
      contents: Any,
      system_instruction: str = "",
      *,
      model: Optional[str] = None,
      temperature: float = 0.1,
  ) -> ModelResponseWrapper:
    """Generates content via hosted Reasoning Engine chat interface."""
    del temperature, system_instruction
    res = self.query(
        action="chat",
        message=str(contents),
        history=[],
        use_pro=(model == self.pro_model),
    )
    text = ""
    if isinstance(res, dict):
      text = res.get("response", "")
    elif isinstance(res, str):
      text = res
    return ModelResponseWrapper(text=text)

  def chat(
      self,
      message: str,
      history: Optional[List[Dict[str, str]]] = None,
      *,
      mode: str = "rebase",
      use_pro: bool = False,
      failure_memory: str = "",
  ) -> Dict[str, Any]:
    """Interactive conversational chat via hosted Reasoning Engine."""
    return self.query(
        action="chat",
        message=message,
        history=history or [],
        mode=mode,
        use_pro=use_pro,
        failure_memory=failure_memory,
    )

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
    """Resolves source/DEPS merge conflict via hosted Reasoning Engine."""
    return self.query(
        action="resolve_conflict",
        file_path=file_path,
        language=language,
        raw_conflict=raw_conflict,
        context_before=context_before,
        context_after=context_after,
        git_context=git_context,
        past_experience=past_experience,
        investigation_history=investigation_history,
        instruction=instruction,
        use_pro=use_pro,
    )

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
    """Fixes GN build error via hosted Reasoning Engine."""
    return self.query(
        action="heal_gn_error",
        error_trace=error_trace,
        file_context=file_context,
        attempt_history=attempt_history,
        past_experience=past_experience,
        investigation_history=investigation_history,
        use_pro=use_pro,
    )

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
    """Fixes compiler / linker error via hosted Reasoning Engine."""
    eff_target = target or target_file or "cobalt"
    eff_diag = diagnostics or error_trace
    eff_ctx = source_contexts or file_context
    eff_inv = investigation_history or history

    return self.query(
        action="heal_compiler_error",
        target=eff_target,
        diagnostics=eff_diag,
        source_contexts=eff_ctx,
        past_experience=past_experience,
        investigation_history=eff_inv,
        use_pro=use_pro,
    )

  def record_successful_fix(
      self,
      issue_description: str,
      solution_diff: str,
      target_file: str = "",
  ) -> bool:
    """Records verified fix to GCS memory via hosted Reasoning Engine."""
    res = self.query(
        action="record_successful_fix",
        issue_description=issue_description,
        solution_diff=solution_diff,
        target_file=target_file,
    )
    if isinstance(res, dict):
      return bool(res.get("success", True))
    return bool(res)

  def get_past_experience(
      self,
      query: str,
      max_items: int = 3,
  ) -> str:
    """Retrieves relevant past experiences from hosted Reasoning Engine."""
    res = self.query(
        action="get_past_experience",
        query=query,
        max_items=max_items,
    )
    if isinstance(res, dict):
      return str(res.get("experience", ""))
    return str(res)
