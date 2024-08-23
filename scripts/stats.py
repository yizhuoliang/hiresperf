import re

def read_log_file(filepath):
    with open(filepath, 'r') as file:
        content = file.read()
    ticks = content.split('---\n')
    return ticks

def process_log_data(ticks, interested_funcs, cycles_overhead_per_tick, mem_overhead_per_tick):
    results = {func: {'total_mem_per_cycle': 0, 'count': 0, 'total_perf_ticks': 0} for func in interested_funcs}
    active_func = None
    cumulative_performance = []

    for tick in ticks:
        if 'STACK_SAMPLE' in tick:
            found_func = any(func in tick for func in interested_funcs)
            if found_func:
                active_func = next((func for func in interested_funcs if func in tick), None)
                if active_func:
                    # Reset the cumulative performance data for a new function call tick
                    cumulative_performance = []
        elif 'CPU usage:' in tick and active_func:
            # Extract performance data
            match = re.search(r'CPU usage: (\d+\.\d+), Memory bandwidth: (\d+\.\d+) Byte/us, Cycles Delta: (\d+), Mem Delta: (\d+) Bytes', tick)
            if match:
                cpu_usage, mem_bw, cycles_delta, mem_delta = map(float, match.groups())
                adjusted_cycles = cycles_delta - cycles_overhead_per_tick
                adjusted_mem = mem_delta - mem_overhead_per_tick
                if adjusted_cycles > 0:  # To avoid division by zero or negative values
                    mem_per_cycle = adjusted_mem / adjusted_cycles
                    cumulative_performance.append(mem_per_cycle)
                    results[active_func]['total_perf_ticks'] += 1
        else:
            if active_func and cumulative_performance:
                # Finalize the function's performance tick group
                average_mem_per_cycle = sum(cumulative_performance) / len(cumulative_performance) if cumulative_performance else 0
                results[active_func]['total_mem_per_cycle'] += average_mem_per_cycle
                results[active_func]['count'] += 1
            active_func = None

    # Calculate final averages and print results
    for func, data in results.items():
        average_over_counts = data['total_mem_per_cycle'] / data['count'] if data['count'] > 0 else 0
        print(f"Function: {func}")
        print(f"Average Memory Consumption per Cycle: {average_over_counts:.6f}")
        print(f"Occurrences: {data['count']}")
        print(f"Total Performance Ticks Considered: {data['total_perf_ticks']}\n")

if __name__ == "__main__":
    filepath = './merged.txt'
    interested_funcs = [
        'readVInt() (SegmentTermDocs.cpp:150:45)',
        'read(docs, freqs) (TermScorer.cpp:57:38)',
        'collect(doc) (TermScorer.cpp:54:22)'
    ]
    cycles_overhead_per_tick = 6341.928
    mem_overhead_per_tick = 345.128

    ticks = read_log_file(filepath)
    process_log_data(ticks, interested_funcs, cycles_overhead_per_tick, mem_overhead_per_tick)