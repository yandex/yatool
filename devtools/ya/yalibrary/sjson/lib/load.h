#pragma once

#include <Python.h>

#include <util/generic/hash_set.h>
#include <util/generic/vector.h>
#include "compact.h"

namespace NSJson {
    struct TLoaderOptions {
        TLoaderOptions() = default;
        bool InternKeys{};
        bool InternValues{};
        THashSet<TString> RootKeyBlackList{};
        THashSet<TString> RootKeyWhiteList{};
        TVector<TString> ArrayItemPath{};
        THashSet<TString> ArrayItemKeyWhiteList{};
        TVector<TVector<TString>> CompactPaths{};
        TCompactStorage* CompactStorage{};
        PyObject* CompactOwner{};
        PyObject* (*CompactFactory)(PyObject*, TIndexType){};
    };

    PyObject* LoadFromStream(PyObject* stream, const TLoaderOptions& options = {});
    PyObject* LoadFromFile(const TString& path, const TLoaderOptions& options = {});
}
