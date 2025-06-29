import sys
import os
import duckdb
import argparse
import re
import polars as pl
import numpy as np

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

def read_logs_to_numpy(file_path: str, use_imc: bool = False) -> np.ndarray:
    if use_imc:
        # Matches "iQQQQQQQQ" -> int32, uint64, uint64, ...
        dt = np.dtype([
            ('cpu_id', np.int32),
            ('timestamp', np.uint64),
            ('stall_mem', np.uint64),
            ('inst_retire', np.uint64),
            ('cpu_unhalt', np.uint64),
            ('llc_misses', np.uint64),
            ('sw_prefetch', np.uint64),
            ('imc_read', np.uint64),
            ('imc_write', np.uint64),
        ])
    else:
        # Matches "iQQQQQQ"
        dt = np.dtype([
            ('cpu_id', np.int32),
            ('timestamp', np.uint64),
            ('stall_mem', np.uint64),
            ('inst_retire', np.uint64),
            ('cpu_unhalt', np.uint64),
            ('llc_misses', np.uint64),
            ('sw_prefetch', np.uint64),
        ])

    print("Reading binary file into NumPy array...")
    try:
        data = np.fromfile(file_path, dtype=dt)
        print(f"Read {len(data)} records to successfully.")
        return data
    except Exception as e:
        print(f"Error reading file with NumPy: {e}")
        return np.array([])

def create_tables(con: duckdb.DuckDBPyConnection, use_raw: bool, use_offcore: bool, use_imc: bool):
    """Create database tables if they don't exist."""
    if use_raw:
        if use_offcore:
            con.execute("""
                CREATE TABLE IF NOT EXISTS performance_events (
                    id BIGINT,
                    cpu_id INTEGER,
                    timestamp_ns UBIGINT,
                    stalls_per_us DOUBLE,
                    inst_retire_rate DOUBLE,
                    cpu_usage DOUBLE,
                    offcore_read_rate DOUBLE,
                    offcore_write_rate DOUBLE,
                    memory_bandwidth_bytes_per_us DOUBLE,
                    time_delta_ns BIGINT,
                    stall_mem UBIGINT,
                    inst_retire UBIGINT,
                    cpu_unhalt UBIGINT,
                    offcore_read UBIGINT,
                    offcore_write UBIGINT
                    {}
                )
            """.format(", imc_read UBIGINT, imc_write UBIGINT" if use_imc else ""))
        else:
            con.execute("""
                CREATE TABLE IF NOT EXISTS performance_events (
                    id BIGINT,
                    cpu_id INTEGER,
                    timestamp_ns UBIGINT,
                    stalls_per_us DOUBLE,
                    inst_retire_rate DOUBLE,
                    cpu_usage DOUBLE,
                    llc_misses_rate DOUBLE,
                    sw_prefetch_rate DOUBLE,
                    memory_bandwidth_bytes_per_us DOUBLE,
                    time_delta_ns BIGINT,
                    stall_mem UBIGINT,
                    inst_retire UBIGINT,
                    cpu_unhalt UBIGINT,
                    llc_misses UBIGINT,
                    sw_prefetch UBIGINT
                    {}
                )
            """.format(", imc_read UBIGINT, imc_write UBIGINT" if use_imc else ""))
    else:
        if use_offcore:
            con.execute("""
                CREATE TABLE IF NOT EXISTS performance_events (
                    id BIGINT,
                    cpu_id INTEGER,
                    timestamp_ns UBIGINT,
                    stalls_per_us DOUBLE,
                    inst_retire_rate DOUBLE,
                    cpu_usage DOUBLE,
                    offcore_read_rate DOUBLE,
                    offcore_write_rate DOUBLE,
                    memory_bandwidth_bytes_per_us DOUBLE,
                    time_delta_ns BIGINT
                    {}
                )
            """.format(", imc_read UBIGINT, imc_write UBIGINT" if use_imc else ""))
        else:
            con.execute("""
                CREATE TABLE IF NOT EXISTS performance_events (
                    id BIGINT,
                    cpu_id INTEGER,
                    timestamp_ns UBIGINT,
                    stalls_per_us DOUBLE,
                    inst_retire_rate DOUBLE,
                    cpu_usage DOUBLE,
                    llc_misses_rate DOUBLE,
                    sw_prefetch_rate DOUBLE,
                    memory_bandwidth_bytes_per_us DOUBLE,
                    time_delta_ns BIGINT
                    {}
                )
            """.format(", imc_read UBIGINT, imc_write UBIGINT" if use_imc else ""))
    
    con.execute("""
        CREATE TABLE IF NOT EXISTS node_memory_bandwidth (
            id BIGINT,
            start_time_ns UBIGINT,
            end_time_ns UBIGINT,
            memory_bandwidth_bytes_per_us DOUBLE
        )
    """)

