#!/usr/bin/env python3
#
# Copyright 2025 The Cobalt Authors. All Rights Reserved.
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
"""Prototype to interact with Cobalt histograms via CDP."""

import argparse
import json
import os
import sys
import time
import threading
import requests

REPOSITORY_ROOT = os.path.abspath(
    os.path.join(os.path.dirname(__file__), '../../../'))
sys.path.append(os.path.join(REPOSITORY_ROOT, 'cobalt', 'tools', 'performance'))
sys.path.append(
    os.path.join(REPOSITORY_ROOT, 'third_party', 'catapult', 'devil'))
sys.path.append(
    os.path.join(REPOSITORY_ROOT, 'third_party', 'catapult', 'telemetry',
                 'third_party', 'websocket-client'))

import websocket  # pylint: disable=wrong-import-position
import subprocess  # pylint: disable=wrong-import-position

from adb_command_runner import run_adb_command  # pylint: disable=wrong-import-position

# Configuration
COBALT_DEBUG_PORT = 9222
CDP_HOST = '127.0.0.1'
TARGET_URL = 'https://www.youtube.com/tv#/watch?v=AB-4pS2Og1g'
DEFAULT_COBALT_PACKAGE_NAME = 'dev.cobalt.coat'
DEFAULT_COBALT_ACTIVITY_NAME = 'dev.cobalt.app.MainActivity'
DEFAULT_POLL_INTERVAL_S = 30.0
MAX_WEBSOCKET_RETRIES = 10
WEBSOCKET_RETRY_DELAY_S = 2


def _print_q(message: str, quiet: bool):
  """Prints a message if the quiet flag is not set."""
  if not quiet:
    print(message)


def is_package_running_android(package_name: str) -> bool:
  """Checks if a given Android package is currently running."""
  stdout, _ = run_adb_command(['adb', 'shell', 'pidof', package_name])
  return bool(stdout and stdout.strip().isdigit())


def is_cobalt_running_linux(process_name: str = 'cobalt') -> bool:
  """Checks if a process with a given name is running on Linux."""
  try:
    # Use pgrep to check if the process is running.
    subprocess.check_output(['pgrep', '-f', process_name])
    return True
  except (subprocess.CalledProcessError, FileNotFoundError):
    return False


def stop_package(package_name: str, quiet: bool) -> bool:
  """Attempts to force-stop an Android package."""
  _print_q(f'Attempting to stop package \'{package_name}\'...', quiet)
  _, stderr = run_adb_command(
      ['adb', 'shell', 'am', 'force-stop', package_name])
  if stderr:
    _print_q(f'Error stopping package \'{package_name}\': {stderr}', quiet)
    return False
  _print_q(f'Package \'{package_name}\' stop command issued.', quiet)
  time.sleep(2)  # Give time for the stop to propagate.
  return True


def launch_cobalt(package_name: str, activity_name: str, url: str, quiet: bool):
  """Launches the Cobalt application with a specified URL.
       Returns True on success and False upon failure."""
  command_str = (f'adb shell am start -n {package_name}/{activity_name} '
                 f'--esa commandLineArgs \'--remote-allow-origins=*,'
                 f'--url=\"{url}\"\'')
  stdout, stderr = run_adb_command(command_str, shell=True)
  if stderr:
    _print_q(f'Error launching Cobalt: {stderr}', quiet)
    return False
  else:
    _print_q(
        'Cobalt launch command sent.' +
        (f' Launch stdout: {stdout}' if stdout else ''), quiet)
    return True


