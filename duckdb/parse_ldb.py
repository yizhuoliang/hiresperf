import sys
import os
import struct
import duckdb
import pandas as pd
from elftools.elf.elffile import ELFFile
from elftools.common.py3compat import bytes2str

# Event types
EVENT_STACK_SAMPLE = 1
EVENT_TAG_SET = 2
EVENT_TAG_BLOCK = 3
EVENT_TAG_UNSET = 4
EVENT_TAG_CLEAR = 5
EVENT_MUTEX_WAIT = 6
EVENT_MUTEX_LOCK = 7
EVENT_MUTEX_UNLOCK = 8
EVENT_JOIN_WAIT = 9
EVENT_JOIN_JOINED = 10
EVENT_THREAD_CREATE = 11
EVENT_THREAD_EXIT = 12

EVENT_TYPE_MAP = {
    EVENT_STACK_SAMPLE: "STACK_SAMPLE",
    EVENT_TAG_SET: "TAG_SET",
    EVENT_TAG_BLOCK: "TAG_BLOCK",
    EVENT_TAG_UNSET: "TAG_UNSET",
    EVENT_TAG_CLEAR: "TAG_CLEAR",
    EVENT_MUTEX_WAIT: "MUTEX_WAIT",
    EVENT_MUTEX_LOCK: "MUTEX_LOCK",
    EVENT_MUTEX_UNLOCK: "MUTEX_UNLOCK",
    EVENT_JOIN_WAIT: "JOIN_WAIT",
    EVENT_JOIN_JOINED: "JOIN_JOINED",
    EVENT_THREAD_CREATE: "THREAD_CREATE",
    EVENT_THREAD_EXIT: "THREAD_EXIT",
}

def parse_ldb_data(ldb_data_filename):
    events = []
    pcs = set()
    last_mutex_ts = {}
    wait_lock_time = {}
    with open(ldb_data_filename, 'rb') as ldb_bin:
        while True:
            byte = ldb_bin.read(40)
            if not byte:
                break
            event_type = int.from_bytes(byte[0:4], "little")
            ts_sec = int.from_bytes(byte[4:8], "little")
            ts_nsec = int.from_bytes(byte[8:12], "little")
            timestamp_ns = ts_sec * 1_000_000_000 + ts_nsec
            tid = int.from_bytes(byte[12:16], "little")
            arg1 = int.from_bytes(byte[16:24], "little")
            arg2 = int.from_bytes(byte[24:32], "little")
            arg3 = int.from_bytes(byte[32:40], "little")

            # Skip invalid event_type
            if event_type < EVENT_STACK_SAMPLE or event_type > EVENT_THREAD_EXIT:
                continue

            event = {
                'timestamp_ns': timestamp_ns,
                'thread_id': tid,
                'event_type': event_type,
                'event_name': EVENT_TYPE_MAP.get(event_type, "UNKNOWN"),
                'func_desc': None,
                'latency_us': None,
                'detail': None,
                'pc': None,
                'ngen': None,
                'depth': None,
                'tag': None,
                'mutex': None,
                'wait_time_us': None,
                'lock_time_us': None,
                'thread_to_join': None,
                'thread_joined': None,
            }

            if event_type == EVENT_STACK_SAMPLE:
                latency_us = arg1 / 1000.0
                # Adjust PC as in original script
                pc = arg2 - 5
                gen_depth = arg3
                ngen = gen_depth >> 16
                depth = gen_depth & 0xFFFF
                event.update({
                    'latency_us': latency_us,
                    'pc': pc,
                    'ngen': ngen,
                    'depth': depth,
                })
                pcs.add(pc)

            elif event_type in (EVENT_TAG_SET, EVENT_TAG_BLOCK, EVENT_TAG_UNSET, EVENT_TAG_CLEAR):
                tag = arg1
                event['tag'] = tag

            elif event_type == EVENT_MUTEX_WAIT:
                mutex = arg1
                event['mutex'] = mutex
                last_mutex_ts[tid] = timestamp_ns
                if tid not in wait_lock_time:
                    wait_lock_time[tid] = []
                wait_lock_time[tid].append([timestamp_ns, -1, -1])

            elif event_type == EVENT_MUTEX_LOCK:
                mutex = arg1
                event['mutex'] = mutex
                if tid in last_mutex_ts:
                    wait_time_us = (timestamp_ns - last_mutex_ts[tid]) / 1000.0
                    event['wait_time_us'] = wait_time_us
                    # Update last_mutex_ts to point to lock time start
                    last_mutex_ts[tid] = timestamp_ns
                if tid in wait_lock_time:
                    wait_lock_time[tid][-1][1] = timestamp_ns

            elif event_type == EVENT_MUTEX_UNLOCK:
                mutex = arg1
                event['mutex'] = mutex
                if tid in last_mutex_ts:
                    lock_time_us = (timestamp_ns - last_mutex_ts[tid]) / 1000.0
                    event['lock_time_us'] = lock_time_us
                    del last_mutex_ts[tid]
                if tid in wait_lock_time:
                    wait_lock_time[tid][-1][2] = timestamp_ns

            elif event_type == EVENT_THREAD_CREATE:
                pass

            elif event_type == EVENT_THREAD_EXIT:
                pass

            elif event_type == EVENT_JOIN_WAIT:
                thread_to_join = arg1
                event['thread_to_join'] = thread_to_join

            elif event_type == EVENT_JOIN_JOINED:
                thread_joined = arg1
                event['thread_joined'] = thread_joined

            events.append(event)

    return events, pcs

