import json

def read_log_file(file_path):
    log_entries = []
    with open(file_path, 'r') as file:
        for line in file:
            if line.strip() and line[0].isdigit():  # Checking if line starts with a digit
                parts = line.split(' ')
                timestamp = int(parts[0].replace('.', ''))
                log_entries.append((timestamp, line.strip()))
    return log_entries

def read_json_file(file_path):
    with open(file_path, 'r') as file:
        return json.load(file)
    
def extract_tag_times(log_entries):
    tag_set_time = None
    tag_clear_time = None
    for time, line in log_entries:
        if "TAG_SET" in line:
            tag_set_time = time
        elif "TAG_CLEAR" in line:
            tag_clear_time = time
    return tag_set_time, tag_clear_time

def integrate_intervals(log_entries, thread_id, intervals):
    integrated_logs = []
    for start, end in intervals:
        integrated_logs.append((start, f"THREAD [{thread_id}] START RUNNING"))
        integrated_logs.append((end, f"THREAD [{thread_id}] END RUNNING, up time: {(end - start)} ns"))
    return integrated_logs + log_entries

def integrate_performance_data(log_entries, cpu_data, tag_set_time, tag_clear_time):
    for entry in cpu_data:
        timestamp = entry['ktime']
        # Only include entries between TAG_SET and TAG_CLEAR times
        if tag_set_time <= timestamp <= tag_clear_time:
            usage = entry['cpu_usage']
            bandwidth = entry['memory_bandwidth']
            log_entries.append((timestamp, f"{timestamp}: CPU Usage: {usage}, Memory Bandwidth: {bandwidth}"))
    return log_entries

def extract_thread_id(log_entries):
    for _, line in log_entries:
        parts = line.split(' ')
        if '[' in parts[2] and ']' in parts[2]:  # Ensure the format is as expected
            thread_id = parts[2].strip('[]')
            return thread_id
    return None  # Return None if no valid thread ID found

def main(req_number):
    log_file = f"req{req_number}.txt"
    executable = "lucene-server"  # This should ideally be extracted from the log file
    interval_file = f"{executable}_intervals.json"
    perf_file = "hrperf_data.json"

    log_entries = read_log_file(log_file)
    intervals_json = read_json_file(interval_file)
    perf_data_json = read_json_file(perf_file)

    thread_id = extract_thread_id(log_entries)
    if thread_id is None:
        print("No valid thread ID found.")
        return

    tag_set_time, tag_clear_time = extract_tag_times(log_entries)
    if not tag_set_time or not tag_clear_time:
        print("TAG_SET or TAG_CLEAR not found.")
        return

    intervals = [(int(x[1]), int(x[2])) for x in intervals_json.get(thread_id, [])]
    log_entries = integrate_intervals(log_entries, thread_id, intervals)

    cpu_number = intervals_json[thread_id][0][0] if thread_id in intervals_json and intervals_json[thread_id] else None
    if cpu_number:
        perf_data = perf_data_json.get(str(cpu_number), [])
        log_entries = integrate_performance_data(log_entries, perf_data, tag_set_time, tag_clear_time)

    log_entries.sort()  # Sorting by timestamp

    with open("consolidate.txt", "w") as out_file:
        for _, entry in log_entries:
            out_file.write(entry + '\n')

if __name__ == "__main__":
    main(303)  # Example request number