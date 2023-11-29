#pragma once

#include <Python.h>

#include <devtools/ya/cpp/graph/graph.h>
#include <devtools/ya/cpp/lib/edl/json/from_json.h>
#include <devtools/ya/cpp/lib/edl/python/from_python.h>
#include <devtools/ya/cpp/lib/edl/python/to_python.h>

#include <library/cpp/ucompress/reader.h>
#include <library/cpp/blockcodecs/codecs.h>

#include <util/stream/mem.h>


namespace NYa::NCCGraph {
    using NYa::NGraph::TGraphPtr;
    using NYa::NEdl::FromPyObject;
    using NYa::NEdl::ToPyObject;

    inline void InitFromJson(TGraphPtr graph, TStringBuf s, bool compressed) {
        if (compressed) {
            TMemoryInput input{s};
            NUCompress::TDecodedInput inputStream{&input};
            NYa::NEdl::LoadJson(inputStream, *graph);
        } else {
            NYa::NEdl::LoadJson(s, *graph);
        }
    }

    inline PyObject* GetGraphAsPyObject(const TGraphPtr graph) {
        return NYa::NEdl::ToPyObject(*graph);
    }

    inline void AddGlobalResources(TGraphPtr graph, const TVector<NYa::NGraph::TGlobalResource>& resources) {
        graph->AddGlobalResources(resources);
    }

    inline TVector<NYa::NGraph::TGlobalResource> GetGlobalResources(TGraphPtr graph) {
        return graph->Conf.Resources;
    }

    inline void SetTags(TGraphPtr graph, const TVector<NYa::NGraph::TGraphString>& tags) {
        graph->SetTags(tags);
    }

    inline void SetPlatform(TGraphPtr graph, NYa::NGraph::TGraphString platform) {
        graph->SetPlatform(platform);
    }

    inline void AddHostMark(TGraphPtr graph, bool sandboxing) {
        graph->AddHostMark(sandboxing);
    }

    void AddToolDeps(TGraphPtr graph) {
        graph->AddToolDeps();
    }

    void AddSandboxingMark(TGraphPtr graph) {
        graph->AddSandboxingMark();
    }

    inline void SetResult(TGraphPtr graph, const TVector<NYa::NGraph::TUid>& result) {
        graph->Result = result;
    }

    inline TVector<NYa::NGraph::TUid> GetResult(TGraphPtr graph) {
        return graph->Result;
    }

    inline void UpdateConf(TGraphPtr graph, const NYa::NGraph::TConf& conf) {
        graph->UpdateConf(conf);
    }

    inline size_t GraphSize(TGraphPtr graph) {
        return graph->Graph.size();
    }

    inline void StripGraph(TGraphPtr graph) {
        graph->Strip();
    }

    inline NYa::NGraph::TGraphMergingResult MergeGraphs(TGraphPtr tools, TGraphPtr pic, TGraphPtr noPic) {
        return NYa::NGraph::MergeGraphs(tools, pic, noPic);
    }
}
