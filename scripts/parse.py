import struct

def parse_hrperf_log(file_path):
    # Define the struct format for parsing (int for CPU ID and 4 unsigned long longs for tick data)
    entry_format = 'iQQQQ'
    entry_size = struct.calcsize(entry_format)
    
    # Dictionary to store the last state, file handles, and first TSC for each CPU
    last_state = {}
    file_handles = {}

    with open(file_path, 'rb') as f:
        while True:
            data = f.read(entry_size)
            if not data:
                break
            
            cpu_id, tsc, cpu_unhalt, llc_misses, sw_prefetch = struct.unpack(entry_format, data)
            
            if cpu_id not in file_handles:
                # Open a new file for this CPU if it hasn't been opened yet
                file_handles[cpu_id] = open(f'log_parsed_{cpu_id}.txt', 'w')
                # Initialize the first record and first TSC for this CPU
                last_state[cpu_id] = {
                    'first_tsc': tsc,
                    'last_tsc': tsc,
                    'last_cpu_unhalt': cpu_unhalt,
                    'last_llc_misses': llc_misses,
                    'last_sw_prefetch': sw_prefetch
                }

            # Retrieve the previous state
            state = last_state[cpu_id]
            tsc_elapsed_since_first = tsc - state['first_tsc']
            tsc_elapsed_since_last = tsc - state['last_tsc']
            cpu_unhalt_diff = cpu_unhalt - state['last_cpu_unhalt']
            llc_misses_diff = llc_misses - state['last_llc_misses']
            sw_prefetch_diff = sw_prefetch - state['last_sw_prefetch']

            # Update the last state for this CPU
            state.update({
                'last_tsc': tsc,
                'last_cpu_unhalt': cpu_unhalt,
                'last_llc_misses': llc_misses,
                'last_sw_prefetch': sw_prefetch
            })

            # Write the computed data to the respective file
            file_handles[cpu_id].write(f"CPU {cpu_id}: TSC={tsc}, TSC Aggregate={tsc_elapsed_since_first}, "
                                       f"TSC Delta={tsc_elapsed_since_last}, CPUUnhalt Delta={cpu_unhalt_diff}, "
                                       f"LLC Miss Delta={llc_misses_diff}, SW Prefetch Delta={sw_prefetch_diff}\n")

    # Close all file handles
    for handle in file_handles.values():
        handle.close()

if __name__ == "__main__":
    parse_hrperf_log('/hrperf_log.bin')