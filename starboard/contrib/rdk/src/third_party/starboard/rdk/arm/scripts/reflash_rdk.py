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
"""Automated tool for reflashing RDK devices using Amlogic burn tools.

NOTE: This script must be run directly on the local machine (e.g. laptop or workstation)
physically connected to the RDK device via USB. Flashing requires local USB access
for adnl_burn_pkg to communicate with the Amlogic bootloader in ADNL/download mode.

Usage Examples:
  1. Reflash device using ADB auto-detection:
     python3 starboard/contrib/rdk/src/third_party/starboard/rdk/arm/scripts/reflash_rdk.py \
       --image ~/Downloads/aml_upgrade_package_20260420.img \
       --burn-tool ~/Downloads/adnl_burn_pkg_4_macos/adnl_burn_pkg

  2. Reflash device while preserving /data partition:
     python3 starboard/contrib/rdk/src/third_party/starboard/rdk/arm/scripts/reflash_rdk.py \
       --image ~/Downloads/aml_upgrade_package_20260420.img \
       --burn-tool /usr/bin/adnl_burn_pkg \
       --preserve-data
"""

import argparse
import os
from pathlib import Path
import re
import subprocess
import sys
import time
from typing import List, Optional, Union


def run_command(
    command: Union[str, List[str]],
    verbose: bool = True,
    check: bool = True,
    sleep_time: int = 0,
    dry_run: bool = False,
) -> str:
    """Runs a shell command and prints/returns stdout."""
    cmd_str = command if isinstance(command, str) else " ".join(command)
    if dry_run:
        print(f"[DRY-RUN] Would execute: {cmd_str}")
        return ""

    if verbose:
        print(f">>> Executing: {cmd_str}")

    res = subprocess.run(
        command, shell=isinstance(command, str), stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True
    )
    if verbose and res.stdout:
        print(res.stdout, end="", flush=True)

    if check and res.returncode != 0:
        sys.exit(res.returncode)

    if sleep_time > 0:
        if verbose:
            print(f"Waiting {sleep_time} seconds...")
        time.sleep(sleep_time)

    return res.stdout or ""


def run_adb_command(
    command: Union[str, List[str]],
    device_id: Optional[str] = None,
    **kwargs,
) -> str:
    """Runs command on locally connected RDK device via ADB."""
    adb_cmd = ["adb"] + (["-s", device_id] if device_id else []) + ["shell"]
    if isinstance(command, str):
        adb_cmd.append(command)
    else:
        adb_cmd.extend(command)
    return run_command(adb_cmd, **kwargs)


def get_device_id() -> str:
    """Detects connected target ADB device serial ID."""
    try:
        res = subprocess.run(["adb", "devices"], capture_output=True, text=True, check=True)
        devices = [
            line.split()[0]
            for line in res.stdout.strip().splitlines()[1:]
            if line.strip() and len(line.split()) >= 2 and line.split()[1] == "device"
        ]
    except (subprocess.CalledProcessError, FileNotFoundError) as e:
        print(f"Error running 'adb devices': {e}")
        sys.exit(1)

    if not devices:
        print("Error: No online ADB devices connected.")
        sys.exit(1)

    if len(devices) > 1:
        print(f"Note: Multiple ADB devices detected: {devices}. Picking first: {devices[0]}")

    dev = devices[0]
    print(f"Using ADB device: {dev}")
    return dev


def reboot_to_adnl(device_id: Optional[str] = None, dry_run: bool = False) -> None:
    """Triggers software reboot on device via ADB to enter ADNL download mode."""
    print("=== Sending software reboot command to enter ADNL mode ===")
    run_adb_command("reboot adnl || reboot update", device_id=device_id, check=False, dry_run=dry_run)
    if not dry_run:
        print("Device is rebooting into download mode...")
        time.sleep(5)


def flash_image(
    burn_tool: Path,
    image_path: Path,
    preserve_data: bool = False,
    auto_reboot: bool = True,
    use_sudo: bool = False,
    dry_run: bool = False,
) -> None:
    """Executes adnl_burn_pkg to flash system image onto RDK device."""
    print("=== Flashing system image onto RDK device ===")
    print("Note: If the burn tool gets stuck or is waiting for device connection:")
    print("  1. In a separate terminal, run: adb shell reboot update")
    print("  2. Or power cycle the device (unplug & replug USB/power cable).")

    cmd = ([ "sudo" ] if use_sudo and os.name != "nt" and os.geteuid() != 0 else []) + [
        str(burn_tool),
        "-p", str(image_path),
        "-r", "1" if auto_reboot else "0",
    ] + (["-e", "0"] if preserve_data else [])

    run_command(cmd, dry_run=dry_run)


