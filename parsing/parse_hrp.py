import sys
import struct
import os
import duckdb
import pandas as pd
import heapq
import argparse
import re


def read_hrp_tsc_config(config_path="../src/config.h"):
    script_dir = os.path.dirname(os.path.abspath(__file__))
    config_path = os.path.join(script_dir, "..", "src", "config.h")
    config_path = os.path.abspath(config_path)
    with open(config_path, "r") as f:
        for line in f:
            # look for tsc flag
            match = re.search(r"#define\s+HRP_USE_TSC\s+(\d+)", line)
            if match:
                return int(match.group(1))
        # If no flag, fall back to original hireserf
        return 0
    
def read_hrp_use_offcore_config(config_path="../src/config.h"):
    script_dir = os.path.dirname(os.path.abspath(__file__))
    config_path = os.path.join(script_dir, "..", "src", "config.h")
    config_path = os.path.abspath(config_path)
    with open(config_path, "r") as f:
        for line in f:
            # look for offcore flag
            match = re.search(r"#define\s+HRP_USE_OFFCORE\s+(\d+)", line)
            if match:
                return int(match.group(1))
        # If no flag, fall back to original hireserf
        return 0


def parse_hrperf_log(perf_log_path, use_raw, use_tsc_ts, tsc_per_us, use_offcore):
    # Updated struct format to match the new log structure
    entry_format = "iqQQQQQ"  # Adjusted for the new fields
    entry_size = struct.calcsize(entry_format)

    # Dictionary to store the per-core data entries
    per_core_data = {}

    # tsc_per_us = max_frequency_ghz * 1000  # TSC increments per microsecond

    # First pass: Read the log file and split data into per-core streams
    with open(perf_log_path, "rb") as f:
        while True:
            data = f.read(entry_size)
            if not data:
                break
            if len(data) < entry_size:
                break

            # Unpack the data according to the updated structure
            (
                cpu_id,
                ktime,
                stall_mem,
                inst_retire,
                cpu_unhalt,
                llc_misses,
                sw_prefetch,
            ) = struct.unpack(entry_format, data)

            # If tsc used, convert tsc to ktime using frequency.
            # if use_tsc_ts:
            #     timestamp_ns = int(ktime / tsc_per_us * 1e3)
            # else:
            #     timestamp_ns = ktime

            if cpu_id not in per_core_data:
                per_core_data[cpu_id] = []

            per_core_data[cpu_id].append(
                {
                    "cpu_id": cpu_id,
                    "timestamp_ns": ktime,
                    "stall_mem": stall_mem,
                    "inst_retire": inst_retire,
                    "cpu_unhalt": cpu_unhalt,
                    "llc_misses": llc_misses,
                    "sw_prefetch": sw_prefetch,
                }
            )

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
        data_list.sort(key=lambda x: x["timestamp_ns"])
        iterators[cpu_id] = iter(data_list)
        first_item = next(iterators[cpu_id], None)
        if first_item:
            heapq.heappush(heap, (first_item["timestamp_ns"], cpu_id, first_item))
            last_state[cpu_id] = None
            per_core_memory_bandwidth[cpu_id] = (
                0.0  # Initialize per-core memory bandwidth
            )

    # Process events in timestamp order across all cores
    unique_id_perf = 1  # Unique ID for performance_events
    unique_id_node_bw = 1  # Unique ID for node_memory_bandwidth

    while heap:
        # Get the earliest timestamp event
        current_timestamp_ns, cpu_id, current_entry = heapq.heappop(heap)

        # Get the next item from the same CPU and push it into the heap
        next_item = next(iterators[cpu_id], None)
        if next_item:
            heapq.heappush(heap, (next_item["timestamp_ns"], cpu_id, next_item))

        # Retrieve the previous state for this CPU
        state = last_state[cpu_id]
        if state is None:
            # Initialize the first record for this CPU
            last_state[cpu_id] = {
                "last_timestamp_ns": current_entry["timestamp_ns"],
                "last_stall_mem": current_entry["stall_mem"],
                "last_inst_retire": current_entry["inst_retire"],
                "last_cpu_unhalt": current_entry["cpu_unhalt"],
                "last_llc_misses": current_entry["llc_misses"],
                "last_sw_prefetch": current_entry["sw_prefetch"],
            }
            continue  # Skip the first record for accurate calculations

        # Calculate time delta for this core
        if use_tsc_ts:
            time_delta_ns = (current_entry["timestamp_ns"] - state["last_timestamp_ns"]) / 1999 * 1e3
        else:
            time_delta_ns = current_entry["timestamp_ns"] - state["last_timestamp_ns"]
        time_delta_us = time_delta_ns / 1e3  # Convert ns to us

        if time_delta_us <= 0:
            # Skip invalid time intervals
            last_state[cpu_id] = {
                "last_timestamp_ns": current_entry["timestamp_ns"],
                "last_stall_mem": current_entry["stall_mem"],
                "last_inst_retire": current_entry["inst_retire"],
                "last_cpu_unhalt": current_entry["cpu_unhalt"],
                "last_llc_misses": current_entry["llc_misses"],
                "last_sw_prefetch": current_entry["sw_prefetch"],
            }
            continue

        # Compute differences
        stall_mem_diff = current_entry["stall_mem"] - state["last_stall_mem"]
        inst_retire_diff = current_entry["inst_retire"] - state["last_inst_retire"]
        cpu_unhalt_diff = current_entry["cpu_unhalt"] - state["last_cpu_unhalt"]
        llc_misses_diff = current_entry["llc_misses"] - state["last_llc_misses"]
        sw_prefetch_diff = current_entry["sw_prefetch"] - state["last_sw_prefetch"]

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
        total_memory_bandwidth = (
            total_memory_bandwidth - old_memory_bandwidth_core + memory_bandwidth
        )

        # Record performance event
        perf_entry = {
            "id": unique_id_perf,
            "cpu_id": cpu_id,
            "timestamp_ns": current_entry["timestamp_ns"],
            "stalls_per_us": stalls_per_us,
            "inst_retire_rate": inst_retire_rate,
            "cpu_usage": cpu_usage,
        }

        if use_offcore: 
            perf_entry["read_misses_rate"] = llc_misses_rate
            perf_entry["write_misses_rate"] = sw_prefetch_rate
        else:
            perf_entry["llc_misses_rate"] = llc_misses_rate
            perf_entry["sw_prefetch_rate"] = sw_prefetch_rate
        
        perf_entry["memory_bandwidth_bytes_per_us"] = total_memory_bandwidth
        perf_entry["time_delta_ns"] = time_delta_ns

        # If user asked, include raw counter data
        if use_raw:
            perf_entry["stall_mem"] = current_entry["stall_mem"]
            perf_entry["inst_retire"] = current_entry["inst_retire"]
            perf_entry["cpu_unhalt"] = current_entry["cpu_unhalt"]
            if use_offcore:
                perf_entry["read_misses"] = current_entry["llc_misses"]
                perf_entry["write_misses"] = current_entry["sw_prefetch"]
            else:
                perf_entry["llc_misses"] = current_entry["llc_misses"]
                perf_entry["sw_prefetch"] = current_entry["sw_prefetch"]

        processed_data.append(perf_entry)
        unique_id_perf += 1

        # Update node memory bandwidth events if timestamp has advanced
        if (
            last_global_timestamp_ns is not None
            and current_timestamp_ns > last_global_timestamp_ns
        ):
            node_bw_entry = {
                "id": unique_id_node_bw,
                "start_time_ns": last_global_timestamp_ns,
                "end_time_ns": current_timestamp_ns,
                "memory_bandwidth_bytes_per_us": total_memory_bandwidth,
            }
            total_memory_bandwidth_events.append(node_bw_entry)
            unique_id_node_bw += 1

        # Update last global timestamp
        last_global_timestamp_ns = (
            current_timestamp_ns
            if last_global_timestamp_ns is None
            else max(last_global_timestamp_ns, current_timestamp_ns)
        )

        # Update last state for this CPU
        last_state[cpu_id] = {
            "last_timestamp_ns": current_entry["timestamp_ns"],
            "last_stall_mem": current_entry["stall_mem"],
            "last_inst_retire": current_entry["inst_retire"],
            "last_cpu_unhalt": current_entry["cpu_unhalt"],
            "last_llc_misses": current_entry["llc_misses"],
            "last_sw_prefetch": current_entry["sw_prefetch"],
        }

    # Convert processed data to DataFrames
    df_performance_events = pd.DataFrame(processed_data)

    df_node_memory_bandwidth = pd.DataFrame(total_memory_bandwidth_events)

    # Convert data types
    if use_raw:
        if use_offcore:
            df_performance_events = df_performance_events.astype(
                {
                    "id": "int64",
                    "cpu_id": "int32",
                    "timestamp_ns": "int64",
                    "stalls_per_us": "float64",
                    "inst_retire_rate": "float64",
                    "cpu_usage": "float64",
                    "read_misses_rate": "float64",
                    "write_misses_rate": "float64",
                    "memory_bandwidth_bytes_per_us": "float64",
                    "time_delta_ns": "int64",
                    "stall_mem": "uint64",
                    "inst_retire": "uint64",
                    "cpu_unhalt": "uint64",
                    "read_misses": "uint64",
                    "write_misses": "uint64",
                }
            )
        else:
            df_performance_events = df_performance_events.astype(
                {
                    "id": "int64",
                    "cpu_id": "int32",
                    "timestamp_ns": "int64",
                    "stalls_per_us": "float64",
                    "inst_retire_rate": "float64",
                    "cpu_usage": "float64",
                    "llc_misses_rate": "float64",
                    "sw_prefetch_rate": "float64",
                    "memory_bandwidth_bytes_per_us": "float64",
                    "time_delta_ns": "int64",
                    "stall_mem": "uint64",
                    "inst_retire": "uint64",
                    "cpu_unhalt": "uint64",
                    "llc_misses": "uint64",
                    "sw_prefetch": "uint64",
                }
        )
    else:
        if use_offcore:
            df_performance_events = df_performance_events.astype(
                {
                    "id": "int64",
                    "cpu_id": "int32",
                    "timestamp_ns": "int64",
                    "stalls_per_us": "float64",
                    "inst_retire_rate": "float64",
                    "cpu_usage": "float64",
                    "read_misses_rate": "float64",
                    "write_misses_rate": "float64",
                    "memory_bandwidth_bytes_per_us": "float64",
                    "time_delta_ns": "int64",
                }
            )
        else:
            df_performance_events = df_performance_events.astype(
                {
                    "id": "int64",
                    "cpu_id": "int32",
                    "timestamp_ns": "int64",
                    "stalls_per_us": "float64",
                    "inst_retire_rate": "float64",
                    "cpu_usage": "float64",
                    "llc_misses_rate": "float64",
                    "sw_prefetch_rate": "float64",
                    "memory_bandwidth_bytes_per_us": "float64",
                    "time_delta_ns": "int64",
                }
            )

    df_node_memory_bandwidth = df_node_memory_bandwidth.astype(
        {
            "id": "int64",
            "start_time_ns": "int64",
            "end_time_ns": "int64",
            "memory_bandwidth_bytes_per_us": "float64",
        }
    )

    # Connect to the 'analysis.duckdb' database
    con = duckdb.connect(database="analysis.duckdb")

    # Create 'performance_events' table if it doesn't exist
    if use_raw:
        if use_offcore:
            con.execute("""
                CREATE TABLE IF NOT EXISTS performance_events (
                    id BIGINT,
                    cpu_id INTEGER,
                    timestamp_ns BIGINT,
                    stalls_per_us DOUBLE,
                    inst_retire_rate DOUBLE,
                    cpu_usage DOUBLE,
                    read_misses_rate DOUBLE,
                    write_misses_rate DOUBLE,
                    memory_bandwidth_bytes_per_us DOUBLE,
                    time_delta_ns BIGINT,
                    stall_mem BIGINT,
                    inst_retire BIGINT,
                    cpu_unhalt BIGINT,
                    read_misses BIGINT,
                    write_misses BIGINT
                )
            """)
        else:
            con.execute("""
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
                    time_delta_ns DOUBLE,
                    stall_mem BIGINT,
                    inst_retire BIGINT,
                    cpu_unhalt BIGINT,
                    llc_misses BIGINT,
                    sw_prefetch BIGINT
                )
            """)
    else:
        if use_offcore:
            con.execute("""
                CREATE TABLE IF NOT EXISTS performance_events (
                    id BIGINT,
                    cpu_id INTEGER,
                    timestamp_ns BIGINT,
                    stalls_per_us DOUBLE,
                    inst_retire_rate DOUBLE,
                    cpu_usage DOUBLE,
                    read_misses_rate DOUBLE,
                    write_misses_rate DOUBLE,
                    memory_bandwidth_bytes_per_us DOUBLE,
                    time_delta_ns BIGINT
                )
            """)
        else:
            con.execute("""
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
            """)

    # Create 'node_memory_bandwidth' table if it doesn't exist
    con.execute("""
        CREATE TABLE IF NOT EXISTS node_memory_bandwidth (
            id BIGINT,
            start_time_ns BIGINT,
            end_time_ns BIGINT,
            memory_bandwidth_bytes_per_us DOUBLE
        )
    """)

    # Register DataFrames and insert data
    con.register("df_performance_events", df_performance_events)
    con.register("df_node_memory_bandwidth", df_node_memory_bandwidth)

    con.execute("""
        INSERT INTO performance_events
        SELECT * FROM df_performance_events
    """)

    con.execute("""
        INSERT INTO node_memory_bandwidth
        SELECT * FROM df_node_memory_bandwidth
    """)

    # Close the connection
    con.close()

    print(
        "Processed performance data has been inserted into 'performance_events' table in 'analysis.duckdb'."
    )
    print(
        "Node memory bandwidth data has been inserted into 'node_memory_bandwidth' table in 'analysis.duckdb'."
    )


