import struct
import json

def parse_hrperf_log_to_json(file_path, output_json_path, max_frequency_ghz):
    # Define the struct format for parsing
    entry_format = 'iqQQQQ'
    entry_size = struct.calcsize(entry_format)

    # Convert max frequency GHz to cycles per microsecond
    tsc_per_us = max_frequency_ghz * 1000
    
    # Dictionary to store the last state and all data entries for each CPU
    last_state = {}
    data_entries = {}

    with open(file_path, 'rb') as f:
        while True:
            data = f.read(entry_size)
            if not data:
                break

            cpu_id, ktime, stalls_l3_miss, cpu_unhalt, llc_misses, sw_prefetch = struct.unpack(entry_format, data)
            
            if cpu_id not in data_entries:
                data_entries[cpu_id] = []
                last_state[cpu_id] = {
                    'first_ktime': ktime,
                    'last_ktime': ktime,
                    'last_stalls_l3_miss': stalls_l3_miss,
                    'last_cpu_unhalt': cpu_unhalt,
                    'last_llc_misses': llc_misses,
                    'last_sw_prefetch': sw_prefetch
                }

            state = last_state[cpu_id]
            ktime_elapsed_since_last = ktime - state['last_ktime']
            us_elapsed_since_last = ktime_elapsed_since_last / 1e3  # convert from ns to us

            # Calculate CPU usage and stalls per us
            cpu_usage = (cpu_unhalt - state['last_cpu_unhalt']) / (tsc_per_us * us_elapsed_since_last) if us_elapsed_since_last > 0 else 0
            stalls_delta = stalls_l3_miss - state['last_stalls_l3_miss']
            stalls_per_us = stalls_delta / us_elapsed_since_last if us_elapsed_since_last > 0 else 0

            # Calculate memory bandwidth metrics
            llc_misses_delta = llc_misses - state['last_llc_misses']
            sw_prefetch_delta = sw_prefetch - state['last_sw_prefetch']
            memory_delta = (llc_misses_delta + sw_prefetch_delta) * 64
            memory_bandwidth = memory_delta / us_elapsed_since_last if us_elapsed_since_last > 0 else 0

            # Append data entry for this CPU
            data_entries[cpu_id].append({
                'ktime': ktime,
                'cpu_usage': cpu_usage,
                'memory_bandwidth': memory_bandwidth,
                'memory_delta': memory_delta,
                'stalls_delta': stalls_delta,
                'stalls_per_us': stalls_per_us,
                'real_cycles_delta': (cpu_usage * max_frequency_ghz * 1000 * us_elapsed_since_last - stalls_delta)
            })
            
            # Update the last state for this CPU
            state.update({
                'last_ktime': ktime,
                'last_stalls_l3_miss': stalls_l3_miss,
                'last_cpu_unhalt': cpu_unhalt,
                'last_llc_misses': llc_misses,
                'last_sw_prefetch': sw_prefetch
            })

    # Write all data to a JSON file
    with open(output_json_path, 'w') as json_file:
        json.dump(data_entries, json_file, indent=4)

if __name__ == "__main__":
    max_frequency_ghz = 1.2  # Example maximum frequency in GHz
    parse_hrperf_log_to_json('/path/to/hrperf_log.bin', 'hrperf_data.json', max_frequency_ghz)
