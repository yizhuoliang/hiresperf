import os
import json
import pickle
import struct
from concurrent.futures import ProcessPoolExecutor, as_completed
from multiprocessing import Manager

def load_performance_data():
    # Load performance data from 'core_performance_data' directory into memory
    perf_data_per_core = {}
    perf_data_files = [f for f in os.listdir('core_performance_data') if f.startswith('core_') and f.endswith('_perf_data.bin')]
    for filename in perf_data_files:
        cpu_id = int(filename.split('_')[1])
        with open(os.path.join('core_performance_data', filename), 'rb') as f:
            entry_size = struct.calcsize('<Q f f f f f f Q')
            file_data = f.read()
            entries = []
            for i in range(0, len(file_data), entry_size):
                data = file_data[i:i+entry_size]
                entries.append(struct.unpack('<Q f f f f f f Q', data))
            perf_data_per_core[cpu_id] = [{
                'timestamp_ns': entry[0],
                'stalls_per_us': entry[1],
                'inst_retire_rate': entry[2],
                'cpu_usage': entry[3],
                'llc_misses_rate': entry[4],
                'sw_prefetch_rate': entry[5],
                'memory_bandwidth_bytes_per_us': entry[6],
                'time_delta_ns': entry[7]
            } for entry in entries]
    return perf_data_per_core

def load_invocations(function_dir):
    # Load invocation data from a binary file
    invocations_file = os.path.join(function_dir, 'invocations.pkl')
    if not os.path.exists(invocations_file):
        print(f"Function invocations file '{invocations_file}' does not exist. Please run the identification script first.")
        return []

    with open(invocations_file, 'rb') as f:
        invocations = pickle.load(f)
    return invocations

def load_total_memory_bandwidth():
    # Load total memory bandwidth data from 'total_memory_bandwidth.bin' into memory
    total_bw_data = []
    with open('total_memory_bandwidth.bin', 'rb') as f:
        entry_size = struct.calcsize('<QQf')
        file_data = f.read()
        for i in range(0, len(file_data), entry_size):
            data = file_data[i:i+entry_size]
            entry = struct.unpack('<QQf', data)
            total_bw_data.append({
                'start_timestamp_ns': entry[0],
                'end_timestamp_ns': entry[1],
                'total_memory_bandwidth_bytes_per_us': entry[2]
            })
    return total_bw_data

def compute_metrics_for_invocation(inv, perf_data_per_core, scheduling_data_int, total_bw_data, total_bw_start_times, total_bw_end_times):
    thread_id = inv['thread_id']
    start_time_ns = inv['start_time_ns']
    end_time_ns = inv['end_time_ns']

    # Get scheduling intervals for this thread
    thread_scheduling = scheduling_data_int.get(thread_id, [])
    # Find the core(s) the thread was running on during the invocation
    cores_during_invocation = [
        core_number for core_number, sched_start_ns, sched_end_ns in thread_scheduling
        if sched_start_ns <= end_time_ns and sched_end_ns >= start_time_ns
    ]

    # Collect performance data for the cores during the invocation
    total_weight = 0
    weighted_stalls = 0
    weighted_cpu_usage = 0
    weighted_memory_bandwidth = 0
    weighted_inst_retire = 0  # Initialize the weighted sum for inst_retire_rate

    # Initialize list to store performance entries
    inv_performance_ticks = []

    for core_id in cores_during_invocation:
        perf_entries = perf_data_per_core.get(core_id, [])
        for entry in perf_entries:
            if entry['timestamp_ns'] > end_time_ns:
                break
            if entry['timestamp_ns'] >= start_time_ns:
                time_delta_ns = entry['time_delta_ns']
                weight = time_delta_ns
                total_weight += weight
                weighted_stalls += entry['stalls_per_us'] * weight
                weighted_cpu_usage += entry['cpu_usage'] * weight
                weighted_memory_bandwidth += entry['memory_bandwidth_bytes_per_us'] * weight
                weighted_inst_retire += entry['inst_retire_rate'] * weight  # Accumulate weighted inst_retire_rate

                # Copy the entry and add 'core_id' for reference
                entry_with_core = entry.copy()
                entry_with_core['core_id'] = core_id

                # Append the entry to inv_performance_ticks
                inv_performance_ticks.append(entry_with_core)

    if total_weight > 0:
        inv['avg_stalls_per_us'] = weighted_stalls / total_weight
        inv['avg_cpu_usage'] = weighted_cpu_usage / total_weight
        inv['avg_memory_bandwidth_bytes_per_us'] = weighted_memory_bandwidth / total_weight
        inv['avg_inst_retire_per_us'] = weighted_inst_retire / total_weight  # Compute average inst_retire_per_us

    # Add the collected performance entries to the invocation
    inv['performance_ticks'] = inv_performance_ticks

    # Now compute the average total memory bandwidth during the invocation's execution time
    # Use bisect to efficiently find overlapping intervals
    import bisect

    total_weight = 0
    weighted_total_memory_bandwidth = 0

    # Find the index range of intervals overlapping with the invocation
    idx_start = bisect.bisect_left(total_bw_end_times, start_time_ns)
    idx_end = bisect.bisect_right(total_bw_start_times, end_time_ns)

    for idx in range(idx_start, idx_end):
        interval = total_bw_data[idx]
        interval_start_ns = interval['start_timestamp_ns']
        interval_end_ns = interval['end_timestamp_ns']
        bw = interval['total_memory_bandwidth_bytes_per_us']

        # Compute overlap
        overlap_start_ns = max(interval_start_ns, start_time_ns)
        overlap_end_ns = min(interval_end_ns, end_time_ns)
        if overlap_start_ns >= overlap_end_ns:
            continue  # No overlap

        duration_ns = overlap_end_ns - overlap_start_ns
        weight = duration_ns
        total_weight += weight
        weighted_total_memory_bandwidth += bw * weight

    if total_weight > 0:
        avg_total_memory_bandwidth = weighted_total_memory_bandwidth / total_weight
        inv['avg_total_memory_bandwidth_bytes_per_us_total'] = avg_total_memory_bandwidth
    else:
        inv['avg_total_memory_bandwidth_bytes_per_us_total'] = 0  # or None

