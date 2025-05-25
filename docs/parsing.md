# Parsing and Analysis

## Parsing Scripts
- `parse_ldb.py`: generate a `ldb_events` table that contains all recorded information about function invocation timelines
- `parse_hrp.py`: compute all the bandwidth/instruction rates based on the performance counter values, genrating a `performance_events` table containing each core's PMC timeline and a `node_memory_bandwidth` table that contains the entire NUMA node's total membw pressure timeline.
- `parse_sched.py`: automatically parses `perf.data` into a table `threads_scheduling`, matching each user thread to the cores it was running on at each point of time

## Function-level Analysis Scripts
- `search_func.py`: given a function name and the data in `analysis.duckdb`, this script extracts all invocations of that function into a function-specific table. This table now only contains start/end times and thread_id, all performance metrics are empty.
- `compute_invocation_metrics.py`: given a function name, this script attribute the performance counter values into each single invocation of that function. Filling in membw, inst rate, node membw pressure, etc. Note that this sometimes take longer times to run.

## Plotting Scripts
- `plot_distributions.py`: given a function name, plot the distributions of the performance metrics of all invocations.
- `draw_invocation.py`: this is the most complex one and still have some bugs. Given a function name, and specify the nth invocation sorted by a specific metric, this script draw the function call timeline and resource timeline into a SVG file.