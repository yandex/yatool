#include "make_plan.h"

#include <devtools/ymake/vars.h>
#include <devtools/ymake/common/json_writer.h>

#include <library/cpp/json/json_reader.h>
#include <util/generic/overloaded.h>

using namespace NJson;

template <>
bool TMakeCmd::Empty() const {
    return std::visit(TOverloaded{
        [](const TString& args)          { return args.empty() || args == "[]"; },
        [](const TVector<TString>& args) { return args.empty(); }
    }, this->CmdArgs);
}

template <>
void TMakeCmd::WriteAsJson(NYMake::TJsonWriter& writer) const {
    if (Empty()) {
        return;
    }
    auto map = writer.OpenMap();
    std::visit(TOverloaded{
        [&](const TString& args) {
            Y_DEBUG_ABORT_UNLESS(ValidateJson(args), "%s", args.c_str());
            writer.WriteMapKeyJsonValue(map, "cmd_args", args);
        },
        [&](const TVector<TString>& args) {
            writer.WriteMapKeyValue(map, "cmd_args", args);
        }
    }, this->CmdArgs);
    if (this->Cwd) {
        writer.WriteMapKeyValue(map, "cwd", *this->Cwd);
    }
    if (this->StdOut) {
        writer.WriteMapKeyValue(map, "stdout", *this->StdOut);
    }
    if (this->Env) {
        writer.WriteMapKeyValue(map, "env", this->Env);
    }
    writer.CloseMap(map);
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
void TMakeNode::WriteAsJson(NYMake::TJsonWriter& writer) const {
    auto map = writer.OpenMap();

    writer.WriteMapKeyValue(map, "uid", this->Uid);

    Y_ASSERT(!this->SelfUid.empty());
    writer.WriteMapKeyValue(map, "self_uid", this->SelfUid);

    {
        writer.WriteMapKey(map, "cmds");
        auto cmdArr = writer.OpenArray();
        for (const auto& cmd: this->Cmds) {
            if (cmd.Empty()) {
                continue;
            }
            writer.WriteArrayValue(cmdArr, cmd);
        }
        writer.CloseArray(cmdArr);
    }

    writer.WriteMapKeyValue(map, "inputs", this->Inputs);

    Y_DEBUG_ABORT_UNLESS(ValidateJson(this->Outputs), "%s", this->Outputs.c_str());
    writer.WriteMapKeyJsonValue(map, "outputs", this->Outputs);

    writer.WriteMapKeyValue(map, "deps", this->Deps);

    if (!this->ToolDeps.empty()) {
        writer.WriteMapKey(map, "foreign_deps");
        auto depsMap = writer.OpenMap();
        writer.WriteMapKeyValue(depsMap, "tool", this->ToolDeps);
        writer.CloseMap(depsMap);
    }

#if !defined (NEW_UID_COMPARE)
    auto prepareMap = [](const THashMap<TString, TString>& map) -> const THashMap<TString, TString>& {
        return map;
    };
#else
    auto prepareMap = [](const THashMap<TString, TString>& map) -> TVector<std::pair<TString, TString>> {
        TVector<std::pair<TString, TString>> sorted{map.begin(), map.end()};
        Sort(sorted);
        return sorted;
    };
#endif

    writer.WriteMapKeyValue(map, "kv", prepareMap(this->KV));

    {
        writer.WriteMapKey(map, "requirements");
        auto reqMap = writer.OpenMap();
        const auto& mapRequirements = prepareMap(this->Requirements);
        size_t value;
        for (const auto& kv : mapRequirements) {
            if (TryFromString<size_t>(kv.second, value)) {
                writer.WriteMapKeyValue(reqMap, kv.first, value);
            } else {
                writer.WriteMapKeyValue(reqMap, kv.first, kv.second);
            }
        }
        writer.CloseMap(reqMap);
    }

    if (this->OldEnv) {
        writer.WriteMapKeyValue(map, "env", prepareMap(this->OldEnv));
    }

    writer.WriteMapKeyValue(map, "target_properties", prepareMap(this->TargetProps));

    if (!this->ResourceUris.empty()) {
        writer.WriteMapKey(map, "resources");
        auto resArr = writer.OpenArray();
        for (const auto& uri : this->ResourceUris) {
            auto uriMap = writer.OpenMap(resArr);
            writer.WriteMapKeyValue(uriMap, "uri", uri);
            writer.CloseMap(uriMap);
        }
        writer.CloseArray(resArr);
    }

    if (!this->TaredOuts.empty()) {
        writer.WriteMapKeyValue(map, "tared_outputs", this->TaredOuts);
    }

    writer.CloseMap(map);
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

TMakePlan::TMakePlan(NYMake::TJsonWriter& writer)
    : Writer(writer) {
}

TMakePlan::~TMakePlan() {
    Flush();
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

void TMakePlan::Flush() {
    if (!ConfWritten) {
        WriteConf();
        ConfWritten = true;
    } else {
    }

    for (const auto& node : this->Nodes) {
        Writer.WriteArrayValue(NodesArr, node);
    }

    Nodes.clear();
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
