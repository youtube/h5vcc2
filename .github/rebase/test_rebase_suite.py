#!/usr/bin/env python3
"""Comprehensive test suite for Cobalt Chromium rebase automation tools."""

import ast
import os
import tempfile
import unittest
from unittest import mock

from autoninja import (
    AutoninjaResolver,
    CompilerDiagnostic,
    find_build_file_for_object,
    parse_compiler_errors,
)
from base_resolver import (
    apply_patch_or_replacement,
    execute_local_tool,
    extract_build_progress,
    get_chromium_milestone,
    is_unmodified_third_party,
)
from conflicts import (
    detect_language,
    extract_conflict_blocks,
    resolve_file_conflicts,
)
from engine_client import ReasoningEngineClient
from gn_gen import GNDiagnostic, GNGenResolver, extract_gn_target_files
from reasoning_engine import CobaltReasoningEngine
from token_usage import TokenUsage

SAMPLE_DEPS_CONFLICT = """git_dependencies = "SYNC"

vars = {
  "build_with_chromium": True,
  "checkout_cobalt_internal": False,
<<<<<<< HEAD
  "skia_revision": "aaaa111122223333444455556666777788889999",
=======
  "skia_revision": "bbbb111122223333444455556666777788889999",
>>>>>>> origin/main
  "checkout_copybara": False,
}
"""

SAMPLE_CPP_CONFLICT = """#include "base/logging.h"

void InitializeMedia() {
<<<<<<< HEAD
#if BUILDFLAG(USE_STARBOARD_MEDIA)
  InitStarboardMediaPipeline();
#endif
=======
  InitChromiumDefaultMediaPipeline();
>>>>>>> origin/main
}
"""

SAMPLE_COMPILER_OUTPUT = (
    "[1420/5400] CXX obj/content/browser/keep_alive_url_loader_service.o\n"
    "../../content/browser/loader/keep_alive_url_loader_service.cc:123:45: "
    "error: no matching function for call to \"NavigationThrottle\"\n"
    "  NavigationThrottle throttle(registry);\n"
    "                     ^~~~~~~~\n"
    "../../content/public/browser/navigation_throttle.h:45:3: note: candidate "
    "function not viable\n"
    "  NavigationThrottle(NavigationThrottleRegistry& registry);\n"
    "  ^\n"
    "[1421/5400] CXX obj/media/audio/audio/audio_manager_android.o\n")


