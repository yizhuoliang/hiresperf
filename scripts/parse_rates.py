import struct

def parse_hrperf_log(file_path, max_frequency_ghz):
    # Updated struct format to match the new log structure
    entry_format = 'iqQQQQQ'  # Adjusted for the new fields
    entry_size = struct.calcsize(entry_format)
    
    # Dictionary to store the last state and file handles for each CPU
    last_state = {}
    file_handles = {}

    tsc_per_us = max_frequency_ghz * 1000  # TSC increments per microsecond

    with open(file_path, 'rb') as f:
        while True:
            data = f.read(entry_size)
            if not data:
                break
            
            # Unpack the data according to the updated structure
            cpu_id, ktime, stall_mem, inst_retire, cpu_unhalt, llc_misses, sw_prefetch = struct.unpack(entry_format, data)
            
            if cpu_id not in file_handles:
                # Open a new file for this CPU if it hasn't been opened yet
                file_handles[cpu_id] = open(f'log_parsed_{cpu_id}.txt', 'w')
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

            # Retrieve the previous state
            state = last_state[cpu_id]
            ktime_elapsed_since_first = ktime - state['first_ktime']
            ktime_elapsed_since_last = ktime - state['last_ktime']
            stall_mem_since_last = stall_mem - state['last_stall_mem']
            inst_retire_since_last = inst_retire - state['last_inst_retire']
            
            # Calculate rates and CPU usage
            stalls_per_us = stall_mem_since_last / (ktime_elapsed_since_last / 1e3) if ktime_elapsed_since_last > 0 else 0
            inst_retire_rate = inst_retire_since_last / (ktime_elapsed_since_last / 1e3) if ktime_elapsed_since_last > 0 else 0
            cpu_usage = (cpu_unhalt - state['last_cpu_unhalt']) / (tsc_per_us * (ktime_elapsed_since_last / 1e3)) if ktime_elapsed_since_last > 0 else 0

            llc_misses_rate = (llc_misses - state['last_llc_misses']) / (ktime_elapsed_since_last / 1e3) if ktime_elapsed_since_last > 0 else 0
            sw_prefetch_rate = (sw_prefetch - state['last_sw_prefetch']) / (ktime_elapsed_since_last / 1e3) if ktime_elapsed_since_last > 0 else 0
            memory_bandwidth = (llc_misses_rate + sw_prefetch_rate) * 64  # in bytes per microsecond

            # Update the last state for this CPU
            state.update({
                'last_ktime': ktime,
                'last_stall_mem': stall_mem,
                'last_inst_retire': inst_retire,
                'last_cpu_unhalt': cpu_unhalt,
                'last_llc_misses': llc_misses,
                'last_sw_prefetch': sw_prefetch
            })

            # Write the computed data to the respective file
            file_handles[cpu_id].write(f"CPU {cpu_id}: ktime={ktime}, ktAgg={ktime_elapsed_since_first}, "
                                       f"ktDelta={ktime_elapsed_since_last}, StallsRate={stalls_per_us:.6f} /us, "
                                       f"InstRetRate={inst_retire_rate:.6f} /us, L3MisRate={llc_misses_rate:.6f}, "
                                       f"SWPrfRate={sw_prefetch_rate:.6f}, EstMBW={memory_bandwidth:.6f} bytes/us, "
                                       f"CPUUse={cpu_usage:.6f}\n")

    # Close all file handles
    for handle in file_handles.values():
        handle.close()

if __name__ == "__main__":
    max_frequency_ghz = 2.5  # Example hardware maximum frequency
    parse_hrperf_log('/hrperf_log.bin', max_frequency_ghz)
