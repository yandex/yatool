#pragma once

#include "resource_section_params.h"

#include <devtools/ymake/common/md5sig.h>

#include <devtools/libs/yaplatform/platform_map.h>

#include <util/ysaveload.h>
#include <util/generic/hash.h>
#include <util/generic/maybe.h>
#include <util/generic/string.h>
#include <util/generic/vector.h>

namespace NJson {
    class TJsonWriter;
}

namespace NCache {
    using TOriginal = TString;
    using TJoinedOriginal = TOriginal;
    using TJoinedCommand = std::variant<
        TOriginal,
        TVector<TOriginal>
    >;
}

template <typename T>
using TKeyValueMap = THashMap<T, T>;

using TMakeUid = TString;

// T - single entity (string or it cached representation (id in namestore))
// TJoined - set of T (array of strings in JSON-format or it cached representation (vector<id>))
template <typename T, typename TJoined>
struct TMakeCmdDescription {
    TJoined CmdArgs;
    TKeyValueMap<T> Env;
    TMaybe<T> Cwd;
    TMaybe<T> StdOut;

    Y_SAVELOAD_DEFINE(CmdArgs, Env, Cwd, StdOut);

    bool operator==(const TMakeCmdDescription& rhs) const;
    bool operator!=(const TMakeCmdDescription& rhs) const;

    void WriteAsJson(NJson::TJsonWriter&) const;
};

template <typename T, typename TJoined, typename TJoinedCommand>
struct TMakeNodeDescription {
    T Uid;
    T SelfUid = {};
    TVector<TMakeCmdDescription<T, TJoinedCommand>> Cmds;
    TKeyValueMap<T> KV;
    TKeyValueMap<T> Requirements;
    TVector<T> Deps;
    TVector<T> ToolDeps;
    TJoined Inputs;
    TJoined Outputs;
    TVector<T> LateOuts; // also contains inside Outputs
    TVector<T> ResourceUris;
    TVector<T> TaredOuts;
    TKeyValueMap<T> TargetProps;
    TKeyValueMap<T> OldEnv;

    Y_SAVELOAD_DEFINE(Uid, SelfUid, Cmds, KV, Requirements, Deps, ToolDeps, Inputs, Outputs, LateOuts, ResourceUris, TaredOuts, TargetProps, OldEnv);

    bool operator==(const TMakeNodeDescription& rhs) const;
    bool operator!=(const TMakeNodeDescription& rhs) const;

    void WriteAsJson(NJson::TJsonWriter&) const;
};

using TMakeCmd = TMakeCmdDescription<NCache::TOriginal, NCache::TJoinedCommand>;
using TMakeNode = TMakeNodeDescription<NCache::TOriginal, NCache::TJoinedOriginal, NCache::TJoinedCommand>;

struct TMakePlan {
    NJson::TJsonWriter& Writer;
    THashMap<TString, TMd5Sig> Inputs;
    TVector<TMakeNode> Nodes;
    TVector<TMakeUid> Results;
    THashMap<TString, TString> Resources;
    TVector<TString> HostResources;

private:
    bool ConfWritten = false;

public:
    explicit TMakePlan(NJson::TJsonWriter& writer);
    ~TMakePlan();

    void Flush();

private:
    void WriteConf();
};
