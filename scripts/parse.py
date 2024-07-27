import struct

def parse_hrperf_log(file_path):
    # Define the struct format for parsing (int for CPU ID and 4 unsigned long longs for tick data)
    entry_format = 'iQQQQ'
    entry_size = struct.calcsize(entry_format)
    
    # Dictionary to store the last state of each CPU
    last_state = {}

    with open(file_path, 'rb') as f:
        while True:
            data = f.read(entry_size)
            if not data:
                break
            
            # Unpack the data into respective fields
            cpu_id, tsc, cpu_unhalt, llc_misses, sw_prefetch = struct.unpack(entry_format, data)
            
            # Calculate the new columns
            if cpu_id in last_state:
                # Retrieve the last state for this CPU
                last_tsc, last_cpu_unhalt, last_llc_misses, last_sw_prefetch = last_state[cpu_id]
                
                # Calculate differences
                tsc_elapsed_since_first = tsc - last_state[cpu_id][0]
                tsc_elapsed_since_last = tsc - last_tsc
                cpu_unhalt_diff = cpu_unhalt - last_cpu_unhalt
                llc_misses_diff = llc_misses - last_llc_misses
                sw_prefetch_diff = sw_prefetch - last_sw_prefetch
            else:
                # Initialize for the first record of this CPU
                tsc_elapsed_since_first = 0
                tsc_elapsed_since_last = 0
                cpu_unhalt_diff = 0
                llc_misses_diff = 0
                sw_prefetch_diff = 0
            
            # Update the last state for this CPU
            last_state[cpu_id] = (tsc, cpu_unhalt, llc_misses, sw_prefetch)
            
            # Print the results
            print(f"CPU {cpu_id}: TSC={tsc}, TSC Elapsed Since First={tsc_elapsed_since_first}, "
                  f"TSC Elapsed Since Last={tsc_elapsed_since_last}, CPU Unhalt Diff={cpu_unhalt_diff}, "
                  f"LLC Misses Diff={llc_misses_diff}, SW Prefetch Diff={sw_prefetch_diff}")

if __name__ == "__main__":
    parse_hrperf_log('/hrperf_log.bin')
