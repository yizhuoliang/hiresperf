import sys
import os
import duckdb
import matplotlib.pyplot as plt

def plot_distributions(function_name):
    invocations_table = f"{function_name}_invocations"
    inv_to_perf_table = f"{function_name}_inv_to_perf"

    # Connect to 'analysis.duckdb'
    con = duckdb.connect('analysis.duckdb')

    # Check if the tables exist
    tables = [row[0] for row in con.execute("SHOW TABLES").fetchall()]
    required_tables = [invocations_table, inv_to_perf_table]
    for tbl in required_tables:
        if tbl not in tables:
            print(f"Error: Table '{tbl}' does not exist in 'analysis.duckdb'.")
            con.close()
            return

    # Fetch invocation-level data
    inv_df = con.execute(f'''
        SELECT
            id,
            latency_us,
            avg_cpu_usage * 100 AS avg_cpu_usage_percent,
            avg_memory_bandwidth_bytes_per_us,
            avg_stalls_per_us,
            avg_inst_retire_per_us,
            avg_total_memory_bandwidth_bytes_per_us_total
        FROM
            {invocations_table}
        WHERE
            latency_us IS NOT NULL
    ''').fetchdf()

    con.close()

    # Define the metrics to plot
    metrics = [
        {
            'data_key': 'latency_us',
            'title': f"Latency Distribution for '{function_name}'",
            'xlabel': 'Latency (μs)',
            'color': 'skyblue',
            'filename': 'latency_distribution.png'
        },
        {
            'data_key': 'avg_cpu_usage_percent',
            'title': f"CPU Usage Distribution for '{function_name}'",
            'xlabel': 'CPU Usage (%)',
            'color': 'orange',
            'filename': 'cpu_usage_distribution.png'
        },
        {
            'data_key': 'avg_memory_bandwidth_bytes_per_us',
            'title': f"Memory Bandwidth Distribution for '{function_name}'",
            'xlabel': 'Memory Bandwidth (bytes/μs)',
            'color': 'green',
            'filename': 'memory_bandwidth_distribution.png'
        },
        {
            'data_key': 'avg_stalls_per_us',
            'title': f"Stalls per Microsecond Distribution for '{function_name}'",
            'xlabel': 'Stalls per μs',
            'color': 'red',
            'filename': 'stalls_distribution.png'
        },
        {
            'data_key': 'avg_inst_retire_per_us',
            'title': f"Instructions Retired per Microsecond Distribution for '{function_name}'",
            'xlabel': 'Instructions Retired per μs',
            'color': 'purple',
            'filename': 'inst_retire_distribution.png'
        },
        {
            'data_key': 'avg_total_memory_bandwidth_bytes_per_us_total',
            'title': f"Total Memory Bandwidth Distribution for '{function_name}'",
            'xlabel': 'Total Memory Bandwidth (bytes/μs)',
            'color': 'teal',
            'filename': 'total_memory_bandwidth_distribution.png'
        }
    ]

    # Create function directory if it doesn't exist
    function_dir = "analysis_" + function_name.replace(' ', '_').replace('(', '').replace(')', '').replace(':', '_').replace('.', '_')
    if not os.path.exists(function_dir):
        os.makedirs(function_dir)

    # For each metric, extract data and plot
    for metric in metrics:
        data_key = metric['data_key']
        if data_key not in inv_df.columns:
            print(f"No data found for {data_key}. Skipping this metric.")
            continue

        data = inv_df[data_key].dropna()
        if data.empty:
            print(f"No data available for {data_key}. Skipping this metric.")
            continue

        plt.figure(figsize=(10, 6))
        plt.hist(data, bins=50, color=metric['color'], edgecolor='black')
        plt.title(metric['title'])
        plt.xlabel(metric['xlabel'])
        plt.ylabel('Frequency')
        plt.tight_layout()
        plt.savefig(os.path.join(function_dir, metric['filename']))
        plt.close()

    print(f"Distributions have been plotted and saved in the '{function_dir}' directory.")

    # Optionally, plot additional metrics by joining with performance_events
    # For example, you can plot the distribution of stalls per us for performance events associated with invocations
    # This requires fetching data from performance_events via the join table

    # Example: Plotting stalls_per_us from performance_events associated with invocations
    print("Fetching performance events associated with invocations...")
    con = duckdb.connect('analysis.duckdb')

    perf_df = con.execute(f'''
        SELECT
            pe.stalls_per_us
        FROM
            {inv_to_perf_table} AS ip
        JOIN
            performance_events AS pe
        ON
            ip.performance_event_id = pe.id
    ''').fetchdf()

    con.close()

    if not perf_df.empty:
        data = perf_df['stalls_per_us'].dropna()
        if not data.empty:
            plt.figure(figsize=(10, 6))
            plt.hist(data, bins=50, color='blue', edgecolor='black')
            plt.title(f"Stalls per μs Distribution for Performance Events Associated with '{function_name}' Invocations")
            plt.xlabel('Stalls per μs')
            plt.ylabel('Frequency')
            plt.tight_layout()
            plt.savefig(os.path.join(function_dir, 'perf_stalls_distribution.png'))
            plt.close()
            print("Additional performance events distribution plotted.")
        else:
            print("No stalls_per_us data available from performance events.")
    else:
        print("No performance events associated with invocations found.")

if __name__ == "__main__":
    if len(sys.argv) != 2:
        print('Expected usage: python plot_distributions.py <function_name>')
        sys.exit(1)

    function_name = sys.argv[1]
    plot_distributions(function_name)