def main():
    parser = argparse.ArgumentParser(
        description="Parse hiresperf log files and store results in DuckDB."
    )
    parser.add_argument(
        "perf_log_path", type=str, help="Path to the hiresperf log file."
    )
    parser.add_argument(
        "--raw_counter",
        action="store_true",
        help="Includes raw counter data in database.",
    )
    parser.add_argument(
        "--tsc_ts", action="store_true", help="Use TSC timestamps instead of ktime."
    )
    parser.add_argument(
        "--tsc_freq",
        type=float,
        required=True,
        help="TSC frequency in cycles per microsecond.",
    )

    args = parser.parse_args()

    if not os.path.isfile(args.perf_log_path):
        print(f"Error: File '{args.perf_log_path}' does not exist.")
        sys.exit(1)

    # Read config flag
    hrp_use_tsc = read_hrp_tsc_config()
    use_tsc_ts = args.tsc_ts
    tsc_per_us = args.tsc_freq
    use_offcore = read_hrp_use_offcore_config()

    # Warn if TSC timestamps are requested but not enabled in the hiresperf build config file.
    if hrp_use_tsc != 1 and use_tsc_ts:
        print(
            "Warning: TSC timestamps are not enabled in the hiresperf config file. Ensure the data is using TSC as the timestamp. Otherwise, timestamp conversion will be incorrect."
        )

    parse_hrperf_log(
        perf_log_path=args.perf_log_path,
        use_raw=args.raw_counter,
        use_tsc_ts=use_tsc_ts,
        tsc_per_us=tsc_per_us,
        use_offcore=use_offcore,
    )


if __name__ == "__main__":
    main()
