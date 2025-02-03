import os
import logging
import threading
import json

from cpython.ref cimport PyObject, Py_INCREF, Py_DECREF
from cpython.bytes cimport PyBytes_FromStringAndSize
from util.generic.string cimport TString
from util.generic.vector cimport TVector
from util.generic.ptr cimport THolder
from util.generic.maybe cimport TMaybe
from util.datetime.base cimport TInstant, TDuration
from util.system.types cimport i32, ui64
from libcpp cimport bool
from libcpp.pair cimport pair


cdef extern from "devtools/libs/universal_fetcher/universal_fetcher/fetchers_interface.h" namespace "NUniversalFetcher" nogil:
    cdef cppclass TFetchProgressCallback:
        pass

cdef extern from "devtools/libs/universal_fetcher/registry/registry.h" namespace "NUniversalFetcher" nogil:
    cdef cppclass TUniversalFetcherLoggingFunction:
        pass

    cdef cppclass TFetchParams:
        cppclass TProgressReporting:
            TFetchProgressCallback Callback
            TDuration MinInterval

        TMaybe[TProgressReporting] ProgressReporting

    THolder[TUniversalFetcher] CreateUniversalFetcher(const TString& configJson, TUniversalFetcherLoggingFunction)

    cdef cppclass TUniversalFetcher:
        cppclass TResult:
            TString ToJsonString() const;

        THolder[TResult] DownloadToFile(const TString&, const TString&, const TFetchParams&) nogil
        THolder[TResult] DownloadToDir(const TString&, const TString&, const TFetchParams&) nogil

cdef extern from "devtools/libs/universal_fetcher/py/helpers/helpers.h" namespace "NUniversalFetcher":
    cdef cppclass TChecksumInfo:
        pass

    cdef TUniversalFetcherLoggingFunction WrapPythonLogFunction(void* pyLogFunc, void(*pyLogFuncProxy)(void*, i32, const TString&))
    cdef TFetchProgressCallback WrapPythonProgressFunction(void* pyProgessFunc, void(*pyProgressFuncProxy)(void*, ui64, ui64))

    cdef pair[TString, TMaybe[TChecksumInfo]] ParseIntegrity(const TString& extUrl)
    bool CheckIntegrity(const TString& filepath, const TChecksumInfo& integrity)

cdef void pyLogFuncProxy(void* pyLogFunc, i32 priority, const TString& msg) with gil:
    logFunc = <object>(<PyObject*>pyLogFunc)
    logFunc(priority, PyBytes_FromStringAndSize(msg.c_str(), msg.size()))

cdef void pyProgressFuncProxy(void* pyProgressFunc, ui64 downloaded, ui64 totalSize) with gil:
    progressFunc = <object>(<PyObject*>pyProgressFunc)
    progressFunc(downloaded, totalSize)


OK = 'ok'
RETRIABLE_ERROR = 'retriable_error'
NON_RETRIABLE_ERROR = 'non_retriable_error'
INTERNAL_ERROR = 'internal_error'


cdef class UniversalFetcher:
    cdef THolder[TUniversalFetcher] _impl
    cdef object _log_function
    __log_priorities = {
        0: logging.CRITICAL,
        1: logging.CRITICAL,
        2: logging.CRITICAL,
        3: logging.ERROR,
        4: logging.WARNING,
        5: logging.INFO,
        6: logging.DEBUG,
        7: logging.DEBUG,
        8: logging.DEBUG,
    }

    def __init__(self, json_config, logger):
        assert isinstance(json_config, str)
        json_config = json_config.encode()
        def log_function(priority, msg):
            logger.log(self.__log_priorities[priority], msg.decode('utf-8').rstrip())
        self._log_function = log_function
        #Py_INCREF(log_function)
        cdef void* lf = <void*>(<PyObject*>self._log_function)
        cdef TUniversalFetcherLoggingFunction wrapped_func = WrapPythonLogFunction(lf, pyLogFuncProxy)
        self._impl = CreateUniversalFetcher(json_config, wrapped_func)

    def download(self, url, dst_path, filename=None, progress_callback=None, progress_callback_call_delay_ms=1000):
        assert isinstance(url, str)
        assert isinstance(dst_path, str)
        assert isinstance(filename, (str, type(None)))

        if filename is not None:
            dst_path = os.path.join(dst_path, filename)

        is_http = url.startswith('http')

        url = url.encode()
        dst_path = dst_path.encode()

        if is_http:
            try:
                url_with_integrity = ParseIntegrity(url)
                url, integrity = url_with_integrity.first, url_with_integrity.second
            except:
                raise Exception("Failed to parse url with params")

        cdef TString c_url = url
        cdef TString c_dst_path = dst_path

        cdef TFetchParams c_fetch_params
        cdef void* pf = <void*>(<PyObject*>progress_callback)
        if progress_callback is not None:
            c_fetch_params.ProgressReporting.ConstructInPlace()
            c_fetch_params.ProgressReporting.GetRef().Callback = WrapPythonProgressFunction(pf, pyProgressFuncProxy)
            c_fetch_params.ProgressReporting.GetRef().MinInterval = TDuration.MicroSeconds(progress_callback_call_delay_ms * 1000)

        with nogil:
            if filename is not None:
                c_result = self._impl.Get().DownloadToFile(c_url, c_dst_path, c_fetch_params)
            else:
                c_result = self._impl.Get().DownloadToDir(c_url, c_dst_path, c_fetch_params)
        
        result = json.loads(c_result.Get().ToJsonString())

        if is_http and integrity.Defined() and result['last_attempt']['result']['status'] == OK:
            if filename is not None:
                result_filepath = dst_path
            else:
                result_filepath = dst_path.decode() + '/' + result['last_attempt']['result']['resource_info']['filename']
                result_filepath = result_filepath.encode()

            try:
                if not CheckIntegrity(result_filepath, integrity.GetRef()):
                    raise
            except:
                raise Exception("Failed to check integrity")

        return result
