from libcpp cimport bool
from libcpp.utility cimport move as std_move

from devtools.ya.build.ccgraph.cpp_string_wrapper cimport CppStringWrapper
from util.generic.string cimport TStringBuf, TString
from util.generic.vector cimport TVector

import itertools


cdef extern from "devtools/ya/cpp/graph/graph.h" namespace "NYa::NGraph":
    cdef cppclass TGraphPtr:
        pass

    cdef cppclass TGlobalResource:
        pass

    cdef cppclass TGraphString:
        pass

    cdef cppclass TUid:
        pass

    cdef cppclass TConf:
        pass

    cdef cppclass TGraphMergingResult:
        TGraphPtr Merged
        TVector[TString] UnmatchedTargetToolDirs

    TGraphPtr CreateGraph()
    void CleanStorage()


cdef extern from "devtools/ya/cpp/lib/edl/python/to_python.h" namespace "NYa::NEdl":
    object ToPyObject[T](const T& val) except+


cdef extern from "devtools/ya/cpp/lib/edl/python/from_python.h" namespace "NYa::NEdl":
    void FromPyObject[T](object obj, T& val) except+


cdef extern from "devtools/ya/build/ccgraph/ccgraph.h" namespace "NYa::NCCGraph" nogil:
    void InitFromJson(TGraphPtr graph, TStringBuf s, bool compressed) except+
    void AddGlobalResources(TGraphPtr graph, const TVector[TGlobalResource]& resources) except+
    TVector[TGlobalResource] GetGlobalResources(TGraphPtr graph) except+
    void SetTags(TGraphPtr graph, const TVector[TGraphString]& tags) except+
    void SetPlatform(TGraphPtr graph, TGraphString platform) except+
    void AddHostMark(TGraphPtr graph, bool sandboxing) except+
    void AddToolDeps(TGraphPtr graph) except+
    void AddSandboxingMark(TGraphPtr graph) except+
    void SetResult(TGraphPtr graph, const TVector[TUid]& result) except+
    TVector[TUid] GetResult(TGraphPtr graph) except+
    void UpdateConf(TGraphPtr graph, TConf conf) except+
    size_t GraphSize(TGraphPtr graph) except+
    void StripGraph(TGraphPtr graph) except+

    TGraphMergingResult MergeGraphs(TGraphPtr tools, TGraphPtr pic, TGraphPtr noPic)


cdef class Graph:
    cdef TGraphPtr graph

    def __cinit__(self, init_graph=True, *args, **kwargs):
        if init_graph:
            self.graph = CreateGraph()

    def __init__(self, CppStringWrapper ymake_output=None, init_graph=True):
        cdef TStringBuf s
        cdef bool compressed
        if ymake_output:
            s = ymake_output.output
            compressed = ymake_output.compressed
            with nogil:
                InitFromJson(self.graph, s, compressed)

    def get(self):
        return ToPyObject(self.graph)

    def add_resource(self, resource):
        self.add_resources([resource])

    def add_resources(self, resources):
        cdef TVector[TGlobalResource] ccresources
        FromPyObject(resources, ccresources)
        AddGlobalResources(self.graph, ccresources)

    def get_resources(self):
        cdef TVector[TGlobalResource] ccresources
        ccresources = GetGlobalResources(self.graph)
        return ToPyObject(ccresources)

    def set_tags(self, tags):
        cdef TVector[TGraphString] cctags
        FromPyObject(tags, cctags)
        SetTags(self.graph, cctags)

    def set_platform(self, platform):
        cdef TGraphString ccplatform
        FromPyObject(platform, ccplatform)
        SetPlatform(self.graph, ccplatform)

    def add_host_mark(self, sandboxing):
        AddHostMark(self.graph, sandboxing)

    def add_tool_deps(self):
        AddToolDeps(self.graph)

    def add_sandboxing_mark(self):
        AddSandboxingMark(self.graph)

    def set_result(self, result):
        cdef TVector[TUid] ccresult
        FromPyObject(result, ccresult)
        SetResult(self.graph, ccresult)

    def get_result(self):
        cdef TVector[TUid] ccresult
        ccresult = GetResult(self.graph)
        return ToPyObject(ccresult)

    def update_conf(self, conf):
        cdef TConf ccconf
        FromPyObject(conf, ccconf)
        UpdateConf(self.graph, ccconf)

    def size(self):
        return GraphSize(self.graph)

    def strip(self):
        with nogil:
            StripGraph(self.graph)


def get_empty_graph():
    return Graph()


def _report_unmatched_target_tool_dirs(unmatched_target_tool_dirs, conf_error_reporter):
    limit = 10
    if len(unmatched_target_tool_dirs) > limit:
        conf_error_reporter(msg="Many tools are not found in host graph. Only first {} will be reported".format(limit), path="", sub="ToolConf")
    for tool_dir in itertools.islice(unmatched_target_tool_dirs, limit):
        conf_error_reporter(msg="Tool is not found in a host graph", path=tool_dir, sub="ToolConf")


def merge_graphs(Graph tools, Graph pic, Graph no_pic, object conf_error_reporter) -> Graph:
    cdef TGraphMergingResult merging_result
    cdef TGraphPtr pic_graph
    cdef TGraphPtr no_pic_graph

    if pic:
        pic_graph = pic.graph
    if no_pic:
        no_pic_graph = no_pic.graph

    with nogil:
        merging_result = MergeGraphs(tools.graph, pic_graph, no_pic_graph)

    if merging_result.UnmatchedTargetToolDirs.size():
        dirs = ToPyObject(merging_result.UnmatchedTargetToolDirs)
        _report_unmatched_target_tool_dirs(dirs, conf_error_reporter)

    merged = Graph(init_graph=False)
    merged.graph = merging_result.Merged

    return merged


def clean_intern_string_storage():
    CleanStorage()
