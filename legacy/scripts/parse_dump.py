import struct

def dump_hrperf_log(file_path):
    # Define the struct format for parsing (int for CPU ID, ktime, tsc, and unsigned long long)
    entry_format = 'iqQQQQQ'
    entry_size = struct.calcsize(entry_format)
    
    # Dictionary to store file handles for each CPU
    file_handles = {}

    with open(file_path, 'rb') as f:
        while True:
            data = f.read(entry_size)
            if not data:
                break
            
            cpu_id, ktime, stall_mem, inst_retire, cpu_unhalt,llc_misses, sw_prefetch = struct.unpack(entry_format, data)
            
            if cpu_id not in file_handles:
                # Open a new file for this CPU if it hasn't been opened yet
                file_handles[cpu_id] = open(f'log_dumped_{cpu_id}.txt', 'w')

            # Write the original data to the respective file
            file_handles[cpu_id].write(f"CPU {cpu_id}: ktime={ktime}, stall_mem={stall_mem}, "
                                       f"inst_retire={inst_retire}, cpu_unhalt={cpu_unhalt}, llc_misses={llc_misses}, "
                                       f"sw_prefetch={sw_prefetch}\n")

    # Close all file handles
    for handle in file_handles.values():
        handle.close()

if __name__ == "__main__":
    dump_hrperf_log('/hrperf_log.bin')
