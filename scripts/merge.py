import json

def read_log_entries(log_filename, thread_id):
    entries = []
    with open(log_filename, 'r') as file:
        current_entry = []
        for line in file:
            if line.startswith('---'):
                if current_entry:
                    entries.append('\n'.join(current_entry) + '\n---')
                    current_entry = []
            else:
                parts = line.split()
                if parts and parts[0].isdigit() and f'[{thread_id}]' in line:
                    current_entry.append(line.strip())
    return entries

def read_performance_data(perf_filename, core_id):
    with open(perf_filename, 'r') as file:
        data = json.load(file)
        if core_id in data:
            return [
                f"{item['ktime']} CPU usage: {item['cpu_usage']:.3f}, Memory Delta: {item['memory_delta']:.3f} Bytes, Real Cycles Delta: {item['real_cycles_delta']}, Stalls Delta: {item['stalls_delta']}\n---"
                for item in data[core_id]
            ]
        else:
            return []

def merge_entries(log_entries, perf_entries):
    log_index = 0
    perf_index = 0
    merged = []

    while log_index < len(log_entries) and perf_index < len(perf_entries):
        log_time = int(log_entries[log_index].split()[0])
        perf_time = int(perf_entries[perf_index].split()[0])

        if log_time <= perf_time:
            merged.append(log_entries[log_index])
            log_index += 1
        else:
            merged.append(perf_entries[perf_index])
            perf_index += 1

    # Append remaining entries
    merged.extend(log_entries[log_index:])
    merged.extend(perf_entries[perf_index:])

    return merged

def main():
    thread_id = '116219'
    cpu_core = '20'
    log_filename = './ldb_dumped.txt'
    perf_filename = './hrperf_data.json'

    log_entries = read_log_entries(log_filename, thread_id)
    perf_entries = read_performance_data(perf_filename, cpu_core)
    merged_entries = merge_entries(log_entries, perf_entries)

    with open('merged.txt', 'w') as out_file:
        out_file.write('\n'.join(merged_entries))

if __name__ == "__main__":
    main()