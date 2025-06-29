import sys
import struct
import os
import duckdb
import argparse
import re
from dataclasses import dataclass
from typing import Iterator, Dict, List, Optional, Tuple


def read_hrp_tsc_config(config_path="../src/config.h") -> bool:
    script_dir = os.path.dirname(os.path.abspath(__file__))
    config_path = os.path.join(script_dir, "..", "src", "config.h")
    config_path = os.path.abspath(config_path)
    with open(config_path, "r") as f:
        for line in f:
            # look for tsc flag
            match = re.search(r"#define\s+HRP_USE_TSC\s+(\d+)", line)
            if match:
                return int(match.group(1)) != 0
        # If no flag, fall back to original hireserf
        return False
    
def read_hrp_use_offcore_config(config_path="../src/config.h") -> bool:
    script_dir = os.path.dirname(os.path.abspath(__file__))
    config_path = os.path.join(script_dir, "..", "src", "config.h")
    config_path = os.path.abspath(config_path)
    with open(config_path, "r") as f:
        for line in f:
            # look for offcore flag
            match = re.search(r"#define\s+HRP_USE_OFFCORE\s+(\d+)", line)
            if match:
                return int(match.group(1)) != 0
        # If no flag, fall back to original hireserf
        return False


@dataclass
class LogEntry:
    cpu_id: int
    timestamp: int
    stall_mem: int
    inst_retire: int
    cpu_unhalt: int
    llc_misses: int
    sw_prefetch: int
    imc_read: int = 0
    imc_write: int = 0

@dataclass
class CPUState:
    last_timestamp_ns: int
    last_stall_mem: int
    last_inst_retire: int
    last_cpu_unhalt: int
    last_llc_misses: int
    last_sw_prefetch: int
    last_imc_read: int = 0
    last_imc_write: int = 0

def read_log_entries(file_path: str, use_imc: bool = False) -> Iterator[LogEntry]:
    """Stream log entries from file without loading everything into memory."""
    if use_imc:
        entry_format = "iqQQQQQQQ"  # Added two more Q for imc_read and imc_write
    else:
        entry_format = "iqQQQQQ"
    entry_size = struct.calcsize(entry_format)
    
    with open(file_path, "rb") as f:
        while True:
            data = f.read(entry_size)
            if not data or len(data) < entry_size:
                break
            
            if use_imc:
                cpu_id, ktime, stall_mem, inst_retire, cpu_unhalt, llc_misses, sw_prefetch, imc_read, imc_write = struct.unpack(entry_format, data)
                yield LogEntry(cpu_id, ktime, stall_mem, inst_retire, cpu_unhalt, llc_misses, sw_prefetch, imc_read, imc_write)
            else:
                cpu_id, ktime, stall_mem, inst_retire, cpu_unhalt, llc_misses, sw_prefetch = struct.unpack(entry_format, data)
                yield LogEntry(cpu_id, ktime, stall_mem, inst_retire, cpu_unhalt, llc_misses, sw_prefetch)

# def get_cpu_file_positions(file_path: str) -> Dict[int, List[int]]:
#     """Get file positions for each CPU's data for efficient seeking."""
#     entry_format = "iqQQQQQ"
#     entry_size = struct.calcsize(entry_format)
#     cpu_positions = {}
    
#     with open(file_path, "rb") as f:
#         pos = 0
#         while True:
#             data = f.read(entry_size)
#             if not data or len(data) < entry_size:
#                 break
            
#             cpu_id = struct.unpack("i", data[:4])[0]
#             if cpu_id not in cpu_positions:
#                 cpu_positions[cpu_id] = []
#             cpu_positions[cpu_id].append(pos)
#             pos += entry_size
    
#     return cpu_positions

def create_tables(con: duckdb.DuckDBPyConnection, use_raw: bool, use_offcore: bool, use_imc: bool):
    """Create database tables if they don't exist."""
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
                    offcore_read_rate DOUBLE,
                    offcore_write_rate DOUBLE,
                    memory_bandwidth_bytes_per_us DOUBLE,
                    time_delta_ns BIGINT,
                    stall_mem BIGINT,
                    inst_retire BIGINT,
                    cpu_unhalt BIGINT,
                    offcore_read BIGINT,
                    offcore_write BIGINT
                    {}
                )
            """.format(", imc_read BIGINT, imc_write BIGINT" if use_imc else ""))
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
                    time_delta_ns BIGINT,
                    stall_mem BIGINT,
                    inst_retire BIGINT,
                    cpu_unhalt BIGINT,
                    llc_misses BIGINT,
                    sw_prefetch BIGINT
                    {}
                )
            """.format(", imc_read BIGINT, imc_write BIGINT" if use_imc else ""))
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
                    offcore_read_rate DOUBLE,
                    offcore_write_rate DOUBLE,
                    memory_bandwidth_bytes_per_us DOUBLE,
                    time_delta_ns BIGINT
                    {}
                )
            """.format(", imc_read BIGINT, imc_write BIGINT" if use_imc else ""))
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
                    {}
                )
            """.format(", imc_read BIGINT, imc_write BIGINT" if use_imc else ""))
    
    con.execute("""
        CREATE TABLE IF NOT EXISTS node_memory_bandwidth (
            id BIGINT,
            start_time_ns BIGINT,
            end_time_ns BIGINT,
            memory_bandwidth_bytes_per_us DOUBLE
        )
    """)

