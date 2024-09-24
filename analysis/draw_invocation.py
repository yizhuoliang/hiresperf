import os
import pickle
import sys
import datetime
from collections import defaultdict
import svgwrite

# Event types
EVENT_STACK_SAMPLE = 1
EVENT_MUTEX_WAIT = 6
EVENT_MUTEX_LOCK = 7
EVENT_MUTEX_UNLOCK = 8
EVENT_JOIN_WAIT = 9
EVENT_JOIN_JOINED = 10

def sort_invocations(invocation_metrics, metric_name):

    # We will filter out any invocations that do not contain all necessary metrics
    required_metrics = ['latency_us', 'avg_cpu_usage', 'avg_memory_bandwidth_bytes_per_us', 'avg_stalls_per_us', 'avg_inst_retire_per_us']
    valid_invocations = [inv for inv in invocation_metrics if all(key in inv for key in required_metrics)]

    # Map input metric name to the correct key in the dictionary
    metric_key_map = {
        'cpu_use': 'avg_cpu_usage',
        'mem_bandwidth': 'avg_memory_bandwidth_bytes_per_us',
        'latencies': 'latency_us',
        'stalls_rate': 'avg_stalls_per_us',
        'inst_retire': 'avg_inst_retire_per_us'  # Added new metric
    }

    metric_key = metric_key_map.get(metric_name)
    if not metric_key:
        print("Invalid metric name provided.")
        return None

    # Sort invocations by the specified metric
    sorted_invocations = sorted(valid_invocations, key=lambda x: x[metric_key])

    return sorted_invocations

