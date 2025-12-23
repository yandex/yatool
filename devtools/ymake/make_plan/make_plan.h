#pragma once

#include <devtools/ymake/common/md5sig.h>
#include "devtools/ymake/common/json_writer.h"

#include <devtools/libs/yaplatform/platform_map.h>

#include <util/ysaveload.h>
#include <util/generic/hash.h>
#include <util/generic/maybe.h>
#include <util/generic/string.h>
#include <util/generic/vector.h>

namespace NCache {
    using TOriginal = TString;
    using TJoinedOriginal = TOriginal;
    using TJoinedCommand = std::variant<
        TOriginal,
        TVector<TOriginal>
    >;
    class TConversionContext;
}

template <typename T>
using TKeyValueMap = THashMap<T, T>;

using TMakeUid = TString;

struct TJsonWriterFuncArgs {
    NYMake::TJsonWriter& Writer;
    NYMake::TJsonWriter::TOpenedMap& Map;
    const TStringBuf& Key;
    const NCache::TConversionContext* Context{nullptr};
};

template<typename TCmd>
struct TCmdJsonWriter {
    void WriteAsJson(NYMake::TJsonWriter& writer, const NCache::TConversionContext* context) const {
        auto* cmd = static_cast<const TCmd*>(this);
        if (cmd->Empty()) {
            return;
        }
        auto map = writer.OpenMap();
        cmd->WriteCmdArgsArr({writer, map, "cmd_args", context});
        cmd->WriteCwdStr({writer, map, "cwd", context});
        cmd->WriteStdoutStr({writer, map, "stdout", context});
        cmd->WriteEnvMap({writer, map, "env", context});
        writer.CloseMap(map);
    }
};

// T - single entity (string or it cached representation (id in namestore))
// TJoined - set of T (array of strings in JSON-format or it cached representation (vector<id>))
template <typename T, typename TJoined>
struct TMakeCmdDescription : public TCmdJsonWriter<TMakeCmdDescription<T, TJoined>> {
    TJoined CmdArgs;
    TKeyValueMap<T> Env;
    TMaybe<T> Cwd;
    TMaybe<T> StdOut;

    TMakeCmdDescription() = default;
    TMakeCmdDescription(TJoined&& cmdArgs, TKeyValueMap<T>&& env, TMaybe<T>&& cwd, TMaybe<T>&& stdOut)
        : CmdArgs(std::move(cmdArgs))
        , Env(std::move(env))
        , Cwd(std::move(cwd))
        , StdOut(std::move(stdOut))
    {}

    Y_SAVELOAD_DEFINE(CmdArgs, Env, Cwd, StdOut);

    bool operator==(const TMakeCmdDescription& rhs) const;
    bool operator!=(const TMakeCmdDescription& rhs) const;

    bool Empty() const;

    void WriteCmdArgsArr(TJsonWriterFuncArgs&& funcArgs) const;
    void WriteCwdStr(TJsonWriterFuncArgs&& funcArgs) const;
    void WriteStdoutStr(TJsonWriterFuncArgs&& funcArgs) const;
    void WriteEnvMap(TJsonWriterFuncArgs&& funcArgs) const;
};
using TMakeCmd = TMakeCmdDescription<NCache::TOriginal, NCache::TJoinedCommand>;

template<typename TNode>
struct TNodeJsonWriter {
    void WriteAsJson(NYMake::TJsonWriter& writer, const NCache::TConversionContext* context = nullptr) const {
        auto* node = static_cast<const TNode*>(this);
        auto map = writer.OpenMap();
        node->WriteUidStr({writer, map, "uid", context});
        node->WriteSelfUidStr({writer, map, "self_uid", context});
        node->WriteCmdsArr({writer, map, "cmds", context});
        node->WriteInputsArr({writer, map, "inputs", context});
        node->WriteOutputsArr({writer, map, "outputs", context});
        node->WriteDepsArr({writer, map, "deps", context});
        node->WriteForeignDepsArr({writer, map, "foreign_deps", context});
        node->WriteKVMap({writer, map, "kv", context});
        node->WriteRequirementsMap({writer, map, "requirements", context});
        node->WriteEnvMap({writer, map, "env", context});
        node->WriteTargetPropertiesMap({writer, map, "target_properties", context});
        node->WriteResourcesMap({writer, map, "resources", context});
        node->WriteTaredOutputsArr({writer, map, "tared_outputs", context});
        writer.CloseMap(map);
    }
};

