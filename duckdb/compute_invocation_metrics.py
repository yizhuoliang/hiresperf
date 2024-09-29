import sys
import duckdb

def main():
    if len(sys.argv) != 2:
        print('Expected usage: python compute_invocation_metrics.py <function_name>')
        sys.exit(1)

    function_name = sys.argv[1]
    invocations_table = f"{function_name}_invocations"
    inv_to_perf_table = f"{function_name}_inv_to_perf"

    # Connect to the 'analysis.duckdb' database
    con = duckdb.connect(database='analysis.duckdb')

    # Ensure the necessary tables exist
    required_tables = ['threads_scheduling', 'performance_events', 'node_memory_bandwidth', invocations_table]
    tables = [row[0] for row in con.execute("SHOW TABLES").fetchall()]
    for tbl in required_tables:
        if tbl not in tables:
            print(f"Error: Required table '{tbl}' does not exist in 'analysis.duckdb'.")
            con.close()
            sys.exit(1)

    # Create indexes to speed up queries
    print("Creating indexes to speed up computations...")
    con.execute("CREATE INDEX IF NOT EXISTS idx_perf_cpu_time ON performance_events (cpu_id, timestamp_ns)")
    con.execute("CREATE INDEX IF NOT EXISTS idx_sched_thread_time ON threads_scheduling (thread_id, start_time_ns, end_time_ns)")
    con.execute("CREATE INDEX IF NOT EXISTS idx_node_bw_time ON node_memory_bandwidth (start_time_ns, end_time_ns)")

    # 1. Associate invocations with cores via threads_scheduling
    print("Associating invocations with cores...")
    con.execute(f'''
        CREATE TEMPORARY TABLE inv_sched AS
        SELECT
            inv.id AS inv_id,
            inv.thread_id,
            GREATEST(inv.start_time_ns, sched.start_time_ns) AS start_time_ns,
            LEAST(inv.end_time_ns, sched.end_time_ns) AS end_time_ns,
            sched.core_number
        FROM
            {invocations_table} AS inv
        JOIN
            threads_scheduling AS sched
        ON
            inv.thread_id = sched.thread_id
        AND
            sched.end_time_ns >= inv.start_time_ns
        AND
            sched.start_time_ns <= inv.end_time_ns
    ''')

    # 2. Associate invocations with performance events
    print("Associating invocations with performance events...")
    con.execute('''
        REATE TEMPORARY TABLE inv_perf AS
        SELECT
            inv_sched.inv_id,
            perf.id AS perf_event_id,
            perf.stalls_per_us,
            perf.cpu_usage,
            perf.memory_bandwidth_bytes_per_us,
            perf.inst_retire_rate,
            perf.time_delta_ns
        FROM
            inv_sched
        JOIN
            performance_events AS perf
        ON
            perf.cpu_id = inv_sched.core_number
        AND
            perf.timestamp_ns >= inv_sched.start_time_ns
        AND
            perf.timestamp_ns <= inv_sched.end_time_ns
    ''')

    # Create the inv_to_perf join table
    print(f"Creating join table '{inv_to_perf_table}'...")
    con.execute(f'''
        CREATE TABLE IF NOT EXISTS {inv_to_perf_table} (
            invocation_id BIGINT,
            performance_event_id BIGINT
        )
    ''')

    # Insert mappings into the join table
    print(f"Inserting mappings into '{inv_to_perf_table}'...")
    con.execute(f'''
        INSERT INTO {inv_to_perf_table}
        SELECT DISTINCT inv_id, perf_event_id FROM inv_perf
    ''')

    # 3. Compute weighted averages
    print("Computing weighted averages...")
    inv_metrics = con.execute('''
        SELECT
            inv_id,
            SUM(stalls_per_us * time_delta_ns) / SUM(time_delta_ns) AS avg_stalls_per_us,
            SUM(cpu_usage * time_delta_ns) / SUM(time_delta_ns) AS avg_cpu_usage,
            SUM(memory_bandwidth_bytes_per_us * time_delta_ns) / SUM(time_delta_ns) AS avg_memory_bandwidth_bytes_per_us,
            SUM(inst_retire_rate * time_delta_ns) / SUM(time_delta_ns) AS avg_inst_retire_per_us
        FROM
            inv_perf
        GROUP BY
            inv_id
    ''').df()

    # 4. Compute average total memory bandwidth for each invocation
    print("Computing average total memory bandwidth for each invocation...")
    con.execute(f'''
        CREATE TEMPORARY TABLE inv_node_bw AS
        SELECT
            inv.id AS inv_id,
            SUM(
                node_bw.memory_bandwidth_bytes_per_us * (
                    LEAST(inv.end_time_ns, node_bw.end_time_ns) - GREATEST(inv.start_time_ns, node_bw.start_time_ns)
                )
            ) / SUM(
                LEAST(inv.end_time_ns, node_bw.end_time_ns) - GREATEST(inv.start_time_ns, node_bw.start_time_ns)
            ) AS avg_total_memory_bandwidth_bytes_per_us_total
        FROM
            {invocations_table} AS inv
        JOIN
            node_memory_bandwidth AS node_bw
        ON
            node_bw.end_time_ns >= inv.start_time_ns
        AND
            node_bw.start_time_ns <= inv.end_time_ns
        GROUP BY
            inv.id
    ''')

    inv_node_bw = con.execute('SELECT * FROM inv_node_bw').df()

    # 5. Merge metrics into one DataFrame
    inv_metrics = inv_metrics.merge(inv_node_bw, on='inv_id', how='left')

    # 6. Update the invocations table
    print("Updating the invocations table with computed metrics...")
    # Convert DataFrame to temporary table
    con.register('inv_metrics_df', inv_metrics)

    # Perform update
    con.execute(f'''
        UPDATE {invocations_table} AS inv
        SET
            avg_stalls_per_us = metrics.avg_stalls_per_us,
            avg_cpu_usage = metrics.avg_cpu_usage,
            avg_memory_bandwidth_bytes_per_us = metrics.avg_memory_bandwidth_bytes_per_us,
            avg_inst_retire_per_us = metrics.avg_inst_retire_per_us,
            avg_total_memory_bandwidth_bytes_per_us_total = metrics.avg_total_memory_bandwidth_bytes_per_us_total
        FROM
            inv_metrics_df AS metrics
        WHERE
            inv.id = metrics.inv_id
    ''')

    # Close the connection
    con.close()
    print(f"Averaged performance metrics computed and updated in '{invocations_table}'.")
    print(f"Join table '{inv_to_perf_table}' created and populated.")

if __name__ == "__main__":
    main()
