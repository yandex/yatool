#include "make_plan.h"

#include <devtools/ymake/vars.h>

#include <library/cpp/json/json_reader.h>
#include <library/cpp/json/json_writer.h>
#include <util/generic/overloaded.h>

using namespace NJson;

template <>
void TMakeCmd::WriteAsJson(TJsonWriter& writer) const {
    auto empty = std::visit(TOverloaded{
        [](const TString& args)          { return args.empty() || args == "[]"; },
        [](const TVector<TString>& args) { return args.empty(); }
    }, this->CmdArgs);
    if (empty)
        return;
    writer.OpenMap();
    std::visit(TOverloaded{
        [&](const TString& args) {
            Y_DEBUG_ABORT_UNLESS(ValidateJson(args), "%s", args.c_str());
            writer.UnsafeWrite("cmd_args", args);
        },
        [&](const TVector<TString>& args) {
            writer.OpenArray("cmd_args");
            for (const auto& arg : args) {
                writer.Write(arg);
            }
            writer.CloseArray();
        }
    }, this->CmdArgs);
    if (this->Cwd) {
        writer.Write("cwd", *this->Cwd);
    }
    if (this->StdOut) {
        writer.Write("stdout", *this->StdOut);
    }
    if (this->Env) {
        writer.OpenMap("env");
        for (const auto& kv : this->Env) {
            writer.Write(kv.first, kv.second);
        }
        writer.CloseMap();
    }
    writer.CloseMap();
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
void TMakeNode::WriteAsJson(TJsonWriter& writer, bool EnableNodeSelfUidPrint) const {
    writer.OpenMap();

    writer.Write("uid", this->Uid);

    if (EnableNodeSelfUidPrint) {
        Y_ASSERT(!this->SelfUid.empty());
        writer.Write("self_uid", this->SelfUid);
    }

    writer.OpenArray("cmds");
    for (const auto& x : this->Cmds) {
        x.WriteAsJson(writer);
    }
    writer.CloseArray();

    Y_DEBUG_ABORT_UNLESS(ValidateJson(this->Inputs), "%s", this->Inputs.c_str());
    writer.UnsafeWrite("inputs", this->Inputs);

    Y_DEBUG_ABORT_UNLESS(ValidateJson(this->Outputs), "%s", this->Outputs.c_str());
    writer.UnsafeWrite("outputs", this->Outputs);

    writer.OpenArray("deps");
    for (const auto& x : this->Deps) {
        writer.Write(x);
    }
    writer.CloseArray();

    if (!this->ToolDeps.empty()) {
        writer.OpenMap("foreign_deps");
        writer.OpenArray("tool");
        for (const auto& tool : this->ToolDeps) {
            writer.Write(tool);
        }
        writer.CloseArray();
        writer.CloseMap();
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

    writer.OpenMap("kv");
    for (const auto& kv : prepareMap(this->KV)) {
        writer.Write(kv.first, kv.second);
    }
    writer.CloseMap();

    writer.OpenMap("requirements");
    size_t value;
    for (const auto& kv : prepareMap(this->Requirements)) {
        if (TryFromString<size_t>(kv.second, value)) {
            writer.Write(kv.first, value);
        } else {
            writer.Write(kv.first, kv.second);
        }
    }
    writer.CloseMap();

    if (this->OldEnv) {
        writer.OpenMap("env");
        for (const auto& kv : prepareMap(this->OldEnv)) {
            writer.Write(kv.first, kv.second);
        }
        writer.CloseMap();
    }

    writer.OpenMap("target_properties");
    for (const auto& kv : prepareMap(this->TargetProps)) {
        writer.Write(kv.first, kv.second);
    }
    writer.CloseMap();

    if (!this->ResourceUris.empty()) {
        writer.OpenArray("resources");
        for (const auto& uri : this->ResourceUris) {
            writer.OpenMap();
            writer.Write("uri", uri);
            writer.CloseMap();
        }
        writer.CloseArray();
    }

    if (!this->TaredOuts.empty()) {
        writer.OpenArray("tared_outputs");
        for (const auto& out : this->TaredOuts) {
            writer.Write(out);
        }
        writer.CloseArray();
    }

    writer.CloseMap();
    writer.Flush();
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

TMakePlan::TMakePlan(NJson::TJsonWriter& writer, bool EnableNodeSelfUidPrint)
    : Writer(writer), EnableNodeSelfUidPrint(EnableNodeSelfUidPrint) {
}

TMakePlan::~TMakePlan() {
    Flush();
    Writer.CloseArray();

    Writer.OpenArray("result");
    for (const auto& x : this->Results) {
        Writer.Write(x);
    }
    Writer.CloseArray();

    Writer.OpenMap("inputs");
    for (const auto& input : Inputs) {
        Writer.OpenArray(input.first);
        Writer.Write(input.second.D0);
        Writer.Write(input.second.D1);
        Writer.CloseArray();
    }
    Writer.CloseMap();

    Writer.CloseMap();
    Writer.Flush();
}

void TMakePlan::Flush() {
    if (!ConfWritten) {
        WriteConf();
        ConfWritten = true;
    }

    for (const auto& x : this->Nodes) {
        x.WriteAsJson(Writer, EnableNodeSelfUidPrint);
    }

    Nodes.clear();
}

void TMakePlan::WriteConf() {
    Writer.OpenMap();

    Writer.OpenMap("conf");
    Writer.OpenArray("resources");
    for (const auto& kv : this->Resources) {
        Writer.UnsafeWrite(kv.second);
    }
    for (const auto& resJson : this->HostResources) {
        Writer.UnsafeWrite(resJson);
    }
    Writer.CloseArray();
    Writer.CloseMap();

    Writer.OpenArray("graph");
}
