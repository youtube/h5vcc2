#!/usr/bin/env python3
"""Parses UMA histogram dumps into an HTML visualization chart."""

import argparse
import json
import os
import sys
from datetime import datetime


def parse_args():
  parser = argparse.ArgumentParser(
      description='Visualize UMA metrics by generating an HTML chart.')
  parser.add_argument(
      '--input',
      type=str,
      required=True,
      help='Path to the uma_histos.txt file')
  parser.add_argument(
      '--output',
      type=str,
      default='memory_timeseries.html',
      help='Path to save the generated HTML file')
  return parser.parse_args()


def parse_uma_data(filepath):
  data = {}
  timestamps = []
  metrics_to_plot = []

  print(f'Parsing {filepath}...')
  with open(filepath, 'r', encoding='utf-8') as f:
    for line in f:
      line = line.strip()
      if not line:
        continue

      parts = line.split(',', 2)
      if len(parts) < 3:
        continue

      ts_str, metric_name, json_str = parts[0], parts[1], parts[2]

      if metric_name not in metrics_to_plot:
        metrics_to_plot.append(metric_name)
        data[metric_name] = []

      try:
        val = json.loads(json_str)
        histograms = val.get('result', {}).get('histograms', [])

        sum_val = 0
        count_val = 0
        if histograms:
          if metric_name == 'Memory.Experimental.Browser2.Malloc':
            for h in histograms:
              if h.get('name'
                      ) == 'Memory.Experimental.Browser2.Malloc.CommittedSize':
                sum_val = h.get('sum', 0)
                count_val = h.get('count', 0)
                break
          else:
            for h in histograms:
              if h.get('name') == metric_name:
                sum_val = h.get('sum', 0)
                count_val = h.get('count', 0)
                break

        current_dt = datetime.strptime(ts_str, '%Y-%m-%d %H:%M:%S')
        if not timestamps:
          timestamps.append(ts_str)
          last_dt = current_dt
        else:
          last_dt = datetime.strptime(timestamps[-1], '%Y-%m-%d %H:%M:%S')
          if (current_dt - last_dt).total_seconds() > 5:
            timestamps.append(ts_str)
        idx = len(timestamps) - 1

        while len(data[metric_name]) <= idx:
          data[metric_name].append({'sum': 0, 'count': 0, 'val': 0})

        prev_sum = 0
        prev_count = 0
        if idx > 0:
          prev_sum = data[metric_name][idx - 1]['sum']
          prev_count = data[metric_name][idx - 1]['count']

        delta_sum = sum_val - prev_sum
        delta_count = count_val - prev_count

        # Handle app restarts where the cumulative sum resets to a smaller value
        if sum_val < prev_sum:
          delta_sum = sum_val
          delta_count = count_val

        if delta_count > 0:
          current_val = delta_sum / delta_count
        else:
          current_val = data[metric_name][idx - 1]['val'] if idx > 0 else 0

        data[metric_name][idx] = {
            'sum': sum_val,
            'count': count_val,
            'val': current_val
        }

      except json.JSONDecodeError as e:
        print(f'JSON decode error for {metric_name}: {e}')
      except KeyError as e:
        print(f'Key error for {metric_name}: {e}')
      except Exception as e:  # pylint: disable=broad-exception-caught
        print(f'Error parsing line for {metric_name}: {e}')

  # Convert the dicts back to simple arrays for plotting
  plot_data = {metric: [] for metric in metrics_to_plot}
  for m, items in data.items():
    for idx in range(len(timestamps)):
      if idx < len(items):
        plot_data[m].append(items[idx]['val'])
      else:
        plot_data[m].append(0)

  return timestamps, plot_data, metrics_to_plot


def generate_html_chart(timestamps, plot_data, metrics_to_plot, output_path):
  if not timestamps:
    print('No matching metrics found to plot.')
    sys.exit(1)

  print('Generating HTML visualization...')

  # Generate Chart.js datasets
  datasets_js = []
  colors = [
      "'#FF6384'", "'#36A2EB'", "'#FFCE56'", "'#4BC0C0'", "'#9966FF'",
      "'#FF9F40'", "'#C9CBCF'", "'#FF4081'", "'#00E676'", "'#D500F9'"
  ]

  for i, m in enumerate(metrics_to_plot):
    short_name = m.split('.')[-1]
    color = colors[i % len(colors)]
    dataset = f"""{{
            label: '{short_name}',
            data: {plot_data[m]},
            borderColor: {color},
            backgroundColor: {color},
            fill: false,
            tension: 0.1
        }}"""
    datasets_js.append(dataset)

  html_content = f"""<!DOCTYPE html>
<html>
<head>
    <title>Cobalt Metrics Time Series</title>
    <script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
    <style>
        body {{ font-family: sans-serif; margin: 20px; }}
        .chart-container {{ width: 90vw; height: 80vh; margin: auto; }}
    </style>
</head>
<body>
    <h2>Cobalt Metrics Time Series</h2>
    <div class="chart-container">
        <canvas id="metricsChart"></canvas>
    </div>
    <script>
        const ctx = document.getElementById('metricsChart').getContext('2d');
        const metricsChart = new Chart(ctx, {{
            type: 'line',
            data: {{
                labels: {json.dumps(timestamps)},
                datasets: [{', '.join(datasets_js)}]
            }},
            options: {{
                responsive: true,
                maintainAspectRatio: false,
                scales: {{
                    y: {{
                        beginAtZero: true,
                        title: {{ display: true, text: 'Value' }}
                    }},
                    x: {{
                        title: {{ display: true, text: 'Timestamp' }}
                    }}
                }},
                plugins: {{
                    legend: {{ position: 'right' }}
                }}
            }}
        }});
    </script>
</body>
</html>
"""

  with open(output_path, 'w', encoding='utf-8') as f:
    f.write(html_content)

  print(f'Visualization successfully saved to {output_path}')
  print(
      f'Open {output_path} in your web browser to view the interactive chart!')


def main():
  args = parse_args()

  if not os.path.exists(args.input):
    print(f"Error: Input file '{args.input}' not found.")
    sys.exit(1)

  timestamps, plot_data, metrics_to_plot = parse_uma_data(args.input)
  generate_html_chart(timestamps, plot_data, metrics_to_plot, args.output)


if __name__ == '__main__':
  main()