def parse_hrperf_log_polars(perf_log_path: str, use_raw: bool, use_tsc_ts: bool, 
                            tsc_per_us: float, use_offcore: bool, use_imc: bool,
                            db_path: str):
    print("Reading all log entries into memory...")
    
    numpy_data = read_logs_to_numpy(perf_log_path, use_imc)
    if numpy_data.size == 0:
        print("Log file is empty or could not be read.")
        return

    # Process the NumPy data
    print("Converting to Polars DataFrame...")
    df = pl.from_numpy(numpy_data)

    print(f"Total log entries read: {df.height}")
    assert df.height == numpy_data.size, "DataFrame height does not match NumPy array size."

    # Sort by cpu_id and timestamp to ensure correct order for delta calculations
    df = df.sort(["cpu_id", "timestamp"])

    df = df.with_columns(
        pl.col("timestamp").shift(1).over("cpu_id").alias("prev_timestamp"),
        pl.col("stall_mem").shift(1).over("cpu_id").alias("prev_stall_mem"),
        pl.col("inst_retire").shift(1).over("cpu_id").alias("prev_inst_retire"),
        pl.col("cpu_unhalt").shift(1).over("cpu_id").alias("prev_cpu_unhalt"),
        pl.col("llc_misses").shift(1).over("cpu_id").alias("prev_llc_misses"),
        pl.col("sw_prefetch").shift(1).over("cpu_id").alias("prev_sw_prefetch"),
    )

    # Calculate time delta
    if use_tsc_ts:
        time_delta_ns = (pl.col("timestamp") - pl.col("prev_timestamp")) / tsc_per_us * 1e3
    else:
        time_delta_ns = pl.col("timestamp") - pl.col("prev_timestamp")
    
    time_delta_us = time_delta_ns / 1e3

    # Filter out invalid time deltas
    df = df.with_columns(
        time_delta_ns=time_delta_ns,
        time_delta_us=time_delta_us
    ).filter(pl.col("time_delta_us") > 0) 

    df = df.with_columns(
        stalls_per_us=(pl.col("stall_mem") - pl.col("prev_stall_mem")) / pl.col("time_delta_us"),
        inst_retire_rate=(pl.col("inst_retire") - pl.col("prev_inst_retire")) / pl.col("time_delta_us"),
        cpu_usage=(pl.col("cpu_unhalt") - pl.col("prev_cpu_unhalt")) / (tsc_per_us * pl.col("time_delta_us")),
        llc_misses_rate=(pl.col("llc_misses") - pl.col("prev_llc_misses")) / pl.col("time_delta_us"),
        sw_prefetch_rate=(pl.col("sw_prefetch") - pl.col("prev_sw_prefetch")) / pl.col("time_delta_us"),
    ).with_columns(
        memory_bandwidth_bytes_per_us=(pl.col("llc_misses_rate") + pl.col("sw_prefetch_rate")) * 64
    )

    # Prepare final performance_events table
    final_cols = [
        "cpu_id", "timestamp_ns", "stalls_per_us", "inst_retire_rate", "cpu_usage",
        "llc_misses_rate", "sw_prefetch_rate", "memory_bandwidth_bytes_per_us", "time_delta_ns"
    ]
    if use_raw:
        final_cols.extend(["stall_mem", "inst_retire", "cpu_unhalt", "llc_misses", "sw_prefetch"])
        if use_imc:
            final_cols.extend(["imc_read", "imc_write"])

    # Rename timestamp to timestamp_ns for consistency with schema
    perf_df = df.rename({"timestamp": "timestamp_ns"}).select(final_cols)
    # Add a unique ID
    perf_df = perf_df.with_row_index("id", offset=1)


    # Calculate Node Memory Bandwidth
    print("Calculating node-wide memory bandwidth...")
    node_bw_df = df.group_by("timestamp", maintain_order=True).agg(
        pl.sum("memory_bandwidth_bytes_per_us").alias("total_memory_bandwidth")
    ).sort("timestamp")
    
    node_bw_df = node_bw_df.with_columns(
        start_time_ns=pl.col("timestamp"),
        end_time_ns=pl.col("timestamp").shift(-1)
    ).drop_nulls()

    node_bw_df = node_bw_df.with_columns(
        memory_bandwidth_bytes_per_us=pl.col("total_memory_bandwidth")
    ).select(["start_time_ns", "end_time_ns", "memory_bandwidth_bytes_per_us"])
    node_bw_df = node_bw_df.with_row_index("id", offset=1)


    print("Writing data to DuckDB...")
    con = duckdb.connect(database=db_path)
    create_tables(con, use_raw, use_offcore, use_imc)

    con.execute("INSERT INTO performance_events SELECT * FROM perf_df")
    con.execute("INSERT INTO node_memory_bandwidth SELECT * FROM node_bw_df")
    
    con.close()

    print(f"Processed performance data has been inserted into 'performance_events' table in '{db_path}'.")
    print(f"Node memory bandwidth data has been inserted into 'node_memory_bandwidth' table in '{db_path}'.")

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
        "--db_path",
        type=str,
        default="analysis.duckdb",
        help="Path to the DuckDB database file to store results.",
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
        
    if not os.path.isabs(args.db_path):
        args.db_path = os.path.abspath(args.db_path)

    print(f"Using TSC timestamps: {use_tsc_ts}, TSC frequency: {tsc_per_us} cycles/us")
    print(f"Add raw counters: {args.raw_counter}") 
    print(f"Using offcore counters: {use_offcore}")
    print(f"Using imc counters: {use_imc}")
    
    parse_hrperf_log_polars(
        perf_log_path=args.perf_log_path,
        use_raw=args.raw_counter,
        use_tsc_ts=args.tsc_ts,
        tsc_per_us=args.tsc_freq,
        use_offcore=args.use_offcore,
        use_imc=args.use_imc,
        db_path=args.db_path,
    )

if __name__ == "__main__":
    main()
