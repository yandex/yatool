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
    TString TrimData(TStringBuf strData) {
        if (strData.length() <= 100) {
            return TString{strData};
        } else {
            return TString{strData.SubStr(0, 45)} + "..." + strData.SubStr(strData.length() - 45);
        }
    }

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

TJsonMd5Base::~TJsonMd5Base() noexcept {
}

TJsonMd5Old::TJsonMd5Old(TNodeDebugOnly nodeDebug, const TString& name, const TSymbols& symbols)
    : NodeName(name)
    , NodeId(NUidDebug::LogNodeDeclaration(ResolveCommandPrefix(name, symbols)))
    , SymbolsTable(symbols)
    , ContextMd5(nodeDebug, "TJsonMd5::ContextMd5"sv)
    , IncludesMd5(nodeDebug, "TJsonMd5::IncludesMd5"sv)
    , SelfContextMd5(nodeDebug, "TJsonMd5::SelfContextMd5"sv)
    , IncludesSelfContextMd5(nodeDebug, "TJsonMd5::IncludesSelfContextMd5"sv)
    , RenderMd5(nodeDebug, "TJsonMd5::RenderMd5"sv)
{
}

void TJsonMd5Old::ContextMd5Update(const TMd5SigValue& md5Sig, TStringBuf reasonOfUpdateName) {
    const TMd5Value oldMd5 = ContextMd5;
    ContextMd5.Update(md5Sig, "TJsonMd5::ContextMd5Update::<md5Sig>"sv);
    YDIAG(Dev)
        << "Update ContextMd5, " << NodeName << " += " << md5Sig.ToBase64() << " (was " << oldMd5.ToBase64()
        << ", become " << ContextMd5.ToBase64() << ")" << Endl;
    if (NUidDebug::Enabled()) {
        LogContextChange(ContextMd5, NUidDebug::GetNodeId(reasonOfUpdateName, SymbolsTable));
    }
}

void TJsonMd5Old::ContextMd5Update(const char* data, size_t len) {
    const TMd5Value oldMd5 = ContextMd5;
    ContextMd5.Update(data, len, "TJsonMd5::ContextMd5Update::<data,len>"sv);
    YDIAG(Dev) << "Update ContextMd5, " << NodeName << " += \"" << TrimData({data, len}) << "\" (len:" << len << ")"
                    << " (was " << oldMd5.ToBase64() << ", become " << ContextMd5.ToBase64() << ")" << Endl;
    if (NUidDebug::Enabled()) {
        LogContextChange(ContextMd5,
                     NUidDebug::LogNodeDeclaration(ResolveCommandPrefix(TString(data, len) + " (UID by name)", SymbolsTable)));
    }
}

void TJsonMd5Old::IncludesMd5Update(const TMd5SigValue& md5Sig, TStringBuf reasonOfUpdateName, TStringBuf description) {
    const TMd5Value oldMd5 = IncludesMd5;
    IncludesMd5.Update(md5Sig, description);
    YDIAG(Dev)
        << "Update IncludesMd5, " << NodeName << " += " << md5Sig.ToBase64() << " (was " << oldMd5.ToBase64()
        << ", become " << IncludesMd5.ToBase64() << ")" << Endl;
    if (NUidDebug::Enabled()) {
        LogIncludedChange(IncludesMd5, NUidDebug::GetNodeId(reasonOfUpdateName, SymbolsTable));
    }
}

void TJsonMd5Old::IncludesMd5Update(const char* data, size_t len) {
    const TMd5Value oldMd5 = IncludesMd5;
    IncludesMd5.Update(data, len, "TJsonMd5::IncludesMd5Update::<data,len>"sv);
    YDIAG(Dev) << "Update IncludesMd5, " << NodeName << " += \"" << TrimData({data, len}) << "\" (len:" << len << ")"
                    << " (was " << oldMd5.ToBase64() << ", become " << IncludesMd5.ToBase64() << ")" << Endl;
    if (NUidDebug::Enabled()) {
        LogIncludedChange(IncludesMd5,
                      NUidDebug::LogNodeDeclaration(ResolveCommandPrefix(TString(data, len) + " (UID by name)",SymbolsTable)));
    }
}

void TJsonMd5Old::SelfContextMd5Update(const TMd5SigValue& md5Sig, TStringBuf reasonOfUpdateName) {
    const TMd5Value oldMd5 = SelfContextMd5;
    SelfContextMd5.Update(md5Sig, "TJsonMd5::SelfContextMd5Update::<md5Sig>"sv);
    YDIAG(Dev)
        << "Update SelfContextMd5, " << NodeName << " += " << md5Sig.ToBase64() << " (was " << oldMd5.ToBase64()
        << ", become " << SelfContextMd5.ToBase64() << ")" << Endl;
    if (NUidDebug::Enabled()) {
        LogSelfContextChange(SelfContextMd5, NUidDebug::GetNodeId(reasonOfUpdateName, SymbolsTable));
    }
}

