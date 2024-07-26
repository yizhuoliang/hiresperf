import struct

def parse_hrperf_log(file_path):
    # Define the struct format for parsing (int for CPU ID and 4 unsigned long longs for tick data)
    entry_format = 'iQQQQ'
    entry_size = struct.calcsize(entry_format)

    with open(file_path, 'rb') as f:
        while True:
            data = f.read(entry_size)
            if not data:
                break
            cpu_id, tsc, cpu_unhalt, llc_misses, sw_prefetch = struct.unpack(entry_format, data)
            print(f"CPU {cpu_id}: TSC={tsc}, CPU Unhalt={cpu_unhalt}, LLC Misses={llc_misses}, SW Prefetch={sw_prefetch}")

if __name__ == "__main__":
    parse_hrperf_log('/hrperf_log.bin')