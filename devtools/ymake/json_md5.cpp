#include "json_md5.h"

#include <devtools/ymake/macro_string.h>
#include <devtools/ymake/diag/dbg.h>
#include <devtools/ymake/symbols/symbols.h>

#include <util/generic/algorithm.h>
#include <util/generic/hash.h>
#include <util/string/cast.h>
#include <util/string/subst.h>
#include <util/string/builder.h>

#define UidDebuggerLog (Y_UNLIKELY(Diag()->TextLog && Diag()->UIDs)) && TEatStream() | Cerr << "UIDs: "

namespace {
    TString FixNodeName(TStringBuf name) {
        TString fixedName(name);
        SubstGlobal(fixedName, '\n', ' ');
        return fixedName;
    }

    TString ResolveCommandPrefix(TStringBuf name, const TSymbols& names) {
        if (name.empty() || !IsAsciiDigit(name.front()) || name.StartsWith("0:")) {
            return TString{name};
        }

        auto id = GetId(name);
        name = SkipId(name);
        TFileView moduleName = names.FileConf.GetName(static_cast<ui32>(id));
        return TStringBuilder() << moduleName << ":" << name;
    }
}

bool NUidDebug::Enabled() {
    return Y_UNLIKELY(Diag()->UIDs);
}

TUidDebugNodeId NUidDebug::GetNodeId(TStringBuf name, const TSymbols& names) {
    if(!Enabled()){
        return TUidDebugNodeId::Invalid;
    }
    return static_cast<TUidDebugNodeId>(THash<TStringBuf>()(FixNodeName(ResolveCommandPrefix(name, names))));
}

TUidDebugNodeId NUidDebug::LogNodeDeclaration(TStringBuf name) {
    if(!Enabled()){
        return TUidDebugNodeId::Invalid;
    }
    auto newNodeName = FixNodeName(ToString(name));
    auto newNodeHash = static_cast<TUidDebugNodeId>(THash<TStringBuf>()(newNodeName));
    LogNodeDeclaration(newNodeName, newNodeHash);
    return newNodeHash;
}

void NUidDebug::LogNodeDeclaration(TStringBuf name, TUidDebugNodeId id) {
    if (!Enabled()) {
        return;
    }
    auto newNodeName = FixNodeName(ToString(name));
    UidDebuggerLog << "NODE %%%" << newNodeName << "%%% = %%%" << id << "%%%" << Endl;
}

void NUidDebug::LogDependency(TUidDebugNodeId from, TUidDebugNodeId to) {
    UidDebuggerLog << "DEP %%%" << from << "%%% -> %%%" << to << "%%%" << Endl;
}

void NUidDebug::LogSelfDependency(TUidDebugNodeId from, TUidDebugNodeId to) {
    UidDebuggerLog << "SELF_DEP %%%" << from << "%%% -> %%%" << to << "%%%" << Endl;
}

void NUidDebug::LogContextMd5Assign(TUidDebugNodeId nodeId, const TString& value) {
    UidDebuggerLog << "VAL %%%" << nodeId << "%%% = %%%" << value << "%%%" << Endl;
}

void NUidDebug::LogIncludedMd5Assign(TUidDebugNodeId nodeId, const TString& value) {
    UidDebuggerLog << "INC %%%" << nodeId << "%%% = %%%" << value << "%%%" << Endl;
}

void NUidDebug::LogSelfContextMd5Assign(TUidDebugNodeId nodeId, const TString& value) {
    UidDebuggerLog << "SELF_VAL %%%" << nodeId << "%%% = %%%" << value << "%%%" << Endl;
}

void NUidDebug::LogSelfIncludedMd5Assign(TUidDebugNodeId nodeId, const TString& value) {
    UidDebuggerLog << "SELF_INC %%%" << nodeId << "%%% = %%%" << value << "%%%" << Endl;
}

TString NUidDebug::LoopNodeName(TNodeId loopId) {
    return "LOOP" + ToString(loopId);
}

TJsonMd5::TJsonMd5(TNodeDebugOnly nodeDebug)
    : StructureMd5(nodeDebug, "TJsonMd5::StructureMd5"sv)
    , IncludeStructureMd5(nodeDebug, "TJsonMd5::IncludeStructureMd5"sv)
    , ContentMd5(nodeDebug, "TJsonMd5::ContentMd5"sv)
    , IncludeContentMd5(nodeDebug, "TJsonMd5::IncludeContentMd5"sv)
{
}

TJsonMultiMd5::TJsonMultiMd5(TNodeId loopId, const TSymbols& symbols, size_t expectedSize)
    : NodeId(NUidDebug::GetNodeId(NUidDebug::LoopNodeName(loopId), symbols))
    , SymbolsTable(symbols)
{
    if (expectedSize) {
        Signs.reserve(expectedSize);
    }
}

void TJsonMultiMd5::AddSign(const TMd5SigValue& sign, TStringBuf depName, bool isLoopPart) {
    Signs.push_back({isLoopPart, sign, TString{depName}});
}

void TJsonMultiMd5::CalcFinalSign(TMd5SigValue& res) {
    Sort(Signs);

    TStringBuilder nodeName;
    nodeName << "LOOP:";

    for (const auto& signData : Signs) {
        if (signData.IsLoopPart && !signData.Name.StartsWith("LOOP")) {
            nodeName << " " << FixNodeName(ResolveCommandPrefix(signData.Name, SymbolsTable));
        }
    }
    NUidDebug::LogNodeDeclaration(nodeName, NodeId);

    TMd5Value md5{"TJsonMultiMd5::CalcFinalSign::<md5>"sv};
    for (const auto& signData : Signs) {
        auto depId = NUidDebug::GetNodeId(signData.Name, SymbolsTable);
        NUidDebug::LogDependency(NodeId, depId);
        md5.Update(signData.Md5, "TJsonMultiMd5::CalcFinalSign::<signData.Md5>"sv);
    }
    res.MoveFrom(std::move(md5));
    NUidDebug::LogContextMd5Assign(NodeId, res.ToBase64());
}
