import sys
import os
import subprocess
import duckdb
import pandas as pd
import re

def parse_sched_script(perf_executable_path, perf_data_filename, executable_name):
    sched_dumped_filename = 'sched_dumped.txt'
    executable_pids = set()
    per_thread_data = {}
    warning_lines = []  # To store any potential warnings

    # Check if perf_data_filename exists
    if not os.path.exists(perf_data_filename):
        print(f"Cannot find '{perf_data_filename}'.")
        return

    # Check if sched_dumped.txt already exists
    if os.path.exists(sched_dumped_filename):
        print(f"Error: '{sched_dumped_filename}' already exists. Please remove or rename it before running the script.")
        return

    # Generate sched_dumped.txt
    print(f"Generating '{sched_dumped_filename}' from '{perf_data_filename}'...")
    cmd = f'{perf_executable_path} sched script --ns -F -comm -i {perf_data_filename}'
    with open(sched_dumped_filename, 'w') as outfile:
        result = subprocess.run(cmd.split(), stdout=outfile)
        if result.returncode != 0:
            print("Failed to generate sched_dumped.txt using perf sched script.")
            return

    # Regular expression to parse the sched_switch lines
    line_pattern = re.compile(r'^(\S+)\s+\[(\d+)\]\s+([\d\.]+):\s+(\S+):\s+(.*)$')

    data = []

    with open(sched_dumped_filename, 'r') as file:
        lines = file.readlines()

    for line in lines:
        line = line.strip()
        match = line_pattern.match(line)
        if not match:
            continue

        current_pid = match.group(1)
        cpu_id = int(match.group(2))
        timestamp_ns = int(float(match.group(3)) * 1e9)
        event_name = match.group(4)
        event_details = match.group(5)

        if event_name != 'sched:sched_switch':
            continue  # We only process sched_switch events

        # Split event_details by '==>'
        if '==' not in event_details:
            continue  # Malformed line

        prev_part_str, next_part_str = event_details.split('==>')

        # Parse previous process
        prev_tokens = prev_part_str.strip().split()
        if len(prev_tokens) < 2:
            continue  # Malformed line

        prev_comm_pid = prev_tokens[0]
        prev_prio = prev_tokens[1].strip('[]')
        prev_state = ' '.join(prev_tokens[2:]) if len(prev_tokens) > 2 else ''

        # Parse next process
        next_tokens = next_part_str.strip().split()
        if len(next_tokens) < 2:
            continue  # Malformed line

        next_comm_pid = next_tokens[0]
        next_prio = next_tokens[1].strip('[]')

        # Extract prev_comm and prev_pid
        if ':' in prev_comm_pid:
            prev_comm, prev_pid_str = prev_comm_pid.rsplit(':', 1)
            try:
                prev_pid = int(prev_pid_str)
            except ValueError:
                continue  # Skip lines with invalid PID
        else:
            continue  # Skip lines without proper prev_comm:prev_pid format

        # Extract next_comm and next_pid
        if ':' in next_comm_pid:
            next_comm, next_pid_str = next_comm_pid.rsplit(':', 1)
            try:
                next_pid = int(next_pid_str)
            except ValueError:
                continue  # Skip lines with invalid PID
        else:
            continue  # Skip lines without proper next_comm:next_pid format

        # Collect PIDs associated with the executable
        if prev_comm == executable_name:
            executable_pids.add(prev_pid)
        if next_comm == executable_name:
            executable_pids.add(next_pid)

        # Handle scheduled out event for prev_pid
        if prev_pid in executable_pids:
            if prev_pid not in per_thread_data:
                per_thread_data[prev_pid] = {'intervals': [], 'current_interval': None}

            if per_thread_data[prev_pid]['current_interval'] is not None:
                # Close the interval
                per_thread_data[prev_pid]['current_interval']['end_time_ns'] = timestamp_ns
                per_thread_data[prev_pid]['intervals'].append(per_thread_data[prev_pid]['current_interval'])
                per_thread_data[prev_pid]['current_interval'] = None

        # Handle scheduled in event for next_pid
        if next_pid in executable_pids:
            if next_pid not in per_thread_data:
                per_thread_data[next_pid] = {'intervals': [], 'current_interval': None}

            if per_thread_data[next_pid]['current_interval'] is None:
                # Start a new interval
                per_thread_data[next_pid]['current_interval'] = {
                    'thread_id': next_pid,
                    'core_number': cpu_id,
                    'start_time_ns': timestamp_ns,
                    'end_time_ns': None
                }
            else:
                # Thread is already running, possible data issue
                warning_lines.append(line)

    # After processing all lines, close any intervals that are still open
    for pid, data_dict in per_thread_data.items():
        if data_dict['current_interval'] is not None:
            # Set end_time_ns to the last timestamp
            data_dict['current_interval']['end_time_ns'] = timestamp_ns
            data_dict['intervals'].append(data_dict['current_interval'])
            data_dict['current_interval'] = None

    # Print warnings if any
    if warning_lines:
        print("\n" + "!"*80)
        print("WARNING: Detected possible overlapping intervals or data issues for executable '{}':".format(executable_name))
        for wline in warning_lines:
            print(wline)
        print("Please check the perf sched script output for inconsistencies.")
        print("!"*80 + "\n")

    # Check if data is not empty
    if not per_thread_data:
        print("No scheduling data found for the specified executable.")
        return

    # Aggregate intervals into a single list
    all_intervals = []
    for pid, data_dict in per_thread_data.items():
        all_intervals.extend(data_dict['intervals'])

    # Convert data to Pandas DataFrame
    df_intervals = pd.DataFrame(all_intervals)

    # Ensure correct data types
    df_intervals = df_intervals.astype({
        'thread_id': 'int64',
        'core_number': 'int32',
        'start_time_ns': 'int64',
        'end_time_ns': 'int64'
    })

    # Connect to the 'analysis.duckdb' database
    con = duckdb.connect(database='analysis.duckdb')

    # Check if 'threads_scheduling' table exists
    table_exists = con.execute("""
        SELECT COUNT(*) FROM information_schema.tables
        WHERE table_name = 'threads_scheduling' AND table_schema = 'main'
    """).fetchone()[0]
    if table_exists:
        print("Error: Table 'threads_scheduling' already exists in 'analysis.duckdb'.")
        con.close()
        return

    # Create 'threads_scheduling' table
    con.execute('''
        CREATE TABLE threads_scheduling (
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
    con.execute('CREATE INDEX idx_thread_id ON threads_scheduling (thread_id)')

    # Close the connection
    con.close()

    print(f"Data inserted into 'threads_scheduling' table in 'analysis.duckdb'")

if __name__ == "__main__":
    if len(sys.argv) != 4:
        print('Expected usage: python parse_sched.py <perf_executable_path> <perf_data_filename> <executable_name>')
        sys.exit(1)

    perf_executable_path = sys.argv[1]  # The path to the perf executable
    perf_data_filename = sys.argv[2]    # The perf.data file
    executable_name = sys.argv[3]       # The executable to filter by
    parse_sched_script(perf_executable_path, perf_data_filename, executable_name)
