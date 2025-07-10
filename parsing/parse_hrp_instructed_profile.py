from dataclasses import dataclass
import numpy as np
import pandas as pd
import argparse

parser = argparse.ArgumentParser(description="Parse HRP instructed profile.")
parser.add_argument("--use_imc", action="store_true", help="Use IMC counters")
parser.add_argument("--use_offcore", action="store_true", help="Use offcore counters")
parser.add_argument(
    "--use_write_est", action="store_true", help="Use write estimate counter"
)
parser.add_argument(
    "--tsc_freq",
    type=float,
    required=True,
    help="TSC frequency in cycles per microsecond.",
)
parser.add_argument(
    "--cpu_store_imc",
    type=int,
    default=0,
    help="CPU ID to store IMC data (default: 0)",
)
parser.add_argument(
    "--bin_path",
    type=str,
    default="/hrperf_log.bin",
    help="Path to the HRP instructed profile binary file (default: /hrperf_log.bin)",
)
args = parser.parse_args()

c1_name = None
c2_name = None

@dataclass
class TimeRangeData:
    duration_ms: float
    c1_diff: int
    c2_diff: int
    imc_read_diff: int
    imc_write_diff: int
    start_ts: int
    end_ts: int

def prepare_core_name():
    global c1_name, c2_name
    if args.use_offcore:
        c1_name = 'offcore_read'
        if args.use_write_est:
            c2_name = 'offcore_write_est'
        else:
            c2_name = 'offcore_write'
    else:
        c1_name = 'llc_misses'
        c2_name = 'sw_prefetch'
        
    

def read_logs_to_numpy(file_path: str) -> np.ndarray:
    global c1_name, c2_name, args

    if args.use_imc:
        # Matches "iQQQQQQQQ" -> int32, uint64, uint64, ...
        dt = np.dtype([
            ('cpu_id', np.int32),
            ('timestamp', np.uint64),
            ('stall_mem', np.uint64),
            ('inst_retire', np.uint64),
            ('cpu_unhalt', np.uint64),
            (f'{c1_name}', np.uint64),
            (f'{c2_name}', np.uint64),
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
            (f'{c1_name}', np.uint64),
            (f'{c2_name}', np.uint64),
        ])

    try:
        data = np.fromfile(file_path, dtype=dt)
        print(f"Read {len(data)} records to successfully.")
        return data
    except Exception as e:
        print(f"Error reading file with NumPy: {e}")
        return np.array([])

def read_into_df(file_path: str) -> pd.DataFrame:
    data = read_logs_to_numpy(file_path)
    df = pd.DataFrame(data)
    return df

def calc_data_in_range(time_range: tuple[int, int], df: pd.DataFrame) -> TimeRangeData:
    global c1_name, c2_name, args
    c1_diff_name = f'{c1_name}_diff'
    c2_diff_name = f'{c2_name}_diff'
    
    time_range_d = TimeRangeData(
        duration_ms=0.0,
        c1_diff=0,
        c2_diff=0,
        imc_read_diff=0,
        imc_write_diff=0,
        start_ts=0,
        end_ts=0
    )

    start, end = time_range
    mask = (df['timestamp'] >= start) & (df['timestamp'] <= end)
    df_in_range = df.loc[mask]
    assert len(df_in_range["timestamp"].unique()) == 2, "There should be exactly two unique timestamps in the specified range."
    timestamp_min = df_in_range['timestamp'].min()
    timestamp_max = df_in_range['timestamp'].max()

    data_at_start = df.loc[df['timestamp'] == timestamp_min, ['cpu_id', f'{c1_name}', f'{c2_name}']]
    data_at_end = df.loc[df['timestamp'] == timestamp_max, ['cpu_id', f'{c1_name}', f'{c2_name}']]

    merged = pd.merge(data_at_end, data_at_start, on='cpu_id', suffixes=('_end', '_start'))
    
    # print(f"{len(merged)} CPUs valid record found in the specified time range.")

    diff = pd.DataFrame({
        'cpu_id': merged['cpu_id'],
        f'{c1_diff_name}': merged[f'{c1_name}_end'] - merged[f'{c1_name}_start'],
        f'{c2_diff_name}': merged[f'{c2_name}_end'] - merged[f'{c2_name}_start']
    })
    # print(f"Duration: {(timestamp_max - timestamp_min)/1999/1e6:.5f} s")
    time_range_d.duration_ms = (timestamp_max - timestamp_min) / int(args.tsc_freq) / 1e3  # in ms

    diff_sum = diff[[c1_diff_name, c2_diff_name]].sum()
    # print(diff_sum.to_dict())
    
    time_range_d.c1_diff = diff_sum[c1_diff_name]
    time_range_d.c2_diff = diff_sum[c2_diff_name]
    time_range_d.start_ts = timestamp_min
    time_range_d.end_ts = timestamp_max
    
    # total_ = diff_sum.sum()
    # print(f"Total diff: {total_}")
    # print(f"Data transferred ((c1+c2) * 64): {total_ * 64 / 1e6:.2f} MB") 
    
    imc_col_names = ['imc_read', 'imc_write']

    start_imc = df.loc[(df['timestamp'] == timestamp_min) & (df['cpu_id'] == args.cpu_store_imc), imc_col_names]
    end_imc = df.loc[(df['timestamp'] == timestamp_max) & (df['cpu_id'] == args.cpu_store_imc), imc_col_names]

    if not start_imc.empty and not end_imc.empty:
        imc_read_diff = int(end_imc['imc_read'].values[0]) - int(start_imc['imc_read'].values[0])
        imc_write_diff = int(end_imc['imc_write'].values[0]) - int(start_imc['imc_write'].values[0])
        # print(f"IMC read diff: {imc_read_diff}, IMC write diff: {imc_write_diff}")
        # print(f"IMC total transferred ((read+write) * 64): {(imc_read_diff + imc_write_diff) * 64 / 1e6:.2f} MB")
        time_range_d.imc_read_diff = imc_read_diff
        time_range_d.imc_write_diff = imc_write_diff
    else:
        print("IMC data not available for the specified cpu_id and timestamps.")

    return time_range_d

