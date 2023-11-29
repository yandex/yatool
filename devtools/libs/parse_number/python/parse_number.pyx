import six

from libc.stdint cimport uint64_t
from libcpp cimport bool as bool_t

from util.generic.string cimport TString, TStringBuf
from util.system.types cimport ui64


cdef extern from "<devtools/libs/parse_number/parse_number.h>" namespace "NDistBuild" nogil:

    bool_t TryParseHumanReadableNumber(TStringBuf str, float* number) except +

    bool_t TryParseHumanReadableNumber(TStringBuf str, uint64_t* number) except +


cdef TString _to_TString(s):
    assert isinstance(s, six.string_types)
    s = six.ensure_binary(s, "UTF-8")
    return TString(<const char*>s, len(s))


def parse_human_readable_number(str):
    cdef ui64 result_i = 0
    # Initialy we were using TStringBuf here, but Py3 can't convert its str directly to const char *
    # Somehow, bytestrings do not seem to work as well (we are getting some jibberish in unknown encoding)
    if TryParseHumanReadableNumber(_to_TString(str), &result_i):
        return result_i

    cdef float result_f = 0
    if TryParseHumanReadableNumber(_to_TString(str), &result_f):
        return result_f

    raise ValueError("Number {} is invalid or has wrong unit prefix".format(str))
