#!/usr/bin/env python3
import os
import fcntl
import mmap
import struct
import errno

# Definitions to mimic the C _IOC and _IO macros
_IOC_NRBITS    = 8
_IOC_TYPEBITS  = 8
_IOC_SIZEBITS  = 14
_IOC_DIRBITS   = 2

_IOC_NRSHIFT   = 0
_IOC_TYPESHIFT = _IOC_NRSHIFT + _IOC_NRBITS
_IOC_SIZESHIFT = _IOC_TYPESHIFT + _IOC_TYPEBITS
_IOC_DIRSHIFT  = _IOC_SIZESHIFT + _IOC_SIZEBITS

_IOC_NONE = 0
_IOC_WRITE = 1
_IOC_READ = 2

def _IOC(direction, type_char, nr, size):
    """
    Mimic the _IOC macro from the C code.
    The type parameter is expected as a one-character string.
    """
    return ((direction << _IOC_DIRSHIFT) |
            (ord(type_char) << _IOC_TYPESHIFT) |
            (nr << _IOC_NRSHIFT) |
            (size << _IOC_SIZESHIFT))

def _IO(type_char, nr):
    """
    Mimic the _IO macro (no arguments, size = 0).
    """
    return _IOC(_IOC_NONE, type_char, nr, 0)

def _IOW(type_char, nr, size):
    """
    Mimic the _IOW macro (write argument, size != 0).
    """
    return _IOC(_IOC_WRITE, type_char, nr, size)

def _IOR(type_char, nr, size):
    """
    Mimic the _IOR macro (read argument, size != 0).
    """
    return _IOC(_IOC_READ, type_char, nr, size)

def _IOWR(type_char, nr, size):
    """
    Mimic the _IOWR macro (read and write arguments, size != 0).
    """
    return _IOC(_IOC_READ | _IOC_WRITE, type_char, nr, size)

# Macro definitions
HRP_PMC_IOC_MAGIC = 'k'
HRP_PMC_IOC_START = _IO(HRP_PMC_IOC_MAGIC, 1)
HRP_IOC_STOP      = _IO(HRP_PMC_IOC_MAGIC, 2)

# Shared buffer ioctl commands
HRP_PMC_IOC_SHARED_INIT    = _IOW(HRP_PMC_IOC_MAGIC, 3, 8)  # unsigned long is 8 bytes
HRP_PMC_IOC_SHARED_START   = _IO(HRP_PMC_IOC_MAGIC, 4)
HRP_PMC_IOC_SHARED_PAUSE   = _IO(HRP_PMC_IOC_MAGIC, 5)
HRP_PMC_IOC_SHARED_INFO    = _IOR(HRP_PMC_IOC_MAGIC, 6, 12)  # 3 uint32_t = 12 bytes
HRP_PMC_IOC_SHARED_CPU_INFO = _IOWR(HRP_PMC_IOC_MAGIC, 7, 16)  # uint32_t + uint64_t + uint32_t = 16 bytes

DEVICE_PATH = "/dev/hrperf_device"

def hrperf_start():
    """
    Open the device and send the start command via ioctl.
    Returns 0 on success, or 1 on failure.
    """
    try:
        fd = os.open(DEVICE_PATH, os.O_RDWR)
    except OSError as e:
        print("open:", e)
        return 1

    try:
        fcntl.ioctl(fd, HRP_PMC_IOC_START)
    except OSError as e:
        print("ioctl:", e)
        os.close(fd)
        return 1

    os.close(fd)
    return 0

def hrperf_pause():
    """
    Open the device and send the stop command via ioctl.
    Returns 0 on success, or 1 on failure.
    """
    try:
        fd = os.open(DEVICE_PATH, os.O_RDWR)
    except OSError as e:
        print("open:", e)
        return 1

    try:
        fcntl.ioctl(fd, HRP_IOC_STOP)
    except OSError as e:
        print("ioctl:", e)
        os.close(fd)
        return 1

    os.close(fd)
    return 0

