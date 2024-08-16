import struct
import json

def parse_hrperf_log_to_json(file_path, output_json_path):
    # Define the struct format for parsing
    entry_format = 'iqQQQQ'
    entry_size = struct.calcsize(entry_format)
    
    # Dictionary to store the last state and all data entries for each CPU
    last_state = {}
    data_entries = {}

    with open(file_path, 'rb') as f:
        while True:
            data = f.read(entry_size)
            if not data:
                break
            
            cpu_id, ktime, tsc, cpu_unhalt, llc_misses, sw_prefetch = struct.unpack(entry_format, data)
            
            if cpu_id not in data_entries:
                # Initialize storage for this CPU
                data_entries[cpu_id] = []
                # Initialize the first record and first ktime for this CPU
                last_state[cpu_id] = {
                    'first_ktime': ktime,
                    'first_tsc': tsc,
                    'last_ktime': ktime,
                    'last_tsc': tsc,
                    'last_cpu_unhalt': cpu_unhalt,
                    'last_llc_misses': llc_misses,
                    'last_sw_prefetch': sw_prefetch
                }

            # Retrieve the previous state
            state = last_state[cpu_id]
            ktime_elapsed_since_last = ktime - state['last_ktime']
            tsc_elapsed_since_last = tsc - state['last_tsc']
            
            # Calculate CPU usage
            cpu_usage = (cpu_unhalt - state['last_cpu_unhalt']) / tsc_elapsed_since_last if tsc_elapsed_since_last > 0 else 0
            
            # Calculate estimated memory bandwidth usage
            llc_misses_rate = (llc_misses - state['last_llc_misses']) / (ktime_elapsed_since_last / 1e3) if ktime_elapsed_since_last > 0 else 0
            sw_prefetch_rate = (sw_prefetch - state['last_sw_prefetch']) / (ktime_elapsed_since_last / 1e3) if ktime_elapsed_since_last > 0 else 0
            memory_bandwidth = (llc_misses_rate + sw_prefetch_rate) * 64  # in bytes per microsecond
            
            # Append data entry for this CPU
            data_entries[cpu_id].append({
                'ktime': ktime,
                'cpu_usage': cpu_usage,
                'memory_bandwidth': memory_bandwidth
            })
            
            # Update the last state for this CPU
            state.update({
                'last_ktime': ktime,
                'last_tsc': tsc,
                'last_cpu_unhalt': cpu_unhalt,
                'last_llc_misses': llc_misses,
                'last_sw_prefetch': sw_prefetch
            })

    # Write all data to a JSON file
    with open(output_json_path, 'w') as json_file:
        json.dump(data_entries, json_file, indent=4)

if __name__ == "__main__":
    parse_hrperf_log_to_json('/hrperf_log.bin', 'hrperf_data.json')