class TestRebaseAutomationSuite(unittest.TestCase):
  """Comprehensive unit test suite for Cobalt rebase automation."""

  def test_language_detection(self):
    """Tests language identification from file extension."""
    self.assertEqual(detect_language("DEPS"), "Python (Chromium DEPS)")
    self.assertEqual(detect_language("cobalt/browser/main.cc"), "C++")
    self.assertEqual(detect_language("cobalt/BUILD.gn"), "GN Build File")
    self.assertEqual(detect_language("cobalt/App.java"), "Java")

  def test_conflict_extraction(self):
    """Tests extraction of conflict blocks with context."""
    blocks = extract_conflict_blocks(SAMPLE_DEPS_CONFLICT)
    self.assertEqual(len(blocks), 1)
    self.assertEqual(blocks[0].index, 1)
    self.assertIn("aaaa1111", blocks[0].ours_content)
    self.assertIn("bbbb1111", blocks[0].theirs_content)

  def test_local_tool_read_file(self):
    """Tests execution of local file inspection tool."""
    with tempfile.NamedTemporaryFile("w+", delete=False) as tmp:
      tmp.write("line 1\nline 2\nline 3\nline 4\nline 5\n")
      tmp_path = tmp.name

    try:
      res = execute_local_tool(
          f"TOOL_READ_FILE: {tmp_path} 2-4",
          repo_path=os.path.dirname(tmp_path),
      )
      self.assertIn("line 2", res)
      self.assertIn("line 4", res)
      self.assertNotIn("line 1", res)
    finally:
      if os.path.exists(tmp_path):
        os.remove(tmp_path)

  def test_deps_mock_resolution_and_ast(self):
    """Tests mock DEPS resolution and syntax verification."""
    with tempfile.NamedTemporaryFile("w+", suffix="DEPS", delete=False) as tmp:
      tmp.write(SAMPLE_DEPS_CONFLICT)
      tmp_path = tmp.name

    try:
      tracker = TokenUsage()
      escalations = []
      ok = resolve_file_conflicts(
          file_path=tmp_path,
          repo_path=os.path.dirname(tmp_path),
          git_context="",
          mock_mode=True,
          token_tracker=tracker,
          escalations=escalations,
      )
      self.assertTrue(ok)
      self.assertEqual(len(escalations), 0)
      with open(tmp_path, "r", encoding="utf-8") as f:
        resolved = f.read()

      self.assertNotIn("<<<<<<<", resolved)
      self.assertNotIn(">>>>>>>", resolved)
      self.assertIn("bbbb1111", resolved)
      self.assertIn("checkout_copybara", resolved)

      # Validate AST syntax
      tree = ast.parse(resolved)
      self.assertIsNotNone(tree)
      self.assertEqual(tracker.calls, 1)
    finally:
      if os.path.exists(tmp_path):
        os.remove(tmp_path)

  def test_source_mock_resolution(self):
    """Tests mock C++ conflict resolution."""
    with tempfile.NamedTemporaryFile("w+", suffix=".cc", delete=False) as tmp:
      tmp.write(SAMPLE_CPP_CONFLICT)
      tmp_path = tmp.name

    try:
      tracker = TokenUsage()
      escalations = []
      ok = resolve_file_conflicts(
          file_path=tmp_path,
          repo_path=os.path.dirname(tmp_path),
          git_context="",
          mock_mode=True,
          token_tracker=tracker,
          escalations=escalations,
      )
      self.assertTrue(ok)
      self.assertEqual(len(escalations), 0)
      with open(tmp_path, "r", encoding="utf-8") as f:
        resolved = f.read()

      self.assertNotIn("<<<<<<<", resolved)
      self.assertNotIn(">>>>>>>", resolved)
      self.assertIn("InitChromiumDefaultMediaPipeline", resolved)
    finally:
      if os.path.exists(tmp_path):
        os.remove(tmp_path)

  def test_unmodified_third_party_fast_path(self):
    """Tests fast-path resolution for pure third-party files."""
    with tempfile.TemporaryDirectory() as tmp_dir:
      tp_dir = os.path.join(tmp_dir, "third_party", "xnnpack")
      os.makedirs(tp_dir, exist_ok=True)
      tp_file = os.path.join(tp_dir, "BUILD.gn")
      with open(tp_file, "w", encoding="utf-8") as f:
        f.write("source_set(\"xnnpack\") {\n"
                "<<<<<<< HEAD\n"
                "  sources = [ \"old.c\" ]\n"
                "=======\n"
                "  sources = [ \"new.c\" ]\n"
                ">>>>>>> upstream/main (CONFLICTED Revert Cobalt.)\n"
                "}\n")

      tracker = TokenUsage()
      escalations = []
      ok = resolve_file_conflicts(
          file_path=tp_file,
          repo_path=tmp_dir,
          git_context="",
          mock_mode=False,  # Should not invoke API at all
          token_tracker=tracker,
          escalations=escalations,
      )
      self.assertTrue(ok)
      self.assertEqual(tracker.calls, 0)
      with open(tp_file, "r", encoding="utf-8") as f:
        resolved = f.read()
      self.assertNotIn("<<<<<<<", resolved)
      self.assertNotIn(">>>>>>>", resolved)
      self.assertIn("sources = [ \"new.c\" ]", resolved)

  def test_compiler_error_parsing(self):
    """Tests parsing of Clang error logs."""
    diags = parse_compiler_errors(SAMPLE_COMPILER_OUTPUT, repo_path="/repo")
    self.assertEqual(len(diags), 1)
    self.assertEqual(diags[0].line_number, 123)
    self.assertIn("NavigationThrottle", diags[0].error_message)

  def test_gn_search_replace_with_slash_prefix(self):
    """Tests search and replace patch application on GN files."""
    with tempfile.NamedTemporaryFile(
        "w+", suffix="BUILD.gn", delete=False) as tmp:
      tmp.write("deps = [\n  \"//third_party/mesa_headers\",\n"
                "  \"//third_party/re2\",\n]\n")
      tmp_path = tmp.name

    try:
      rel = os.path.basename(tmp_path)
      parent = os.path.dirname(tmp_path)
      ai_patch = f"""FILE: //{rel}
<<<<<<< SEARCH
deps = [
  "//third_party/mesa_headers",
  "//third_party/re2",
]
=======
deps = [
  "//third_party/re2",
]
>>>>>>> REPLACE
"""
      modified = apply_patch_or_replacement(ai_patch, repo_path=parent)
      self.assertTrue(modified)
      with open(tmp_path, "r", encoding="utf-8") as f:
        content = f.read()
      self.assertNotIn("mesa_headers", content)
      self.assertIn("third_party/re2", content)
    finally:
      if os.path.exists(tmp_path):
        os.remove(tmp_path)

  def test_search_replace_sanitizes_rogue_markers(self):
    """Tests that apply_search_replace strips rogue ======= markers."""
    with tempfile.NamedTemporaryFile("w+", suffix=".gn", delete=False) as tmp:
      tmp.write("enable_rust_png = false\n")
      tmp_path = tmp.name

    try:
      rel = os.path.basename(tmp_path)
      parent = os.path.dirname(tmp_path)
      # Simulates model repeating ======= in replacement
      ai_patch = f"""FILE: //{rel}
<<<<<<< SEARCH
enable_rust_png = false
=======
=======
# enable_rust_png = false
>>>>>>> REPLACE
"""
      modified = apply_patch_or_replacement(ai_patch, repo_path=parent)
      self.assertTrue(modified)
      with open(tmp_path, "r", encoding="utf-8") as f:
        content = f.read()
      self.assertNotIn("=======", content)
      self.assertIn("# enable_rust_png = false", content)
    finally:
      if os.path.exists(tmp_path):
        os.remove(tmp_path)

  def test_search_replace_token_normalized_matching(self):
    """Tests matching auto-generated files with quirky internal whitespace."""
    file_content = (
        "DAWN_NO_SANITIZE(\"cfi-icall\")\n"
        "__attribute__((weak)) WGPUStatus  wgpuGetInstanceCapabilities("
        "WGPUInstanceCapabilities * capabilities) {\n"
        "return     procs.getInstanceCapabilities(capabilities);\n"
        "}\n")
    with tempfile.NamedTemporaryFile("w+", suffix=".cc", delete=False) as tmp:
      tmp.write(file_content)
      tmp_path = tmp.name

    try:
      rel = os.path.basename(tmp_path)
      parent = os.path.dirname(tmp_path)
      ai_patch = (f"FILE: //{rel}\n"
                  "<<<<<<< SEARCH\n"
                  "55: DAWN_NO_SANITIZE(\"cfi-icall\")\n"
                  "56: __attribute__((weak)) WGPUStatus "
                  "wgpuGetInstanceCapabilities("
                  "WGPUInstanceCapabilities * capabilities) {\n"
                  "57:   return procs.getInstanceCapabilities(capabilities);\n"
                  "58: }\n"
                  "=======\n"
                  "DAWN_NO_SANITIZE(\"cfi-icall\")\n"
                  "__attribute__((weak)) WGPUStatus "
                  "wgpuGetInstanceCapabilities(void* capabilities) {\n"
                  "  return 0;\n"
                  "}\n"
                  ">>>>>>> REPLACE\n")
      modified = apply_patch_or_replacement(ai_patch, repo_path=parent)
      self.assertTrue(modified)
      with open(tmp_path, "r", encoding="utf-8") as f:
        content = f.read()
      self.assertIn("void* capabilities", content)
      self.assertNotIn("WGPUInstanceCapabilities", content)
    finally:
      if os.path.exists(tmp_path):
        os.remove(tmp_path)

  def test_apply_explicit_delete_block(self):
    """Tests that <<<<<<< DELETE ... >>>>>>> DELETE removes code cleanly."""
    file_content = ("void KeepBefore() {}\n"
                    "void ObsoleteFunc() {\n"
                    "  // To be removed\n"
                    "}\n"
                    "void KeepAfter() {}\n")
    with tempfile.NamedTemporaryFile("w+", suffix=".cc", delete=False) as tmp:
      tmp.write(file_content)
      tmp_path = tmp.name

    try:
      rel = os.path.basename(tmp_path)
      parent = os.path.dirname(tmp_path)
      del_patch = f"""FILE: //{rel}
<<<<<<< DELETE
void ObsoleteFunc() {{
  // To be removed
}}
>>>>>>> DELETE
"""
      modified = apply_patch_or_replacement(del_patch, repo_path=parent)
      self.assertTrue(modified)
      with open(tmp_path, "r", encoding="utf-8") as f:
        content = f.read()
      self.assertNotIn("ObsoleteFunc", content)
      self.assertIn("KeepBefore", content)
      self.assertIn("KeepAfter", content)
    finally:
      if os.path.exists(tmp_path):
        os.remove(tmp_path)

  def test_reject_empty_search_replace_block(self):
    """Tests that empty REPLACE in SEARCH/REPLACE is rejected as a glitch."""
    with tempfile.NamedTemporaryFile("w+", suffix=".cc", delete=False) as tmp:
      tmp.write("void Foo() {}\n")
      tmp_path = tmp.name

    try:
      rel = os.path.basename(tmp_path)
      parent = os.path.dirname(tmp_path)
      glitch_patch = f"""FILE: //{rel}
<<<<<<< SEARCH
void Foo() {{}}
=======
>>>>>>> REPLACE
"""
      modified = apply_patch_or_replacement(glitch_patch, repo_path=parent)
      self.assertEqual(modified, [])
      with open(tmp_path, "r", encoding="utf-8") as f:
        content = f.read()
      self.assertIn("void Foo() {}", content)
    finally:
      if os.path.exists(tmp_path):
        os.remove(tmp_path)

  def test_extract_build_progress(self):
    """Tests extraction of Ninja and Siso build step progress."""
    # Ninja format
    sample_ninja = ("[11464/39292] 3m35.89s F ACTION //foo:bar\n"
                    "[11465/39291] 3m35.90s S ACTION //foo:baz\n")
    res = extract_build_progress(sample_ninja)
    self.assertIn("11465/39291", res)
    self.assertIn("29.2%", res)

    # Siso format
    sample_siso = "build finished: Stats{Done:50222, Fail:1, Total:50832}"
    res_siso = extract_build_progress("", sample_siso)
    self.assertIn("50222/50832", res_siso)
    self.assertIn("98.8%", res_siso)

  def test_autoninja_sends_full_file_context_on_repeated_errors(self):
    """Tests sending full source files when error count >= 3."""
    with tempfile.NamedTemporaryFile("w+", suffix=".cc", delete=False) as tmp:
      # Write 100 lines of dummy C++ code
      tmp.write("\n".join(f"// Line {i}" for i in range(1, 101)) + "\n")
      tmp_path = tmp.name

    try:
      repo_dir = os.path.dirname(tmp_path)
      resolver = AutoninjaResolver(
          repo_path=repo_dir,
          out_dir="out/dummy",
          target="cobalt_apk",
      )
      diag = CompilerDiagnostic(
          file_path=tmp_path,
          line_number=50,
          column=1,
          error_message="sample error",
          raw_snippet="snippet",
          notes=[],
      )

      # 1st attempt: should send 60-line window
      called_contexts = []

      class MockEngine:
        flash_model = "gemini-2.5-flash"
        pro_model = "gemini-2.5-pro"

        def heal_compiler_error(self, **kwargs):
          called_contexts.append(kwargs.get("file_context", ""))
          return {"status": "SUCCESS", "patch": "", "model_used": "flash"}

      resolver.reasoning_engine = MockEngine()
      resolver.file_error_counts[tmp_path] = 1
      resolver.resolve_diagnostic(diag, [], use_pro=False)
      self.assertTrue(len(called_contexts[0].splitlines()) < 80)

      # 3rd error: should send full 100-line file
      resolver.file_error_counts[tmp_path] = 3
      resolver.resolve_diagnostic(diag, [], use_pro=False)
      self.assertEqual(len(called_contexts[1].splitlines()), 100)
    finally:
      if os.path.exists(tmp_path):
        os.remove(tmp_path)

  def test_autoninja_resolves_raw_string_diagnostic(self):
    """Tests that AutoninjaResolver handles unstructured string error traces."""
    resolver = AutoninjaResolver(
        repo_path="/tmp", out_dir="out/test", target="cobalt_apk")
    called_traces = []

    class MockEngine:
      flash_model = "gemini-2.5-flash"
      pro_model = "gemini-2.5-pro"

      def heal_compiler_error(self, **kwargs):
        called_traces.append(kwargs.get("error_trace", ""))
        return {"patch": "PATCH", "model_used": "gemini-2.5-flash"}

    resolver.reasoning_engine = MockEngine()
    raw_error = (
        "FAILED: obj/cobalt/apk/cobalt_apk.jar\njavac: package not found")
    patch, model_used, rel_target = resolver.resolve_diagnostic(
        raw_error, [], use_pro=False)
    self.assertEqual(patch, "PATCH")
    self.assertEqual(model_used, "gemini-2.5-flash")
    self.assertEqual(rel_target, "cobalt_apk")
    self.assertIn("FAILED: obj/cobalt/apk/cobalt_apk.jar", called_traces[0])

  def test_parse_linker_errors_and_map_build_file(self):
    """Tests that linker errors map to the offending component's BUILD.gn."""
    with tempfile.TemporaryDirectory() as tmp_dir:
      ft_dir = os.path.join(tmp_dir, "third_party", "freetype")
      os.makedirs(ft_dir, exist_ok=True)
      gn_file = os.path.join(ft_dir, "BUILD.gn")
      with open(gn_file, "w", encoding="utf-8") as f:
        f.write("component(\"freetype\") {}\n")

      linker_output = (
          "build step: solink \"./libchrobalt.so\"\n"
          "ld.lld: error: obj/third_party/freetype/libfreetype.a(autofit.o) "
          "is incompatible with armelf_linux_eabi\n"
          "clang++: error: linker command failed with exit code 1")

      diags = parse_compiler_errors(linker_output, tmp_dir)
      self.assertEqual(len(diags), 1)
      self.assertEqual(diags[0].file_path, os.path.abspath(gn_file))
      self.assertIn("incompatible with armelf_linux_eabi",
                    diags[0].error_message)
      self.assertIn("ld.lld: error:", diags[0].raw_snippet)

      # Test direct helper
      found_gn = find_build_file_for_object(
          "obj/third_party/freetype/libfreetype.a(autofit.o)", tmp_dir)
      self.assertEqual(found_gn, os.path.abspath(gn_file))

  def test_gn_stray_conflict_marker_auto_removal(self):
    """Tests that GNGenResolver automatically removes stray conflict markers."""
    with tempfile.NamedTemporaryFile("w+", suffix=".gn", delete=False) as tmp:
      tmp.write("is_cobalt = true\n=======\nenable_vulkan = false\n")
      tmp_path = tmp.name

    try:
      repo_dir = os.path.dirname(tmp_path)
      rel = os.path.basename(tmp_path)
      resolver = GNGenResolver(
          repo_path=repo_dir, platform="android", build_type="devel")
      diag = GNDiagnostic(
          error_message="Unexpected token '=='",
          raw_output=f"ERROR at //{rel}:2:1: Unexpected token '=='",
          target_files={tmp_path: 2},
          is_structural_break=True,
      )
      patch, model_used, rel_target = resolver.resolve_diagnostic(
          diag, [], use_pro=False)
      self.assertEqual(model_used, "auto-stray-marker-cleaner")
      self.assertEqual(rel_target, rel)
      modified = apply_patch_or_replacement(patch, repo_path=repo_dir)
      self.assertTrue(modified)
      with open(tmp_path, "r", encoding="utf-8") as f:
        content = f.read()
      self.assertNotIn("=======", content)
      self.assertIn("is_cobalt = true", content)
      self.assertIn("enable_vulkan = false", content)
    finally:
      if os.path.exists(tmp_path):
        os.remove(tmp_path)

  def test_compiler_stray_conflict_marker_auto_removal(self):
    """Tests that AutoninjaResolver removes stray conflict markers."""
    with tempfile.NamedTemporaryFile("w+", suffix=".cc", delete=False) as tmp:
      tmp.write("#include <iostream>\n=======\nvoid Init() {}\n")
      tmp_path = tmp.name

    try:
      repo_dir = os.path.dirname(tmp_path)
      rel = os.path.basename(tmp_path)
      resolver = AutoninjaResolver(
          repo_path=repo_dir, out_dir="out", target="cobalt")
      diag = CompilerDiagnostic(
          file_path=tmp_path,
          line_number=2,
          column=1,
          error_message="expected unqualified-id",
          raw_snippet="=======",
          notes=[],
      )
      patch, model_used, rel_target = resolver.resolve_diagnostic(
          diag, [], use_pro=False)
      self.assertEqual(model_used, "auto-stray-marker-cleaner")
      self.assertEqual(rel_target, rel)
      modified = apply_patch_or_replacement(patch, repo_path=repo_dir)
      self.assertTrue(modified)
      with open(tmp_path, "r", encoding="utf-8") as f:
        content = f.read()
      self.assertNotIn("=======", content)
      self.assertIn("#include <iostream>", content)
      self.assertIn("void Init() {}", content)
    finally:
      if os.path.exists(tmp_path):
        os.remove(tmp_path)

  def test_is_unmodified_third_party_allows_cobalt_stubs(self):
    """Tests that Cobalt stubs under third_party are not blocked by guard."""
    with tempfile.TemporaryDirectory() as tmp_dir:
      cobalt_stub = os.path.join(
          tmp_dir,
          "third_party/blink/renderer/platform/graphics/gpu",
          "cobalt_webgpu_stubs.cc",
      )
      os.makedirs(os.path.dirname(cobalt_stub), exist_ok=True)
      with open(cobalt_stub, "w", encoding="utf-8") as f:
        f.write("// Fake webgpu stub\n")

      pure_third_party = os.path.join(tmp_dir, "third_party/xnnpack/src/xnn.h")
      os.makedirs(os.path.dirname(pure_third_party), exist_ok=True)
      with open(pure_third_party, "w", encoding="utf-8") as f:
        f.write("// Pure upstream xnnpack\n")

      self.assertFalse(is_unmodified_third_party(cobalt_stub, tmp_dir))
      self.assertTrue(is_unmodified_third_party(pure_third_party, tmp_dir))

  def test_record_and_load_memory(self):
    """Tests recording and loading knowledge memory bank entries on engine."""
    engine = CobaltReasoningEngine()
    engine.record_successful_fix(
        issue_description="no member named 'InitStarboardMediaPipeline'",
        solution_diff="InitStarboardMediaPipelineV2();",
        target_file="cobalt/media.cc",
    )
    # Re-recording identical fix should be a no-op / update
    engine.record_successful_fix(
        issue_description="no member named 'InitStarboardMediaPipeline'",
        solution_diff="InitStarboardMediaPipelineV2();",
        target_file="cobalt/media.cc",
    )
    exp = engine.get_past_experience(
        query="no member named 'InitStarboardMediaPipeline'", max_items=5)
    self.assertIn("Target File: cobalt/media.cc", exp)
    self.assertIn("InitStarboardMediaPipelineV2();", exp)

  def test_token_usage_multi_model_tracking(self):
    """Tests that TokenUsage tracks Flash and Pro models separately."""
    tracker = TokenUsage()
    tracker.add(prompt=100, completion=20, total=120, model="gemini-2.5-flash")
    tracker.add(prompt=150, completion=30, total=180, model="gemini-2.5-flash")
    tracker.add(prompt=500, completion=100, total=600, model="gemini-2.5-pro")

    self.assertEqual(tracker.calls, 3)
    self.assertEqual(tracker.prompt_tokens, 750)
    self.assertEqual(tracker.completion_tokens, 150)
    self.assertEqual(tracker.total_tokens, 900)

    self.assertIn("gemini-2.5-flash", tracker.by_model)
    self.assertIn("gemini-2.5-pro", tracker.by_model)
    self.assertEqual(tracker.by_model["gemini-2.5-flash"].calls, 2)
    self.assertEqual(tracker.by_model["gemini-2.5-flash"].total_tokens, 300)
    self.assertEqual(tracker.by_model["gemini-2.5-pro"].calls, 1)
    self.assertEqual(tracker.by_model["gemini-2.5-pro"].total_tokens, 600)

    table = tracker.format_summary_table()
    self.assertIn("`gemini-2.5-flash`", table)
    self.assertIn("`gemini-2.5-pro`", table)
    self.assertIn("**Total**", table)

  def test_get_chromium_milestone(self):
    """Tests reading Chromium major milestone from VERSION file."""
    with tempfile.TemporaryDirectory() as tmp_dir:
      chrome_dir = os.path.join(tmp_dir, "chrome")
      os.makedirs(chrome_dir, exist_ok=True)
      version_file = os.path.join(chrome_dir, "VERSION")
      with open(version_file, "w", encoding="utf-8") as f:
        f.write("MAJOR=138\nMINOR=0\nBUILD=7204\nPATCH=311\n")

      milestone = get_chromium_milestone(tmp_dir)
      self.assertEqual(milestone, "M138")

  def test_extract_gn_target_files_universal(self):
    """Tests that GN file extraction captures all referenced .gn/.gni files."""
    with tempfile.TemporaryDirectory() as tmp_dir:
      buildconfig = os.path.join(tmp_dir, "build/config/BUILDCONFIG.gn")
      rtc_gn = os.path.join(tmp_dir, "third_party/webrtc/rtc_tools/BUILD.gn")
      os.makedirs(os.path.dirname(buildconfig), exist_ok=True)
      os.makedirs(os.path.dirname(rtc_gn), exist_ok=True)
      with open(buildconfig, "w", encoding="utf-8") as f:
        f.write("# buildconfig")
      with open(rtc_gn, "w", encoding="utf-8") as f:
        f.write("# rtc_gn")

      gn_trace = ("ERROR at //build/config/BUILDCONFIG.gn:703:5: "
                  "Source file not found.\n"
                  "See //third_party/webrtc/rtc_tools/BUILD.gn:167:3: for "
                  "'rtp_generator'\n")
      res = extract_gn_target_files(gn_trace, tmp_dir)
      self.assertIn(buildconfig, res)
      self.assertEqual(res[buildconfig], 703)
      self.assertIn(rtc_gn, res)
      self.assertEqual(res[rtc_gn], 167)

  def test_reject_nested_file_in_search_replace(self):
    """Tests that SEARCH/REPLACE blocks with nested FILE: are rejected."""
    with tempfile.NamedTemporaryFile("w+", suffix=".gn", delete=False) as tmp:
      tmp.write("var_a = true\n")
      tmp_path = tmp.name

    try:
      rel = os.path.basename(tmp_path)
      parent = os.path.dirname(tmp_path)
      bad_patch = f"""FILE: //{rel}
<<<<<<< SEARCH
var_a = true
=======
FILE: //other/BUILD.gn
target("foo") {{}}
>>>>>>> REPLACE
"""
      modified = apply_patch_or_replacement(bad_patch, repo_path=parent)
      self.assertEqual(modified, [])
      with open(tmp_path, "r", encoding="utf-8") as f:
        content = f.read()
      self.assertIn("var_a = true", content)
    finally:
      if os.path.exists(tmp_path):
        os.remove(tmp_path)

  def test_autoninja_sends_full_context_for_build_files(self):
    """Tests that AutoninjaResolver sends full context for build files."""
    with tempfile.TemporaryDirectory() as tmp_dir:
      ft_dir = os.path.join(tmp_dir, "third_party", "freetype")
      os.makedirs(ft_dir, exist_ok=True)
      gn_file = os.path.join(ft_dir, "BUILD.gn")
      lines = [f"# Line {i}\n" for i in range(1, 150)]
      lines.append(
          "component(\"freetype\") { visibility = [ \"//public\" ] }\n")
      with open(gn_file, "w", encoding="utf-8") as f:
        f.writelines(lines)

      called_contexts = []

      class MockEngine:
        flash_model = "gemini-2.5-flash"
        pro_model = "gemini-2.5-pro"

        def heal_compiler_error(self, **kwargs):
          called_contexts.append(kwargs.get("file_context", ""))
          return {"status": "SUCCESS", "patch": "PATCH", "model_used": "gemini"}

      resolver = AutoninjaResolver(
          repo_path=tmp_dir,
          out_dir="out",
          target="cobalt",
          engine=MockEngine(),
      )
      diag = CompilerDiagnostic(
          file_path=gn_file,
          line_number=1,
          column=1,
          error_message="incompatible with arm",
          raw_snippet="ld.lld: error",
          notes=[],
      )
      resolver.resolve_diagnostic(diag, [], use_pro=False)
      self.assertEqual(len(called_contexts), 1)
      self.assertIn("component(\"freetype\")", called_contexts[0])
      self.assertIn("150: component(\"freetype\")", called_contexts[0])

  def test_reasoning_engine_client_dispatch(self):
    """Tests that ReasoningEngineClient routes calls to remote mock engine."""
    client = ReasoningEngineClient(
        resource_id="projects/p/locations/l/reasoningEngines/123",
        project_id="test-p",
        location="us-central1",
    )
    mock_remote = mock.MagicMock()
    mock_remote.query.return_value = {
        "status": "SUCCESS",
        "patch": "TEST_PATCH",
        "model_used": "gemini-2.5-flash",
    }
    client._remote_engine = mock_remote  # pylint: disable=protected-access

    res = client.heal_compiler_error(
        target="cobalt",
        diagnostics="error: foo",
    )
    self.assertEqual(res["status"], "SUCCESS")
    self.assertEqual(res["patch"], "TEST_PATCH")
    mock_remote.query.assert_called_once_with(
        action="heal_compiler_error",
        target="cobalt",
        diagnostics="error: foo",
        source_contexts="",
        past_experience="",
        investigation_history="",
        use_pro=False,
    )


if __name__ == "__main__":
  unittest.main()