void TJsonMd5Old::SelfContextMd5Update(const char* data, size_t len) {
    const TMd5Value oldMd5 = SelfContextMd5;
    SelfContextMd5.Update(data, len, "TJsonMd5::SelfContextMd5Update::<data,len>"sv);
    YDIAG(Dev) << "Update SelfContextMd5, " << NodeName << " += \"" << TrimData({data, len}) << "\" (len:" << len << ")"
                    << " (was " << oldMd5.ToBase64() << ", become " << SelfContextMd5.ToBase64() << ")" << Endl;
    if (NUidDebug::Enabled()) {
        LogSelfContextChange(SelfContextMd5,
                             NUidDebug::LogNodeDeclaration(ResolveCommandPrefix(TString(data, len) + " (UID by name)", SymbolsTable)));
    }
}

void TJsonMd5Old::IncludesSelfContextMd5Update(const TMd5SigValue& md5Sig, TStringBuf reasonOfUpdateName) {
    const TMd5Value oldMd5 = IncludesSelfContextMd5;
    IncludesSelfContextMd5.Update(md5Sig, "TJsonMd5::IncludesSelfContextMd5Update::<md5Sig>"sv);
    YDIAG(Dev)
        << "Update SelfIncludesMd5, " << NodeName << " += " << md5Sig.ToBase64() << " (was " << oldMd5.ToBase64()
        << ", become " << IncludesSelfContextMd5.ToBase64() << ")" << Endl;
    if (NUidDebug::Enabled()) {
        LogSelfIncludedChange(IncludesSelfContextMd5, NUidDebug::GetNodeId(reasonOfUpdateName, SymbolsTable));
    }
}

void TJsonMd5Old::IncludesSelfContextMd5Update(const char* data, size_t len) {
    const TMd5Value oldMd5 = IncludesSelfContextMd5;
    IncludesSelfContextMd5.Update(data, len, "TJsonMd5::IncludesSelfContextMd5Update::<data,len>"sv);
    YDIAG(Dev) << "Update SelfIncludesMd5, " << NodeName << " += \"" << TrimData({data, len}) << "\" (len:" << len << ")"
                    << " (was " << oldMd5.ToBase64() << ", become " << IncludesSelfContextMd5.ToBase64() << ")" << Endl;
    if (NUidDebug::Enabled()) {
        LogSelfIncludedChange(IncludesSelfContextMd5,
                              NUidDebug::LogNodeDeclaration(ResolveCommandPrefix(TString(data, len) + " (UID by name)",SymbolsTable)));
    }
}

void TJsonMd5Old::RenderMd5Update(const TMd5SigValue& md5Sig, TStringBuf) {
    RenderMd5.Update(md5Sig, "TJsonMd5::RenderMd5Update::<md5Sig>"sv);
}

void TJsonMd5Old::RenderMd5Update(const char* data, size_t len) {
    RenderMd5.Update(data, len, "TJsonMd5::RenderMd5Update::<data,len>"sv);
}

void TJsonMd5Old::LogContextChange(const TMd5Value& ContextMd5, TUidDebugNodeId depNodeId) const {
    if(!NUidDebug::Enabled()){
        return;
    }
    NUidDebug::LogDependency(NodeId, depNodeId);
    NUidDebug::LogContextMd5Assign(NodeId, ContextMd5.ToBase64());
}

void TJsonMd5Old::LogIncludedChange(const TMd5Value& IncludesMd5, TUidDebugNodeId depNodeId) const {
    if(!NUidDebug::Enabled()){
        return;
    }
    NUidDebug::LogDependency(NodeId, depNodeId);
    NUidDebug::LogIncludedMd5Assign(NodeId, IncludesMd5.ToBase64());
}

void TJsonMd5Old::LogSelfContextChange(const TMd5Value& ContextMd5, TUidDebugNodeId depNodeId) const {
    if(!NUidDebug::Enabled()){
        return;
    }
    NUidDebug::LogSelfDependency(NodeId, depNodeId);
    NUidDebug::LogSelfContextMd5Assign(NodeId, ContextMd5.ToBase64());
}

void TJsonMd5Old::LogSelfIncludedChange(const TMd5Value& IncludesMd5, TUidDebugNodeId depNodeId) const {
    if(!NUidDebug::Enabled()){
        return;
    }
    NUidDebug::LogSelfDependency(NodeId, depNodeId);
    NUidDebug::LogSelfIncludedMd5Assign(NodeId, IncludesMd5.ToBase64());
}

TJsonMd5New::TJsonMd5New(TNodeDebugOnly nodeDebug)
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