template <typename T, typename TJoined, typename TJoinedCommand>
struct TMakeNodeDescription : public TNodeJsonWriter<TMakeNodeDescription<T, TJoined, TJoinedCommand>> {
    T Uid;
    T SelfUid = {};
    TVector<TMakeCmdDescription<T, TJoinedCommand>> Cmds;
    TKeyValueMap<T> KV;
    TKeyValueMap<T> Requirements;
    TVector<T> Deps;
    TVector<T> ToolDeps;
    TVector<T> Inputs;
    TJoined Outputs;
    TVector<T> LateOuts; // also contains inside Outputs
    TVector<T> ResourceUris;
    TVector<T> TaredOuts;
    TKeyValueMap<T> TargetProps;
    TKeyValueMap<T> OldEnv;

    TMakeNodeDescription() = default;
    TMakeNodeDescription(
        T&& uid, T&& selfUid, TVector<TMakeCmdDescription<T, TJoinedCommand>>&& cmds,
        TKeyValueMap<T>&& kv, TKeyValueMap<T>&& requirements, TVector<T>&& deps,
        TVector<T>&& toolDeps, TVector<T>&& inputs, TJoined&& outputs, TVector<T>&& lateOuts,
        TVector<T>&& resourceUris, TVector<T>&& taredOuts, TKeyValueMap<T>&& targetProps, TKeyValueMap<T>&& oldEnv
    )
        : Uid(std::move(uid))
        , SelfUid(std::move(selfUid))
        , Cmds(std::move(cmds))
        , KV(std::move(kv))
        , Requirements(std::move(requirements))
        , Deps(std::move(deps))
        , ToolDeps(std::move(toolDeps))
        , Inputs(std::move(inputs))
        , Outputs(std::move(outputs))
        , LateOuts(std::move(lateOuts))
        , ResourceUris(std::move(resourceUris))
        , TaredOuts(std::move(taredOuts))
        , TargetProps(std::move(targetProps))
        , OldEnv(std::move(oldEnv))
    {}

    Y_SAVELOAD_DEFINE(Uid, SelfUid, Cmds, KV, Requirements, Deps, ToolDeps, Inputs, Outputs, LateOuts, ResourceUris, TaredOuts, TargetProps, OldEnv);

    bool operator==(const TMakeNodeDescription& rhs) const;
    bool operator!=(const TMakeNodeDescription& rhs) const;

    static auto PrepareMap(const TKeyValueMap<T>& map);

    void WriteUidStr(TJsonWriterFuncArgs&& funcArgs) const;
    void WriteSelfUidStr(TJsonWriterFuncArgs&& funcArgs) const;
    void WriteCmdsArr(TJsonWriterFuncArgs&& funcArgs) const;
    void WriteInputsArr(TJsonWriterFuncArgs&& funcArgs) const;
    void WriteOutputsArr(TJsonWriterFuncArgs&& funcArgs) const;
    void WriteDepsArr(TJsonWriterFuncArgs&& funcArgs) const;
    void WriteForeignDepsArr(TJsonWriterFuncArgs&& funcArgs) const;
    void WriteKVMap(TJsonWriterFuncArgs&& funcArgs) const;
    void WriteRequirementsMap(TJsonWriterFuncArgs&& funcArgs) const;
    void WriteEnvMap(TJsonWriterFuncArgs&& funcArgs) const;
    void WriteTargetPropertiesMap(TJsonWriterFuncArgs&& funcArgs) const;
    void WriteResourcesMap(TJsonWriterFuncArgs&& funcArgs) const;
    void WriteTaredOutputsArr(TJsonWriterFuncArgs&& funcArgs) const;
};

using TMakeNode = TMakeNodeDescription<NCache::TOriginal, NCache::TJoinedOriginal, NCache::TJoinedCommand>;

struct TMakePlan {
    NYMake::TJsonWriter& Writer;
    mutable NYMake::TJsonWriter::TOpenedMap Map;
    mutable NYMake::TJsonWriter::TOpenedArray NodesArr;
    THashMap<TString, TMd5Sig> Inputs;
    TVector<TMakeUid> Results;
    THashMap<TString, TString> Resources;
    TVector<TString> HostResources;

public:
    explicit TMakePlan(NYMake::TJsonWriter& writer);
    ~TMakePlan();

    void WriteConf();
};
