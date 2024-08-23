import re

def read_log_file(filepath):
    with open(filepath, 'r') as file:
        content = file.read()
    ticks = content.split('---\n')
    return ticks

def process_log_data(ticks, interested_funcs, cycles_overhead, mem_overhead):
    results = {func: {'total_mem_per_cycle': 0, 'total_perf_ticks': 0} for func in interested_funcs}
    active_func = None

    for tick in ticks:
        if 'STACK_SAMPLE' in tick:
            # Determine if current tick is related to an interested function
            found_func = next((func for func in interested_funcs if func in tick), None)
            active_func = found_func if found_func else None
        elif 'CPU usage:' in tick and active_func:
            # Extract performance data if there is an active interested function
            match = re.search(r'CPU usage: (\d+\.\d+), Memory bandwidth: (\d+\.\d+) Byte/us, Cycles Delta: (\d+), Mem Delta: (\d+) Bytes', tick)
            if match:
                cpu_usage, mem_bw, cycles_delta, mem_delta = map(float, match.groups())
                adjusted_cycles = cycles_delta - cycles_overhead
                adjusted_mem = mem_delta - mem_overhead
                if adjusted_cycles > 0:  # Avoid division by zero
                    mem_per_cycle = adjusted_mem / adjusted_cycles
                    results[active_func]['total_mem_per_cycle'] += mem_per_cycle
                    results[active_func]['total_perf_ticks'] += 1
        else:
            # Reset active function when encountering a non-performance, non-interested tick
            active_func = None

    # Calculate final averages
    for func, data in results.items():
        if data['total_perf_ticks'] > 0:
            average_mem_per_cycle = data['total_mem_per_cycle'] / data['total_perf_ticks']
            results[func]['average_mem_per_cycle'] = average_mem_per_cycle
        else:
            results[func]['average_mem_per_cycle'] = 0

    # Print results
    for func, data in results.items():
        print(f"Function: {func}")
        print(f"Average Memory Consumption per Cycle: {data['average_mem_per_cycle']:.6f}")
        print(f"Total Performance Ticks Considered: {data['total_perf_ticks']}")


if __name__ == "__main__":
    filepath = './merged.txt'
    interested_funcs = [
        'collect(doc) (TermScorer.cpp:54:22)',
        'read(docs, freqs) (TermScorer.cpp:57:38)'
    ]
    cycles_overhead_per_tick = 6341.928
    mem_overhead_per_tick = 345.128

    ticks = read_log_file(filepath)
    process_log_data(ticks, interested_funcs, cycles_overhead_per_tick, mem_overhead_per_tick)