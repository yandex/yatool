#include "make_plan.h"

#include <devtools/ymake/vars.h>
#include <devtools/ymake/common/json_writer.h>

#include <library/cpp/json/json_reader.h>
#include <util/generic/overloaded.h>

template <>
bool TMakeCmd::Empty() const {
    return std::visit(TOverloaded{
        [](const TString& args)          { return args.empty() || args == "[]"; },
        [](const TVector<TString>& args) { return args.empty(); }
    }, CmdArgs);
}

template <>
void TMakeCmd::WriteCmdArgsArr(TJsonWriterFuncArgs&& funcArgs) const {
    std::visit(TOverloaded{
        [&](const TString& args) {
            Y_DEBUG_ABORT_UNLESS(NJson::ValidateJson(args), "%s", args.c_str());
            funcArgs.Writer.WriteMapKeyJsonValue(funcArgs.Map, funcArgs.Key, args);
        },
        [&](const TVector<TString>& args) {
            funcArgs.Writer.WriteMapKeyValue(funcArgs.Map, funcArgs.Key, args);
        }
    }, CmdArgs);
}

template <>
void TMakeCmd::WriteCwdStr(TJsonWriterFuncArgs&& funcArgs) const {
    if (Cwd) {
        funcArgs.Writer.WriteMapKeyValue(funcArgs.Map, funcArgs.Key, *Cwd);
    }
}

template <>
void TMakeCmd::WriteStdoutStr(TJsonWriterFuncArgs&& funcArgs) const {
    if (StdOut) {
        funcArgs.Writer.WriteMapKeyValue(funcArgs.Map, funcArgs.Key, *StdOut);
    }
}

template <>
void TMakeCmd::WriteEnvMap(TJsonWriterFuncArgs&& funcArgs) const {
    if (!Env.empty()) {
        funcArgs.Writer.WriteMapKeyValue(funcArgs.Map, funcArgs.Key, Env);
    }
}

template <>
bool TMakeCmd::operator==(const TMakeCmd& rhs) const {
    return CmdArgs == rhs.CmdArgs && Env == rhs.Env && Cwd == rhs.Cwd && StdOut == rhs.StdOut;
}

template <>
bool TMakeCmd::operator!=(const TMakeCmd& rhs) const {
    return !(rhs == *this);
}

template <>
bool TMakeNode::operator==(const TMakeNode& rhs) const {
    return Uid == rhs.Uid
        && SelfUid == rhs.SelfUid
        && Cmds == rhs.Cmds
        && KV == rhs.KV
        && Requirements == rhs.Requirements
        && Deps == rhs.Deps
        && ToolDeps == rhs.ToolDeps
        && Inputs == rhs.Inputs
        && Outputs == rhs.Outputs
        && TaredOuts == rhs.TaredOuts
        && TargetProps == rhs.TargetProps
        && OldEnv == rhs.OldEnv
        && ResourceUris == rhs.ResourceUris;
}

template <>
bool TMakeNode::operator!=(const TMakeNode& rhs) const {
    return !(rhs == *this);
}

template <>
void TMakeNode::WriteUidStr(TJsonWriterFuncArgs&& funcArgs) const {
    funcArgs.Writer.WriteMapKeyValue(funcArgs.Map, funcArgs.Key, Uid);
}

template <>
void TMakeNode::WriteSelfUidStr(TJsonWriterFuncArgs&& funcArgs) const {
    Y_ASSERT(!SelfUid.empty());
    funcArgs.Writer.WriteMapKeyValue(funcArgs.Map, funcArgs.Key, SelfUid);
}

template <>
void TMakeNode::WriteCmdsArr(TJsonWriterFuncArgs&& funcArgs) const {
    auto& writer = funcArgs.Writer;
    writer.WriteMapKey(funcArgs.Map, funcArgs.Key);
    auto cmdArr = writer.OpenArray();
    for (const auto& cmd: Cmds) {
        if (cmd.Empty()) {
            continue;
        }
        writer.WriteArrayValue(cmdArr, cmd, nullptr);
    }
    writer.CloseArray(cmdArr);
}

template <>
void TMakeNode::WriteInputsArr(TJsonWriterFuncArgs&& funcArgs) const {
    funcArgs.Writer.WriteMapKeyValue(funcArgs.Map, funcArgs.Key, Inputs);
}

template <>
void TMakeNode::WriteOutputsArr(TJsonWriterFuncArgs&& funcArgs) const {
    Y_DEBUG_ABORT_UNLESS(NJson::ValidateJson(Outputs), "%s", Outputs.c_str());
    funcArgs.Writer.WriteMapKeyJsonValue(funcArgs.Map, funcArgs.Key, Outputs);
}

