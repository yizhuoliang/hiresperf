import struct
import os

def parse_hrperf_log(file_path, max_frequency_ghz):
    # Updated struct format to match the new log structure
    entry_format = 'iqQQQQQ'  # Adjusted for the new fields
    entry_size = struct.calcsize(entry_format)

    # Dictionary to store the last state and file handles for each CPU
    last_state = {}
    perf_data_per_core = {}

    tsc_per_us = max_frequency_ghz * 1000  # TSC increments per microsecond

    with open(file_path, 'rb') as f:
        while True:
            data = f.read(entry_size)
            if not data:
                break

            # Unpack the data according to the updated structure
            cpu_id, ktime, stall_mem, inst_retire, cpu_unhalt, llc_misses, sw_prefetch = struct.unpack(entry_format, data)

            if cpu_id not in last_state:
                # Initialize the first record and first ktime for this CPU
                last_state[cpu_id] = {
                    'first_ktime': ktime,
                    'last_ktime': ktime,
                    'last_stall_mem': stall_mem,
                    'last_inst_retire': inst_retire,
                    'last_cpu_unhalt': cpu_unhalt,
                    'last_llc_misses': llc_misses,
                    'last_sw_prefetch': sw_prefetch
                }
                perf_data_per_core[cpu_id] = []
                continue  # Skip the first record for accurate calculations

            # Retrieve the previous state
            state = last_state[cpu_id]
            ktime_elapsed_since_first = ktime - state['first_ktime']
            ktime_elapsed_since_last = ktime - state['last_ktime']
            stall_mem_since_last = stall_mem - state['last_stall_mem']
            inst_retire_since_last = inst_retire - state['last_inst_retire']

            # Calculate rates and CPU usage
            time_delta_us = ktime_elapsed_since_last / 1e3  # Convert ns to us
            stalls_per_us = stall_mem_since_last / time_delta_us if time_delta_us > 0 else 0
            inst_retire_rate = inst_retire_since_last / time_delta_us if time_delta_us > 0 else 0
            cpu_usage = (cpu_unhalt - state['last_cpu_unhalt']) / (tsc_per_us * time_delta_us) if time_delta_us > 0 else 0

            llc_misses_rate = (llc_misses - state['last_llc_misses']) / time_delta_us if time_delta_us > 0 else 0
            sw_prefetch_rate = (sw_prefetch - state['last_sw_prefetch']) / time_delta_us if time_delta_us > 0 else 0
            memory_bandwidth = (llc_misses_rate + sw_prefetch_rate) * 64  # in bytes per microsecond

            # Prepare the performance data entry
            perf_entry = {
                'timestamp_ns': ktime,
                'stalls_per_us': stalls_per_us,
                'inst_retire_rate': inst_retire_rate,
                'cpu_usage': cpu_usage,
                'llc_misses_rate': llc_misses_rate,
                'sw_prefetch_rate': sw_prefetch_rate,
                'memory_bandwidth_bytes_per_us': memory_bandwidth,
                'time_delta_ns': ktime_elapsed_since_last
            }
            perf_data_per_core[cpu_id].append(perf_entry)

            # Update the last state for this CPU
            last_state[cpu_id].update({
                'last_ktime': ktime,
                'last_stall_mem': stall_mem,
                'last_inst_retire': inst_retire,
                'last_cpu_unhalt': cpu_unhalt,
                'last_llc_misses': llc_misses,
                'last_sw_prefetch': sw_prefetch
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

if __name__ == "__main__":
    max_frequency_ghz = float(input("Enter the maximum CPU frequency in GHz (e.g., 2.5): "))
    perf_log_path = input("Enter the path to the performance log file (e.g., 'hrperf_log.bin'): ").strip()
    parse_hrperf_log(perf_log_path, max_frequency_ghz)
