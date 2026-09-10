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
"""Build and deploy Cobalt to RDK devices.

This script handles the full lifecycle of building, packaging, deploying,
and launching Cobalt (as a plugin or executable) or Cobalt tests on RDK.

Usage Examples:
  1. Build and deploy as Cobalt plugin (default: config: qa):
     python3 starboard/contrib/rdk/src/third_party/starboard/rdk/arm/scripts/deploy_rdk.py

  2. Build, deploy, and RUN Cobalt plugin or tests on device:
     python3 starboard/contrib/rdk/src/third_party/starboard/rdk/arm/scripts/deploy_rdk.py --run
     python3 starboard/contrib/rdk/src/third_party/starboard/rdk/arm/scripts/deploy_rdk.py --tests nplb --run

  (For more complex workflows—such as deep-linking, profiling, DevTools, or log streaming—run the script with --help to see all available parameters.)
"""

import argparse
from datetime import datetime
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
from typing import List, Optional, Union

# Disable AI agent environment variables to avoid siso wrapper flag issues
os.environ.pop("ANTIGRAVITY_AGENT", None)
os.environ.pop("GEMINI_CLI", None)

# Constants
PLATFORM = "evergreen-arm-hardfp-rdk"
DEFAULT_REMOTE_DIR = "/data/out_cobalt"
TEST_REMOTE_DIR = "/data/test"
MIN_SYSTEM_SOFTWARE_VERSION = "20260420"


def run_command(
    command: Union[str, List[str]],
    verbose: bool = True,
    check: bool = True,
    sleep_time: int = 0,
) -> str:
    """Utility to run shell commands."""
    if verbose:
        cmd_str = command if isinstance(command, str) else " ".join(command)
        print(f">>> Executing: {cmd_str}")

    is_shell = isinstance(command, str)
    process = subprocess.Popen(
        command, shell=is_shell, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True
    )
    stdout_lines = []
    for line in process.stdout:
        print(line, end="", flush=True)
        stdout_lines.append(line)
    process.wait()
    stdout = "".join(stdout_lines)

    if check and process.returncode != 0:
        sys.exit(process.returncode)

    if sleep_time > 0:
        if verbose:
            print(f"Waiting {sleep_time} seconds...")
        time.sleep(sleep_time)

    return stdout


def run_remote_command(
    command: Union[str, List[str]],
    device_id: Optional[str] = None,
    device_ip: Optional[str] = None,
    verbose: bool = True,
    check: bool = True,
    sleep_time: int = 0,
) -> str:
    """Runs command on remote device via ADB or SSH."""
    if device_id:
        adb_cmd = ["adb", "-s", device_id, "shell"]
        if isinstance(command, str):
            adb_cmd.append(command)
        else:
            adb_cmd.extend(command)
        return run_command(adb_cmd, verbose, check, sleep_time)
    elif device_ip:
        ssh_cmd = ["ssh", "-q", f"root@{device_ip}"]
        if isinstance(command, str):
            ssh_cmd.append(command)
        else:
            ssh_cmd.append(" ".join(command))
        return run_command(ssh_cmd, verbose, check, sleep_time)
    else:
        raise ValueError("Either device_id or device_ip must be provided for remote command.")


def push_to_device(
    local_path: Union[str, Path],
    remote_path: str,
    device_id: Optional[str] = None,
    device_ip: Optional[str] = None,
    verbose: bool = True,
) -> None:
    """Pushes local file/directory to remote device via ADB or SCP."""
    if device_id:
        adb_cmd = ["adb", "-s", device_id, "push", str(local_path), remote_path]
        run_command(adb_cmd, verbose=verbose)
    elif device_ip:
        scp_cmd = [
            "scp",
            "-q",
            "-r",
            str(local_path),
            f"root@{device_ip}:{remote_path}",
        ]
        run_command(scp_cmd, verbose=verbose)
    else:
        raise ValueError("Either device_id or device_ip must be provided to push file.")


def configure_build(platform: str, config: str, out_dir: Path, no_rbe: bool = False) -> None:
    """Runs GN configuration."""
    print(f"=== Configuring {platform} ({config}) ===")
    cmd = [
        "python3", "cobalt/build/gn.py", "-p", platform, "-C", config,
        str(out_dir)
    ]
    if no_rbe:
        cmd.append("--no-rbe")
    run_command(cmd)



