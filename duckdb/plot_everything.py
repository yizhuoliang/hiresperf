import sys
import os
import duckdb
import matplotlib
import matplotlib.pyplot as plt
import pandas as pd
import numpy as np
from scipy.stats import pearsonr, linregress

def plot_metrics(function_name, color_by='threads', show_fit_line=True):
    invocations_table = f"{function_name}_invocations"

    # ----------------------------------------
    # Connect to the Database
    # ----------------------------------------
    con = duckdb.connect('analysis.duckdb')

    # ----------------------------------------
    # Check if the Invocations Table Exists
    # ----------------------------------------
    tables = [row[0] for row in con.execute("SHOW TABLES").fetchall()]
    if invocations_table not in tables:
        print(f"Error: Table '{invocations_table}' does not exist in 'analysis.duckdb'.")
        con.close()
        return

    # ----------------------------------------
    # Fetch Invocation-Level Data
    # ----------------------------------------
    # If coloring by cores, fetch core_number using threads_scheduling table
    if color_by == 'cores':
        print("Warning: Assuming no migrations during the function invocation when coloring by cores.")
        inv_df = con.execute(f'''
            WITH inv AS (
                SELECT
                    id,
                    thread_id,
                    start_time_ns,
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
            ),
            sched AS (
                SELECT
                    thread_id,
                    start_time_ns AS sched_start_ns,
                    end_time_ns AS sched_end_ns,
                    core_number
                FROM
                    threads_scheduling
            ),
            inv_sched AS (
                SELECT
                    inv.*,
                    sched.core_number
                FROM
                    inv
                LEFT JOIN sched
                ON inv.thread_id = sched.thread_id
                AND inv.start_time_ns BETWEEN sched.sched_start_ns AND sched.sched_end_ns
            )
            SELECT * FROM inv_sched
        ''').fetchdf()
    else:
        inv_df = con.execute(f'''
            SELECT
                id,
                thread_id,
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

    # ----------------------------------------
    # Create Function Directory
    # ----------------------------------------
    function_dir = "analysis_" + function_name.replace(' ', '_').replace('(', '').replace(')', '').replace(':', '_').replace('.', '_')
    if not os.path.exists(function_dir):
        os.makedirs(function_dir)

    # ----------------------------------------
    # Define Metrics to Plot
    # ----------------------------------------
    metrics = [
        {
            'data_key': 'avg_total_memory_bandwidth_bytes_per_us_total',
            'label': 'Total Memory Bandwidth (bytes/μs)',
            'xlabel': 'Total Memory Bandwidth (bytes/μs)',
            'color': 'teal',
            'filename': 'total_memory_bandwidth_distribution.png'
        },
        {
            'data_key': 'latency_us',
            'label': 'Latency (μs)',
            'xlabel': 'Latency (μs)',
            'color': 'skyblue',
            'filename': 'latency_distribution.png'
        },
        {
            'data_key': 'avg_cpu_usage_percent',
            'label': 'CPU Usage (%)',
            'xlabel': 'CPU Usage (%)',
            'color': 'orange',
            'filename': 'cpu_usage_distribution.png'
        },
        {
            'data_key': 'avg_memory_bandwidth_bytes_per_us',
            'label': 'Memory Bandwidth (bytes/μs)',
            'xlabel': 'Memory Bandwidth (bytes/μs)',
            'color': 'green',
            'filename': 'memory_bandwidth_distribution.png'
        },
        {
            'data_key': 'avg_stalls_per_us',
            'label': 'Stalls per μs',
            'xlabel': 'Stalls per μs',
            'color': 'red',
            'filename': 'stalls_distribution.png'
        },
        {
            'data_key': 'avg_inst_retire_per_us',
            'label': 'Instructions Retired per μs',
            'xlabel': 'Instructions Retired per μs',
            'color': 'purple',
            'filename': 'inst_retire_distribution.png'
        }
    ]

    # ----------------------------------------
    # Plot Distributions
    # ----------------------------------------
    print("Plotting distributions...")
    for metric in metrics:
        data_key = metric['data_key']
        if data_key not in inv_df.columns:
            print(f"No data found for {data_key}. Skipping this metric.")
            continue

        data = inv_df[data_key].dropna()
        if data.empty:
            print(f"No data available for {data_key}. Skipping this metric.")
            continue

        # Calculate the average value
        avg_value = data.mean()

        plt.figure(figsize=(10, 6))
        plt.hist(data, bins=50, color=metric['color'], edgecolor='black', alpha=0.7)
        plt.axvline(avg_value, color='black', linestyle='dashed', linewidth=2, label=f'Average = {avg_value:.2f}')
        plt.title(f"{metric['label']} Distribution for '{function_name}'")
        plt.xlabel(metric['xlabel'])
        plt.ylabel('Frequency')
        plt.legend()
        plt.tight_layout()
        plt.savefig(os.path.join(function_dir, metric['filename']))
        plt.close()

    print(f"Distribution plots have been saved in the '{function_dir}' directory.")

    # ----------------------------------------
    # Plot Correlations with Log-Log Regression
    # ----------------------------------------
    print("Plotting correlations with log-log regression...")
    num_metrics = len(metrics)
    for i in range(num_metrics):
        for j in range(i+1, num_metrics):
            metric_x = metrics[i]
            metric_y = metrics[j]

            # Extract data for both metrics
            if color_by == 'cores' and 'core_number' in inv_df.columns:
                data_df = inv_df[[metric_x['data_key'], metric_y['data_key'], 'core_number']].dropna()
            else:
                data_df = inv_df[[metric_x['data_key'], metric_y['data_key'], 'thread_id']].dropna()

            # Remove non-positive values (since log of zero or negative numbers is undefined)
            data_df = data_df[(data_df[metric_x['data_key']] > 0) & (data_df[metric_y['data_key']] > 0)]

            if data_df.empty:
                print(f"No positive data found for {metric_x['data_key']} and {metric_y['data_key']}. Skipping this pair.")
                continue

            x_values = data_df[metric_x['data_key']].values
            y_values = data_df[metric_y['data_key']].values

            # Get the identifiers for coloring
            if color_by == 'cores' and 'core_number' in data_df.columns:
                identifiers = data_df['core_number'].astype(str).values
                id_label = 'Core'
            else:
                identifiers = data_df['thread_id'].astype(str).values
                id_label = 'Thread'

            # Map identifiers to numeric values for coloring
            unique_ids = np.unique(identifiers)
            id_to_num = {id_: idx for idx, id_ in enumerate(unique_ids)}
            colors = np.array([id_to_num[id_] for id_ in identifiers])

            # Compute Pearson correlation coefficient
            if len(x_values) > 1:
                corr_coef, _ = pearsonr(x_values, y_values)
            else:
                corr_coef = 0.0  # Not enough data to compute correlation

            plt.figure(figsize=(10, 6))
            scatter = plt.scatter(x_values, y_values, c=colors, cmap='tab20', alpha=0.5, label='Data Points')

            if show_fit_line:
                # Perform log-log regression
                log_x = np.log(x_values)
                log_y = np.log(y_values)

                # Compute linear regression on log-transformed data
                slope, intercept, r_value, p_value, std_err = linregress(log_x, log_y)
                elasticity = slope  # The slope is the elasticity coefficient
                r_squared = r_value**2  # Coefficient of determination

                # Generate regression line for plotting
                x_fit = np.linspace(log_x.min(), log_x.max(), 100)
                y_fit = intercept + slope * x_fit

                # Convert back to original scale
                x_fit_original = np.exp(x_fit)
                y_fit_original = np.exp(y_fit)

                # Plot regression line with semi-transparent line
                plt.plot(x_fit_original, y_fit_original, color='black', linewidth=2, alpha=0.7, label=f'Fit Line (Elasticity = {elasticity:.2f})')

                # Update the plot title to include Pearson r and R^2
                plt.title(f"{metric_x['label']} vs {metric_y['label']}\nPearson r = {corr_coef:.2f}, R² = {r_squared:.2f}")
            else:
                # Plot title without elasticity and R^2
                plt.title(f"{metric_x['label']} vs {metric_y['label']}\nPearson r = {corr_coef:.2f}")

            plt.xlabel(metric_x['label'])
            plt.ylabel(metric_y['label'])

            # Create a legend for identifiers
            unique_ids = np.unique(identifiers)
            num_ids = len(unique_ids)
            max_legend_entries = 20  # Limit the number of entries in the legend

            if num_ids <= max_legend_entries:
                # Create a color map
                cmap = matplotlib.colormaps['tab20'].resampled(num_ids)
                color_map = {id_: cmap(i) for i, id_ in enumerate(unique_ids)}
                patches = [plt.Line2D([0], [0], marker='o', color='w', label=f'{id_label} {id_}',
                                       markerfacecolor=color_map[id_], markersize=8) for id_ in unique_ids]

                legend_handles = patches
                if show_fit_line:
                    legend_handles.append(plt.Line2D([0], [0], color='black', linewidth=2, alpha=0.7, label='Fit Line'))

                plt.legend(handles=legend_handles)
            else:
                if show_fit_line:
                    plt.legend([plt.Line2D([0], [0], color='black', linewidth=2, alpha=0.7, label='Fit Line')])

            plt.grid(True)
            plt.tight_layout()

            # Save plot with a filename indicating the metrics
            filename = f"{metric_x['data_key']}_vs_{metric_y['data_key']}_scatter_{color_by}.png"
            plt.savefig(os.path.join(function_dir, filename))
            plt.close()

    print(f"Correlation plots with log-log regression have been saved in the '{function_dir}' directory.")

if __name__ == "__main__":
    args = sys.argv[1:]

    if len(args) < 1:
        print('Expected usage: python plot_metrics.py <function_name> [color_by] [--no-fit-line]')
        print('color_by: "threads" (default) or "cores"')
        sys.exit(1)

    function_name = args[0]
    color_by = 'threads'
    show_fit_line = True

    # Check for '--no-fit-line' argument
    if '--no-fit-line' in args:
        show_fit_line = False
        args.remove('--no-fit-line')

    # Check for 'threads' or 'cores' argument
    if len(args) > 1:
        color_by = args[1]
        if color_by not in ['threads', 'cores']:
            print('Invalid value for color_by. Use "threads" or "cores".')
            sys.exit(1)

    plot_metrics(function_name, color_by, show_fit_line)