template <>
void TMakeNode::WriteDepsArr(TJsonWriterFuncArgs&& funcArgs) const {
    funcArgs.Writer.WriteMapKeyValue(funcArgs.Map, funcArgs.Key, Deps);
}

template <>
void TMakeNode::WriteForeignDepsArr(TJsonWriterFuncArgs&& funcArgs) const {
    if (!ToolDeps.empty()) {
        auto& writer = funcArgs.Writer;
        writer.WriteMapKey(funcArgs.Map, funcArgs.Key);
        auto depsMap = writer.OpenMap();
        writer.WriteMapKeyValue(depsMap, "tool", ToolDeps);
        writer.CloseMap(depsMap);
    }
}

template <>
void TMakeNode::WriteKVMap(TJsonWriterFuncArgs&& funcArgs) const {
    funcArgs.Writer.WriteMapKeyValue(funcArgs.Map, funcArgs.Key, KV);
}

template <>
void TMakeNode::WriteRequirementsMap(TJsonWriterFuncArgs&& funcArgs) const {
    auto& writer = funcArgs.Writer;
    writer.WriteMapKey(funcArgs.Map, funcArgs.Key);
    auto reqMap = writer.OpenMap();
    size_t value;
    for (const auto& kv : Requirements) {
        if (TryFromString<size_t>(kv.second, value)) {
            writer.WriteMapKeyValue(reqMap, kv.first, value);
        } else {
            writer.WriteMapKeyValue(reqMap, kv.first, kv.second);
        }
    }
    writer.CloseMap(reqMap);
}

template <>
void TMakeNode::WriteEnvMap(TJsonWriterFuncArgs&& funcArgs) const {
    if (!OldEnv.empty()) {
        funcArgs.Writer.WriteMapKeyValue(funcArgs.Map, funcArgs.Key, OldEnv);
    }
}

template <>
void TMakeNode::WriteTargetPropertiesMap(TJsonWriterFuncArgs&& funcArgs) const {
    funcArgs.Writer.WriteMapKeyValue(funcArgs.Map, funcArgs.Key, TargetProps);
}

template <>
void TMakeNode::WriteResourcesMap(TJsonWriterFuncArgs&& funcArgs) const {
    if (!ResourceUris.empty()) {
        auto& writer = funcArgs.Writer;
        writer.WriteMapKey(funcArgs.Map, funcArgs.Key);
        auto resArr = writer.OpenArray();
        for (const auto& uri : ResourceUris) {
            auto uriMap = writer.OpenMap(resArr);
            writer.WriteMapKeyValue(uriMap, "uri", uri);
            writer.CloseMap(uriMap);
        }
        writer.CloseArray(resArr);
    }
}

template <>
void TMakeNode::WriteTaredOutputsArr(TJsonWriterFuncArgs&& funcArgs) const {
    if (!TaredOuts.empty()) {
        funcArgs.Writer.WriteMapKeyValue(funcArgs.Map, funcArgs.Key, TaredOuts);
    }
}

TMakePlan::TMakePlan(NYMake::TJsonWriter& writer)
    : Writer(writer) {
}

TMakePlan::~TMakePlan() {
    Writer.CloseArray(NodesArr);

    Writer.WriteMapKeyValue(Map, "result", this->Results);

    {
        Writer.WriteMapKey(Map, "inputs");
        auto inputsMap = Writer.OpenMap();
        for (const auto& input : Inputs) {
            Writer.WriteMapKey(inputsMap, input.first);
            auto inputArr = Writer.OpenArray();
            Writer.WriteArrayValue(inputArr, input.second.D0);
            Writer.WriteArrayValue(inputArr, input.second.D1);
            Writer.CloseArray(inputArr);
        }
        Writer.CloseMap(inputsMap);
    }

    Writer.CloseMap(Map);
    Writer.Flush();
}

void TMakePlan::WriteConf() {
    Map = Writer.OpenMap();

    Writer.WriteMapKey(Map, "conf");
    auto confMap = Writer.OpenMap();
    Writer.WriteMapKey(confMap, "resources");
    auto resArr = Writer.OpenArray();
    for (const auto& kv : this->Resources) {
        Writer.WriteArrayJsonValue(resArr, kv.second);
    }
    for (const auto& resJson : this->HostResources) {
        Writer.WriteArrayJsonValue(resArr, resJson);
    }
    Writer.CloseArray(resArr);
    Writer.CloseMap(confMap);

    Writer.WriteMapKey(Map, "graph");
    NodesArr = Writer.OpenArray();
}
