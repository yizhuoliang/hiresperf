import sys
import os
import struct
import pickle

# Import modules for ELF parsing
from elftools.elf.elffile import ELFFile
from elftools.common.py3compat import bytes2str
from elftools.dwarf.descriptions import describe_form_class

# Event types
EVENT_STACK_SAMPLE = 1

def parse_ldb_data(ldb_data_filename):
    thread_events = {}
    pcs = set()
    with open(ldb_data_filename, 'rb') as ldb_bin:
        while True:
            byte = ldb_bin.read(40)
            if not byte:
                break
            event_type = int.from_bytes(byte[0:4], "little")
            ts_sec = int.from_bytes(byte[4:8], "little")
            ts_nsec = int.from_bytes(byte[8:12], "little")
            timestamp_ns = ts_sec * 1000000000 + ts_nsec
            tid = int.from_bytes(byte[12:16], "little")
            arg1 = int.from_bytes(byte[16:24], "little")
            arg2 = int.from_bytes(byte[24:32], "little")
            arg3 = int.from_bytes(byte[32:40], "little")
            if event_type < EVENT_STACK_SAMPLE:
                continue
            event = {'timestamp_ns': timestamp_ns,
                     'thread_id': tid,
                     'event_type': event_type,
                     'arg1': arg1,
                     'arg2': arg2,
                     'arg3': arg3,
                     'raw_data': byte  # Store raw binary data if needed
                    }
            if event_type == EVENT_STACK_SAMPLE:
                latency_us = arg1 / 1000.0
                pc = arg2
                gen_depth = arg3
                ngen = gen_depth >> 16
                depth = gen_depth & 0xFFFF
                pc -= 5  # Adjust pc as in original script
                event['latency_us'] = latency_us
                event['pc'] = pc
                event['ngen'] = ngen
                event['depth'] = depth  # Store depth in the event
                pcs.add(pc)
            thread_events.setdefault(tid, []).append(event)
    return thread_events, pcs

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
                    ret[addr] = {'fname': fe.name,
                                 'dir': dir_path,
                                 'line': prevstate.line,
                                 'col': prevstate.column}
                    addresses.remove(addr)
                if len(addresses) == 0:
                    return ret
            if entry.state.end_sequence:
                prevstate = None
            else:
                prevstate = entry.state
    return ret

def extract_func_desc_from_source(file_path, nline, ncol, source_dir_path):
    if not os.path.exists(file_path):
        file_path = os.path.join(source_dir_path, file_path[2:])
    try:
        with open(file_path, "r") as f:
            lines = f.readlines()
            if nline - 1 < len(lines):
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
    ret = decode_file_line(dwarfinfo, addresses)
    for key in ret:
        func_desc = extract_func_desc_from_source(bytes2str(ret[key]['dir']) + "/" + bytes2str(ret[key]['fname']),
                                                  ret[key]['line'], ret[key]['col'],
                                                  source_dir_path)
        finfomap[key] = func_desc
    return finfomap

def main():
    if len(sys.argv) != 4:
        print('Expected usage: {0} <executable> <source_dir_path> <ldb_data>'.format(sys.argv[0]))
        sys.exit(1)
    executable = sys.argv[1]
    source_dir_path = sys.argv[2]
    ldb_data_filename = sys.argv[3]

    # Ask user for function name
    function_of_interest = input("Enter the function name (e.g., 'collect'): ").strip()

    # Parse ldb.data
    print("Parsing ldb.data...")
    thread_events, pcs = parse_ldb_data(ldb_data_filename)

    # Get function names
    print("Parsing ELF file and extracting function names...")
    dwarfinfo = parse_elf(executable)
    finfomap = {}
    if dwarfinfo is not None:
        finfomap = get_finfos(dwarfinfo, pcs, source_dir_path)

    # Annotate events with function names
    for tid, events in thread_events.items():
        for event in events:
            if event['event_type'] == EVENT_STACK_SAMPLE:
                pc = event['pc']
                function_desc = finfomap.get(pc, "???")
                event['function_desc'] = function_desc

    # Now process events per thread
    print("Processing events per thread...")
    invocations = []

    for thread_id, events in thread_events.items():
        # Sort events by timestamp
        events.sort(key=lambda e: e['timestamp_ns'])
        # Process events
        for event in events:
            if event['event_type'] == EVENT_STACK_SAMPLE:
                function_desc = event.get('function_desc', '???')
                if function_desc.startswith(function_of_interest):
                    # Identify the execution interval
                    end_time_ns = event['timestamp_ns']
                    latency_us = event['latency_us']
                    latency_ns = int(latency_us * 1000)  # Convert us to ns
                    start_time_ns = end_time_ns - latency_ns
                    # Collect events of the same thread between start_time_ns and end_time_ns
                    events_in_interval = [e for e in events if start_time_ns <= e['timestamp_ns'] <= end_time_ns]
                    # Create invocation record
                    invocation = {
                        'thread_id': thread_id,
                        'start_time_ns': start_time_ns,
                        'end_time_ns': end_time_ns,
                        'latency_us': latency_us,
                        'function_desc': function_desc,
                        'events': events_in_interval
                    }
                    invocations.append(invocation)

    # Create output directory
    sanitized_function_name = function_of_interest.replace(' ', '_').replace('(', '').replace(')', '').replace(':', '_').replace('.', '_')
    output_dir = f"analysis_{sanitized_function_name}"
    if not os.path.exists(output_dir):
        os.makedirs(output_dir)

    # Save invocations to pickle file
    output_filename = os.path.join(output_dir, 'invocations.pkl')
    with open(output_filename, 'wb') as f:
        pickle.dump(invocations, f)
    print(f"Saved {len(invocations)} invocations to {output_filename}")

if __name__ == "__main__":
    main()