def get_websocket_url(platform: str, port: int, quiet: bool):
  """
    Connects to the Cobalt DevTools Protocol endpoint to get the WebSocket URL.
    """
  if platform == 'android':
    for i in range(MAX_WEBSOCKET_RETRIES):
      try:
        run_adb_command([
            'adb', 'forward', f'tcp:{port}',
            'localabstract:content_shell_devtools_remote'
        ])
        response = requests.get(f'http://{CDP_HOST}:{port}/json', timeout=5)
        response.raise_for_status()
        targets = response.json()

        for target in targets:
          if target.get('type') == 'page' and target.get(
              'webSocketDebuggerUrl'):
            _print_q(
                'Found existing tab: ' + target['title'] + ' - ' +
                target['webSocketDebuggerUrl'], quiet)
            return target['webSocketDebuggerUrl']

        _print_q(
            'No suitable existing page found, trying to create a new tab...',
            quiet)
        new_tab_url = f'http://{CDP_HOST}:{port}/json/new'
        new_tab_response = requests.get(new_tab_url, timeout=5)
        new_tab_response.raise_for_status()
        new_tab_data = new_tab_response.json()
        if new_tab_data.get('webSocketDebuggerUrl'):
          _print_q(
              'Created new tab: ' + new_tab_data['title'] + ' - ' +
              new_tab_data['webSocketDebuggerUrl'], quiet)
          return new_tab_data['webSocketDebuggerUrl']

        raise RuntimeError(
            'Could not find or create a suitable WebSocket debugger URL.')

      except requests.exceptions.ConnectionError:
        _print_q(
            f'Attempt {i + 1}/{MAX_WEBSOCKET_RETRIES}: ' +
            f'Connection error to Cobalt on {CDP_HOST}:{port}. Retrying...',
            quiet)
      except RuntimeError as e:
        _print_q(
            f'Attempt {i + 1}/{MAX_WEBSOCKET_RETRIES}: An error occurred ' +
            f'while getting WebSocket URL: {e}. Retrying...', quiet)
      time.sleep(WEBSOCKET_RETRY_DELAY_S)
    _print_q('Failed to get WebSocket URL after multiple retries.', quiet)
    return None
  elif platform == 'linux':
    try:
      response = requests.get(f'http://127.0.0.1:{port}/json', timeout=5)
      response.raise_for_status()
      targets = response.json()

      browser_target = next((t for t in targets if t['type'] == 'browser'),
                            None)
      if not browser_target and targets:
        browser_target = next((t for t in targets if t['type'] == 'page'),
                              targets[0])

      if browser_target and 'webSocketDebuggerUrl' in browser_target:
        ws_url = browser_target['webSocketDebuggerUrl']
        _print_q(f'Found CDP websocket URL: {ws_url}', quiet)
        return ws_url
      else:
        _print_q('Could not find a suitable CDP target.', quiet)
        if targets:
          _print_q(f'Available targets: {json.dumps(targets, indent=2)}', quiet)
        return None
    except requests.exceptions.ConnectionError:
      _print_q(f"Error: Could not connect to Cobalt's DevTools on port {port}.",
               quiet)
      _print_q(
          'Please ensure Cobalt is running with the remote debugging port '
          'enabled.', quiet)
      return None
    except Exception as e:  # pylint: disable=broad-exception-caught
      _print_q(f'An unexpected error occurred: {e}', quiet)
      return None
  return None


def _get_histograms_from_file(file_path: str) -> list:
  """Reads a list of histograms from a text file."""
  with open(file_path, 'r', encoding='utf-8') as f:
    return [
        line.strip() for line in f if line.strip() and not line.startswith('#')
    ]


def _get_chrome_guiding_metrics_for_memory() -> list:
  # TODO(482357006): Re-add process-specific private memory metrics (Browser,
  # Renderer, GPU) when moving to multi-process architecture.
  return [
      'Memory.Total.PrivateMemoryFootprint',
  ]


def _get_cobalt_resident_memory_metrics() -> list:
  # TODO(482357006): Re-add process-specific resident memory metrics (Browser,
  # Renderer, GPU) when moving to multi-process architecture.
  return [
      'Memory.Total.Resident',
  ]


