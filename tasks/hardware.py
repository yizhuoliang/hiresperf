#!/usr/bin/env python3
"""
Hardware measurement tasks for hiresperf
"""

import json
import time
import logging
import math
from invoke import task
from pystream import StreamBenchmark, StreamOperation

# Configure logging
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(levelname)s - %(message)s'
)
logger = logging.getLogger("hardware_tasks")

# Constants for array sizing
BASE_ARRAY_SIZE = 67108864  # 64MB base size (same as in slow.py)

@task
def measureStreamSlowDown(ctx, thread_counts, cpus, numa_nodes, 
                         operation="add", array_size=None, scalar=None,
                         iterations_per_thread=1):
    """
    Run STREAM benchmark with different thread counts and see memory bandwidth contention slowdowns.
    
    Args:
        thread_counts: Comma-separated list of thread counts to test
        cpus: Comma-separated list of CPU IDs to use for thread affinity
        numa_nodes: Comma-separated list of NUMA node IDs to use for memory allocation
        operation: STREAM operation to perform (copy, scale, add, triad)
        array_size: Size of arrays used in the benchmark (default: auto-sized based on threads)
        scalar: Scalar value for operations that require it (default: 3.0)
        iterations_per_thread: Number of iterations each thread should perform (default: 1)
    """
    # Parse arguments
    thread_counts = [int(t) for t in thread_counts.split(',')]
    cpus = [int(c) for c in cpus.split(',')]
    numa_nodes = [int(n) for n in numa_nodes.split(',')]
    iterations_per_thread = int(iterations_per_thread)
    
    # Map operation string to StreamOperation enum
    op_map = {
        "copy": StreamOperation.COPY,
        "scale": StreamOperation.SCALE,
        "add": StreamOperation.ADD,
        "triad": StreamOperation.TRIAD
    }
    
    if operation.lower() not in op_map:
        logger.error(f"Invalid operation: {operation}. Must be one of: copy, scale, add, triad")
        return
        
    stream_operation = op_map[operation.lower()]
    
    # Convert scalar to float if provided
    if scalar is not None:
        scalar = float(scalar)
    
    # Convert array_size to int if provided
    if array_size is not None:
        array_size = int(array_size)
    
    logger.info(f"Running STREAM benchmark with:")
    logger.info(f"- Thread counts: {thread_counts}")
    logger.info(f"- CPUs: {cpus}")
    logger.info(f"- NUMA nodes: {numa_nodes}")
    logger.info(f"- Operation: {operation}")
    logger.info(f"- Iterations per thread: {iterations_per_thread}")
    
    results = {}
    
    # Run benchmark for each thread count
    for thread_count in thread_counts:
        logger.info(f"\n{'='*80}\nRunning benchmark with {thread_count} threads\n{'='*80}")
        
        # Scale array size based on thread count
        # unless explicit array_size was provided
        actual_array_size = array_size
        if actual_array_size is None:
            actual_array_size = BASE_ARRAY_SIZE * thread_count
            logger.info(f"Auto-sizing arrays to {actual_array_size} elements based on thread count")
        
        # Calculate total iterations based on threads × iterations_per_thread
        # This ensures each thread gets the same number of iterations
        total_iterations = thread_count * iterations_per_thread
        logger.info(f"Setting total iterations to {total_iterations} " +
                   f"({iterations_per_thread} per thread × {thread_count} threads)")
        
        # Create benchmark instance
        stream = StreamBenchmark(
            threads=thread_count,
            array_size=actual_array_size,
            operation=stream_operation,
            scalar=scalar,  # None will use default
            cpus=cpus[:thread_count] if thread_count <= len(cpus) else cpus,
            numa_nodes=numa_nodes
        )
        
        # Explicitly disable silent mode to see the benchmark output
        stream.set_silent_mode(False)
        
        # We need to specify the correct number of iterations using the build_command method
        # This ensures we pass the right -i parameter to the stream executable
        original_build_command = stream.build_command
        
        def custom_build_command():
            cmd = original_build_command()
            # Replace -i parameter with our calculated total_iterations
            for i, arg in enumerate(cmd):
                if arg == "-i":
                    cmd[i+1] = str(total_iterations)
                    break
            return cmd
            
        # Override the build_command method
        stream.build_command = custom_build_command
        
        # Run benchmark in blocking mode to get results
        logger.info(f"Starting STREAM benchmark with {thread_count} threads " +
                   f"and {total_iterations} total iterations...")
        result = stream.start(blocking=True)
        
        if result.returncode != 0:
            logger.error(f"Benchmark failed with code {result.returncode}")
            if result.stderr:
                logger.error(f"Error: {result.stderr}")
            continue
        
        # Store results
        results[thread_count] = {
            "stdout": result.stdout,
            "return_code": result.returncode,
            "array_size": actual_array_size,
            "total_iterations": total_iterations,
            "iterations_per_thread": iterations_per_thread
        }
        
        # Print the raw output from the benchmark
        logger.info(f"\n{'='*20} STREAM BENCHMARK RESULTS {'='*20}")
        print(result.stdout)
        logger.info(f"{'='*65}")
        
        # Give system time to cool down between runs
        if thread_count != thread_counts[-1]:
            logger.info("Cooling down for 2 seconds before next run...")
            time.sleep(2)
    
    # Summary of runs
    logger.info("\n\nSTREAM Benchmark Summary:")
    logger.info(f"{'='*60}")
    logger.info(f"{'Threads':<10} {'Array Size':<15} {'Iter/Thread':<15} {'Operation':<10}")
    logger.info(f"{'-'*60}")
    
    for thread_count in sorted(results.keys()):
        if thread_count in results:
            logger.info(f"{thread_count:<10} {results[thread_count]['array_size']:<15} {iterations_per_thread:<15} {operation}")
    
    logger.info(f"\nBenchmark complete. Results above show memory bandwidth in MB/s.")
    
    return results 