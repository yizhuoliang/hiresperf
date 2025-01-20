import sys
import os
import duckdb

# Event type constants (should match those in the first script)
EVENT_STACK_SAMPLE = 1

def main():
    # We expect either 1 or 2 arguments:
    #   1) The required func_desc_id (integer)
    #   2) The optional sampling_ratio (float)
    if len(sys.argv) < 2 or len(sys.argv) > 3:
        print('Expected usage: {0} <func_desc_id> [sampling_ratio]'.format(sys.argv[0]))
        sys.exit(1)
        
    # Parse the integer func_desc_id
    try:
        function_of_interest_id = int(sys.argv[1])
    except ValueError:
        print("func_desc_id must be an integer.")
        sys.exit(1)
    
    # Parse optional sampling ratio or default to 1.0
    sampling_ratio = float(sys.argv[2]) if len(sys.argv) == 3 else 1.0

    # Validate sampling ratio
    if not (0.0 < sampling_ratio <= 1.0):
        print("Sampling ratio must be between 0 (exclusive) and 1 (inclusive).")
        sys.exit(1)

    # Connect to the on-disk database
    con = duckdb.connect(database='analysis.duckdb')

    # Create indices if they don't exist
    con.execute("CREATE INDEX IF NOT EXISTS idx_timestamp ON ldb_events (timestamp_ns)")
    con.execute("CREATE INDEX IF NOT EXISTS idx_thread_id ON ldb_events (thread_id)")
    con.execute("CREATE INDEX IF NOT EXISTS idx_event_type ON ldb_events (event_type)")
    con.execute("CREATE INDEX IF NOT EXISTS idx_func_desc_id ON ldb_events (func_desc_id)")

    # Create or ensure existence of a table named after func_desc_id for storing invocations
    # For clarity, you may wish to rename the table to something generic or keep it per func_desc_id
    invocations_table = f"func_{function_of_interest_id}_invocations"
    con.execute(f'''
        CREATE TABLE IF NOT EXISTS {invocations_table} (
            id BIGINT,
            thread_id BIGINT,
            start_time_ns BIGINT,
            end_time_ns BIGINT,
            latency_us DOUBLE,
            func_desc VARCHAR,
            avg_stalls_per_us DOUBLE,
            avg_cpu_usage DOUBLE,
            avg_memory_bandwidth_bytes_per_us DOUBLE,
            avg_inst_retire_per_us DOUBLE,
            performance_event_ids VARCHAR,
            avg_total_memory_bandwidth_bytes_per_us_total DOUBLE
        )
    ''')

    # Fetch existing invocation IDs to determine the next ID
    result = con.execute(f"SELECT MAX(id) FROM {invocations_table}").fetchone()
    next_id = (result[0] + 1) if result[0] is not None else 1

    # Find invocations (stack samples) of the target func_desc_id, with sampling
    print(f"Finding invocations of func_desc_id '{function_of_interest_id}' with sampling ratio {sampling_ratio}...")
    # Fetch all STACK_SAMPLE events for the func_desc_id, applying sampling
    stack_samples = con.execute(f'''
        SELECT timestamp_ns, thread_id, latency_us, func_desc
        FROM ldb_events
        WHERE event_type = {EVENT_STACK_SAMPLE}
          AND func_desc_id = {function_of_interest_id}
          AND random() < {sampling_ratio}
    ''').fetchall()

    # Insert invocations into the table
    print(f"Inserting {len(stack_samples)} invocations into '{invocations_table}' table...")
    insert_query = f'''
        INSERT INTO {invocations_table} (
            id, thread_id, start_time_ns, end_time_ns, latency_us, func_desc
        ) VALUES (?, ?, ?, ?, ?, ?)
    '''

    for event in stack_samples:
        end_time_ns = event[0]     # timestamp_ns of the event
        thread_id = event[1]
        latency_us = event[2]
        func_desc = event[3]
        latency_ns = int(latency_us * 1000)  # Convert us to ns
        start_time_ns = end_time_ns - latency_ns

        con.execute(insert_query, (
            next_id,
            thread_id,
            start_time_ns,
            end_time_ns,
            latency_us,
            func_desc
        ))
        next_id += 1

    # Close the connection
    con.close()
    print(f"Invocations table '{invocations_table}' updated in 'analysis.duckdb'")

if __name__ == "__main__":
    main()