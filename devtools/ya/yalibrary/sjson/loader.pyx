from libcpp cimport bool

import io
import six
import sys

cdef extern from "devtools/ya/yalibrary/sjson/lib/load.h" namespace "NSJson":
    object LoadFromStream(object stream, bool internKeys, bool internVals)


def load(stream: io.RawIOBase, intern_keys=False, intern_vals=False):
    return LoadFromStream(stream, intern_keys, intern_vals)


def loads(s: str|bytes, intern_keys=False, intern_vals=False):
    if isinstance(s, str) and six.PY3:
        s = s.encode()
    stream = io.BytesIO(s)
    return load(stream)
