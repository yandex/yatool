#pragma once

#include <Python.h>

#include <util/generic/hash_set.h>
#include <util/generic/vector.h>

namespace NSJson {
    struct TLoaderOptions {
        TLoaderOptions() = default;
        bool InternKeys{};
        bool InternValues{};
        THashSet<TString> RootKeyBlackList{};
        THashSet<TString> RootKeyWhiteList{};
        TVector<TString> ArrayItemPath{};
        THashSet<TString> ArrayItemKeyWhiteList{};
    };

    PyObject* LoadFromStream(PyObject* stream, const TLoaderOptions& options = {});
    PyObject* LoadFromFile(const TString& path, const TLoaderOptions& options = {});
}