def hrperf_init_shared_buffers(per_buffer_size=1024*1024):
    """
    Initialize per-CPU shared buffers for real-time monitoring.
    Args:
        per_buffer_size: Size of each CPU's buffer in bytes (default: 1MB)
    Returns:
        0 on success, negative errno on failure
    """
    try:
        fd = os.open(DEVICE_PATH, os.O_RDWR)
    except OSError as e:
        return -e.errno

    try:
        fcntl.ioctl(fd, HRP_PMC_IOC_SHARED_INIT, per_buffer_size)
        return 0
    except OSError as e:
        return -e.errno
    finally:
        os.close(fd)

def hrperf_start_shared_buffers():
    """
    Start writing to shared buffers.
    Returns:
        0 on success, negative errno on failure
    """
    try:
        fd = os.open(DEVICE_PATH, os.O_RDWR)
    except OSError as e:
        return -e.errno

    try:
        fcntl.ioctl(fd, HRP_PMC_IOC_SHARED_START)
        return 0
    except OSError as e:
        return -e.errno
    finally:
        os.close(fd)

def hrperf_pause_shared_buffers():
    """
    Pause writing to shared buffers.
    Returns:
        0 on success, negative errno on failure
    """
    try:
        fd = os.open(DEVICE_PATH, os.O_RDWR)
    except OSError as e:
        return -e.errno

    try:
        fcntl.ioctl(fd, HRP_PMC_IOC_SHARED_PAUSE)
        return 0
    except OSError as e:
        return -e.errno
    finally:
        os.close(fd)

def hrperf_get_shared_buffer_info():
    """
    Get information about all shared buffers.
    Returns:
        Dictionary with buffer info on success, None on failure
    """
    try:
        fd = os.open(DEVICE_PATH, os.O_RDWR)
    except OSError:
        return None

    try:
        # cpu_count, buffer_size, entry_size (3 uint32_t)
        buffer_info = struct.pack("III", 0, 0, 0)
        buffer_info = fcntl.ioctl(fd, HRP_PMC_IOC_SHARED_INFO, buffer_info)
        cpu_count, buffer_size, entry_size = struct.unpack("III", buffer_info)
        return {
            "cpu_count": cpu_count,
            "buffer_size": buffer_size,
            "entry_size": entry_size
        }
    except OSError:
        return None
    finally:
        os.close(fd)

def hrperf_get_cpu_buffer_info(cpu_id):
    """
    Get information about a specific CPU's buffer.
    Args:
        cpu_id: CPU ID to get buffer info for
    Returns:
        Dictionary with CPU buffer info on success, None on failure
    """
    try:
        fd = os.open(DEVICE_PATH, os.O_RDWR)
    except OSError:
        return None

    try:
        # cpu_id, phys_addr, buffer_size (uint32_t + uint64_t + uint32_t)
        info = struct.pack("IQI", cpu_id, 0, 0)
        info = fcntl.ioctl(fd, HRP_PMC_IOC_SHARED_CPU_INFO, info)
        cpu_id, phys_addr, buffer_size = struct.unpack("IQI", info)
        return {
            "cpu_id": cpu_id,
            "phys_addr": phys_addr,
            "buffer_size": buffer_size
        }
    except OSError:
        return None
    finally:
        os.close(fd)

def hrperf_map_cpu_buffer(cpu_id):
    """
    Map a specific CPU's buffer into userspace.
    Args:
        cpu_id: CPU ID to map buffer for
    Returns:
        mmap object for the buffer on success, None on failure
    """
    try:
        fd = os.open(DEVICE_PATH, os.O_RDWR)
    except OSError:
        return None

    try:
        # Get buffer info for this CPU
        info = hrperf_get_cpu_buffer_info(cpu_id)
        if not info:
            return None
            
        # Map the buffer using the CPU ID as the offset
        buf = mmap.mmap(fd, info["buffer_size"], mmap.MAP_SHARED, 
                        mmap.PROT_READ, offset=cpu_id * mmap.PAGESIZE)
        return buf
    except:
        return None
    finally:
        os.close(fd)
        
def hrperf_unmap_buffer(buffer):
    """
    Unmap a previously mapped buffer.
    Args:
        buffer: mmap object returned by hrperf_map_cpu_buffer
    """
    if buffer:
        buffer.close()