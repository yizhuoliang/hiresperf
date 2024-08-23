# Calculate the average cpu cycles and mem access caused by each hrperf tick
import struct
import sys

def calculate_averages(file_path, cpu_id):
    # Define the struct format for parsing
    entry_format = 'iqQQQQ'
    entry_size = struct.calcsize(entry_format)

    # Variables to store cumulative values and count
    total_cycles_delta = 0
    total_mem_delta = 0
    entry_count = 0

    with open(file_path, 'rb') as f:
        while True:
            data = f.read(entry_size)
            if not data:
                break

            read_cpu_id, ktime, tsc, cpu_unhalt, llc_misses, sw_prefetch = struct.unpack(entry_format, data)
            if read_cpu_id == cpu_id:
                if entry_count == 0:
                    # Initialize the first record
                    last_ktime = ktime
                    last_tsc = tsc
                    last_cpu_unhalt = cpu_unhalt
                    last_llc_misses = llc_misses
                    last_sw_prefetch = sw_prefetch
                else:
                    # Calculate deltas
                    cycles_delta = cpu_unhalt - last_cpu_unhalt
                    llc_misses_delta = llc_misses - last_llc_misses
                    sw_prefetch_delta = sw_prefetch - last_sw_prefetch
                    mem_delta = llc_misses_delta + sw_prefetch_delta

                    # Accumulate values
                    total_cycles_delta += cycles_delta
                    total_mem_delta += mem_delta

                # Update last values
                last_ktime = ktime
                last_tsc = tsc
                last_cpu_unhalt = cpu_unhalt
                last_llc_misses = llc_misses
                last_sw_prefetch = sw_prefetch
                entry_count += 1

    if entry_count > 0:
        # Calculate averages
        avg_cycles_delta = total_cycles_delta / entry_count
        avg_mem_delta = total_mem_delta / entry_count
        print(f"Averaged cycles_delta for CPU {cpu_id}: {avg_cycles_delta}")
        print(f"Averaged mem_delta for CPU {cpu_id}: {avg_mem_delta}")
    else:
        print(f"No data found for CPU {cpu_id}")

if __name__ == "__main__":
    if len(sys.argv) != 3:
        print("Usage: python average_overhead.py <file_path> <cpu_id>")
    else:
        file_path = sys.argv[1]
        cpu_id = int(sys.argv[2])
        calculate_averages(file_path, cpu_id)