def parse_elf(executable):
    if not os.path.exists(executable):
        print('Cannot find executable: {}'.format(executable))
        return None
    with open(executable, 'rb') as e:
        elffile = ELFFile(e)
        if not elffile.has_dwarf_info():
            print('File has no debugging information')
            return None
        dwarfinfo = elffile.get_dwarf_info()
        return dwarfinfo

def decode_file_line(dwarfinfo, addresses):
    ret = {}
    for CU in dwarfinfo.iter_CUs():
        lineprog = dwarfinfo.line_program_for_CU(CU)
        prevstate = None
        offset = 1
        # Workaround for some corner cases in file_entry
        if len(lineprog['file_entry']) > 1 and \
                lineprog['file_entry'][0] == lineprog['file_entry'][1]:
            offset = 0

        for entry in lineprog.get_entries():
            if entry.state is None:
                continue
            if prevstate:
                addrs = [x for x in addresses if prevstate.address <= x < entry.state.address]
                for addr in addrs:
                    fe = lineprog['file_entry'][prevstate.file - offset]
                    dir_path = b'.'
                    if fe.dir_index > 0:
                        dir_path = lineprog['include_directory'][fe.dir_index - 1]
                    ret[addr] = {
                        'fname': fe.name,
                        'dir': dir_path,
                        'line': prevstate.line,
                        'col': prevstate.column
                    }
                    addresses.remove(addr)
                if len(addresses) == 0:
                    return ret
            if entry.state.end_sequence:
                prevstate = None
            else:
                prevstate = entry.state
    return ret

def extract_func_desc_from_source(file_path, nline, ncol, source_dir_path):
    file_path = bytes2str(file_path)
    if not os.path.exists(file_path):
        file_path = os.path.join(source_dir_path, file_path[2:])
    try:
        with open(file_path, "r") as f:
            lines = f.readlines()
            if (nline - 1) < len(lines):
                line = lines[nline - 1]
                return extract_func_desc(line[ncol-1:])
    except:
        return "???"
    return "???"

def extract_func_desc(line):
    # Simple extraction of function name
    line = line.strip()
    tokens = line.split()
    if tokens:
        return tokens[0]
    else:
        return "???"

