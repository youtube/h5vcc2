#!/usr/bin/env python3

import subprocess
import time
import re
import pandas as pd
import matplotlib.pyplot as plt
import matplotlib.dates as mdates
from datetime import datetime, timedelta
import os
import sys
import argparse
import threading

# --- Configurable Defaults ---
DEFAULT_APK1_PATH = "/usr/local/google/home/sideboard/Code/cobalt/src/out/android-arm_devel/apks/Cobalt.apk"
DEFAULT_APK2_PATH = "/usr/local/google/home/sideboard/Code/cobalt-25/out/android-arm_devel/cobalt.apk"
DEFAULT_DURATION = 60  # Seconds
DEFAULT_OUTPUT_DIR = "/tmp/cobalt_monitoring"
#DEFAULT_YOUTUBE_URL = "https://youtube.com/tv/watch?v=UdqZ3xoaekk&absolute_experiments=9415422"
DEFAULT_YOUTUBE_URL = "https://www.youtube.com/tv/playlist?list=PLH-do-y953IiystRnbK49ot5y0lfBYxSd"
# --- End Configurable Defaults ---

PACKAGE_NAME = "dev.cobalt.coat"
POLL_INTERVAL = 5  # Seconds
LAUNCH_ACTIVITY = "dev.cobalt.app.MainActivity"

LOGCAT_STATE_REGEX = re.compile(r"StarboardRenderer::OnPlayerStatus\(\) called with (kSbPlayerState\w+)")

def run_adb_command(command, check=True, suppress_output=False):
    full_command = ['adb'] + command
    if not suppress_output:
        print(f"Running ADB: {' '.join(full_command)}")
    try:
        result = subprocess.run(full_command, capture_output=True, text=True, check=check)
        return result.stdout.strip()
    except subprocess.CalledProcessError as e:
        if not suppress_output:
            print(f"ADB command failed: {' '.join(command)}")
            print(f"Error: {e}")
            print(f"Stderr: {e.stderr}")
        if check:
            raise
        return None

def get_cobalt_pid():
    return run_adb_command(['shell', 'pidof', PACKAGE_NAME], check=False, suppress_output=True)

def monitor_memory(pid):
    try:
        meminfo = run_adb_command(['shell', 'dumpsys', 'meminfo', pid], suppress_output=True)
        if not meminfo: return None

        def get_pss(category_name):
            match = re.search(r"^\s*" + re.escape(category_name) + r"\s+(\d+)", meminfo, re.MULTILINE)
            return int(match.group(1)) if match else 0

        def get_app_summary_pss(category_name):
            match = re.search(r"\s+" + re.escape(category_name) + r":\s+(\d+)", meminfo)
            return int(match.group(1)) if match else 0

        return {
            'Timestamp': datetime.now(),
            'PSS': get_app_summary_pss("TOTAL PSS"), 'JavaHeap': get_app_summary_pss("Java Heap"),
            'NativeHeap': get_app_summary_pss("Native Heap"), 'Code': get_app_summary_pss("Code"),
            'Stack': get_app_summary_pss("Stack"), 'Graphics': get_app_summary_pss("Graphics"),
            'Other': get_app_summary_pss("Private Other"), 'System': get_app_summary_pss("System"),
            'so_mmap': get_pss(".so mmap"), 'jar_mmap': get_pss(".jar mmap"), 'apk_mmap': get_pss(".apk mmap"),
            'ttf_mmap': get_pss(".ttf mmap"), 'dex_mmap': get_pss(".dex mmap"), 'oat_mmap': get_pss(".oat mmap"),
            'art_mmap': get_pss(".art mmap"), 'EGL_mtrack': get_pss("EGL mtrack"), 'GL_mtrack': get_pss("GL mtrack"),
            'Ashmem': get_pss("Ashmem"), 'Other_dev': get_pss("Other dev"), 'Other_mmap': get_pss("Other mmap"),
            'Unknown': get_pss("Unknown"),
        }
    except Exception as e:
        print(f"Error parsing meminfo: {e}")
        return None