def _print_cobalt_histogram_names(ws, message_id: int, histograms: list,
                                  output_file, quiet: bool):
  """Prints histogram data to the console and writes it to a file."""
  _print_q('\n' + '-' * 50 + '\n', quiet)
  for metric in histograms:
    command = {
        'id': message_id,
        'method': 'Browser.getHistograms',
        'params': {
            'query': metric
        }
    }
    ws.send(json.dumps(command))
    response = json.loads(ws.recv())
    message_id += 1
    _print_q(f'\nMetric - {metric}:\n{response}\n', quiet)
    if output_file:
      timestamp = time.strftime('%Y-%m-%d %H:%M:%S', time.localtime())
      output_file.write(f'{timestamp},{metric},{json.dumps(response)}\n')
      output_file.flush()
  _print_q('\n' + '-' * 50 + '\n', quiet)


def _interact_via_cdp(websocket_url: str, histograms: list, output_file,
                      url: str, quiet: bool):
  ws = None
  try:
    ws = websocket.create_connection(websocket_url)
    _print_q(f'Connected to WebSocket: {websocket_url}', quiet)

    message_id = 1
    command = {
        'id': message_id,
        'method': 'Performance.enable',
        'params': {
            'timeDomain': 'threadTicks'
        }
    }
    ws.send(json.dumps(command))
    response = json.loads(ws.recv())
    _print_q('Enabled Performance domain (ID: ' + str(response['id']) + ')',
             quiet)
    message_id += 1

    command = {
        'id': message_id,
        'method': 'Performance.getMetrics',
        'params': {}
    }
    ws.send(json.dumps(command))
    response = json.loads(ws.recv())
    message_id += 1

    if 'result' in response and 'metrics' in response['result']:
      _print_q(f'\nPerformance Metrics for {url}:', quiet)
      for metric in response['result']['metrics']:
        _print_q('  ' + metric['name'] + ': ' + str(metric['value']), quiet)
    else:
      _print_q(f'Could not retrieve metrics: {response}', quiet)

    _print_cobalt_histogram_names(ws, message_id, histograms, output_file,
                                  quiet)
  except Exception as e:  # pylint: disable=broad-exception-caught
    _print_q(f'An error occurred: {e}', quiet)
  finally:
    if ws:
      ws.close()
      _print_q('WebSocket connection closed.', quiet)


def loop(websocket_url: str, stop_event, histograms: list, args, output_file):
  """Runs the data polling loop in a dedicated thread."""
  _print_q('Starting data polling thread', args.quiet)
  iteration = 0
  while not stop_event.is_set():
    iteration += 1
    current_time_str = time.strftime('%Y-%m-%d %H:%M:%S', time.localtime())
    header_prefix = f'[{current_time_str}][Poll Iter: {iteration}]'

    app_is_running = False
    if args.platform == 'android':
      app_is_running = is_package_running_android(args.package_name)
    elif args.platform == 'linux':
      app_is_running = is_cobalt_running_linux()

    if app_is_running:
      _interact_via_cdp(websocket_url, histograms, output_file, args.url,
                        args.quiet)
    elif iteration % 5 == 0:
      _print_q(f'{header_prefix} App not running.', args.quiet)

    time.sleep(args.poll_interval_s)
  _print_q('Data polling thread stopped.', args.quiet)


