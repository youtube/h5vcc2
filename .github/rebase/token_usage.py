#!/usr/bin/env python3
"""Token usage and metrics tracking for AI-driven Cobalt rebase pipeline."""

import dataclasses
from typing import Dict


@dataclasses.dataclass
class ModelUsage:
  """Tracks token metrics for a specific Gemini model."""

  prompt_tokens: int = 0
  completion_tokens: int = 0
  total_tokens: int = 0
  calls: int = 0

  def add(self, prompt: int, completion: int, total: int):
    """Accumulates token counts for this model."""
    self.prompt_tokens += prompt
    self.completion_tokens += completion
    self.total_tokens += total
    self.calls += 1


class TokenUsage:
  """Tracks token metrics partitioned by model name."""

  def __init__(self):
    self.by_model: Dict[str, ModelUsage] = {}

  def add(self, prompt: int, completion: int, total: int, model: str):
    """Accumulates prompt and response token usage for the given model."""
    if model not in self.by_model:
      self.by_model[model] = ModelUsage()
    self.by_model[model].add(prompt, completion, total)

  @property
  def prompt_tokens(self) -> int:
    """Returns aggregate prompt tokens across all models."""
    return sum(m.prompt_tokens for m in self.by_model.values())

  @property
  def completion_tokens(self) -> int:
    """Returns aggregate completion tokens across all models."""
    return sum(m.completion_tokens for m in self.by_model.values())

  @property
  def total_tokens(self) -> int:
    """Returns aggregate total tokens across all models."""
    return sum(m.total_tokens for m in self.by_model.values())

  @property
  def calls(self) -> int:
    """Returns aggregate API calls across all models."""
    return sum(m.calls for m in self.by_model.values())

  def format_summary_table(self) -> str:
    """Generates a Markdown table breakdown by model."""
    if not self.by_model:
      return ("| Model | Calls | Prompt Tokens | Completion Tokens | Total"
              " Tokens |\n| :--- | :--- | :--- | :--- | :--- |\n| `N/A` | 0"
              " | 0 | 0 | 0 |")
    rows = [
        "| Model | Calls | Prompt Tokens | Completion Tokens | Total"
        " Tokens |",
        "| :--- | :--- | :--- | :--- | :--- |",
    ]
    for model_name, usage in sorted(self.by_model.items()):
      rows.append(f"| `{model_name}` | {usage.calls:,} | "
                  f"{usage.prompt_tokens:,} | {usage.completion_tokens:,} | "
                  f"{usage.total_tokens:,} |")
    if len(self.by_model) > 1:
      rows.append(f"| **Total** | **{self.calls:,}** | "
                  f"**{self.prompt_tokens:,}** | "
                  f"**{self.completion_tokens:,}** | "
                  f"**{self.total_tokens:,}** |")
    return "\n".join(rows)