def insert_batch(con: duckdb.DuckDBPyConnection, table_name: str, batch_data: List[Tuple], 
                use_raw: bool, use_offcore: bool, use_imc: bool):
    """Insert a batch of data directly into DuckDB."""
    if not batch_data:
        return
    
    if table_name == "performance_events":
        if use_raw:
            if use_offcore:
                if use_imc:
                    placeholders = "(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"
                else:
                    placeholders = "(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"
            else:
                if use_imc:
                    placeholders = "(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"
                else:
                    placeholders = "(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"
        else:
            if use_offcore:
                if use_imc:
                    placeholders = "(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"
                else:
                    placeholders = "(?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"
            else:
                if use_imc:
                    placeholders = "(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"
                else:
                    placeholders = "(?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"
    else:  # node_memory_bandwidth
        placeholders = "(?, ?, ?, ?)"
    
    values_list = ", ".join([placeholders] * len(batch_data))
    flat_data = [item for sublist in batch_data for item in sublist]
    
    con.execute(f"INSERT INTO {table_name} VALUES {values_list}", flat_data)

def parse_hrperf_log(perf_log_path: str, use_raw: bool, use_tsc_ts: bool, 
                    tsc_per_us: float, use_offcore: bool, use_imc: bool, batch_size: int = 10000):
    """Memory-efficient parser using streaming and batch processing."""
    con = duckdb.connect(database="analysis.duckdb")
    create_tables(con, use_raw, use_offcore, use_imc)
    
    # Get all entries sorted by timestamp for proper processing
    entries = list(read_log_entries(perf_log_path, use_imc))
    entries.sort(key=lambda x: x.timestamp)
    
    # Process entries in batches
    cpu_states: Dict[int, Optional[CPUState]] = {}
    per_core_memory_bandwidth: Dict[int, float] = {}
    total_memory_bandwidth = 0.0
    last_global_timestamp_ns = None
    
    unique_id_perf = 1
    unique_id_node_bw = 1
    
    perf_batch = []
    node_bw_batch = []
    
    for entry in entries:
        cpu_id = entry.cpu_id
        
        # Initialize CPU state if first time seeing this CPU
        if cpu_id not in cpu_states:
            cpu_states[cpu_id] = None
            per_core_memory_bandwidth[cpu_id] = 0.0
        
        state = cpu_states[cpu_id]
        if state is None:
            # Initialize first record for this CPU
            cpu_states[cpu_id] = CPUState(
                entry.timestamp, entry.stall_mem, entry.inst_retire,
                entry.cpu_unhalt, entry.llc_misses, entry.sw_prefetch,
                entry.imc_read, entry.imc_write
            )
            continue
        
        # Calculate time delta
        if use_tsc_ts:
            time_delta_ns = (entry.timestamp - state.last_timestamp_ns) / tsc_per_us * 1e3
        else:
            time_delta_ns = entry.timestamp - state.last_timestamp_ns
        
        time_delta_us = time_delta_ns / 1e3
        
        if time_delta_us <= 0:
            # Update state and continue
            cpu_states[cpu_id] = CPUState(
                entry.timestamp, entry.stall_mem, entry.inst_retire,
                entry.cpu_unhalt, entry.llc_misses, entry.sw_prefetch,
                entry.imc_read, entry.imc_write
            )
            continue
        
        # Compute differences and rates
        stall_mem_diff = entry.stall_mem - state.last_stall_mem
        inst_retire_diff = entry.inst_retire - state.last_inst_retire
        cpu_unhalt_diff = entry.cpu_unhalt - state.last_cpu_unhalt
        llc_misses_diff = entry.llc_misses - state.last_llc_misses
        sw_prefetch_diff = entry.sw_prefetch - state.last_sw_prefetch

        # imc_read_diff = entry.imc_read - state.last_imc_read
        # imc_write_diff = entry.imc_write - state.last_imc_write

        stalls_per_us = stall_mem_diff / time_delta_us
        inst_retire_rate = inst_retire_diff / time_delta_us
        cpu_usage = cpu_unhalt_diff / (tsc_per_us * time_delta_us)
        llc_misses_rate = llc_misses_diff / time_delta_us
        sw_prefetch_rate = sw_prefetch_diff / time_delta_us
        # imc_read_rate = imc_read_diff / time_delta_us
        # imc_write_rate = imc_write_diff / time_delta_us
        memory_bandwidth = (llc_misses_rate + sw_prefetch_rate) * 64
        
        # Update total memory bandwidth
        old_memory_bandwidth_core = per_core_memory_bandwidth[cpu_id]
        per_core_memory_bandwidth[cpu_id] = memory_bandwidth
        total_memory_bandwidth = total_memory_bandwidth - old_memory_bandwidth_core + memory_bandwidth
        
        # Build performance event tuple
        perf_row = [
            unique_id_perf, cpu_id, entry.timestamp, stalls_per_us,
            inst_retire_rate, cpu_usage
        ]
        
        perf_row.extend([llc_misses_rate, sw_prefetch_rate])
        perf_row.extend([total_memory_bandwidth, int(time_delta_ns)])
        
        # if use_imc:
        #     perf_row.extend([imc_read_rate, imc_write_rate])

        if use_raw:
            perf_row.extend([
                entry.stall_mem, entry.inst_retire, entry.cpu_unhalt,
                entry.llc_misses, entry.sw_prefetch
            ])
            if use_imc:
                perf_row.extend([entry.imc_read, entry.imc_write])
        
        perf_batch.append(tuple(perf_row))
        unique_id_perf += 1
        
        # Update node memory bandwidth
        if (last_global_timestamp_ns is not None and 
            entry.timestamp > last_global_timestamp_ns):
            node_bw_row = (
                unique_id_node_bw, last_global_timestamp_ns,
                entry.timestamp, total_memory_bandwidth
            )
            node_bw_batch.append(node_bw_row)
            unique_id_node_bw += 1
        
        last_global_timestamp_ns = (
            entry.timestamp if last_global_timestamp_ns is None
            else max(last_global_timestamp_ns, entry.timestamp)
        )
        
        # Update CPU state
        cpu_states[cpu_id] = CPUState(
            entry.timestamp, entry.stall_mem, entry.inst_retire,
            entry.cpu_unhalt, entry.llc_misses, entry.sw_prefetch,
            entry.imc_read, entry.imc_write
        )
        
        # Insert batches when they reach the batch size
        if len(perf_batch) >= batch_size:
            insert_batch(con, "performance_events", perf_batch, use_raw, use_offcore, use_imc)
            perf_batch = []
        
        if len(node_bw_batch) >= batch_size:
            insert_batch(con, "node_memory_bandwidth", node_bw_batch, use_raw, use_offcore, use_imc)
            node_bw_batch = []
    
    # Insert remaining data
    if perf_batch:
        insert_batch(con, "performance_events", perf_batch, use_raw, use_offcore, use_imc)
    if node_bw_batch:
        insert_batch(con, "node_memory_bandwidth", node_bw_batch, use_raw, use_offcore, use_imc)
    
    con.close()
    
    print("Processed performance data has been inserted into 'performance_events' table in 'analysis.duckdb'.")
    print("Node memory bandwidth data has been inserted into 'node_memory_bandwidth' table in 'analysis.duckdb'.")


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
    parser.add_argument(
        "--use_offcore",
        action="store_true",
        help="Indicates if offcore counters are used (must match hiresperf build).",
    )
    parser.add_argument(
        "--use_imc",
        action="store_true",
        help="Indicates if IMC counters are used (input data has two extra imc_read and imc_write fields).",
    )
    parser.add_argument(
        "--batch_size",
        type=int,
        default=10000,
        help="Batch size for database insertions (default: 10000).",
    )

    args = parser.parse_args()

    if not os.path.isfile(args.perf_log_path):
        print(f"Error: File '{args.perf_log_path}' does not exist.")
        sys.exit(1)

    # Read config flag
    hrp_use_tsc = read_hrp_tsc_config()
    use_tsc_ts = args.tsc_ts
    tsc_per_us = args.tsc_freq
    use_offcore = args.use_offcore
    use_imc = args.use_imc
    hrp_use_offcore = read_hrp_use_offcore_config()

    # Warn if TSC timestamps are requested but not enabled in the hiresperf build config file.
    if (not hrp_use_tsc) and use_tsc_ts:
        print(
            "Warning: TSC timestamps are not enabled in the hiresperf config file. Ensure the data is using TSC as the timestamp. Otherwise, timestamp conversion will be incorrect."
        )
    
    # Warn if offcore counters are requested but not enabled in the hiresperf build config file.
    if (not hrp_use_offcore) and use_offcore:
        print(
            "Warning: Offcore counters are not enabled in the hiresperf config file. Ensure the data is using offcore counters. Otherwise, parsing will be incorrect."
        )
        
    print(f"Using TSC timestamps: {use_tsc_ts}, TSC frequency: {tsc_per_us} cycles/us")
    print(f"Using offcore counters: {use_offcore}")
    print(f"Using imc counters: {use_imc}")
    print(f"Using raw counters: {args.raw_counter}") 

    parse_hrperf_log(
        perf_log_path=args.perf_log_path,
        use_raw=args.raw_counter,
        use_tsc_ts=use_tsc_ts,
        tsc_per_us=tsc_per_us,
        use_offcore=use_offcore,
        use_imc=use_imc,
        batch_size=args.batch_size,
    )


if __name__ == "__main__":
    main()
