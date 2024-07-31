import struct

def parse_hrperf_log(file_path):
    # Define the struct format for parsing (int for CPU ID, ktime, tsc, and unsigned long long)
    entry_format = 'iqQQQQ'
    entry_size = struct.calcsize(entry_format)
    
    # Dictionary to store the last state and file handles for each CPU
    last_state = {}
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
            ktime_elapsed_since_first = ktime - state['first_ktime']
            ktime_elapsed_since_last = ktime - state['last_ktime']
            tsc_elapsed_since_last = tsc - state['last_tsc']
            cpu_unhalt_diff = cpu_unhalt - state['last_cpu_unhalt']
            llc_misses_diff = llc_misses - state['last_llc_misses']
            sw_prefetch_diff = sw_prefetch - state['last_sw_prefetch']

            # Calculate estimated memory bandwidth usage
            memory_bandwidth_rate = (llc_misses_diff + sw_prefetch_diff) * 64 / (ktime_elapsed_since_last / 1e3) if ktime_elapsed_since_last > 0 else 0  # in bytes per microsecond

            # Calculate CPU usage
            cpu_usage_rate = cpu_unhalt_diff / tsc_elapsed_since_last if tsc_elapsed_since_last > 0 else 0

            # Update the last state for this CPU
            state.update({
                'last_ktime': ktime,
                'last_tsc': tsc,
                'last_cpu_unhalt': cpu_unhalt,
                'last_llc_misses': llc_misses,
                'last_sw_prefetch': sw_prefetch
            })

            # Write the computed data to the respective file
            file_handles[cpu_id].write(f"CPU {cpu_id}: ktime={ktime}, ktAgg={ktime_elapsed_since_first}, "
                                       f"CPUUnhaltCycles={cpu_unhalt_diff}, "
                                       f"MemoryConsumption={(llc_misses_diff + sw_prefetch_diff) * 64} bytes, "
                                       f"EstMBWRate={memory_bandwidth_rate:.6f} bytes/us, "
                                       f"CPUUse={cpu_usage_rate:.6f}\n")

    # Close all file handles
    for handle in file_handles.values():
        handle.close()

if __name__ == "__main__":
    parse_hrperf_log('/hrperf_log.bin')
