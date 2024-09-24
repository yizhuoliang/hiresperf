import struct
import os
import heapq

def parse_hrperf_log(file_path, max_frequency_ghz):
    # Updated struct format to match the new log structure
    entry_format = 'iqQQQQQ'  # Adjusted for the new fields
    entry_size = struct.calcsize(entry_format)

    # Dictionary to store the per-core data entries
    per_core_data = {}

    tsc_per_us = max_frequency_ghz * 1000  # TSC increments per microsecond

    # First pass: Read the log file and split data into per-core streams
    with open(file_path, 'rb') as f:
        while True:
            data = f.read(entry_size)
            if not data:
                break

            # Unpack the data according to the updated structure
            cpu_id, ktime, stall_mem, inst_retire, cpu_unhalt, llc_misses, sw_prefetch = struct.unpack(entry_format, data)

            if cpu_id not in per_core_data:
                per_core_data[cpu_id] = []

            per_core_data[cpu_id].append({
                'cpu_id': cpu_id,
                'timestamp_ns': ktime,
                'stall_mem': stall_mem,
                'inst_retire': inst_retire,
                'cpu_unhalt': cpu_unhalt,
                'llc_misses': llc_misses,
                'sw_prefetch': sw_prefetch
            })

    # Dictionary to store the last state for each CPU
    last_state = {}
    perf_data_per_core = {}

    # Data structures for total memory bandwidth computation
    per_core_memory_bandwidth = {}  # Current memory bandwidth per core
    total_memory_bandwidth = 0

    # Initialize heap for merging per-core data streams
    heap = []
    iterators = {}
    for cpu_id, data_list in per_core_data.items():
        # Sort each per-core data list by timestamp (should already be sorted, but to be safe)
        data_list.sort(key=lambda x: x['timestamp_ns'])
        iterators[cpu_id] = iter(data_list)
        first_item = next(iterators[cpu_id], None)
        if first_item:
            heapq.heappush(heap, (first_item['timestamp_ns'], cpu_id, first_item))

        # Initialize per-core structures
        last_state[cpu_id] = None
        perf_data_per_core[cpu_id] = []
        per_core_memory_bandwidth[cpu_id] = 0

    # Open file to write total memory bandwidth over time
    total_memory_bandwidth_file = 'total_memory_bandwidth.bin'
    with open(total_memory_bandwidth_file, 'wb') as total_bw_file:
        last_global_timestamp = None

        while heap:
            # Get the earliest timestamp event
            current_timestamp_ns, cpu_id, current_entry = heapq.heappop(heap)

            # Get the next item from the same CPU and push it into the heap
            next_item = next(iterators[cpu_id], None)
            if next_item:
                heapq.heappush(heap, (next_item['timestamp_ns'], cpu_id, next_item))

            # Retrieve the previous state
            state = last_state[cpu_id]
            if state is None:
                # Initialize the first record for this CPU
                last_state[cpu_id] = {
                    'first_ktime': current_entry['timestamp_ns'],
                    'last_ktime': current_entry['timestamp_ns'],
                    'last_stall_mem': current_entry['stall_mem'],
                    'last_inst_retire': current_entry['inst_retire'],
                    'last_cpu_unhalt': current_entry['cpu_unhalt'],
                    'last_llc_misses': current_entry['llc_misses'],
                    'last_sw_prefetch': current_entry['sw_prefetch']
                }
                per_core_memory_bandwidth[cpu_id] = 0
                per_core_last_timestamp = current_entry['timestamp_ns']
                continue  # Skip the first record for accurate calculations

            # Calculate time deltas
            time_delta_ns = current_entry['timestamp_ns'] - state['last_ktime']
            time_delta_us = time_delta_ns / 1e3  # Convert ns to us

            if time_delta_us <= 0:
                # Skip invalid time intervals
                continue

            # Calculate per-core metrics
            stall_mem_since_last = current_entry['stall_mem'] - state['last_stall_mem']
            inst_retire_since_last = current_entry['inst_retire'] - state['last_inst_retire']
            cpu_unhalt_since_last = current_entry['cpu_unhalt'] - state['last_cpu_unhalt']
            llc_misses_since_last = current_entry['llc_misses'] - state['last_llc_misses']
            sw_prefetch_since_last = current_entry['sw_prefetch'] - state['last_sw_prefetch']

            stalls_per_us = stall_mem_since_last / time_delta_us
            inst_retire_rate = inst_retire_since_last / time_delta_us
            cpu_usage = cpu_unhalt_since_last / (tsc_per_us * time_delta_us)

            llc_misses_rate = llc_misses_since_last / time_delta_us
            sw_prefetch_rate = sw_prefetch_since_last / time_delta_us
            memory_bandwidth = (llc_misses_rate + sw_prefetch_rate) * 64  # in bytes per microsecond

            # Prepare the performance data entry
            perf_entry = {
                'timestamp_ns': current_entry['timestamp_ns'],
                'stalls_per_us': stalls_per_us,
                'inst_retire_rate': inst_retire_rate,
                'cpu_usage': cpu_usage,
                'llc_misses_rate': llc_misses_rate,
                'sw_prefetch_rate': sw_prefetch_rate,
                'memory_bandwidth_bytes_per_us': memory_bandwidth,
                'time_delta_ns': time_delta_ns
            }
            perf_data_per_core[cpu_id].append(perf_entry)

            # --- Total memory bandwidth computation ---
            # Update total memory bandwidth
            old_memory_bandwidth_core = per_core_memory_bandwidth[cpu_id]
            per_core_memory_bandwidth[cpu_id] = memory_bandwidth
            total_memory_bandwidth = total_memory_bandwidth - old_memory_bandwidth_core + memory_bandwidth

            # Calculate delta_time since last_global_timestamp
            if last_global_timestamp is not None and current_timestamp_ns > last_global_timestamp:
                delta_time_ns = current_timestamp_ns - last_global_timestamp

                if delta_time_ns > 0:
                    # Write the interval and total memory bandwidth to the file
                    # We can write: start_timestamp_ns, end_timestamp_ns, total_memory_bandwidth_bytes_per_us
                    data_bytes = struct.pack(
                        '<QQf',
                        last_global_timestamp,
                        current_timestamp_ns,
                        total_memory_bandwidth  # in bytes per microsecond
                    )
                    total_bw_file.write(data_bytes)

            # Update last_global_timestamp
            last_global_timestamp = current_timestamp_ns

            # Update the last state for this CPU
            last_state[cpu_id].update({
                'last_ktime': current_entry['timestamp_ns'],
                'last_stall_mem': current_entry['stall_mem'],
                'last_inst_retire': current_entry['inst_retire'],
                'last_cpu_unhalt': current_entry['cpu_unhalt'],
                'last_llc_misses': current_entry['llc_misses'],
                'last_sw_prefetch': current_entry['sw_prefetch']
            })

    # Save the performance data per core to files
    if not os.path.exists('core_performance_data'):
        os.makedirs('core_performance_data')

    for cpu_id, perf_entries in perf_data_per_core.items():
        filename = f'core_performance_data/core_{cpu_id}_perf_data.bin'
        with open(filename, 'wb') as f:
            for entry in perf_entries:
                # Serialize the data
                data_bytes = struct.pack(
                    '<Q f f f f f f Q',
                    entry['timestamp_ns'],
                    entry['stalls_per_us'],
                    entry['inst_retire_rate'],
                    entry['cpu_usage'],
                    entry['llc_misses_rate'],
                    entry['sw_prefetch_rate'],
                    entry['memory_bandwidth_bytes_per_us'],
                    entry['time_delta_ns']
                )
                f.write(data_bytes)

    print("Memory bandwidth and performance data per core have been saved in 'core_performance_data' directory.")
    print(f"Total memory bandwidth over time has been saved in '{total_memory_bandwidth_file}'.")

if __name__ == "__main__":
    max_frequency_ghz = float(input("Enter the maximum CPU frequency in GHz (e.g., 2.5): "))
    perf_log_path = input("Enter the path to the performance log file (e.g., 'hrperf_log.bin'): ").strip()
    parse_hrperf_log(perf_log_path, max_frequency_ghz)
