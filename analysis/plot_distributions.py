import os
import pickle
import matplotlib.pyplot as plt

def plot_distributions(function_name):
    # Create function directory name by replacing certain characters
    function_dir = "analysis_" + function_name.replace(' ', '_').replace('(', '').replace(')', '').replace(':', '_').replace('.', '_')
    metrics_file = os.path.join(function_dir, 'invocation_metrics.pkl')

    if not os.path.exists(metrics_file):
        print(f"Invocation metrics file '{metrics_file}' does not exist. Please run the metrics computation script first.")
        return

    # Load invocation metrics
    with open(metrics_file, 'rb') as f:
        invocation_metrics = pickle.load(f)

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
            'data_key': 'avg_cpu_usage',
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

    # For each metric, extract data and plot
    for metric in metrics:
        data = [inv[metric['data_key']] for inv in invocation_metrics if metric['data_key'] in inv]
        if not data:
            print(f"No data found for {metric['data_key']}. Skipping this metric.")
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

if __name__ == "__main__":
    function_name = input("Enter the function name (e.g., 'process_chunk'): ").strip()
    plot_distributions(function_name)