def _run_main(args, output_file):
  """The main logic of the script."""
  if args.histogram_file:
    histograms = _get_histograms_from_file(args.histogram_file)
  else:
    histograms = _get_chrome_guiding_metrics_for_memory()
    histograms.extend(_get_cobalt_resident_memory_metrics())

  _print_q(f'Ensure Cobalt is running with --remote-debugging-port={args.port}',
           args.quiet)
  _print_q('-' * 50, args.quiet)
  stop_event = threading.Event()

  if args.platform == 'android' and not args.no_manage_cobalt:
    if not is_package_running_android(args.package_name):
      launch_cobalt(
          package_name=args.package_name,
          activity_name=DEFAULT_COBALT_ACTIVITY_NAME,
          url=args.url,
          quiet=args.quiet)
      _print_q(f'Waiting for {args.package_name} to start...', args.quiet)
      started = False
      for _ in range(5):  # Poll for 5 seconds
        if is_package_running_android(args.package_name):
          started = True
          _print_q(f'{args.package_name} has started.', args.quiet)
          break
        time.sleep(1)

      if not started:
        _print_q(
            f'Error: Failed to start {args.package_name}/'
            f'{DEFAULT_COBALT_ACTIVITY_NAME} within 5 seconds. \nExiting...\n',
            args.quiet)
        sys.exit(1)
  time.sleep(1)

  websocket_url = get_websocket_url(
      platform=args.platform, port=args.port, quiet=args.quiet)
  if not websocket_url:
    _print_q('Websocket not found. Exiting...', args.quiet)
    if args.platform == 'android' and not args.no_manage_cobalt:
      stop_package(args.package_name, args.quiet)
    return

  polling_thread = threading.Thread(
      target=loop,
      args=(websocket_url, stop_event, histograms, args, output_file),
      daemon=True,
  )
  polling_thread.start()

  _print_q(
      f'\nPolling every {args.poll_interval_s}s. '
      'Ctrl+C to stop & save.', args.quiet)

  try:
    while polling_thread.is_alive():
      time.sleep(1)
  except KeyboardInterrupt:
    _print_q('\nCtrl+C received. Signaling polling thread to stop...',
             args.quiet)
  except Exception as e:  # pylint: disable=broad-exception-caught
    _print_q(f'Main loop encountered an error: {e}', args.quiet)
  finally:
    stop_event.set()
    if args.platform == 'android' and not args.no_manage_cobalt:
      stop_package(args.package_name, args.quiet)
  _print_q('\nDONE!!!\n', args.quiet)

  if args.output_file:
    html_file = args.output_file.rsplit('.', 1)[0] + '.html'
    _print_q(f'Generating visualization: {html_file}', args.quiet)
    try:
      # Use sys.executable to ensure we use the same Python binary
      plot_script = os.path.join(
          os.path.dirname(os.path.abspath(__file__)), 'plot_uma_metrics.py')
      subprocess.run([
          sys.executable, plot_script, '--input', args.output_file, '--output',
          html_file
      ],
                     check=False)
    except (subprocess.CalledProcessError, OSError) as e:
      _print_q(f'Failed to run plot_uma_metrics.py: {e}', args.quiet)


def main():
  parser = argparse.ArgumentParser()
  parser.add_argument(
      '--histogram-file',
      help='Path to a text file containing a list of histograms to query.')
  parser.add_argument(
      '--no-manage-cobalt',
      action='store_true',
      help='If set, the script will not start or stop the Cobalt process. '
      'This is the default behavior on Linux.')
  parser.add_argument(
      '--package-name',
      default=DEFAULT_COBALT_PACKAGE_NAME,
      help='The package name of the Cobalt application.')
  parser.add_argument(
      '--platform',
      choices=['android', 'linux'],
      default='android',
      help='The platform to run on (android or linux).')
  parser.add_argument(
      '--poll-interval-s',
      type=float,
      default=DEFAULT_POLL_INTERVAL_S,
      help='The polling frequency in seconds.')
  parser.add_argument(
      '--port',
      type=int,
      default=COBALT_DEBUG_PORT,
      help='CDP port for Cobalt on Linux.')
  parser.add_argument(
      '--output-file', help='Path to a file to direct all output to.')
  parser.add_argument(
      '--url',
      default=TARGET_URL,
      help='The target URL for Cobalt to navigate to on launch.')
  parser.add_argument(
      '-q',
      '--quiet',
      action='store_true',
      help='If set, suppresses all non-essential print output.')
  args = parser.parse_args()

  # On Linux, managing the Cobalt process is not supported by this script.
  if args.platform == 'linux':
    args.no_manage_cobalt = True

  if args.output_file:
    with open(args.output_file, 'a', encoding='utf-8') as f:
      _run_main(args, f)
  else:
    _run_main(args, None)


if __name__ == '__main__':
  main()
