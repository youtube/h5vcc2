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
"""Tests for the RDK reflashing script.

This module provides unit tests for reflash_rdk.py using mocks to verify
command line argument parsing, ADNL mode reboot sequence, and adnl_burn_pkg
execution flow without requiring physical hardware.
"""

import importlib.util
from pathlib import Path
import sys
import unittest
from unittest import mock

# Dynamically import reflash_rdk
_SCRIPT_DIR = Path(__file__).parent
_REFLASH_RDK_PATH = _SCRIPT_DIR / "reflash_rdk.py"

spec = importlib.util.spec_from_file_location("reflash_rdk", str(_REFLASH_RDK_PATH))
reflash_rdk = importlib.util.module_from_spec(spec)
if spec.loader:
    spec.loader.exec_module(reflash_rdk)


class TestReflashRdk(unittest.TestCase):
    """Unit tests for the reflash_rdk script."""

    def setUp(self):
        """Sets up mocks for system-level calls and subprocesses."""
        super().setUp()
        self.mock_run = mock.patch.object(reflash_rdk, "run_command").start()
        self.mock_run.return_value = "imagename:lib32-rdk\ncustom version: RDK_V6_AH212_20260420\n"

        self.mock_sub_run = mock.patch("subprocess.run").start()
        def sub_run_side_effect(cmd, *args, **kwargs):
            cmd_str = " ".join(cmd) if isinstance(cmd, list) else cmd
            if "adb devices" in cmd_str:
                return mock.MagicMock(returncode=0, stdout="List of devices attached\nrdk_device_serial\tdevice\n")
            elif "cat /version.txt" in cmd_str:
                return mock.MagicMock(returncode=0, stdout="imagename:lib32-rdk\ncustom version: RDK_V6_AH212_20260420\n")
            return mock.MagicMock(returncode=0, stdout="")
        self.mock_sub_run.side_effect = sub_run_side_effect

        mock.patch("time.sleep").start()
        mock.patch("os.path.exists", return_value=True).start()
        mock.patch("pathlib.Path.exists", return_value=True).start()
        mock.patch("pathlib.Path.is_file", return_value=True).start()
        mock.patch("pathlib.Path.is_dir", return_value=False).start()
        mock.patch("pathlib.Path.mkdir").start()
        mock.patch("os.geteuid", return_value=0).start()
        self.mock_exit = mock.patch("sys.exit").start()

    def tearDown(self):
        """Cleans up all active mocks."""
        mock.patch.stopall()
        super().tearDown()

    def test_full_reflash_flow_adb(self):
        """Verifies full reflashing sequence using ADB device auto-detection."""
        argv = [
            "reflash_rdk.py",
            "--image", "/tmp/aml_upgrade_package.img",
            "--burn-tool", "/tmp/adnl_burn_pkg",
        ]
        with mock.patch("sys.argv", argv):
            reflash_rdk.main()

        # Verify reboot command called via ADB
        reboot_call = any(
            "adb" in str(call) and "reboot adnl" in str(call)
            for call in self.mock_run.call_args_list
        )
        self.assertTrue(reboot_call, "Reboot into ADNL mode was not called via ADB")

        # Verify adnl_burn_pkg invocation with default -r 1
        burn_call = next(
            call for call in self.mock_run.call_args_list if "adnl_burn_pkg" in str(call)
        )
        burn_args = burn_call[0][0]
        self.assertIn("-p", burn_args)
        self.assertIn("-r", burn_args)
        self.assertIn("1", burn_args)

    def test_preserve_data_flag(self):
        """Verifies --preserve-data passes -e 0 to adnl_burn_pkg."""
        argv = [
            "reflash_rdk.py",
            "--image", "/tmp/aml_upgrade_package.img",
            "--burn-tool", "/tmp/adnl_burn_pkg",
            "--preserve-data",
        ]
        with mock.patch("sys.argv", argv):
            reflash_rdk.main()

        burn_call = next(
            call for call in self.mock_run.call_args_list if "adnl_burn_pkg" in str(call)
        )
        burn_args = burn_call[0][0]
        self.assertIn("-e", burn_args)
        self.assertIn("0", burn_args)

    def test_device_id_flag(self):
        """Verifies --device-id targets specific ADB device."""
        argv = [
            "reflash_rdk.py",
            "--image", "/tmp/aml_upgrade_package.img",
            "--burn-tool", "/tmp/adnl_burn_pkg",
            "--device-id", "custom_adb_serial",
        ]
        with mock.patch("sys.argv", argv):
            reflash_rdk.main()

        reboot_call = any(
            "custom_adb_serial" in str(call) and "reboot adnl" in str(call)
            for call in self.mock_run.call_args_list
        )
        self.assertTrue(reboot_call, "Specific --device-id was not passed to ADB reboot command")

    def test_skip_reboot_to_adnl(self):
        """Verifies --skip-reboot-to-adnl skips sending reboot commands."""
        argv = [
            "reflash_rdk.py",
            "--image", "/tmp/aml_upgrade_package.img",
            "--burn-tool", "/tmp/adnl_burn_pkg",
            "--skip-reboot-to-adnl",
        ]
        with mock.patch("sys.argv", argv):
            reflash_rdk.main()

        reboot_called = any(
            "reboot adnl" in str(call) for call in self.mock_run.call_args_list
        )
        self.assertFalse(reboot_called, "Reboot command was unexpectedly called despite --skip-reboot-to-adnl")

    def test_dry_run_flag(self):
        """Verifies --dry-run prints commands without invoking actual subprocesses."""
        argv = [
            "reflash_rdk.py",
            "--image", "/tmp/aml_upgrade_package.img",
            "--burn-tool", "/tmp/adnl_burn_pkg",
            "--dry-run",
        ]
        with mock.patch("sys.argv", argv):
            reflash_rdk.main()

        # Check that run_command was invoked with dry_run=True
        dry_run_calls = [
            call for call in self.mock_run.call_args_list if call[1].get("dry_run") is True
        ]
        self.assertGreater(len(dry_run_calls), 0, "No commands were passed dry_run=True")

    def test_resolve_latest_image_local(self):
        """Verifies resolve_latest_image selects the newest local .img file in directory."""
        img1 = Path("/tmp/aml_upgrade_package_20260420.img")
        img2 = Path("/tmp/aml_upgrade_package_20260715.img")

        mock_dir = mock.MagicMock()
        mock_dir.is_dir.return_value = True
        mock_dir.is_file.return_value = False
        mock_dir.glob.side_effect = lambda pat: [img1, img2] if "img" in pat else []

        resolved = reflash_rdk.resolve_latest_image(mock_dir)
        self.assertEqual(resolved, img2.resolve())


if __name__ == "__main__":
    unittest.main()