def draw_flamegraph(invocation, output_svg_path):
    import os

    events = invocation['events']
    thread_id = invocation['thread_id']

    # Sort events by timestamp
    events.sort(key=lambda e: e['timestamp_ns'])

    function_intervals = []
    mutex_events = []
    join_events = []

    # Process events
    for event in events:
        timestamp_ns = event['timestamp_ns']
        event_type = event['event_type']

        if event_type == EVENT_STACK_SAMPLE:
            # Calculate start time using latency
            latency_ns = int(event['latency_us'] * 1000)
            start_time_ns = timestamp_ns - latency_ns
            function_desc = event.get('function_desc', '???')

            # Create function interval
            function_interval = {
                'start_time_ns': start_time_ns,
                'end_time_ns': timestamp_ns,
                'function_desc': function_desc
            }
            function_intervals.append(function_interval)

        elif event_type in [EVENT_MUTEX_WAIT, EVENT_MUTEX_LOCK, EVENT_MUTEX_UNLOCK]:
            mutex_event = {
                'timestamp_ns': timestamp_ns,
                'event_type': event_type,
                'arg1': event['arg1'],  # Mutex ID
                'thread_id': thread_id
            }
            mutex_events.append(mutex_event)

        elif event_type in [EVENT_JOIN_WAIT, EVENT_JOIN_JOINED]:
            join_event = {
                'timestamp_ns': timestamp_ns,
                'event_type': event_type,
                'arg1': event['arg1'],  # Thread ID being joined
                'thread_id': thread_id
            }
            join_events.append(join_event)

    # Assign depths to function intervals based on overlapping times
    function_intervals.sort(key=lambda x: x['start_time_ns'])
    active_intervals = []
    max_depth = 0
    target_func_appeared = False

    # Exclude events that start earlier than the analyzed function itself
    filtered_intervals = []
    for interval in function_intervals:
        if not target_func_appeared and interval['function_desc'] != invocation["function_desc"]:
            continue
        else:
            target_func_appeared = True
            filtered_intervals.append(interval)
    function_intervals = filtered_intervals  # Keep the name 'function_intervals'

    # Assigning depths to the intervals
    for interval in function_intervals:
        # Remove intervals that have ended
        active_intervals = [i for i in active_intervals if i['end_time_ns'] > interval['start_time_ns']]
        # Depth is current number of active intervals
        depth = len(active_intervals)
        interval['depth'] = depth
        # Update max_depth
        if depth > max_depth:
            max_depth = depth
        # Add to active intervals
        active_intervals.append(interval)

    # Determine total duration
    total_duration_ns = invocation['end_time_ns'] - invocation['start_time_ns']

    # Set up SVG dimensions
    width = 1000
    height_per_level = 20
    margin_top = 100  # Increased top margin to accommodate performance metrics
    margin_bottom = 50  # Increased bottom margin
    margin_left = 20
    margin_right = 20

    # Calculate the maximum depth
    existing_total_height = (max_depth + 1) * height_per_level + margin_top + margin_bottom + 100  # Extra space for mutex/join events

    # Performance metrics plotting parameters
    metric_plot_height = 100
    metric_plot_margin = 50

    # Updated metrics_to_plot to include 'inst_retire_per_us'
    metrics_to_plot = [
        {'name': 'cpu_usage', 'label': 'CPU Usage', 'color': 'red'},
        {'name': 'memory_bandwidth_bytes_per_us', 'label': 'Memory Bandwidth (bytes/us)', 'color': 'blue'},
        {'name': 'stalls_per_us', 'label': 'Stalls per us', 'color': 'purple'},
        {'name': 'inst_retire_rate', 'label': 'Instructions Retired per us', 'color': 'green'}  # Added new metric
    ]
    num_metrics = len(metrics_to_plot)
    perf_metrics_total_height = num_metrics * (metric_plot_height + metric_plot_margin)

    total_height = existing_total_height + perf_metrics_total_height

    # Start building the SVG content
    svg_lines = []
    svg_lines.append(f'<svg xmlns="http://www.w3.org/2000/svg" version="1.1" width="{width + margin_left + margin_right}" height="{total_height}">')

    # Add performance metrics at the top
    metrics_text_y = 20
    metrics = [
        f"Latency: {invocation.get('latency_us', 'N/A')} us",
        f"Average CPU Usage: {invocation.get('avg_cpu_usage', 'N/A')}",
        f"Average Memory Bandwidth: {invocation.get('avg_memory_bandwidth_bytes_per_us', 'N/A')} bytes/us",
        f"Average Stalls per us: {invocation.get('avg_stalls_per_us', 'N/A')}",
        f"Average Instructions Retired per us: {invocation.get('avg_inst_retire_per_us', 'N/A')}"  # Added new metric
    ]
    for metric in metrics:
        svg_lines.append(f'<text x="{margin_left}" y="{metrics_text_y}" font-size="14px" fill="black">{metric}</text>')
        metrics_text_y += 18  # Line spacing

    # Function to map timestamp to x-coordinate
    def time_to_x(timestamp_ns):
        return margin_left + (timestamp_ns - invocation['start_time_ns']) / total_duration_ns * width

    # Reverse the depth for drawing (shallowest at bottom)
    for interval in function_intervals:
        interval['depth'] = max_depth - interval['depth']

    # Draw function intervals
    for interval in function_intervals:
        start_x = time_to_x(interval['start_time_ns'])
        end_x = time_to_x(interval['end_time_ns'])
        depth = interval['depth']
        y = margin_top + depth * height_per_level
        rect_height = height_per_level - 2
        # Use a minimum width to ensure very short intervals are visible
        min_width = 0.5
        rect_width = max(end_x - start_x, min_width)

        # Escape special characters in function description
        func_desc_escaped = interval["function_desc"].replace('&', '&amp;').replace('<', '&lt;').replace('>', '&gt;')

        # Create a rectangle for the function interval with title
        rect_svg = f'<rect x="{start_x}" y="{y}" width="{rect_width}" height="{rect_height}" fill="lightblue" stroke="black">'
        rect_svg += f'<title>{func_desc_escaped}</title>'
        rect_svg += '</rect>'

        svg_lines.append(rect_svg)

    # Draw mutex events timeline with more spacing
    mutex_timeline_y = margin_top + (max_depth + 1) * height_per_level + 30
    # Draw the line
    svg_lines.append(f'<line x1="{margin_left}" y1="{mutex_timeline_y}" x2="{width + margin_left}" y2="{mutex_timeline_y}" stroke="black"/>')
    # Add label
    svg_lines.append(f'<text x="{margin_left}" y="{mutex_timeline_y - 10}" font-size="12px" fill="black">Mutex Events</text>')

    # Process mutex events to find wait and lock periods
    mutex_waits = {}
    for event in mutex_events:
        timestamp_ns = event['timestamp_ns']
        event_type = event['event_type']
        mutex_id = event['arg1']
        if event_type == EVENT_MUTEX_WAIT:
            mutex_waits[mutex_id] = timestamp_ns
        elif event_type == EVENT_MUTEX_LOCK:
            if mutex_id in mutex_waits:
                start_ns = mutex_waits[mutex_id]
                end_ns = timestamp_ns
                # Draw the mutex wait period
                start_x = time_to_x(start_ns)
                end_x = time_to_x(end_ns)
                rect_width = max(end_x - start_x, min_width)
                rect_svg = f'<rect x="{start_x}" y="{mutex_timeline_y - 5}" width="{rect_width}" height="10" fill="orange" stroke="black"/>'
                svg_lines.append(rect_svg)
                del mutex_waits[mutex_id]
    # Handle any unmatched waits
    for mutex_id, start_ns in mutex_waits.items():
        start_x = time_to_x(start_ns)
        end_x = time_to_x(invocation['end_time_ns'])
        rect_width = max(end_x - start_x, min_width)
        rect_svg = f'<rect x="{start_x}" y="{mutex_timeline_y - 5}" width="{rect_width}" height="10" fill="orange" stroke="black"/>'
        svg_lines.append(rect_svg)

    # Draw join events timeline with more spacing
    join_timeline_y = mutex_timeline_y + 50
    # Draw the line
    svg_lines.append(f'<line x1="{margin_left}" y1="{join_timeline_y}" x2="{width + margin_left}" y2="{join_timeline_y}" stroke="black"/>')
    # Add label
    svg_lines.append(f'<text x="{margin_left}" y="{join_timeline_y - 10}" font-size="12px" fill="black">Thread Join Events</text>')

    # Process join events to find wait and joined periods
    join_waits = {}
    for event in join_events:
        timestamp_ns = event['timestamp_ns']
        event_type = event['event_type']
        thread_join_id = event['arg1']
        if event_type == EVENT_JOIN_WAIT:
            join_waits[thread_join_id] = timestamp_ns
        elif event_type == EVENT_JOIN_JOINED:
            if thread_join_id in join_waits:
                start_ns = join_waits[thread_join_id]
                end_ns = timestamp_ns
                # Draw the join wait period
                start_x = time_to_x(start_ns)
                end_x = time_to_x(end_ns)
                rect_width = max(end_x - start_x, min_width)
                rect_svg = f'<rect x="{start_x}" y="{join_timeline_y - 5}" width="{rect_width}" height="10" fill="green" stroke="black"/>'
                svg_lines.append(rect_svg)
                del join_waits[thread_join_id]
    # Handle any unmatched waits
    for thread_join_id, start_ns in join_waits.items():
        start_x = time_to_x(start_ns)
        end_x = time_to_x(invocation['end_time_ns'])
        rect_width = max(end_x - start_x, min_width)
        rect_svg = f'<rect x="{start_x}" y="{join_timeline_y - 5}" width="{rect_width}" height="10" fill="green" stroke="black"/>'
        svg_lines.append(rect_svg)

    # Now plot the performance metrics timelines
    # Extract performance entries
    performance_ticks = invocation.get('performance_ticks', [])

    perf_metrics_y_start = join_timeline_y + 80

    for i, metric_info in enumerate(metrics_to_plot):
        metric_name = metric_info['name']
        metric_label = metric_info['label']
        metric_color = metric_info['color']

        metric_plot_y = perf_metrics_y_start + i * (metric_plot_height + metric_plot_margin)

        # Collect data
        performance_ticks_sorted = sorted(performance_ticks, key=lambda x: x['timestamp_ns'])
        timestamps = [invocation['start_time_ns']]  # Start with invocation start time
        values = []

        # For the values, create intervals between timestamps
        for entry in performance_ticks_sorted:
            timestamps.append(entry['timestamp_ns'])
            values.append(entry.get(metric_name, 0))

        timestamps.append(invocation['end_time_ns'])  # End with invocation end time

        # Determine min and max values for scaling
        if values:
            min_value = min(values)
            max_value = max(values)
        else:
            min_value = 0
            max_value = 1  # Avoid division by zero

        # Handle case where min_value == max_value
        if max_value == min_value:
            max_value += 1e-6  # Add a small value to avoid division by zero

        # Draw the baseline
        svg_lines.append(f'<line x1="{margin_left}" y1="{metric_plot_y + metric_plot_height}" x2="{width + margin_left}" y2="{metric_plot_y + metric_plot_height}" stroke="black"/>')
        # Add label and min/max values with increased spacing
        svg_lines.append(f'<text x="{margin_left}" y="{metric_plot_y - 10}" font-size="12px" fill="black">{metric_label}</text>')
        svg_lines.append(f'<text x="{margin_left}" y="{metric_plot_y + metric_plot_height + 25}" font-size="10px" fill="black">Min: {min_value}</text>')
        svg_lines.append(f'<text x="{width + margin_left - 50}" y="{metric_plot_y + metric_plot_height + 25}" font-size="10px" fill="black" text-anchor="end">Max: {max_value}</text>')

        # Plot the values as histograms (bars)
        for j in range(len(values)):
            start_time_ns = timestamps[j]
            end_time_ns = timestamps[j + 1]
            value = values[j]

            start_x = time_to_x(start_time_ns)
            end_x = time_to_x(end_time_ns)
            rect_width = max(end_x - start_x, min_width)
            # Map value to bar height
            bar_height = ((value - min_value) / (max_value - min_value)) * metric_plot_height
            y = metric_plot_y + metric_plot_height - bar_height  # Top y-coordinate of the bar

            rect_svg = f'<rect x="{start_x}" y="{y}" width="{rect_width}" height="{bar_height}" fill="{metric_color}" stroke="none"/>'
            svg_lines.append(rect_svg)

    # Close the SVG
    svg_lines.append('</svg>')

    # Write the SVG code to the output file
    with open(output_svg_path, 'w') as f:
        f.write('\n'.join(svg_lines))

    print(f"Flamegraph saved to {output_svg_path}")