def verify_flash(device_id: Optional[str] = None, dry_run: bool = False) -> None:
    """Verifies system software version after device reboot."""
    print("=== Waiting for device to reboot and verify software version ===")
    if dry_run:
        run_adb_command("cat /version.txt", device_id=device_id, check=False, verbose=False, dry_run=True)
        return

    for i in range(12):
        print(f"Checking connection to device... ({i+1}/12)")
        out = run_adb_command("cat /version.txt", device_id=device_id, check=False, verbose=False).strip()
        if "imagename:" in out or "version" in out.lower():
            print("\n=== System Software Version (/version.txt) ===")
            print(out)
            return
        time.sleep(5)

    print("Warning: Flashing completed, but could not read /version.txt after reboot.")


def resolve_burn_tool(burn_tool_arg: Optional[Path]) -> Path:
    """Resolves adnl_burn_pkg binary from argument or ~/Downloads."""
    if burn_tool_arg:
        path = burn_tool_arg.resolve()
        if not path.is_file():
            raise FileNotFoundError(f"Burn tool not found or is not a file: {path}")
        try:
            path.chmod(path.stat().st_mode | 0o111)
        except (OSError, AttributeError):
            pass
        return path

    target_dir = Path.home() / "Downloads"
    if target_dir.exists():
        for candidate in list(target_dir.glob("adnl_burn_pkg")) + list(target_dir.glob("*/adnl_burn_pkg")):
            if candidate.is_file():
                try:
                    candidate.chmod(candidate.stat().st_mode | 0o111)
                except (OSError, AttributeError):
                    pass
                return candidate.resolve()

    raise FileNotFoundError(
        f"adnl_burn_pkg tool not found in {target_dir}. Please specify via --burn-tool."
    )


def resolve_latest_image(image_arg: Optional[Path]) -> Path:
    """Resolves target image path from argument or local ~/Downloads directory."""
    if image_arg:
        if not image_arg.exists():
            raise FileNotFoundError(f"Specified image path does not exist: {image_arg}")
        if image_arg.is_file():
            return image_arg.resolve()
        search_dir = image_arg
    else:
        search_dir = Path.home() / "Downloads"

    if not search_dir.exists():
        raise FileNotFoundError(f"Directory does not exist: {search_dir}")

    candidates = list(search_dir.glob("*.img"))
    if not candidates:
        raise FileNotFoundError(f"No .img system files found in {search_dir}. Please specify via --image.")

    def sort_key(p: Path):
        m = re.search(r"(\d{8})", p.name)
        return (1, m.group(1)) if m else (0, p.stat().st_mtime)

    latest_img = sorted(candidates, key=sort_key, reverse=True)[0]
    print(f"Using local image: {latest_img}")
    return latest_img.resolve()


def parse_args() -> argparse.Namespace:
    """Parses command line arguments."""
    parser = argparse.ArgumentParser(
        description="Automated RDK Device Reflashing Tool (runs on local connected machine)."
    )
    parser.add_argument(
        "--image",
        type=Path,
        help="Path to aml_upgrade_package.img file or directory (defaults to searching ~/Downloads).",
    )
    parser.add_argument(
        "--burn-tool",
        type=Path,
        help="Path to adnl_burn_pkg executable binary (defaults to searching ~/Downloads).",
    )
    parser.add_argument(
        "--device-id",
        type=str,
        help="Target RDK device serial ID (uses ADB).",
    )
    parser.add_argument(
        "--preserve-data",
        action="store_true",
        help="Preserve /data partition contents during flashing (passes -e 0).",
    )
    parser.add_argument(
        "--no-reboot",
        action="store_true",
        help="Do not automatically reboot device after flashing succeeds.",
    )
    parser.add_argument(
        "--skip-reboot-to-adnl",
        action="store_true",
        help="Skip reboot step (use if device is already in ADNL download mode).",
    )
    parser.add_argument(
        "--use-sudo",
        action="store_true",
        help="Prepend sudo to adnl_burn_pkg command execution.",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Perform a dry run: print exact commands without executing destructive steps.",
    )
    return parser.parse_args()


def main() -> None:
    """Main execution entry point."""
    args = parse_args()

    try:
        image_path = resolve_latest_image(args.image)
        burn_tool = resolve_burn_tool(args.burn_tool)
    except FileNotFoundError as e:
        print(f"Error: {e}")
        sys.exit(1)

    device_id = args.device_id
    if not device_id:
        try:
            device_id = get_device_id()
        except SystemExit:
            device_id = None

    if not args.skip_reboot_to_adnl:
        if device_id:
            reboot_to_adnl(device_id, dry_run=args.dry_run)
        else:
            print("Note: No active ADB device connected. Skipping software reboot into ADNL mode.")

    flash_image(
        burn_tool=burn_tool,
        image_path=image_path,
        preserve_data=args.preserve_data,
        auto_reboot=not args.no_reboot,
        use_sudo=args.use_sudo,
        dry_run=args.dry_run,
    )

    if not args.no_reboot:
        verify_flash(device_id, dry_run=args.dry_run)


if __name__ == "__main__":
    main()
