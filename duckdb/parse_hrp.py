import sys
import struct
import os
import duckdb
import pandas as pd
import heapq

def parse_hrperf_log(perf_log_path, max_frequency_ghz):
    # Updated struct format to match the new log structure
    entry_format = 'iqQQQQQ'  # Adjusted for the new fields
    entry_size = struct.calcsize(entry_format)

    # Dictionary to store the per-core data entries
    per_core_data = {}

    tsc_per_us = max_frequency_ghz * 1000  # TSC increments per microsecond

    # First pass: Read the log file and split data into per-core streams
    with open(perf_log_path, 'rb') as f:
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

    # List to collect processed data for all cores
    processed_data = []

    # Initialize data structures for node memory bandwidth computation
    heap = []
    iterators = {}
    last_state = {}
    per_core_memory_bandwidth = {}
    total_memory_bandwidth_events = []
    total_memory_bandwidth = 0.0  # Initialize total memory bandwidth
    last_global_timestamp_ns = None  # Keep track of last global timestamp

    # Initialize iterators and heap
    for cpu_id, data_list in per_core_data.items():
        # Sort each per-core data list by timestamp (should already be sorted)
        data_list.sort(key=lambda x: x['timestamp_ns'])
        iterators[cpu_id] = iter(data_list)
        first_item = next(iterators[cpu_id], None)
        if first_item:
            heapq.heappush(heap, (first_item['timestamp_ns'], cpu_id, first_item))
            last_state[cpu_id] = None
            per_core_memory_bandwidth[cpu_id] = 0.0  # Initialize per-core memory bandwidth

    # Process events in timestamp order across all cores
    unique_id_perf = 1  # Unique ID for performance_events
    unique_id_node_bw = 1  # Unique ID for node_memory_bandwidth

    while heap:
        # Get the earliest timestamp event
        current_timestamp_ns, cpu_id, current_entry = heapq.heappop(heap)

        # Get the next item from the same CPU and push it into the heap
        next_item = next(iterators[cpu_id], None)
        if next_item:
            heapq.heappush(heap, (next_item['timestamp_ns'], cpu_id, next_item))

        # Retrieve the previous state for this CPU
        state = last_state[cpu_id]
        if state is None:
            # Initialize the first record for this CPU
            last_state[cpu_id] = {
                'last_timestamp_ns': current_entry['timestamp_ns'],
                'last_stall_mem': current_entry['stall_mem'],
                'last_inst_retire': current_entry['inst_retire'],
                'last_cpu_unhalt': current_entry['cpu_unhalt'],
                'last_llc_misses': current_entry['llc_misses'],
                'last_sw_prefetch': current_entry['sw_prefetch']
            }
            continue  # Skip the first record for accurate calculations

        # Calculate time delta for this core
        time_delta_ns = current_entry['timestamp_ns'] - state['last_timestamp_ns']
        time_delta_us = time_delta_ns / 1e3  # Convert ns to us

        if time_delta_us <= 0:
            # Skip invalid time intervals
            last_state[cpu_id] = {
                'last_timestamp_ns': current_entry['timestamp_ns'],
                'last_stall_mem': current_entry['stall_mem'],
                'last_inst_retire': current_entry['inst_retire'],
                'last_cpu_unhalt': current_entry['cpu_unhalt'],
                'last_llc_misses': current_entry['llc_misses'],
                'last_sw_prefetch': current_entry['sw_prefetch']
            }
            continue

        # Compute differences
        stall_mem_diff = current_entry['stall_mem'] - state['last_stall_mem']
        inst_retire_diff = current_entry['inst_retire'] - state['last_inst_retire']
        cpu_unhalt_diff = current_entry['cpu_unhalt'] - state['last_cpu_unhalt']
        llc_misses_diff = current_entry['llc_misses'] - state['last_llc_misses']
        sw_prefetch_diff = current_entry['sw_prefetch'] - state['last_sw_prefetch']

        # Compute rates
        stalls_per_us = stall_mem_diff / time_delta_us
        inst_retire_rate = inst_retire_diff / time_delta_us
        cpu_usage = cpu_unhalt_diff / (tsc_per_us * time_delta_us)
        llc_misses_rate = llc_misses_diff / time_delta_us
        sw_prefetch_rate = sw_prefetch_diff / time_delta_us
        memory_bandwidth = (llc_misses_rate + sw_prefetch_rate) * 64  # in bytes per us

        # Update per-core memory bandwidth
        old_memory_bandwidth_core = per_core_memory_bandwidth[cpu_id]
        per_core_memory_bandwidth[cpu_id] = memory_bandwidth

        # Update total memory bandwidth
        total_memory_bandwidth = total_memory_bandwidth - old_memory_bandwidth_core + memory_bandwidth

        # Record performance event
        perf_entry = {
            'id': unique_id_perf,
            'cpu_id': cpu_id,
            'timestamp_ns': current_entry['timestamp_ns'],
            'stalls_per_us': stalls_per_us,
            'inst_retire_rate': inst_retire_rate,
            'cpu_usage': cpu_usage,
            'llc_misses_rate': llc_misses_rate,
            'sw_prefetch_rate': sw_prefetch_rate,
            'memory_bandwidth_bytes_per_us': memory_bandwidth,
            'time_delta_ns': time_delta_ns
        }
        processed_data.append(perf_entry)
        unique_id_perf += 1

        # Update node memory bandwidth events if timestamp has advanced
        if last_global_timestamp_ns is not None and current_timestamp_ns > last_global_timestamp_ns:
            node_bw_entry = {
                'id': unique_id_node_bw,
                'start_time_ns': last_global_timestamp_ns,
                'end_time_ns': current_timestamp_ns,
                'memory_bandwidth_bytes_per_us': total_memory_bandwidth
            }
            total_memory_bandwidth_events.append(node_bw_entry)
            unique_id_node_bw += 1

        # Update last global timestamp
        last_global_timestamp_ns = current_timestamp_ns if last_global_timestamp_ns is None else max(last_global_timestamp_ns, current_timestamp_ns)

        # Update last state for this CPU
        last_state[cpu_id] = {
            'last_timestamp_ns': current_entry['timestamp_ns'],
            'last_stall_mem': current_entry['stall_mem'],
            'last_inst_retire': current_entry['inst_retire'],
            'last_cpu_unhalt': current_entry['cpu_unhalt'],
            'last_llc_misses': current_entry['llc_misses'],
            'last_sw_prefetch': current_entry['sw_prefetch']
        }

    # Convert processed data to DataFrames
    df_performance_events = pd.DataFrame(processed_data)

    df_node_memory_bandwidth = pd.DataFrame(total_memory_bandwidth_events)

    # Convert data types
    df_performance_events = df_performance_events.astype({
        'id': 'int64',
        'cpu_id': 'int32',
        'timestamp_ns': 'int64',
        'stalls_per_us': 'float64',
        'inst_retire_rate': 'float64',
        'cpu_usage': 'float64',
        'llc_misses_rate': 'float64',
        'sw_prefetch_rate': 'float64',
        'memory_bandwidth_bytes_per_us': 'float64',
        'time_delta_ns': 'int64'
    })

    df_node_memory_bandwidth = df_node_memory_bandwidth.astype({
        'id': 'int64',
        'start_time_ns': 'int64',
        'end_time_ns': 'int64',
        'memory_bandwidth_bytes_per_us': 'float64'
    })

    # Connect to the 'analysis.duckdb' database
    con = duckdb.connect(database='analysis.duckdb')

    # Create 'performance_events' table if it doesn't exist
    con.execute('''
        CREATE TABLE IF NOT EXISTS performance_events (
            id BIGINT,
            cpu_id INTEGER,
            timestamp_ns BIGINT,
            stalls_per_us DOUBLE,
            inst_retire_rate DOUBLE,
            cpu_usage DOUBLE,
            llc_misses_rate DOUBLE,
            sw_prefetch_rate DOUBLE,
            memory_bandwidth_bytes_per_us DOUBLE,
            time_delta_ns BIGINT
        )
    ''')

    # Create 'node_memory_bandwidth' table if it doesn't exist
    con.execute('''
        CREATE TABLE IF NOT EXISTS node_memory_bandwidth (
            id BIGINT,
            start_time_ns BIGINT,
            end_time_ns BIGINT,
            memory_bandwidth_bytes_per_us DOUBLE
        )
    ''')

    # Register DataFrames and insert data
    con.register('df_performance_events', df_performance_events)
    con.register('df_node_memory_bandwidth', df_node_memory_bandwidth)

    con.execute('''
        INSERT INTO performance_events
        SELECT * FROM df_performance_events
    ''')

    con.execute('''
        INSERT INTO node_memory_bandwidth
        SELECT * FROM df_node_memory_bandwidth
    ''')

    # Close the connection
    con.close()

    print("Processed performance data has been inserted into 'performance_events' table in 'analysis.duckdb'.")
    print("Node memory bandwidth data has been inserted into 'node_memory_bandwidth' table in 'analysis.duckdb'.")

def main():
    if len(sys.argv) != 3:
        print('Expected usage: python parse_hrperf_log.py <perf_log_path> <max_frequency_ghz>')
        sys.exit(1)

    perf_log_path = sys.argv[1]
    max_frequency_ghz = float(sys.argv[2])

    if not os.path.isfile(perf_log_path):
        print(f"Error: File '{perf_log_path}' does not exist.")
        sys.exit(1)

    parse_hrperf_log(perf_log_path, max_frequency_ghz)

if __name__ == "__main__":
    main()