def compute_invocation_metrics(function_name):
    function_dir = "analysis_" + function_name.replace(' ', '_').replace('(', '').replace(')', '').replace(':', '_').replace('.', '_')
    invocations = load_invocations(function_dir)
    if not invocations:
        return

    perf_data_per_core = load_performance_data()

    # Load the scheduling data
    scheduling_json_path = input("Enter the path to the scheduling JSON file: ").strip()
    with open(scheduling_json_path, 'r') as f:
        scheduling_data = json.load(f)
    scheduling_data_int = {}
    for thread_id_str, intervals in scheduling_data.items():
        thread_id = int(thread_id_str)
        intervals_sorted = sorted(intervals, key=lambda x: x[1])
        intervals_ns = [(int(interval[0]), int(interval[1] * 1000), int(interval[2] * 1000)) for interval in intervals_sorted]
        scheduling_data_int[thread_id] = intervals_ns

    # Load total memory bandwidth data
    total_bw_data = load_total_memory_bandwidth()
    total_bw_start_times = [interval['start_timestamp_ns'] for interval in total_bw_data]
    total_bw_end_times = [interval['end_timestamp_ns'] for interval in total_bw_data]

    num_processes = 36
    # Split the work
    base_chunk_size = len(invocations) // num_processes
    remainder = len(invocations) % num_processes

    # Create chunks
    chunks = []
    start = 0
    for i in range(num_processes):
        # If there's a remainder, add 1 more item to the chunks until it runs out
        end = start + base_chunk_size + (i < remainder)
        chunks.append(invocations[start:end])
        start = end

    # Set up progress reporting
    with Manager() as manager:
        progress = manager.dict()
        with ProcessPoolExecutor(max_workers=num_processes) as executor:
            futures = [executor.submit(process_chunk, chunk, perf_data_per_core, scheduling_data_int,
                                       total_bw_data, total_bw_start_times, total_bw_end_times, progress, i) for i, chunk in enumerate(chunks)]

            total_invocations = len(invocations)
            processed_invocations = 0
            import time

            # Periodically report progress
            while processed_invocations < total_invocations:
                time.sleep(5)  # Update every 5 seconds
                processed_invocations = sum(progress.values())
                print(f"Progress: {processed_invocations}/{total_invocations} invocations processed")

            invocation_metrics = []
            for future in as_completed(futures):
                invocation_metrics.extend(future.result())

            # Save the invocation metrics
            with open(os.path.join(function_dir, 'invocation_metrics.pkl'), 'wb') as f:
                pickle.dump(invocation_metrics, f)

            print(f"Computed averaged performance metrics for {len(invocation_metrics)} invocations.")

def process_chunk(invocations_chunk, perf_data_per_core, scheduling_data_int, total_bw_data, total_bw_start_times, total_bw_end_times, progress, index):
    results = []
    for inv in invocations_chunk:
        compute_metrics_for_invocation(inv, perf_data_per_core, scheduling_data_int, total_bw_data, total_bw_start_times, total_bw_end_times)
        results.append(inv)
    progress[index] = len(invocations_chunk)
    return results

if __name__ == "__main__":
    function_name = input("Enter the function name (e.g., 'process_chunk'): ").strip()
    compute_invocation_metrics(function_name)
