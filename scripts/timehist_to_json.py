import json

def parse_timehist(log_filename, executable_name):
    thread_intervals = {}
    with open(log_filename, 'r') as file:
        for line in file:
            parts = line.split()
            if len(parts) < 6:
                continue
            # Check if the current line contains the executable name
            if executable_name in parts[2]:
                # Extract relevant data
                timestamp = int(float(parts[0]) * 1e9)  # Convert to nanoseconds
                tid_pid = parts[2].split('[')[1][:-1]  # Extract thread ID or PID
                runtime = float(parts[5]) * 1e9  # Convert runtime to nanoseconds
                end_timestamp = timestamp + int(runtime)

                # Handle TID/PID formatting
                if '/' in tid_pid:
                    tid = tid_pid.split('/')[0]  # Only take the TID
                else:
                    tid = tid_pid  # Use PID as TID if no TID specified

                # Initialize or append the interval
                if tid not in thread_intervals:
                    thread_intervals[tid] = []
                thread_intervals[tid].append((timestamp, end_timestamp))

    # Write output to a JSON file
    with open(executable_name + '_intervals.json', 'w') as outfile:
        json.dump(thread_intervals, outfile, indent=4)

    print(f"Intervals written to {executable_name}_intervals.json")

# Example usage
log_filename = 'perf_timehist_output.txt'  # The file with perf sched timehist data
executable_name = 'cpu_load'  # The executable to filter by
parse_timehist(log_filename, executable_name)