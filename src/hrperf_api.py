#!/usr/bin/env python3
import os
import fcntl

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

# Macro definitions (keeping the same semantics as the C version)
HRP_PMC_IOC_MAGIC = 'k'
HRP_PMC_IOC_START = _IO(HRP_PMC_IOC_MAGIC, 1)
HRP_IOC_STOP      = _IO(HRP_PMC_IOC_MAGIC, 2)

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