import os
import pickle
import matplotlib.pyplot as plt
import numpy as np
from scipy.stats import pearsonr

def plot_correlations(function_name):
    # Create function directory name by replacing certain characters
    function_dir = "analysis_" + function_name.replace(' ', '_').replace('(', '').replace(')', '').replace(':', '_').replace('.', '_')
    metrics_file = os.path.join(function_dir, 'invocation_metrics.pkl')

    if not os.path.exists(metrics_file):
        print(f"Invocation metrics file '{metrics_file}' does not exist. Please run the metrics computation script first.")
        return

    # Load invocation metrics
    with open(metrics_file, 'rb') as f:
        invocation_metrics = pickle.load(f)

    # Define the metrics to analyze
    metrics = [
        {
            'data_key': 'latency_us',
            'label': 'Latency (μs)'
        },
        {
            'data_key': 'avg_cpu_usage',
            'label': 'CPU Usage (%)'
        },
        {
            'data_key': 'avg_memory_bandwidth_bytes_per_us',
            'label': 'Memory Bandwidth (bytes/μs)'
        },
        {
            'data_key': 'avg_stalls_per_us',
            'label': 'Stalls per μs'
        },
        {
            'data_key': 'avg_inst_retire_per_us',
            'label': 'Instructions Retired per μs'
        },
        {
            'data_key': 'avg_total_memory_bandwidth_bytes_per_us_total',
            'label': 'Total Memory Bandwidth (bytes/μs)'
        }
    ]

    # For each pair of metrics, compute correlation and plot scatter plot
    num_metrics = len(metrics)
    for i in range(num_metrics):
        for j in range(i+1, num_metrics):
            metric_x = metrics[i]
            metric_y = metrics[j]

            # Extract data for both metrics, only when both metrics are present
            data = [
                (inv[metric_x['data_key']], inv[metric_y['data_key']])
                for inv in invocation_metrics
                if metric_x['data_key'] in inv and metric_y['data_key'] in inv
            ]

            if not data:
                print(f"No data found for {metric_x['data_key']} and {metric_y['data_key']}. Skipping this pair.")
                continue

            # Separate data into x and y arrays
            x_values, y_values = zip(*data)
            x_values = np.array(x_values)
            y_values = np.array(y_values)

            # Compute correlation coefficient
            corr_coef, _ = pearsonr(x_values, y_values)

            # Plot scatter plot
            plt.figure(figsize=(10, 6))
            plt.scatter(x_values, y_values, alpha=0.5)
            plt.title(f"{metric_x['label']} vs {metric_y['label']} (r = {corr_coef:.2f})")
            plt.xlabel(metric_x['label'])
            plt.ylabel(metric_y['label'])
            plt.grid(True)
            plt.tight_layout()

            # Save plot with a filename indicating the metrics
            filename = f"{metric_x['data_key']}_vs_{metric_y['data_key']}_scatter.png"
            plt.savefig(os.path.join(function_dir, filename))
            plt.close()

    print(f"Correlation plots have been saved in the '{function_dir}' directory.")

if __name__ == "__main__":
    function_name = input("Enter the function name (e.g., 'process_chunk'): ").strip()
    plot_correlations(function_name)