def get_all_time_ranges(df: pd.DataFrame) -> list[tuple[int, int]]:
    timestamps = df['timestamp'].unique()
    timestamps.sort()
    
    if len(timestamps) < 2:
        print("Not enough timestamps to form a time range.")
        return []

    time_ranges = []
    for i in range(len(timestamps) - 1):
        start = timestamps[i]
        end = timestamps[i + 1]
        time_ranges.append((start, end))
    return time_ranges

def parse_hrp_instructed_profile(file_path: str) -> list[TimeRangeData]:
    df = read_into_df(file_path)
    if df.empty:
        print("No data to parse.")
        return []

    time_ranges = get_all_time_ranges(df)
    if not time_ranges:
        print("No valid time ranges found.")
        return []

    results = []
    for time_range in time_ranges:
        result = calc_data_in_range(time_range, df)
        results.append(result)
    return results

def print_avg_time_ranges_data(time_ranges_data: list[TimeRangeData]):
    if not time_ranges_data:
        print("No time ranges data to print.")
        return

    avg_duration = np.mean([data.duration_ms for data in time_ranges_data])
    avg_c1_diff = np.mean([data.c1_diff for data in time_ranges_data])
    avg_c2_diff = np.mean([data.c2_diff for data in time_ranges_data])
    avg_imc_read_diff = np.mean([data.imc_read_diff for data in time_ranges_data])
    avg_imc_write_diff = np.mean([data.imc_write_diff for data in time_ranges_data])

    print(f"Total num of loops: {len(time_ranges_data)}")
    print(f"Average Duration (ms): {avg_duration:.2f}")
    print(f"Average {c1_name} Diff: {avg_c1_diff}")
    print(f"Average {c2_name} Diff: {avg_c2_diff}")
    print(f"Average {c1_name} + {c2_name} Total Transferred ((c1+c2) * 64): {(avg_c1_diff + avg_c2_diff) * 64 / 1e6:.2f} MB")
    print(f"Average IMC Read Diff: {avg_imc_read_diff}")
    print(f"Average IMC Write Diff: {avg_imc_write_diff}")
    print(f"Average IMC Total Transferred ((read+write) * 64): {(avg_imc_read_diff + avg_imc_write_diff) * 64 / 1e6:.2f} MB")

def main():
    global args

    if args.use_imc:
        print("Using IMC")
    if args.use_offcore:
        print("Using offcore")
    if args.use_write_est:
        print("Using write estimate counter")
        
    file_path = args.bin_path
    trs = parse_hrp_instructed_profile(file_path) 
    print_avg_time_ranges_data(trs)

if __name__ == "__main__":
    prepare_core_name()
    main()
