import struct
import os

# Define the struct format
event_format = 'QIIIQL'  # Corresponds to the structure of hrp_bpf_event
event_size = struct.calcsize(event_format)

# Dictionary of event types for easy lookup
event_types = {
    1: "TCP_IN_START", 2: "TCP_OUT_START", 3: "UDP_IN_START", 4: "UDP_OUT_START",
    5: "BLKIO_READ_START", 6: "BLKIO_WRITE_START", 7: "TCP_IN_END", 8: "TCP_OUT_END",
    9: "UDP_IN_END", 10: "UDP_OUT_END", 11: "BLKIO_READ_END", 12: "BLKIO_WRITE_END"
}

def parse_log_file(filepath):
    # Read the entire log file
    with open(filepath, "rb") as file:
        log_data = file.read()

    # Prepare to parse the log data
    entries = []
    for offset in range(0, len(log_data), event_size):
        entry = struct.unpack(event_format, log_data[offset:offset + event_size])
        entries.append(entry)
    
    # Sort entries by tid and write to respective files
    for entry in entries:
        ts_ns, pid, tid, event_type, size_or_ret, rbp_or_bio_addr = entry
        
        # Check if event type is known, else print it as an unsigned int
        event_name = event_types.get(event_type, f"UNKNOWN EVENT {event_type}")
        type_parts = event_name.split()

        # Determine if it's a size or a return value
        if "END" in type_parts:
            size_or_ret = struct.unpack('i', struct.pack('I', size_or_ret & 0xFFFFFFFF))[0]  # Convert to signed int
        
        # Format the output line
        output_line = f"{ts_ns} {event_name} {size_or_ret}\n"
        
        # Write to a file named after the thread id
        with open(f"{tid}.txt", "a") as tid_file:
            tid_file.write(output_line)

# Path to the binary log file
log_file_path = "/bpf_log.bin"
parse_log_file(log_file_path)