def monitor_cpu(pid):
    try:
        top_output = run_adb_command(['shell', 'top', '-p', pid, '-n', '1'], suppress_output=True)
        if not top_output: return None
        lines = top_output.splitlines()
        header_line = None
        header_index = -1
        cpu_index = -1

        for i, line in enumerate(lines):
            if 'PID' in line and '%CPU' in line:
                header_line = line
                header_index = i
                break

        if not header_line:
            print(f"Error: Could not find header row with %CPU in top output")
            return None

        headers = header_line.split()
        try:
            cpu_index = headers.index('%CPU')
        except ValueError:
            # Some versions of top use S[%CPU]
            try:
                cpu_index = headers.index('S[%CPU]')
            except ValueError:
                print(f"Error: '%CPU' or 'S[%CPU]' not found in header: {header_line}")
                return None

        for i in range(header_index + 1, len(lines)):
            line = lines[i]
            if pid in line:
                parts = line.split()
                if len(parts) > cpu_index:
                    cpu_val = parts[cpu_index]
                    try:
                        return {'Timestamp': datetime.now(), 'CPU': float(cpu_val)}
                    except ValueError:
                        # If conversion fails, it might be the status column. Try the next index.
                        if len(parts) > cpu_index + 1:
                            cpu_val = parts[cpu_index + 1]
                            try:
                                return {'Timestamp': datetime.now(), 'CPU': float(cpu_val)}
                            except ValueError:
                                print(f"Error converting CPU: {parts[cpu_index]} or {cpu_val} in line: {line}")
                                return None
                        else:
                            print(f"Error converting CPU: {parts[cpu_index]} in line: {line} - No next column to try")
                            return None
                else:
                    print(f"Error: Not enough parts in line for CPU index: {line}")
        return None
    except Exception as e:
        print(f"Error parsing top: {e}")
        return None