def get_finfos(dwarfinfo, addresses, source_dir_path):
    addresses = list(set(addresses))
    addresses.sort()
    finfomap = {}
    if dwarfinfo is None:
        return finfomap
    line_info_map = decode_file_line(dwarfinfo, addresses)
    for addr, info in line_info_map.items():
        func_desc = extract_func_desc_from_source(
            os.path.join(info['dir'], info['fname']),
            info['line'], info['col'],
            source_dir_path
        )
        finfomap[addr] = func_desc
    return finfomap

def main():
    if len(sys.argv) != 4:
        print('Expected usage: {0} <executable> <source_dir_path> <ldb_data>'.format(sys.argv[0]))
        sys.exit(1)
    executable = sys.argv[1]
    source_dir_path = sys.argv[2]
    ldb_data_filename = sys.argv[3]

    # Parse ldb.data
    print("Parsing ldb.data...")
    events, pcs = parse_ldb_data(ldb_data_filename)

    # Parse ELF file and extract function names
    print("Parsing ELF file and extracting function names...")
    dwarfinfo = parse_elf(executable)
    finfomap = {}
    if dwarfinfo is not None:
        finfomap = get_finfos(dwarfinfo, pcs, source_dir_path)
    else:
        print(f"Cannot find or parse '{executable}'. Function descriptors may remain None or ???")

    # We will build an on-the-fly map from func_desc -> func_desc_id
    # so that we do not need an extra pass over 'events'.
    func_desc_map = {}
    next_func_desc_id = 1

    # Annotate events with function names and assign func_desc_id in one pass
    for event in events:
        pc = event['pc']
        if pc is not None and pc in finfomap:
            event['func_desc'] = finfomap[pc]
        elif pc is not None:
            # If PC not in map, we fallback to "???"
            event['func_desc'] = "???"
        # else: event['func_desc'] remains None if pc is None

        # Assign func_desc_id = 0 if func_desc is None
        # Otherwise, use the map (allocating new IDs as needed)
        fd = event['func_desc']
        if fd is None:
            event['func_desc_id'] = 0
        else:
            if fd not in func_desc_map:
                func_desc_map[fd] = next_func_desc_id
                next_func_desc_id += 1
            event['func_desc_id'] = func_desc_map[fd]

    # Convert events list to Pandas DataFrame
    print("Converting events list to Pandas DataFrame...")
    df_events = pd.DataFrame(events)

    # Initialize DuckDB connection (on-disk database)
    con = duckdb.connect(database='analysis.duckdb')

    # Define or create the schema for the ldb_events table (now including func_desc_id)
    con.execute('''
        CREATE TABLE IF NOT EXISTS ldb_events (
            timestamp_ns BIGINT,
            thread_id BIGINT,
            event_type INTEGER,
            event_name VARCHAR,
            func_desc VARCHAR,
            func_desc_id INTEGER,
            latency_us DOUBLE,
            detail VARCHAR,
            pc BIGINT,
            ngen INTEGER,
            depth INTEGER,
            tag INTEGER,
            mutex BIGINT,
            wait_time_us DOUBLE,
            lock_time_us DOUBLE,
            thread_to_join BIGINT,
            thread_joined BIGINT
        )
    ''')

    # Register the DataFrame as a virtual table
    print("Registering DataFrame as a virtual table...")
    con.register('df_events', df_events)

    # Insert data from DataFrame into ldb_events table
    print("Inserting events into ldb_events table...")
    con.execute('''
        INSERT INTO ldb_events
        SELECT
            timestamp_ns,
            thread_id,
            event_type,
            event_name,
            func_desc,
            func_desc_id,
            latency_us,
            detail,
            pc,
            ngen,
            depth,
            tag,
            mutex,
            wait_time_us,
            lock_time_us,
            thread_to_join,
            thread_joined
        FROM df_events
    ''')

    # Close the connection
    con.close()
    print("Database updated and saved to 'analysis.duckdb'")

if __name__ == "__main__":
    main()