def main():
    function_name = input("Enter the function name (e.g., 'process_chunk'): ").strip()
    metric_name = input("Enter the metric by which to sort the invocations (cpu_use, mem_bandwidth, latencies, stalls_rate, inst_retire): ").strip()

    # Load the invocations
    invocation_metrics_path = os.path.join(f"analysis_{function_name}", "invocation_metrics.pkl")
    if not os.path.exists(invocation_metrics_path):
        print(f"Invocation metrics file '{invocation_metrics_path}' does not exist. Please run the metrics computation script first.")
        return None

    with open(invocation_metrics_path, 'rb') as f:
        invocation_metrics = pickle.load(f)

    # Do the sorting
    sorted_invocations = sort_invocations(invocation_metrics, metric_name)

    if sorted_invocations is None:
        return

    print(f"There are {len(sorted_invocations)} sorted invocations.")
    try:
        n = int(input("Enter the index of the invocation you want to draw (starting from 0): "))
        if n < 0 or n >= len(sorted_invocations):
            print("Invalid index number.")
        else:
            output_svg_path = os.path.join(f"analysis_{function_name}", f"{n}th_in_{metric_name}.svg")
            draw_flamegraph(sorted_invocations[n], output_svg_path)
            print("Completed.")
    except ValueError:
        print("Invalid input. Please enter a valid number.")

if __name__ == "__main__":
    main()