def monitor_logcat(pid, duration_seconds, events):
    print(f"Starting logcat monitoring for PID {pid} for {duration_seconds}s...")
    try:
        process = subprocess.Popen(['adb', 'logcat', '-v', 'time', '--pid', pid], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        start_time = time.time()
        while time.time() - start_time < duration_seconds:
            if process.stdout:
                line = process.stdout.readline()
                if not line:
                    if process.poll() is not None:
                        break
                    continue

                state_match = LOGCAT_STATE_REGEX.search(line)
                if state_match:
                    events.append({
                        'Timestamp': datetime.now(),
                        'Type': 'StateChange',
                        'To': state_match.group(1) # Capture the kSbPlayerState
                    })
        if process.poll() is None:
             process.terminate()
             try:
                 process.wait(timeout=5)
             except subprocess.TimeoutExpired:
                 process.kill()
                 print("Logcat process killed.")
    except Exception as e:
        print(f"Error during logcat monitoring: {e}")
    print("Logcat monitoring finished.")

def uninstall_apk(package_name):
    print(f"Uninstalling {package_name}...")
    run_adb_command(['uninstall', package_name], check=False)
    time.sleep(2) # Wait for uninstall to complete

def install_apk(apk_path):
    print(f"Installing {apk_path}...")
    if not os.path.exists(apk_path):
        print(f"Error: APK not found at {apk_path}")
        return False
    # Use -r to replace existing, -d to allow downgrade
    run_adb_command(['install', '-r', '-d', apk_path])
    print("Install complete.")
    time.sleep(2)
    return True

def launch_app(package_name, youtube_url):
    print(f"Launching {package_name} with URL: {youtube_url}")
    url_arg = f'--url="{youtube_url}"'
    launch_command = [
        'shell', 'am', 'start',
        '--esa', 'commandLineArgs', url_arg,
        '--esa', 'args', url_arg,
        f'{package_name}/{LAUNCH_ACTIVITY}'
    ]
    run_adb_command(launch_command)
    print("Waiting for app to start...")
    time.sleep(10)

def parse_surfaceflinger_stats(dumpsys_output, package_name):
    janky_frames = 0
    in_surfaceview_section = False
    layer_regex = re.compile(r"layerName = SurfaceView\[" + re.escape(package_name))
    histogram_regex = re.compile(r"present2present histogram is as below:")
    bucket_regex = re.compile(r"(\d+)ms=(\d+)")

    lines = dumpsys_output.splitlines()
    for i, line in enumerate(lines):
        if layer_regex.search(line):
            in_surfaceview_section = True
            continue

        if in_surfaceview_section and not line.strip():
            # End of section
            in_surfaceview_section = False

        if in_surfaceview_section and histogram_regex.search(line):
            # Next lines are the histogram data
            for j in range(i + 1, len(lines)):
                hist_line = lines[j]
                if not hist_line.strip() or "ms=" not in hist_line:
                    break  # End of histogram

                buckets = bucket_regex.findall(hist_line)
                for time_ms, count in buckets:
                    try:
                        time_ms = int(time_ms)
                        count = int(count)
                        # Threshold for jank: > 20ms, assuming 60Hz target (16.67ms)
                        if time_ms > 20:
                            janky_frames += count
                    except ValueError:
                        continue
            return janky_frames
    return janky_frames # Return 0 if not found

def run_test_on_apk(apk_path, package_name, duration_seconds, youtube_url):
    mem_data, cpu_data, events = [], [], []
    uninstall_apk(package_name)
    if not install_apk(apk_path):
        return None

    run_adb_command(['logcat', '-c']) # Clear logcat
    run_adb_command(['shell', 'dumpsys', 'SurfaceFlinger', '--timestats', '-clear'], check=False)
    run_adb_command(['shell', 'dumpsys', 'SurfaceFlinger', '--timestats', '-enable'], check=False)

    launch_app(package_name, youtube_url)

    cobalt_pid = None
    for _ in range(5):
        cobalt_pid = get_cobalt_pid()
        if cobalt_pid: break
        time.sleep(2)

    if not cobalt_pid:
        print(f"Failed to get PID for {package_name}.")
        uninstall_apk(package_name)
        return None
    print(f"Cobalt PID: {cobalt_pid}")

    logcat_thread = threading.Thread(target=monitor_logcat, args=(cobalt_pid, duration_seconds, events))
    logcat_thread.start()

    print(f"Starting monitoring for {os.path.basename(apk_path)} for {duration_seconds}s...")
    start_time = time.time()
    while time.time() - start_time < duration_seconds:
        mem = monitor_memory(cobalt_pid)
        if mem: mem_data.append(mem)
        cpu = monitor_cpu(cobalt_pid)
        if cpu: cpu_data.append(cpu)

        remaining = duration_seconds - (time.time() - start_time)
        sleep_time = min(POLL_INTERVAL, remaining)
        if sleep_time > 0:
            time.sleep(sleep_time)

    print(f"Waiting for logcat thread to finish for {os.path.basename(apk_path)}...")
    logcat_thread.join()
    print(f"Monitoring finished for {os.path.basename(apk_path)}.")
    run_adb_command(['shell', 'am', 'force-stop', package_name], check=False)

    sf_stats_output = run_adb_command(['shell', 'dumpsys', 'SurfaceFlinger', '--timestats', '-dump'], check=False)
    run_adb_command(['shell', 'dumpsys', 'SurfaceFlinger', '--timestats', '-disable'], check=False)

    sf_janky_frames = 0
    if sf_stats_output:
        sf_janky_frames = parse_surfaceflinger_stats(sf_stats_output, package_name)
        print(f"SurfaceFlinger Janky Frames: {sf_janky_frames}")

    uninstall_apk(package_name)
    return mem_data, cpu_data, sf_janky_frames, events

def plot_combined_data(data1, data2, label1, label2, output_dir):
    if not os.path.exists(output_dir):
        os.makedirs(output_dir)

    mem_data1, cpu_data1, sf_janky1, events1 = data1 if data1 else ([], [], 0, [])
    mem_data2, cpu_data2, sf_janky2, events2 = data2 if data2 else ([], [], 0, [])

    # Normalize timestamps to be relative to the start of each run
    def normalize_time(data, start_time_override=None):
        if not data:
            return [], start_time_override or datetime.now()
        start_time = start_time_override or data[0]['Timestamp']
        for item in data:
            item['RelTime'] = (item['Timestamp'] - start_time).total_seconds()
        return data, start_time

    mem_data1, run1_start_time = normalize_time(mem_data1)
    cpu_data1, _ = normalize_time(cpu_data1, run1_start_time)
    events1, _ = normalize_time(events1, run1_start_time)

    mem_data2, run2_start_time = normalize_time(mem_data2)
    if not mem_data2:
        run2_start_time = run1_start_time
    cpu_data2, _ = normalize_time(cpu_data2, run2_start_time)
    events2, _ = normalize_time(events2, run2_start_time)

    fig = plt.figure(figsize=(24, 28))
    fig.suptitle(f'Cobalt Performance Comparison: {label1} vs {label2}\n'
                 f'{label1} Janky Frames: {sf_janky1} | {label2} Janky Frames: {sf_janky2}', fontsize=16)
    gs = fig.add_gridspec(3, 2) # 3 rows: mem, cpu

    # Determine shared Y-axis limit for memory plots
    max_pss = 0
    if mem_data1:
        df_mem1_temp = pd.DataFrame(mem_data1)
        if not df_mem1_temp.empty and 'PSS' in df_mem1_temp.columns:
            max_pss = max(max_pss, df_mem1_temp['PSS'].max())
    if mem_data2:
        df_mem2_temp = pd.DataFrame(mem_data2)
        if not df_mem2_temp.empty and 'PSS' in df_mem2_temp.columns:
            max_pss = max(max_pss, df_mem2_temp['PSS'].max())
    mem_ylim = (0, max_pss * 1.1 + 1)

    mem_labels = ['JavaHeap', 'NativeHeap', 'Stack', 'System', 'so_mmap', 'jar_mmap', 'apk_mmap',
              'ttf_mmap', 'dex_mmap', 'oat_mmap', 'art_mmap', 'EGL_mtrack', 'GL_mtrack',
              'Ashmem', 'Other_dev', 'Other_mmap', 'Unknown']

    def plot_memory(ax, mem_data, title_prefix):
        if mem_data:
            df_mem = pd.DataFrame(mem_data)
            if not df_mem.empty and 'RelTime' in df_mem.columns:
                columns = [df_mem.get(label, pd.Series([0] * len(df_mem))) for label in mem_labels]
                ax.stackplot(df_mem['RelTime'], *columns, labels=mem_labels, alpha=0.8)
                if 'PSS' in df_mem.columns:
                    ax.plot(df_mem['RelTime'], df_mem['PSS'], label='Total PSS', color='black', linestyle='--')
        ax.set_xlabel('Time (seconds)')
        ax.set_ylabel('Memory (KB)')
        ax.set_title(f'Memory Usage: {title_prefix}')
        ax.set_ylim(mem_ylim)
        ax.grid(True)
        handles, labels = ax.get_legend_handles_labels()
        if handles:
            ax.legend(handles[::-1], labels[::-1], loc='upper left', fontsize='small')

    ax_mem1 = fig.add_subplot(gs[0, 0])
    plot_memory(ax_mem1, mem_data1, label1)

    ax_mem2 = fig.add_subplot(gs[0, 1])
    plot_memory(ax_mem2, mem_data2, label2)

    # CPU Plots
    def plot_cpu(ax, cpu_data, title_prefix):
        if cpu_data:
            df_cpu = pd.DataFrame(cpu_data)
            if not df_cpu.empty and 'RelTime' in df_cpu.columns and 'CPU' in df_cpu.columns:
                ax.plot(df_cpu['RelTime'], df_cpu['CPU'], label=f'CPU %', linestyle='-')
        ax.set_xlabel('Time (seconds)')
        ax.set_ylabel('CPU Usage (%)')
        ax.set_title(f'CPU Usage: {title_prefix}')
        ax.grid(True)
        if cpu_data:
            ax.legend(loc='upper right')

    ax_cpu1 = fig.add_subplot(gs[1, 0])
    plot_cpu(ax_cpu1, cpu_data1, label1)

    ax_cpu2 = fig.add_subplot(gs[1, 1])
    plot_cpu(ax_cpu2, cpu_data2, label2)

    # Event markers and annotations
    state_colors = {}
    color_cycle = plt.cm.get_cmap('hsv', 10) # Cycle through 10 colors

    def get_state_color(state):
        if state not in state_colors:
            state_colors[state] = color_cycle(len(state_colors) % 10)
        return state_colors[state]

    def add_event_markers(ax, events_list):
        if not events_list: return
        anno_offsets = [10, 30]  # Pixels above the x-axis
        state_events = sorted([e for e in events_list if e['Type'] == 'StateChange'], key=lambda x: x['RelTime'])
        for i, event in enumerate(state_events):
            state = event['To']
            display_state = state.replace("kSbPlayerState", "")
            event_color = get_state_color(state)
            
            ax.axvline(x=event['RelTime'], color=event_color, linestyle=':', alpha=0.7)
            
            offset = anno_offsets[i % len(anno_offsets)]
            ax.annotate(f"{display_state}",
                        xy=(event['RelTime'], 0), xycoords=('data', 'axes fraction'),
                        xytext=(3, offset), textcoords='offset points',
                        ha='left', va='bottom', color=event_color, fontsize=7,
                        rotation=45)

    add_event_markers(ax_mem1, events1)
    add_event_markers(ax_mem2, events2)
    add_event_markers(ax_cpu1, events1)
    add_event_markers(ax_cpu2, events2)

    plt.tight_layout(rect=[0, 0.03, 1, 0.95])
    combined_filename = os.path.join(output_dir, f'comparison_{label1}_vs_{label2}.png')
    plt.savefig(combined_filename)
    print(f"Combined comparison plot saved to {combined_filename}")
    plt.close(fig)

def main(apk1_path, apk2_path, duration, output_dir, youtube_url):
    print(f"--- Running Test for APK 1: {apk1_path} ---")
    data1 = run_test_on_apk(apk1_path, PACKAGE_NAME, duration, youtube_url)

    print(f"\n--- Running Test for APK 2: {apk2_path} ---")
    data2 = run_test_on_apk(apk2_path, PACKAGE_NAME, duration, youtube_url)

    if data1 or data2:
        print("\n--- Generating Comparison Plots ---")
        plot_combined_data(data1, data2, "APK1", "APK2", output_dir)
    else:
        print("Data collection failed for both APKs, skipping plotting.")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Monitor and Compare Cobalt APKs.")
    parser.add_argument("--apk1", default=DEFAULT_APK1_PATH, help="Path to the first Cobalt APK")
    parser.add_argument("--apk2", default=DEFAULT_APK2_PATH, help="Path to the second Cobalt APK")
    parser.add_argument("--duration", type=int, default=DEFAULT_DURATION, help="Duration to monitor each APK in seconds")
    parser.add_argument("--outdir", default=DEFAULT_OUTPUT_DIR, help="Directory to save plots and CSV files")
    parser.add_argument("--url", default=DEFAULT_YOUTUBE_URL, help="YouTube URL to launch")
    args = parser.parse_args()

    try:
        run_adb_command(['--version'])
    except Exception as e:
        print(f"Error: ADB check failed: {e}")
        sys.exit(1)
    main(args.apk1, args.apk2, args.duration, args.outdir, args.url)
