import sys
import os
import duckdb
import pandas as pd

def parse_timehist(log_filename, executable_name):
    data = []
    with open(log_filename, 'r') as file:
        for line in file:
            parts = line.strip().split()
            if len(parts) < 6:
                continue
            # Check if the current line contains the executable name
            if executable_name in parts[2]:
                # Extract relevant data
                timestamp_ns = int(float(parts[0]) * 1e9)  # Convert to nanoseconds
                tid_pid = parts[2].split('[')[1][:-1]  # Extract thread ID or PID
                runtime_ns = int(float(parts[5]) * 1e6)  # Convert runtime to nanoseconds
                end_timestamp_ns = timestamp_ns + runtime_ns
                core_number = int(parts[1].strip('[]'))  # Extract core number

                # Handle TID/PID formatting
                if '/' in tid_pid:
                    tid = int(tid_pid.split('/')[0])  # Only take the TID
                else:
                    tid = int(tid_pid)  # Use PID as TID if no TID specified

                # Append the interval data
                data.append({
                    'thread_id': tid,
                    'core_number': core_number,
                    'start_time_ns': timestamp_ns,
                    'end_time_ns': end_timestamp_ns
                })

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

    log_filename = sys.argv[1]  # The file with perf sched timehist data
    executable_name = sys.argv[2]  # The executable to filter by
    parse_timehist(log_filename, executable_name)
