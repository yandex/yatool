from __future__ import print_function
import struct

# File format version, will be incremented for each incompatible change
FORMAT_VERSION = 0x1007
# Magic number in header for file format identification
MAGIC_NUMBER = 0xC0C0
# Block identifier for file headers
BLOCK_HEADER = 0x01
# Block identifier for session information
BLOCK_SESSIONINFO = 0x10
# Block identifier for execution data of a single class
BLOCK_EXECUTIONDATA = 0x11

_FMT_SIZE_MAP = {
    1: ">B",
    2: ">H",
    4: ">L",
    8: ">Q",
}


class SessionInfo(object):
    def __init__(self, sid, start, dump):
        self.sid = sid
        self.start = start
        self.dump = dump
        self.execution_data = []


class ExecutionData(object):
    def __init__(self, record_id, name, probes):
        self.record_id = record_id
        self.name = name
        self.probes = probes


def load_report(filename):
    arr = []
    with open(filename, 'rb') as afile:
        while True:
            try:
                block_type = read_ui8(afile)
            except EOFError:
                break

            read_block(block_type, afile, arr)
    return arr


def read_block(btype, stream, sessions):
    # fmt: off
    return {
        BLOCK_HEADER: read_header,
        BLOCK_SESSIONINFO: read_session_info,
        BLOCK_EXECUTIONDATA: read_execution_data,
    }[btype](stream, sessions)
    # fmt: on


def read_header(stream, _sessions):
    if read_ui16(stream) != MAGIC_NUMBER:
        raise Exception("Invalid execution data file")
    version = read_ui16(stream)
    if version != FORMAT_VERSION:
        raise Exception("Incompatible exec data version - expected ({}), got ({})".format(FORMAT_VERSION, version))


def read_session_info(stream, sessions):
    sid = read_utf(stream)
    start = read_ui64(stream)
    dump = read_ui64(stream)
    sessions.append(SessionInfo(sid, start, dump))


def read_execution_data(stream, sessions):
    record_id = read_ui64(stream)
    name = read_utf(stream)
    probes = read_boolean_array(stream)
    last = sessions[-1]
    last.execution_data.append(ExecutionData(record_id, name, probes))


def read_boolean_array(stream):
    size = read_var_int(stream)
    arr = [0] * size
    buf = 0
    for i in range(0, size):
        if (i % 8) == 0:
            buf = read_ui8(stream)
        arr[i] = (buf & 0x01) != 0
        buf >>= 1
    return arr


def read_var_int(stream):
    # Reads a variable length representation of an integer value
    val = read_ui8(stream)
    if (val & 0x80) == 0:
        return val
    return (val & 0x7F) | (read_var_int(stream) << 7)


def read_bytes(stream, size):
    data = stream.read(size)
    dsize = len(data)
    if dsize != size:
        raise EOFError(
            "Failed to read {} bytes (read data [{}] '{}' at pos: {})".format(
                size,
                len(data),
                data,
                stream.tell(),
            )
        )
    return struct.unpack(_FMT_SIZE_MAP[size], data)[0]


def read_ui8(stream):
    return read_bytes(stream, 1)


def read_ui16(stream):
    return read_bytes(stream, 2)


def read_ui64(stream):
    return read_bytes(stream, 8)


def read_utf(stream):
    size = read_ui16(stream)
    return stream.read(size).decode("utf8")


if __name__ == "__main__":
    import sys

    load_report(sys.argv[1])
    print("Report loaded")
