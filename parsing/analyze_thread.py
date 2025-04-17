import sys
import os
import duckdb
import pandas as pd
from collections import defaultdict
import matplotlib.pyplot as plt
import matplotlib.patches as mpatches
import numpy as np

def main():
    # ----------------------------------------
    # Parse Command Line Arguments
    # ----------------------------------------
    if len(sys.argv) != 2:
        print('Usage: python analyze.py <thread_id>')
        sys.exit(1)
    thread_id = int(sys.argv[1])

    # ----------------------------------------
    # Connect to the Database
    # ----------------------------------------
    con = duckdb.connect(database='analysis.duckdb')

    # ----------------------------------------
    # Retrieve Events and Scheduling Data
    # ----------------------------------------
    df_events = con.execute('''
        SELECT
            timestamp_ns,
            event_name,
            mutex,
            wait_time_us,
            lock_time_us
        FROM ldb_events
        WHERE thread_id = ?
        ORDER BY timestamp_ns
    ''', (thread_id,)).fetchdf()

    if df_events.empty:
        print(f"No events found for thread_id {thread_id}")
        con.close()
        sys.exit(1)

    df_sched = con.execute('''
        SELECT
            start_time_ns,
            end_time_ns,
            core_number
        FROM threads_scheduling
        WHERE thread_id = ?
        ORDER BY start_time_ns
    ''', (thread_id,)).fetchdf()

    if df_sched.empty:
        print(f"No scheduling data found for this thread.")
        con.close()
        sys.exit(1)

    # ----------------------------------------
    # Compute Thread Lifespan and Interrupts
    # ----------------------------------------
    thread_lifespan_start_ns = df_sched['start_time_ns'].min()
    thread_lifespan_end_ns = df_sched['end_time_ns'].max()
    thread_lifespan_ns = thread_lifespan_end_ns - thread_lifespan_start_ns
    thread_lifespan_s = thread_lifespan_ns / 1e9

    number_of_interrupts = len(df_sched) - 1

    # ----------------------------------------
    # Process Scheduling Data
    # ----------------------------------------
    core_migrations = 0
    previous_core = None
    core_times = defaultdict(int)  # core_number -> total_time_ns

    for idx, row in df_sched.iterrows():
        start_time_ns = row['start_time_ns']
        end_time_ns = row['end_time_ns']
        core_number = row['core_number']
        duration_ns = end_time_ns - start_time_ns

        core_times[core_number] += duration_ns

        if previous_core is not None and core_number != previous_core:
            core_migrations += 1

        previous_core = core_number

    # Compute total scheduled time
    total_scheduled_time_ns = sum(core_times.values())
    total_scheduled_time_s = total_scheduled_time_ns / 1e9

    # Calculate unscheduled time
    unscheduled_time_ns = thread_lifespan_ns - total_scheduled_time_ns
    core_times['Not scheduled'] = unscheduled_time_ns

    # Compute percentages relative to total lifespan
    core_percentages = {core: (time_ns / thread_lifespan_ns * 100) for core, time_ns in core_times.items()}

    # ----------------------------------------
    # Compute Lock Analysis with Integrity Checks
    # ----------------------------------------
    lock_count = 0
    lock_intervals = []
    lock_start_time = None

    waiting_mutexes = {}
    wait_intervals = []  # List of [start_time_ns, end_time_ns, mutex]
    lock_start_times_per_mutex = {}
    lock_intervals_per_mutex = defaultdict(list)
    wait_start_times_per_mutex = {}
    wait_intervals_per_mutex = defaultdict(list)

    for idx, row in df_events.iterrows():
        timestamp_ns = row['timestamp_ns']
        event_name = row['event_name']
        mutex = row['mutex']

        if event_name == 'MUTEX_WAIT':
            # Integrity check: Should not be already waiting for this mutex
            if mutex in wait_start_times_per_mutex:
                print(f"!!! Error: Thread {thread_id} is already waiting for mutex {mutex} at timestamp {timestamp_ns}")
                sys.exit(1)
            wait_start_times_per_mutex[mutex] = timestamp_ns
        elif event_name == 'MUTEX_LOCK':
            # Integrity check: If waiting, end waiting interval
            if mutex in wait_start_times_per_mutex:
                start_time = wait_start_times_per_mutex.pop(mutex)
                wait_intervals.append([start_time, timestamp_ns, mutex])
                wait_intervals_per_mutex[mutex].append([start_time, timestamp_ns])
            else:
                # Integrity check: Should not acquire a lock without waiting
                print(f"!!! Error: Thread {thread_id} is locking mutex {mutex} without waiting at timestamp {timestamp_ns}")
                sys.exit(1)
            # Integrity check: Should not already hold this mutex
            if mutex in lock_start_times_per_mutex:
                print(f"!!! Error: Thread {thread_id} already holds mutex {mutex} at timestamp {timestamp_ns}")
                sys.exit(1)
            # Start holding a lock
            if lock_count == 0:
                lock_start_time = timestamp_ns
            lock_count += 1
            lock_start_times_per_mutex[mutex] = timestamp_ns
        elif event_name == 'MUTEX_UNLOCK':
            # Integrity check: Should be holding this mutex
            if mutex not in lock_start_times_per_mutex:
                print(f"!!! Error: Thread {thread_id} is unlocking mutex {mutex} that it does not hold at timestamp {timestamp_ns}")
                sys.exit(1)
            lock_count -= 1
            start_time = lock_start_times_per_mutex.pop(mutex)
            lock_intervals_per_mutex[mutex].append([start_time, timestamp_ns])

            if lock_count == 0 and lock_start_time is not None:
                lock_intervals.append([lock_start_time, timestamp_ns])
                lock_start_time = None

    # Check if any mutexes are left in wait or lock states
    if wait_start_times_per_mutex:
        for mutex in wait_start_times_per_mutex:
            print(f"!!! Error: Thread {thread_id} did not finish waiting for mutex {mutex}")
        sys.exit(1)
    if lock_start_times_per_mutex:
        for mutex in lock_start_times_per_mutex:
            print(f"!!! Error: Thread {thread_id} did not unlock mutex {mutex}")
        sys.exit(1)

    # Calculate total wait and lock times without merging intervals
    total_wait_time_ns = sum(end - start for start, end, _ in wait_intervals)
    total_lock_time_ns = sum(end - start for intervals in lock_intervals_per_mutex.values() for start, end in intervals)

    # Compute proportions relative to total lifespan
    proportion_waiting = total_wait_time_ns / thread_lifespan_ns
    proportion_holding_lock = total_lock_time_ns / thread_lifespan_ns

    # ----------------------------------------
    # Print Results
    # ----------------------------------------
    print(f"Thread ID: {thread_id}")
    print(f"Total clock time lifespan: {thread_lifespan_s:.6f} seconds")
    print(f"Total scheduled runtime: {total_scheduled_time_s:.6f} seconds")
    print(f"Number of interrupts: {number_of_interrupts}")
    print(f"Total time waiting for locks: {total_wait_time_ns / 1e9:.6f} seconds "
          f"({proportion_waiting * 100:.2f}%)")
    print(f"Total time holding at least one lock: {total_lock_time_ns / 1e9:.6f} seconds "
          f"({proportion_holding_lock * 100:.2f}%)")
    print()

    # Breakdown per mutex
    print("Breakdown per mutex:")
    print(f"{'Mutex':<20}{'Wait Time (s)':>15}{'Hold Time (s)':>15}")
    print("-" * 50)
    for mutex in sorted(set(df_events['mutex'].dropna())):
        mutex = int(mutex)
        # Wait intervals for this mutex
        intervals = wait_intervals_per_mutex.get(mutex, [])
        wait_time_ns = sum(end - start for start, end in intervals)
        # Lock intervals for this mutex
        intervals = lock_intervals_per_mutex.get(mutex, [])
        lock_time_ns = sum(end - start for start, end in intervals)
        print(f"{mutex:<20}{wait_time_ns / 1e9:>15.6f}{lock_time_ns / 1e9:>15.6f}")

    # ----------------------------------------
    # Scheduling Analysis
    # ----------------------------------------
    print("\nScheduling Analysis:")

    print(f"Number of core migrations: {core_migrations}")
    print("\nBreakdown of time scheduled on each core:")
    print(f"{'Core Number':<15}{'Time (s)':>15}{'Percentage (%)':>20}")
    print("-" * 50)
    # Ensure 'Not scheduled' appears at the end
    for core_number in sorted(core_times, key=lambda x: (str(x) if x != 'Not scheduled' else 'ZZZ')):
        time_ns = core_times[core_number]
        time_s = time_ns / 1e9
        percentage = core_percentages[core_number]
        label = f'Core {core_number}' if core_number != 'Not scheduled' else 'Not scheduled'
        print(f"{label:<15}{time_s:>15.6f}{percentage:>20.2f}%")

    # ----------------------------------------
    # Plot Performance Metrics
    # ----------------------------------------
    print("\nGenerating performance metric plots...")

    # Create directory for plots
    plot_dir = f'analysis_thread{thread_id}'
    os.makedirs(plot_dir, exist_ok=True)

    # List of performance metrics to plot
    metrics = ['cpu_usage', 'memory_bandwidth_bytes_per_us', 'stalls_per_us', 'inst_retire_rate']

    # Determine the number of bins (e.g., pixels in the x-axis)
    num_bins = 1000  # Adjust this number based on desired resolution

    # Use the thread lifespan start and end times
    min_timestamp_ns = thread_lifespan_start_ns
    max_timestamp_ns = thread_lifespan_end_ns
    total_duration_ns = max_timestamp_ns - min_timestamp_ns
    bin_size_ns = total_duration_ns / num_bins

    # Prepare SQL query to bin data and aggregate
    query = f'''
        WITH
        sched AS (
            SELECT
                start_time_ns,
                end_time_ns,
                core_number
            FROM threads_scheduling
            WHERE thread_id = {thread_id}
        ),
        bins AS (
            SELECT
                generate_series AS bin_index,
                {min_timestamp_ns} + generate_series * {bin_size_ns} AS bin_start_ns,
                {min_timestamp_ns} + (generate_series + 1) * {bin_size_ns} AS bin_end_ns
            FROM generate_series(0, {num_bins} - 1)
        ),
        bin_sched AS (
            SELECT
                bins.bin_index,
                bins.bin_start_ns,
                bins.bin_end_ns,
                sched.core_number
            FROM bins
            LEFT JOIN sched
            ON sched.start_time_ns < bins.bin_end_ns AND sched.end_time_ns > bins.bin_start_ns
        ),
        perf AS (
            SELECT
                pe.timestamp_ns,
                pe.cpu_id,
                pe.cpu_usage,
                pe.memory_bandwidth_bytes_per_us,
                pe.stalls_per_us,
                pe.inst_retire_rate
            FROM performance_events pe
            WHERE pe.timestamp_ns BETWEEN {thread_lifespan_start_ns} AND {thread_lifespan_end_ns}
        ),
        binned AS (
            SELECT
                bin_sched.bin_index,
                AVG(pe.cpu_usage) as cpu_usage,
                AVG(pe.memory_bandwidth_bytes_per_us) as memory_bandwidth_bytes_per_us,
                AVG(pe.stalls_per_us) as stalls_per_us,
                AVG(pe.inst_retire_rate) as inst_retire_rate,
                bin_sched.core_number
            FROM bin_sched
            LEFT JOIN perf pe
            ON pe.timestamp_ns >= bin_sched.bin_start_ns AND pe.timestamp_ns < bin_sched.bin_end_ns
            AND pe.cpu_id = bin_sched.core_number
            GROUP BY bin_sched.bin_index, bin_sched.core_number
            ORDER BY bin_sched.bin_index
        )
        SELECT
            bin_index,
            {min_timestamp_ns} + bin_index * {bin_size_ns} as timestamp_ns,
            cpu_usage,
            memory_bandwidth_bytes_per_us,
            stalls_per_us,
            inst_retire_rate,
            core_number
        FROM binned
    '''

    # Execute the query
    df_perf = con.execute(query).fetchdf()

    # Convert timestamp_ns to seconds relative to the start time
    df_perf['time_s'] = (df_perf['timestamp_ns'] - min_timestamp_ns) / 1e9

    # Replace NaN core_number with 'Not scheduled'
    df_perf['core_number'] = df_perf['core_number'].fillna('Not scheduled')

    # Create a mapping for core colors
    cores = df_perf['core_number'].unique()
    colors = plt.cm.get_cmap('tab20', len(cores))
    core_color_map = {core: colors(i) for i, core in enumerate(cores)}

    # For each metric, generate the plot
    for metric in metrics:
        plt.figure(figsize=(12, 6))
        plt.plot(df_perf['time_s'], df_perf[metric], marker='o', linestyle='-', markersize=2)

        # Shade the background
        for idx in range(len(df_perf)):
            if idx == 0:
                if len(df_perf) > 1:
                    x_start = df_perf['time_s'].iloc[idx] - (df_perf['time_s'].iloc[1] - df_perf['time_s'].iloc[0]) / 2
                    x_start = max(x_start, 0)
                else:
                    x_start = 0
            else:
                x_start = (df_perf['time_s'].iloc[idx-1] + df_perf['time_s'].iloc[idx]) / 2
            if idx == len(df_perf) -1:
                if len(df_perf) > 1:
                    x_end = df_perf['time_s'].iloc[idx] + (df_perf['time_s'].iloc[idx] - df_perf['time_s'].iloc[idx-1]) / 2
                else:
                    x_end = df_perf['time_s'].iloc[idx] + 1  # Arbitrary end
            else:
                x_end = (df_perf['time_s'].iloc[idx] + df_perf['time_s'].iloc[idx+1]) / 2

            core = df_perf['core_number'].iloc[idx]
            plt.axvspan(x_start, x_end, facecolor=core_color_map[core], alpha=0.2)

        plt.xlabel('Time (s)')
        plt.ylabel(metric)
        plt.title(f'Thread {thread_id} - {metric}')
        # Create legend for cores
        patches = [mpatches.Patch(color=core_color_map[core],
                                  label=f'Core {core}' if core != 'Not scheduled' else 'Not scheduled') for core in cores]
        plt.legend(handles=patches)
        plt.tight_layout()
        # Save the plot
        plot_filename = os.path.join(plot_dir, f'thread_{thread_id}_{metric}.png')
        plt.savefig(plot_filename)
        plt.close()
        print(f"Saved plot: {plot_filename}")

    # ----------------------------------------
    # Plot Lock Waiting Times with Microsecond Units
    # ----------------------------------------
    print("\nGenerating lock waiting time plot...")

    # Create DataFrame of wait events
    df_wait_events = pd.DataFrame(wait_intervals, columns=['start_time_ns', 'end_time_ns', 'mutex'])
    df_wait_events['duration_ns'] = df_wait_events['end_time_ns'] - df_wait_events['start_time_ns']
    df_wait_events['start_time_s'] = (df_wait_events['start_time_ns'] - thread_lifespan_start_ns) / 1e9
    df_wait_events['duration_us'] = df_wait_events['duration_ns'] / 1e3  # Convert to microseconds

    plt.figure(figsize=(12, 6))

    # Create a mapping for mutex colors
    mutexes = df_wait_events['mutex'].unique()
    colors = plt.cm.get_cmap('tab20', len(mutexes))
    mutex_color_map = {mutex: colors(i) for i, mutex in enumerate(mutexes)}

    for mutex in mutexes:
        df_mutex = df_wait_events[df_wait_events['mutex'] == mutex]
        plt.scatter(df_mutex['start_time_s'], df_mutex['duration_us'],
                    label=f'Mutex {mutex}', color=mutex_color_map[mutex], alpha=0.7)

    plt.xlabel('Time (s)')
    plt.ylabel('Lock Wait Time (µs)')
    plt.title(f'Thread {thread_id} - Lock Wait Times')
    plt.legend()
    plt.tight_layout()

    # Save the plot
    plot_filename = os.path.join(plot_dir, f'thread_{thread_id}_lock_wait_times.png')
    plt.savefig(plot_filename)
    plt.close()
    print(f"Saved plot: {plot_filename}")

    # Close the database connection
    con.close()

if __name__ == "__main__":
    main()
