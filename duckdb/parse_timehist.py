import sys
import os
import duckdb
import pandas as pd

def parse_timehist(log_filename, executable_name):
    data = []
    executable_pids = set()  # Set to store PIDs associated with the executable
    warning_lines = []       # List to store lines with '-1' TID

    with open(log_filename, 'r') as file:
        lines = file.readlines()

    for line in lines:
        parts = line.strip().split()
        if len(parts) < 6:
            continue

        # Extract the process info
        process_info = parts[2]

        # Extract process name and TID/PID
        if '[' in process_info and ']' in process_info:
            process_name = process_info.split('[')[0]
            tid_pid_str = process_info.split('[')[1][:-1]  # Remove the closing ']'
        else:
            continue  # Skip lines without proper formatting

        # Extract TID and PID
        if '/' in tid_pid_str:
            tid_str, pid_str = tid_pid_str.split('/')
            try:
                tid = int(tid_str)
                pid = int(pid_str)
            except ValueError:
                continue  # Skip lines with invalid TID/PID
        else:
            try:
                tid = int(tid_pid_str)
                pid = None
            except ValueError:
                continue  # Skip lines with invalid TID

        # Collect PIDs associated with the executable
        if process_name == executable_name:
            if pid is not None:
                executable_pids.add(pid)

        # Check for '-1' TID with matching PID
        if tid == -1 and pid in executable_pids:
            warning_lines.append(line.strip())

        # Only process lines related to the executable
        if process_name == executable_name:
            # Extract relevant data
            try:
                end_timestamp_ns = int(float(parts[0]) * 1e9)      # Convert to nanoseconds
                runtime_ns = int(float(parts[5]) * 1e6)            # Convert runtime to nanoseconds
                start_timestamp_ns = end_timestamp_ns - runtime_ns
                core_number = int(parts[1].strip('[]'))            # Extract core number
            except (ValueError, IndexError):
                continue  # Skip lines with invalid numerical values

            # Append the interval data
            data.append({
                'thread_id': tid,
                'core_number': core_number,
                'start_time_ns': start_timestamp_ns,
                'end_time_ns': end_timestamp_ns
            })

    # Print warnings if any '-1' TID lines were found
    if warning_lines:
        print("\n" + "!"*80)
        print("WARNING: Detected '-1' TID for the following lines associated with executable '{}':".format(executable_name))
        for wline in warning_lines:
            print(wline)
        print("Perf sched output may be corrupted. Please consider rerunning the experiments.")
        print("!"*80 + "\n")

    # Check if data is not empty
    if not data:
        print("No data found for the specified executable.")
        return

    # Convert data to Pandas DataFrame
    df_intervals = pd.DataFrame(data)

    # Ensure correct data types
    df_intervals = df_intervals.astype({
        'thread_id': 'int64',
        'core_number': 'int32',
        'start_time_ns': 'int64',
        'end_time_ns': 'int64'
    })

    # Connect to the 'analysis.duckdb' database
    con = duckdb.connect(database='analysis.duckdb')

    # Create 'threads_scheduling' table if it doesn't exist
    con.execute('''
        CREATE TABLE IF NOT EXISTS threads_scheduling (
            thread_id BIGINT,
            core_number INTEGER,
            start_time_ns BIGINT,
            end_time_ns BIGINT
        )
    ''')

    # Register DataFrame and insert data
    con.register('df_intervals', df_intervals)
    con.execute('''
        INSERT INTO threads_scheduling
        SELECT thread_id, core_number, start_time_ns, end_time_ns
        FROM df_intervals
    ''')

    # Create an index on 'thread_id'
    con.execute('CREATE INDEX IF NOT EXISTS idx_thread_id ON threads_scheduling (thread_id)')

    # Close the connection
    con.close()

    print(f"Data inserted into 'threads_scheduling' table in 'analysis.duckdb'")

if __name__ == "__main__":
    if len(sys.argv) != 3:
        print('Expected usage: python parse_timehist.py <log_filename> <executable_name>')
        sys.exit(1)

    log_filename = sys.argv[1]      # The file with perf sched timehist data
    executable_name = sys.argv[2]   # The executable to filter by
    parse_timehist(log_filename, executable_name)