def build_targets(out_dir: Path, targets: List[str]) -> str:
    """Builds the specified targets using autoninja."""
    print(f"=== Building {' '.join(targets)} ===")
    return run_command(["autoninja", "-C", str(out_dir)] + targets)


def deploy_only_lib(device_id: Optional[str], device_ip: Optional[str], out_dir: Path, remote_dir: str) -> None:
    """Pushes libcobalt.lz4 directly to the device."""
    local_lz4 = out_dir / "app/cobalt/lib/libcobalt.lz4"
    if not local_lz4.exists():
        print(f"Error: {local_lz4} not found.")
        sys.exit(1)

    remote_lib_dir = f"{remote_dir}/app/cobalt/lib"
    run_remote_command(f"mkdir -p {remote_lib_dir}", device_id, device_ip)
    push_to_device(local_lz4, f"{remote_lib_dir}/libcobalt.lz4", device_id, device_ip)


def package_and_deploy(
    device_id: Optional[str],
    device_ip: Optional[str],
    out_dir: Path,
    remote_dir: str,
    deps_file: Optional[Path], is_test: bool) -> None:
    """Packages artifacts using runtime_deps and pushes to device."""
    print("=== Packaging & Deploying artifacts ===")
    archive_name = "archive.tar.gz"

    if deps_file and deps_file.exists():
        tar_cmd = ["tar", "-czvf", archive_name, "-C", str(out_dir), "-T", str(deps_file)]
        if not is_test:
            tar_cmd.append("libloader_app.so")
        build_info = out_dir / "gen/build_info.json"
        if build_info.exists():
            tar_cmd.append("gen/build_info.json")
    else:
        if deps_file:
            print(f"Warning: deps_file {deps_file} not found.")
        print(f"Packaging everything in {out_dir}")
        tar_cmd = ["tar", "-czvf", archive_name, "-C", str(out_dir), "."]

    print(f"Packaging with: {' '.join(tar_cmd)}")
    run_command(tar_cmd)
    if not remote_dir or remote_dir == "/":
        print(f"Error: remote_dir '{remote_dir}' is invalid or dangerous for cleanup.")
        sys.exit(1)
    print("=== Cleaning previous remote deployment ===")
    # Remove old app directories and binaries, ignoring errors if they don't exist
    run_remote_command(
        f"rm -rf {remote_dir}/app {remote_dir}/*.so {remote_dir}/loader_app {remote_dir}/*.lz4 {remote_dir}/archive.tar.gz || true",
        device_id, device_ip, check=False)

    run_remote_command(f"mkdir -p {remote_dir}", device_id, device_ip)
    push_to_device(archive_name, f"{remote_dir}/", device_id, device_ip)
    Path(archive_name).unlink(missing_ok=True)

    print("=== Extracting archive on device ===")
    extract_cmds = [
        f"cd {remote_dir}",
        "tar -xzf archive.tar.gz",
        "rm archive.tar.gz",
        f"chmod -R 777 {remote_dir}",
    ]
    run_remote_command(" && ".join(extract_cmds), device_id, device_ip)


def ensure_dolby_vision_policy(device_id: Optional[str], device_ip: Optional[str]) -> None:
    """Ensures /sys/module/aml_media/parameters/dolby_vision_policy is set to 1."""
    # TODO(b/532753892): Remove this workaround once the device driver is updated.
    policy_file = "/sys/module/aml_media/parameters/dolby_vision_policy"
    run_remote_command(f"echo 1 > {policy_file}", device_id, device_ip)


def _extract_flag_key(arg: str) -> str:
    """Extracts option key from flag string (e.g. '--foo' from '--foo=val' or '--foo')."""
    return arg.split("=", 1)[0]


def _filter_args_by_keys(base_args: List[str], override_args: List[str]) -> List[str]:
    """Filters flags from base_args whose option keys match any flag in override_args."""
    override_keys = {
        _extract_flag_key(arg)
        for arg in override_args
        if arg.startswith("--")
    }
    return [
        arg
        for arg in base_args
        if not (arg.startswith("--") and _extract_flag_key(arg) in override_keys)
    ]


def _merge_args(base_args: List[str], override_args: List[str]) -> List[str]:
    """Replaces flags in base_args with matching option keys from override_args, and appends override_args."""
    return _filter_args_by_keys(base_args, override_args) + override_args


