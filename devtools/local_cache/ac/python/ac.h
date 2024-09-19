#pragma once

#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include <util/generic/string.h>

#include <tuple>

namespace NAcClient {
    using TAddress = std::tuple<pid_t, time_t, TString>;
    using TBlob = std::tuple<TString, TString>;

    void InitGlobals(PyObject* acPb2Module, PyObject* logger, PyObject* grpcErrorCls);
    void ClearGlobals();
    void ExceptionHandler();

    PyObject* PutUid(
        const TAddress& paddress,
        const TString& uid,
        const TString& rootPath,
        const TVector<TBlob>& blobs,
        long long weight,
        bool hardlink,
        bool replace,
        bool isResult,
        const TVector<TString>& fileNames,
        double timeout,
        bool waitForReady
    );

    PyObject* GetUid(
        const TAddress& paddress,
        const TString& uid,
        const TString& destPath,
        bool hardlink,
        bool isResult,
        bool release,
        double timeout,
        bool waitForReady
    );

   PyObject* HasUid(
        const TAddress& paddress,
        const TString& uid,
        bool isResult,
        double timeout,
        bool waitForReady
    );

   PyObject* RemoveUid(
        const TAddress& paddress,
        const TString& uid,
        bool forcedRemoval,
        double timeout,
        bool waitForReady
    );

   PyObject* GetTaskStats(
        const TAddress& paddress,
        double timeout,
        bool waitForReady
    );

    PyObject* PutDeps(
        const TAddress& paddress,
        const TString& uid,
        const TVector<TString>& deps,
        double timeout,
        bool waitForReady
    );

    PyObject* ForceGC(
        const TAddress& paddress,
        uint64_t diskLimit,
        double timeout,
        bool waitForReady
    );

   PyObject* ReleaseAll(
        const TAddress& paddress,
        double timeout,
        bool waitForReady
    );

   PyObject* GetCacheStats(
        const TAddress& paddress,
        double timeout,
        bool waitForReady
    );

   PyObject* AnalyzeDU(
        const TAddress& paddress,
        double timeout,
        bool waitForReady
    );

   PyObject* SynchronousGC(
        const TAddress& paddress,
        long long totalSize,
        long long minLastAccess,
        long long maxObjectSize,
        double timeout,
        bool waitForReady
    );
}
