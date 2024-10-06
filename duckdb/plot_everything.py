import sys
import os
import duckdb
import matplotlib.pyplot as plt
import pandas as pd
import numpy as np
from scipy.stats import pearsonr, linregress

def plot_metrics(function_name):
    invocations_table = f"{function_name}_invocations"

    # Connect to 'analysis.duckdb'
    con = duckdb.connect('analysis.duckdb')

    # Check if the invocations table exists
    tables = [row[0] for row in con.execute("SHOW TABLES").fetchall()]
    if invocations_table not in tables:
        print(f"Error: Table '{invocations_table}' does not exist in 'analysis.duckdb'.")
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

    # Create function directory if it doesn't exist
    function_dir = "analysis_" + function_name.replace(' ', '_').replace('(', '').replace(')', '').replace(':', '_').replace('.', '_')
    if not os.path.exists(function_dir):
        os.makedirs(function_dir)

    # Define the metrics to plot
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

    # Plot distributions
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

    # Plot correlations with log-log regression
    print("Plotting correlations with log-log regression...")
    num_metrics = len(metrics)
    for i in range(num_metrics):
        for j in range(i+1, num_metrics):
            metric_x = metrics[i]
            metric_y = metrics[j]

            # Extract data for both metrics, only when both metrics are present
            data_df = inv_df[[metric_x['data_key'], metric_y['data_key']]].dropna()

            # Remove non-positive values (since log of zero or negative numbers is undefined)
            data_df = data_df[(data_df[metric_x['data_key']] > 0) & (data_df[metric_y['data_key']] > 0)]

            if data_df.empty:
                print(f"No positive data found for {metric_x['data_key']} and {metric_y['data_key']}. Skipping this pair.")
                continue

            x_values = data_df[metric_x['data_key']].values
            y_values = data_df[metric_y['data_key']].values

            # Compute Pearson correlation coefficient
            if len(x_values) > 1:
                corr_coef, _ = pearsonr(x_values, y_values)
            else:
                corr_coef = 0.0  # Not enough data to compute correlation

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

            # Plot scatter plot with regression line
            plt.figure(figsize=(10, 6))
            plt.scatter(x_values, y_values, alpha=0.5, label='Data Points')
            plt.plot(x_fit_original, y_fit_original, color='red', label=f'Fit Line (Elasticity = {elasticity:.2f})')
            plt.title(f"{metric_x['label']} vs {metric_y['label']}\nPearson r = {corr_coef:.2f}, R² = {r_squared:.2f}")
            plt.xlabel(metric_x['label'])
            plt.ylabel(metric_y['label'])
            plt.legend()
            plt.grid(True)
            plt.tight_layout()

            # Save plot with a filename indicating the metrics
            filename = f"{metric_x['data_key']}_vs_{metric_y['data_key']}_scatter.png"
            plt.savefig(os.path.join(function_dir, filename))
            plt.close()

    print(f"Correlation plots with log-log regression have been saved in the '{function_dir}' directory.")

if __name__ == "__main__":
    if len(sys.argv) != 2:
        print('Expected usage: python plot_metrics.py <function_name>')
        sys.exit(1)

    function_name = sys.argv[1]
    plot_metrics(function_name)
