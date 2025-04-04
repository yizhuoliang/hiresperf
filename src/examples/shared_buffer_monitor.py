#!/usr/bin/env python3
"""
Example script demonstrating the use of shared memory buffers
for real-time performance monitoring with hiresperf.

This script initializes the shared buffers, maps them, and monitors
performance counters in real-time from each CPU.
"""

import os
import sys
import time
import threading
import struct
import signal
import argparse

# Add parent directory to path for importing hrperf_api
sys.path.append(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from hrperf_api import *

def monitor_cpu(cpu_id, stop_event):
    """Monitor the shared buffer for a specific CPU"""
    buffer = hrperf_map_cpu_buffer(cpu_id)
    if not buffer:
        print(f"Failed to map buffer for CPU {cpu_id}")
        return

    # Get buffer info
    info = hrperf_get_shared_buffer_info()
    entry_size = info["entry_size"]
    
    # Header format (16 bytes: magic, cpu_id, entry_size, write_idx)
    header_format = "IIII"
    header_size = struct.calcsize(header_format)
    
    # HrperfLogEntry format (need to match the struct definition)
    # - int cpu_id
    # - HrperfTick tick:
    #   - ktime_t kts (8 bytes)
    #   - 5 unsigned long long values (8 bytes each)
    entry_format = "I8sQQQQQ"
    
    last_read_idx = 0
    print(f"Monitoring CPU {cpu_id}...")
    
    try:
        while not stop_event.is_set():
            # Read current buffer indices
            buffer.seek(0)
            magic, buffer_cpu, record_size, write_idx = struct.unpack(header_format, buffer.read(header_size))
            
            if write_idx > last_read_idx:
                entries_to_read = min(20, write_idx - last_read_idx)  # Limit entries to avoid flooding
                print(f"CPU {cpu_id}: Reading {entries_to_read} new entries")
                
                for i in range(entries_to_read):
                    # Calculate position in buffer
                    idx = last_read_idx + i
                    pos = header_size + (idx % ((info["buffer_size"] - header_size) // entry_size)) * entry_size
                    buffer.seek(pos)
                    
                    # Read entry
                    entry_data = buffer.read(entry_size)
                    if len(entry_data) == entry_size:
                        cpu, kts, stall_mem, inst_retire, cpu_unhalt, llc_misses, sw_prefetch = struct.unpack(entry_format, entry_data)
                        print(f"CPU {cpu}: stall_mem={stall_mem}, inst_retire={inst_retire}, llc_misses={llc_misses}")
                
                # Update read position
                last_read_idx = write_idx
            
            # Small sleep to avoid hammering CPU
            time.sleep(0.01)
    except Exception as e:
        print(f"Error monitoring CPU {cpu_id}: {e}")
    finally:
        hrperf_unmap_buffer(buffer)
        print(f"Stopped monitoring CPU {cpu_id}")

def main():
    parser = argparse.ArgumentParser(description="Real-time HRPerf monitor using shared buffers")
    parser.add_argument("--buffer-size", type=int, default=1024*1024, 
                        help="Size of each CPU's buffer in bytes (default: 1MB)")
    parser.add_argument("--duration", type=int, default=60,
                        help="Duration to monitor in seconds (default: 60)")
    args = parser.parse_args()

    # Initialize shared buffers
    ret = hrperf_init_shared_buffers(args.buffer_size)
    if ret < 0:
        print(f"Failed to initialize shared buffers: {os.strerror(-ret)}")
        return 1
    
    # Get buffer info
    info = hrperf_get_shared_buffer_info()
    if not info:
        print("Failed to get buffer info")
        return 1
    
    print(f"Initialized {info['cpu_count']} shared buffers, {info['buffer_size']} bytes each")
    
    # Set up a stop event for clean shutdown
    stop_event = threading.Event()
    
    # Create monitoring threads for each CPU
    threads = []
    for cpu in range(info["cpu_count"]):
        t = threading.Thread(target=monitor_cpu, args=(cpu, stop_event))
        t.daemon = True
        threads.append(t)
    
    # Handle graceful shutdown
    def signal_handler(sig, frame):
        print("\nShutting down...")
        stop_event.set()
    
    signal.signal(signal.SIGINT, signal_handler)
    signal.signal(signal.SIGTERM, signal_handler)
    
    # Start monitoring
    print("Starting shared buffer monitoring...")
    ret = hrperf_start_shared_buffers()
    if ret < 0:
        print(f"Failed to start shared buffer monitoring: {os.strerror(-ret)}")
        return 1
    
    # Start monitor threads
    for t in threads:
        t.start()
    
    # Wait for specified duration or until interrupted
    try:
        if args.duration > 0:
            time.sleep(args.duration)
            stop_event.set()
        else:
            # Wait indefinitely until interrupted
            while not stop_event.is_set():
                time.sleep(1)
    except KeyboardInterrupt:
        print("\nInterrupted by user")
        stop_event.set()
    
    # Wait for threads to finish
    for t in threads:
        t.join(timeout=2.0)
    
    # Pause shared buffer monitoring
    hrperf_pause_shared_buffers()
    print("Shared buffer monitoring stopped")
    
    return 0

if __name__ == "__main__":
    sys.exit(main()) 