import struct

def parse_hrperf_log(file_path):
    # Define the struct format for parsing (int for CPU ID, ktime, tsc, and unsigned long long)
    entry_format = 'iqQQQQ'
    entry_size = struct.calcsize(entry_format)
    
    # Dictionary to store the initial state and file handles for each CPU
    initial_state = {}
    file_handles = {}

    with open(file_path, 'rb') as f:
        while True:
            data = f.read(entry_size)
            if not data:
                break
            
            cpu_id, ktime, tsc, cpu_unhalt, llc_misses, sw_prefetch = struct.unpack(entry_format, data)
            
            if cpu_id not in file_handles:
                # Open a new file for this CPU if it hasn't been opened yet
                file_handles[cpu_id] = open(f'log_aggregated_{cpu_id}.txt', 'w')
                # Initialize the initial record for this CPU
                initial_state[cpu_id] = {
                    'first_ktime': ktime,
                    'first_tsc': tsc,
                    'first_cpu_unhalt': cpu_unhalt,
                    'first_llc_misses': llc_misses,
                    'first_sw_prefetch': sw_prefetch
                }

            # Retrieve the initial state
            state = initial_state[cpu_id]
            ktime_elapsed = ktime - state['first_ktime']
            ktime_elapsed_since_last = ktime - state['first_ktime']
            cpu_unhalt_diff = cpu_unhalt - state['first_cpu_unhalt']
            llc_misses_diff = llc_misses - state['first_llc_misses']
            sw_prefetch_diff = sw_prefetch - state['first_sw_prefetch']
            
            # Calculate estimated memory bandwidth usage
            memory_consumption = (llc_misses_diff + sw_prefetch_diff) * 64  # in bytes

            # Calculate CPU usage
            cpu_usage = cpu_unhalt_diff / (tsc - state['first_tsc']) if (tsc - state['first_tsc']) > 0 else 0
            
            # Calculate memory bandwidth rate
            memory_bandwidth_rate = memory_consumption / (ktime_elapsed / 1e3) if ktime_elapsed > 0 else 0  # in bytes per microsecond

            # Write the computed data to the respective file
            file_handles[cpu_id].write(f"CPU {cpu_id}: ktime={ktime}, ktAgg={ktime_elapsed}, "
                                       f"CPUUnhaltCycles={cpu_unhalt_diff}, "
                                       f"MemoryConsumption={memory_consumption} bytes, "
                                       f"EstMBWRate={memory_bandwidth_rate:.6f} bytes/us, "
                                       f"CPUUse={cpu_usage:.6f}\n")

    # Close all file handles
    for handle in file_handles.values():
        handle.close()

if __name__ == "__main__":
    parse_hrperf_log('/hrperf_log.bin')
