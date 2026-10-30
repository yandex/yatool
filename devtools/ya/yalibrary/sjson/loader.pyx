from libcpp cimport bool
from util.generic.hash_set cimport THashSet
from util.generic.string cimport TString
from util.generic.vector cimport TVector
from cpython.ref cimport PyObject
from .compact cimport _GraphStorage, _compact_factory, TCompactStorage, TIndexType

import io
import os
import six
import sys
import typing


cdef extern from "devtools/ya/yalibrary/sjson/lib/load.h" namespace "NSJson":
    cppclass TLoaderOptions:
        bool InternKeys
        bool InternValues
        THashSet[TString] RootKeyBlackList
        THashSet[TString] RootKeyWhiteList
        TVector[TString] ArrayItemPath
        THashSet[TString] ArrayItemKeyWhiteList
        TVector[TVector[TString]] CompactPaths
        TCompactStorage* CompactStorage
        PyObject* CompactOwner
        PyObject* (*CompactFactory)(PyObject*, TIndexType) except NULL

    object LoadFromStream(object stream, const TLoaderOptions& options)
    object LoadFromFile(const TString& path, const TLoaderOptions& options)

cdef extern from "devtools/ya/yalibrary/sjson/lib/rewrite.h" namespace "NSJson":
    object RewriteObjectFromStream(object stream, object output, object root_key, object replacements)
    object RewriteObjectFromFile(const TString& path, object output, object root_key, object replacements)


wb_list_type = typing.Optional[typing.Iterable[str]]


def _file_path(path):
    path = os.fsencode(path) if hasattr(os, 'fsencode') else six.ensure_binary(path, sys.getfilesystemencoding())
    if b'\0' in path:
        raise ValueError('embedded null byte in path')
    return path


cdef class _FileInputPath:
    cdef bytes path

    def __init__(self, path):
        self.path = _file_path(path)


def load_file(path, **options):
    """Load raw JSON or header-based UC directly from a native file stream."""
    return load(_FileInputPath(path), **options)


def rewrite_file(path, output, root_key=None, replacements=None):
    """Rewrite raw JSON or header-based UC without a Python input/pipe bridge."""
    cdef TString filename = _file_path(path)
    fields = {six.ensure_binary(key): value for key, value in (replacements or {}).items()}
    return RewriteObjectFromFile(
        filename, output, six.ensure_binary(root_key) if root_key is not None else None, fields
    )


def rewrite_object(stream, output, root_key=None, replacements=None):
    """Stream an object (optionally selected by a root key), patching its fields.

    Unchanged values are parsed and written without constructing a Python DOM.
    Replacement values are serialized directly to the output stream, without
    intermediate dumps() buffers. Output writes are bounded to 64 KiB.
    """
    fields = {six.ensure_binary(key): value for key, value in (replacements or {}).items()}
    return RewriteObjectFromStream(
        stream, output, six.ensure_binary(root_key) if root_key is not None else None, fields
    )


def load(
    stream: io.IOBase,
    intern_keys: bool = False,
    intern_vals: bool = False,
    root_key_white_list: wb_list_type = None,
    root_key_black_list: wb_list_type = None,
    array_item_path: wb_list_type = None,
    array_item_key_white_list: wb_list_type = None,
    compact_paths=None,
) -> typing.Any:
    """Load JSON, optionally storing selected container subtrees in a C++ arena.

    compact_paths consists of tuples of object keys (an empty tuple selects the
    root). CompactMap/CompactList preserve mutable mapping/sequence operations;
    strings are ordinary str and shared value blocks have independent mutations.
    """
    cdef TLoaderOptions opts
    cdef _GraphStorage storage
    cdef TVector[TString] compact_path
    opts.InternKeys = intern_keys
    opts.InternValues = intern_vals
    if root_key_white_list and root_key_black_list:
        raise ValueError("Only one of lists is allowed: root_key_white_list or root_key_black_list")
    if root_key_white_list:
        for key in root_key_white_list:
            opts.RootKeyWhiteList.insert(six.ensure_binary(key))
    if root_key_black_list:
        for key in root_key_black_list:
            opts.RootKeyBlackList.insert(six.ensure_binary(key))
    if array_item_path is not None:
        array_item_path = tuple(six.ensure_binary(key) for key in array_item_path)
        if not array_item_key_white_list:
            raise ValueError("array_item_path requires a nonempty array_item_key_white_list")
        for key in array_item_path:
            opts.ArrayItemPath.push_back(key)
        for key in array_item_key_white_list:
            opts.ArrayItemKeyWhiteList.insert(six.ensure_binary(key))
    elif array_item_key_white_list is not None:
        raise ValueError("array_item_key_white_list requires array_item_path")

    if compact_paths is not None:
        for path in compact_paths:
            path = tuple(six.ensure_binary(key) for key in path)
            if array_item_path is not None and len(path) < len(array_item_path) and array_item_path[:len(path)] == path:
                raise ValueError('A compact path cannot contain a deeper projected array')
            compact_path.clear()
            for key in path:
                compact_path.push_back(key)
            opts.CompactPaths.push_back(compact_path)
        if not opts.CompactPaths.empty():
            storage = _GraphStorage(intern_keys, intern_vals)
            opts.CompactStorage = storage.native
            opts.CompactOwner = <PyObject*>storage
            opts.CompactFactory = _compact_factory

    if isinstance(stream, _FileInputPath):
        return LoadFromFile((<_FileInputPath>stream).path, opts)
    return LoadFromStream(stream, opts)


def loads(
    s: str|bytes,
    intern_keys: bool = False,
    intern_vals: bool = False,
    root_key_white_list: wb_list_type = None,
    root_key_black_list: wb_list_type = None,
    array_item_path: wb_list_type = None,
    array_item_key_white_list: wb_list_type = None,
    compact_paths=None,
) -> typing.Any:
    if isinstance(s, str) and six.PY3:
        s = s.encode()
    stream = io.BytesIO(s)
    return load(
        stream,
        intern_keys=intern_keys,
        intern_vals=intern_vals,
        root_key_white_list=root_key_white_list,
        root_key_black_list=root_key_black_list,
        array_item_path=array_item_path,
        array_item_key_white_list=array_item_key_white_list,
        compact_paths=compact_paths,
    )