def remove_duplicate_sb_args(
    cobalt_json_args: List[str],
    script_args: Optional[List[str]] = None,
    user_override_args: Optional[List[str]] = None,
) -> List[str]:
    """Combines cobalt_json_args, script_args, and user_override_args with key deduplication.

    Precedence order (highest to lowest):
      1. user_override_args (passed via --param)
      2. script_args (script-added flags e.g. --remote-debugging-port=9222)
      3. cobalt_json_args (pre-existing flags from WPEFramework cobalt.json / sbmainargs)
    """
    script_args = script_args or []
    user_override_args = user_override_args or []

    # 1. Merge script_args with user_override_args
    override_args = _merge_args(script_args, user_override_args)

    # 2. Merge cobalt_json_args with override_args
    return _merge_args(cobalt_json_args, override_args)


def launch_on_device(
    device_id: Optional[str],
    device_ip: Optional[str],
    remote_dir: str,
    test_name: Optional[str],
    devtools: bool = False,
    param: Optional[List[str]] = None,
    deeplink: Optional[str] = None,
) -> None:
    """Executes remote commands to launch Cobalt or tests."""
    print("=== Launching on device ===")
    remote_cmds = [f"cd {remote_dir}"]

    if test_name:
        remote_cmds += ["rdkDisplay remove || true", "sleep 2", "mkdir -p results"]
        gtest_filter = ""
        filter_file = (Path("cobalt/testing/filters") / PLATFORM /
                       f"{test_name}_loader_filter.json")
        if filter_file.exists():
            with open(filter_file) as f:
                fails = json.load(f).get("failing_tests", [])
                if fails:
                    gtest_filter = f" --gtest_filter=-{':'.join(fails)}"

        xml_out = f"--gtest_output=xml:{remote_dir}/results/{test_name}_loader.xml"
        remote_cmds += [
            "rdkDisplay create",
            "sleep 2",
            f"XDG_RUNTIME_DIR=/run WAYLAND_DISPLAY=test-0 ./{test_name}_loader.py {xml_out}{gtest_filter}",
            "rdkDisplay remove",
            "sleep 2",
        ]
    else:
        callsign = "YouTube"  # Default fallback
        try:
            plugins_json = run_remote_command("curl -s http://127.0.0.1:9998/Service/Controller/Plugins", device_id, device_ip)
            plugins_data = json.loads(plugins_json)
            plugin_list = []
            if isinstance(plugins_data, dict):
                plugin_list = plugins_data.get("plugins", [])
            elif isinstance(plugins_data, list):
                plugin_list = plugins_data

            if isinstance(plugin_list, list):
                for cand in ["YouTube", "Cobalt", "Cobalt_custom"]:
                    if any(isinstance(p, dict) and p.get("callsign") == cand for p in plugin_list):
                        callsign = cand
                        break
        except Exception as e:
            print(f"[WARNING] Failed to fetch plugins list: {e}")

        if devtools:
            print("[INFO] Enabling DevTools support...")

        # Deactivate first to change config
        deactivate_json = json.dumps({
            "jsonrpc": "2.0",
            "id": 1,
            "method": "Controller.1.deactivate",
            "params": {"callsign": callsign}
        })
        run_remote_command(f"curl -s http://127.0.0.1:9998/jsonrpc -d '{deactivate_json}'", device_id, device_ip)

        # Get configuration
        get_config_json = json.dumps({
            "jsonrpc": "2.0",
            "id": 1,
            "method": f"Controller.1.configuration@{callsign}"
        })
        res_str = run_remote_command(f"curl -s http://127.0.0.1:9998/jsonrpc -d '{get_config_json}'", device_id, device_ip)

        rpc_deactivate = json.dumps({
            "jsonrpc": "2.0",
            "id": 1,
            "method": "Controller.1.deactivate",
            "params": {"callsign": callsign}
        }).replace('"', r'\"')

        try:
            res = json.loads(res_str.strip())
            if "result" in res:
                config = res["result"]
                cobalt_json_args = config.get("sbmainargs", [])
                
                script_args = []
                if devtools:
                    script_args.append("--remote-debugging-port=9222")

                user_override_args = param if param else []

                config["sbmainargs"] = remove_duplicate_sb_args(
                    cobalt_json_args, script_args, user_override_args
                )

                # Set configuration
                rpc_set_config = json.dumps({
                    "jsonrpc": "2.0",
                    "id": 1,
                    "method": f"Controller.1.configuration@{callsign}",
                    "params": config
                })
                run_remote_command(f"curl -s http://127.0.0.1:9998/jsonrpc -d '{rpc_set_config}'", device_id, device_ip)
                print("[INFO] DevTools configuration updated successfully.")
        except Exception as e:
            print(f"[WARNING] Failed to update DevTools configuration: {e}")

        rpc_activate = json.dumps({
            "jsonrpc": "2.0",
            "id": 1,
            "method": "Controller.1.activate",
            "params": {"callsign": callsign}
        }).replace('"', r'\"')
        
        remote_cmds += [
            "rdkDisplay remove || true",
            "sleep 2",
            f"curl http://127.0.0.1:9998/jsonrpc -d '{rpc_deactivate}'",
            "sleep 2",
            f"curl -s http://127.0.0.1:9998/jsonrpc -d '{rpc_activate}'",
        ]

        if deeplink:
            rpc_deeplink_json = json.dumps({
                "jsonrpc": "2.0",
                "id": 3,
                "method": f"{callsign}.deeplink",
                "params": deeplink
            }).replace('"', r'\"')
            remote_cmds.append(f"curl -X POST http://127.0.0.1:9998/jsonrpc -d '{rpc_deeplink_json}'")

        if devtools:
            print("[INFO] Setting up DevTools port forwarding...")
            if device_id:
                run_command(["adb", "-s", device_id, "forward", "tcp:9222", "tcp:9222"])
            elif device_ip:
                ssh_tunnel_cmd = [
                    "ssh",
                    "-q",
                    "-fN",
                    "-L",
                    "9222:localhost:9222",
                    f"root@{device_ip}",
                ]
                try:
                    subprocess.Popen(ssh_tunnel_cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                except Exception as e:
                    print(f"[WARNING] Failed to start SSH tunnel: {e}")
            print("[INFO] DevTools is enabled. Please open Chrome and navigate to 'chrome://inspect' (add 'localhost:9222' or the device IP to discover targets).")


    full_cmd = " && ".join(remote_cmds)
    output = run_remote_command(f"bash -l -c \"{full_cmd}\"", device_id, device_ip)
    print(output)
    if "ERROR_OPENING_FAILED" in output or "error" in output.lower():
        print("\n[WARNING] Activation failed with error (e.g., ERROR_OPENING_FAILED).")
        print("[WARNING] Please check the physical device state. It might be in setup/Out-of-Box Experience (OOBE) mode or not connected to a network.")


def parse_args() -> argparse.Namespace:
    """Parses command line arguments."""
    parser = argparse.ArgumentParser(
        description="Build and deploy Cobalt to RDK.")
    parser.add_argument(
        "--only-lib", action="store_true", help="Deploy only libcobalt.lz4.")
    parser.add_argument(
        "--tests",
        type=str,
        metavar="TEST_NAME",
        help="Build and run a test (e.g., nplb).",
    )
    parser.add_argument(
        "--device-ip",
        type=str,
        help="Target RDK device IP address (uses SSH/SCP instead of ADB).",
    )
    parser.add_argument(
        "--config", type=str, help="Override default build configuration.")
    parser.add_argument(
        "--out-dir", type=str, help="Custom build output directory.")
    parser.add_argument(
        "--skip-build",
        action="store_true",
        help="Skip configure and build steps.",
    )
    parser.add_argument(
        "--skip-deploy",
        action="store_true",
        help="Skip deployment step (assumes files are already on the device).",
    )
    parser.add_argument(
        "--run", action="store_true", help="Run on device after build/deploy.")
    parser.add_argument(
        "--force-deploy",
        action="store_true",
        help="Force deployment even if up-to-date.",
    )

    parser.add_argument(
        "--deeplink",
        type=str,
        dest="deeplink",
        help="Deeplink parameter (e.g. v=dQw4w9WgXcQ) to pass to Cobalt.",
    )
    parser.add_argument(
        "--reset",
        action="store_true",
        help=(
            "Run rdkDisplay remove and restart WPEFramework on the device. "
            "Useful if the display is inactive or WPEFramework is stuck."),
    )
    parser.add_argument(
        "--setup-toolchain",
        action="store_true",
        help="Download and install the RDK toolchain to RDK_HOME.",
    )
    parser.add_argument(
        "--revert-c25",
        action="store_true",
        help="Revert the active Cobalt configuration on the device back to Cobalt 25.",
    )
    parser.add_argument(
        "--no-rbe",
        action="store_true",
        help="Disable Remote Build Execution (RBE) in GN configuration.",
    )
    parser.add_argument(
        "--logs",
        action="store_true",
        help="View system/Cobalt logs from the device.",
    )
    parser.add_argument(
        "--follow",
        action="store_true",
        help="Follow log output in real-time (runs journalctl -f).",
    )
    parser.add_argument(
        "--system-logs",
        action="store_true",
        help="View global OS/kernel/systemd logs from the device (runs raw journalctl).",
    )
    parser.add_argument(
        "--param",
        nargs=argparse.REMAINDER,
        default=[],
        help=(
            "Additional runtime parameter(s) to pass to StarboardMain (must be specified last). "
            "All arguments must start with '--' (positional arguments are not supported)."
        ),
    )
    args = parser.parse_args()

    if args.param:
        for arg in args.param:
            if not arg.startswith("--"):
                print(
                    f"Error: All arguments passed to --param must start with '--' (positional arguments are not supported). "
                    f"Positional argument '{arg}' is not supported."
                )
                sys.exit(1)

    return args


def get_model_name(device_id: str) -> Optional[str]:
    """Attempts to retrieve the model name from /etc/device.properties."""
    try:
        res = subprocess.run(
            ["adb", "-s", device_id, "shell", "cat /etc/device.properties"],
            capture_output=True, text=True, timeout=5
        )
        if res.returncode == 0:
            for line in res.stdout.splitlines():
                if "MODEL_NAME=" in line:
                    return line.split("=", 1)[1].strip().strip('"')
    except Exception:
        pass
    return None


def is_rdk_device(device_id: str) -> bool:
    """Checks if the device is an RDK device by checking if model is AH212."""
    model = get_model_name(device_id)
    return model is not None and model.lower() == "ah212"


def get_device_id() -> str:
    """Gets the target RDK device ID."""
    try:
        result = subprocess.run(
            ["adb", "devices"],
            capture_output=True,
            text=True,
            check=True
        )
    except (subprocess.CalledProcessError, FileNotFoundError) as e:
        print(f"Error running 'adb devices': {e}")
        sys.exit(1)

    # Parse 'adb devices' output to collect online device serial IDs.
    devices = []
    for line in result.stdout.strip().split("\n")[1:]:
        if not line.strip():
            continue
        parts = line.split()
        if len(parts) >= 2 and parts[1] == "device":
            devices.append(parts[0])

    if not devices:
        print("Error: No ADB devices connected.")
        sys.exit(1)

    # Filter for RDK devices
    rdk_devices = []
    for dev in devices:
        if is_rdk_device(dev):
            rdk_devices.append(dev)

    if not rdk_devices:
        print(f"Error: None of the connected devices {devices} were identified as RDK (AH212).")
        sys.exit(1)

    # Explicit assumption: if multiple devices are connected, the first one is picked.
    if len(rdk_devices) > 1:
        print(f"Note: Multiple RDK devices detected: {rdk_devices}. Picking the first one: {rdk_devices[0]}")

    dev = rdk_devices[0]
    print(f"Using RDK device: {dev} (AH212)")
    return dev


def assert_software_version(device_id: Optional[str], device_ip: Optional[str], min_version_date: str) -> None:
    """Asserts that the device software date is at least the min_version_date."""
    try:
        output = run_remote_command("cat /version.txt", device_id, device_ip, check=False)
        if "imagename:" not in output:
            print("Error: Could not read /version.txt from the device to verify system software version.")
            print(f"Details: {output}")
            sys.exit(1)

        custom_version = None
        for line in output.splitlines():
            if "custom version:" in line.lower():
                custom_version = line.split(":", 1)[1].strip()
                break

        if not custom_version:
            print("Error: 'custom version' field not found in /version.txt.")
            sys.exit(1)

        # Extract date suffix from custom version (e.g. RDK_V6_AH212_20260330 -> 20260330)
        parts = custom_version.split("_")
        if not parts:
            print(f"Error: Could not parse date from custom version: {custom_version}")
            sys.exit(1)

        date_str = parts[-1]
        try:
            min_date = datetime.strptime(min_version_date, "%Y%m%d").date()
        except ValueError:
            print(f"Error: Invalid min_version_date format: {min_version_date}. Expected YYYYMMDD.")
            sys.exit(1)

        try:
            device_date = datetime.strptime(date_str, "%Y%m%d").date()
        except ValueError:
            print(f"Error: Extracted version suffix '{date_str}' is not a valid YYYYMMDD calendar date.")
            sys.exit(1)

        if device_date < min_date:
            print(f"Error: Device system software version ({date_str}) is older than the required minimum version ({min_version_date}).")
            sys.exit(1)

        print(f"Device system software version verified: {date_str} (required: >= {min_version_date})")

    except Exception as e:
        print(f"Error checking software version: {e}")
        sys.exit(1)


def check_and_switch_cobalt_version(device_id: Optional[str], device_ip: Optional[str]) -> None:
    """Checks if Cobalt 26 is active on the device, otherwise switches and reboots."""
    try:
        current_target = run_remote_command(
            "readlink /usr/lib/libloader_app.so", device_id, device_ip, check=False
        ).strip()

        if current_target == "/data/out_cobalt/libloader_app.so":
            print("Cobalt 26 configuration is already active on the device.")
            return

        print("Cobalt configuration is not active. Running 'chCobalt custom_cobalt' on the device...")
        run_remote_command("chCobalt custom_cobalt", device_id, device_ip)

        print("Device is being rebooted to apply changes...")
        run_remote_command("bash -l -c 'reboot'", device_id, device_ip, check=False)

        print("Waiting 30 seconds for the device to reboot...")
        time.sleep(30)

        print("Attempting to reconnect to device...")
        reconnected = False
        for i in range(10): # try up to 10 times with 3 second intervals
            out = run_remote_command("echo ok", device_id, device_ip, check=False, verbose=False).strip()
            if out == "ok":
                reconnected = True
                break
            print(f"Device not ready yet. Retrying in 3 seconds... ({i+1}/10)")
            time.sleep(3)

        if not reconnected:
            print("Error: Could not reconnect to the device after reboot.")
            sys.exit(1)

        print("Reconnected to device successfully.")

    except Exception as e:
        print(f"Error during Cobalt version switch: {e}")
        sys.exit(1)


def revert_to_cobalt_25(device_id: Optional[str], device_ip: Optional[str]) -> None:
    """Reverts the active Cobalt configuration on the device back to Cobalt 25."""
    target = run_remote_command(
        "readlink /usr/lib/libloader_app.so", device_id, device_ip, check=False
    ).strip()

    if target != "/data/out_cobalt/libloader_app.so":
        print("Cobalt 25 is already active on the device.")
        return

    print("Running 'chCobalt c25' on the device...")
    run_remote_command("chCobalt c25", device_id, device_ip)
    run_remote_command("bash -l -c 'reboot -f'", device_id, device_ip, check=False)
    print("Revert to Cobalt 25 completed. The device is rebooting.")


def is_toolchain_installed(rdk_home: Optional[str]) -> bool:
    """Checks if the RDK toolchain is installed in the target path."""
    return bool(rdk_home and os.path.exists(os.path.join(rdk_home, "sysroots")))


def setup_toolchain() -> None:
    """Downloads and installs the RDK toolchain."""
    rdk_home = os.environ.get("RDK_HOME")
    if not rdk_home:
        print("Error: RDK_HOME environment variable is not set.")
        print("Please add it to your ~/.bashrc (e.g., export RDK_HOME=/workspaces/rdk/toolchain) and restart your shell.")
        sys.exit(1)

    if is_toolchain_installed(rdk_home):
        print(f"RDK toolchain is already installed in: {rdk_home}. Skipping setup.")
        return

    url = "https://storage.googleapis.com/cobalt-static-storage-public/20250521_rdk-glibc-x86_64-arm-toolchain-2.0.sh"
    print(f"=== Setting up toolchain in: {rdk_home} ===")
    with tempfile.TemporaryDirectory() as tmp_dir:
        installer_path = os.path.join(tmp_dir, "installer.sh")
        run_command(f"wget {url} -O '{installer_path}'")
        run_command(f"sh '{installer_path}' -d '{rdk_home}' -y")


def main() -> None:
    """Main execution flow."""
    args = parse_args()

    if args.deeplink and args.tests:
        print("Error: --deeplink is only supported when running the Cobalt plugin (not tests).")
        sys.exit(1)

    if args.setup_toolchain:
        setup_toolchain()
        return

    device_ip = args.device_ip
    device_id = None
    if not device_ip:
        device_id = get_device_id()

    if args.revert_c25:
        revert_to_cobalt_25(device_id, device_ip)
        return

    if args.logs or args.system_logs:
        if not device_ip:
            cmd = ["adb", "-s", device_id, "shell", "journalctl"]
        else:
            cmd = ["ssh", "-q", f"root@{device_ip}", "journalctl"]
        if args.logs:
            cmd.extend([
                "-t",
                "YouTube",
                "-t",
                "Cobalt",
                "-t",
                "loader_app",
                "-t",
                "WPEFramework",
            ])
        if args.follow:
            cmd.append("-f")
        try:
            run_command(cmd)
        except KeyboardInterrupt:
            print("\nLog streaming stopped.")
        return

    if args.reset:
        print("=== Resetting display ===")
        run_remote_command("bash -l -c 'rdkDisplay remove || true'", device_id, device_ip)
        print("=== Restarting WPEFramework ===")
        run_remote_command("systemctl restart wpeframework", device_id, device_ip, sleep_time=5)
        print("=== Cleaning up DevTools ports on host ===")
        # Always try to kill SSH tunnel on host
        run_command(["pkill", "-f", "9222:localhost:9222"], check=False)
        # Try to remove ADB forward if we have a device_id
        if device_id:
            run_command(["adb", "-s", device_id, "forward", "--remove", "tcp:9222"], check=False)
        if not (args.run or args.tests or args.force_deploy):
            print("=== Reset finished. ===")
            return

    assert_software_version(device_id, device_ip, MIN_SYSTEM_SOFTWARE_VERSION)
    if not args.tests:
        check_and_switch_cobalt_version(device_id, device_ip)

    # Setup Build Paths
    config = args.config or ("devel" if args.tests else "qa")
    out_dir = Path(args.out_dir or f"out/{PLATFORM}_{config}")

    if args.tests:
        targets = [f"{args.tests}_loader"]
        remote_dir = TEST_REMOTE_DIR
        deps_file = out_dir / f"{args.tests}_loader.runtime_deps"
    elif args.only_lib:
        targets = ["cobalt"]
        remote_dir = DEFAULT_REMOTE_DIR
        deps_file = None
    else:
        # Standard deployment uses cobalt_loader to generate the runtime_deps list.
        targets = ["cobalt_loader"]
        deps_file = out_dir / "cobalt_loader.runtime_deps"
        remote_dir = DEFAULT_REMOTE_DIR

    if not args.skip_build:
        rdk_home = os.environ.get("RDK_HOME")
        if not is_toolchain_installed(rdk_home):
            print(f"Error: RDK toolchain is not set up in RDK_HOME ({rdk_home}).")
            print("Please run this script with --setup-toolchain to download and install the toolchain.")
            print("Also, make sure to add it to your ~/.bashrc (e.g., export RDK_HOME=/workspaces/rdk/toolchain).")
            sys.exit(1)

        configure_build(PLATFORM, config, out_dir, args.no_rbe)
        build_output = build_targets(out_dir, targets)
        is_up_to_date = build_output and any(
            msg in build_output for msg in ["Everything is up-to-date", "no work to do"])
    else:
        print("=== Skipping build step ===")
        is_up_to_date = False

    skip_deployment = args.skip_deploy

    if skip_deployment:
        print("=== Skipping deployment ===")
        if not args.run:
            return
    else:
        if args.only_lib:
            deploy_only_lib(device_id, device_ip, out_dir, remote_dir)
        else:
            package_and_deploy(device_id, device_ip, out_dir, remote_dir, deps_file, is_test=bool(args.tests))

    if args.run:
        ensure_dolby_vision_policy(device_id, device_ip)
        launch_on_device(
            device_id,
            device_ip,
            remote_dir,
            args.tests,
            devtools=(config != "gold" and not args.tests),
            param=args.param,
            deeplink=args.deeplink,
        )

    print("=== Finished ===")


if __name__ == "__main__":
    main()
