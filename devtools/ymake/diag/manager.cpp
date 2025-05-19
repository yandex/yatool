#include "manager.h"
#include "trace.h"

#include <devtools/ymake/context_executor.h>

#include <library/cpp/on_disk/multi_blob/multiblob_builder.h>
#include <library/cpp/iterator/mapped.h>

#include <util/generic/iterator_range.h>
#include <util/generic/string.h>
#include <util/string/join.h>

using TStreamMessage = TConfMsgManager::TStreamMessage;

TStringStream& TConfMsgManager::SaveConfigureMessage(EConfMsgType type, TStringBuf kind, size_t row, size_t column, bool isPersistent) {
    ui32 owner = Diag()->Where.back().first;
    Messages[owner].emplace_back(type, kind, TStringStream(), row, column, isPersistent);
    return Messages[owner].back().Message;
}

void TConfMsgManager::ReportConfigureEvent(ETraceEvent what, const TString& event) {
    if (Delayed) {
        SaveEvent(what, event);
    } else if (NYMake::TraceEnabled(what)) {
        NYMake::Trace(event);
    }
}

TStreamMessage TConfMsgManager::ReportConfigureMessage(EConfMsgType type, TStringBuf var, size_t row, size_t column) {
    if (Delayed) {
        return TStreamMessage(&SaveConfigureMessage(type, var, row, column, Diag()->Persistency), [](IOutputStream*) {});
    } else {
        if (type == EConfMsgType::Error) {
            Diag()->HasConfigurationErrors = true;
        }
        return TStreamMessage(Display()->NewConfMsg(type, var, Diag()->Where.back().second, row, column).Release());
    }
}

void TConfMsgManager::AddDupSrcLink(ui32 id, ui32 modid, bool force) {
    Y_ASSERT(modid != 0);
    if (force) {
        DupSrcMap[id].insert(modid);
    } else if (auto iter = DupSrcMap.find(id); iter != DupSrcMap.end()) {
        iter->second.insert(modid);
    }
}

void TConfMsgManager::AddVisitedModule(ui32 modId) {
    VisitedModules.insert(modId);
}

void TConfMsgManager::ReportDupSrcConfigureErrors(std::function<TStringBuf (ui32)> toString) {
    TVector<ui32> ids;
    for (const auto& [fid, modids] : DupSrcMap) {
        ids.clear();
        CopyIf(begin(modids), end(modids), std::back_inserter(ids), [this](auto id) { return VisitedModules.contains(id); });
        if (ids.size() > 1) {
            TString message;
            {
                auto&& range = MakeMappedRange(ids, [toString](auto id) { return TString::Join("[[imp]]", toString(id), "[[rst]]"); });
                TVector<TString> moduleNames(begin(range), end(range));
                Sort(moduleNames);
                TStringOutput output(message);
                output << "output [[alt1]]" << toString(fid) << "[[rst]] was added to the graph in more than one module: "
                    << MakeRangeJoiner(", "sv, MakeIteratorRange(begin(moduleNames), end(moduleNames)));
                output.Finish();
            }
            *Display()->NewConfMsg(EConfMsgType::Error, "-WDupSrc", TDiagCtrl::TWhere::TOP_LEVEL, 0, 0) << message;
            Diag()->HasConfigurationErrors = true;
        }
    }
}

void TConfMsgManager::SaveEvent(ETraceEvent what, const TString& event) {
    Events[Diag()->Where.back().first].emplace_back(what, event, Diag()->Persistency);
}

void TConfMsgManager::Flush() const {
    for (const auto& msg : Messages) {
        FlushConfigureMessages(msg.first);
    }
    for (const auto& event : Events) {
        FlushEvents(event.first);
    }
}

void TConfMsgManager::Flush(ui32 owner) {
    FlushConfigureMessages(owner);
    FlushEvents(owner);
}

void TConfMsgManager::FlushTopLevel() const {
    FlushConfigureMessages(0);
    FlushEvents(0);
}

void TConfMsgManager::FlushConfigureMessages(ui32 owner) const {
    if (Messages.contains(owner)) {
        for (const auto& msg : Messages.at(owner)) {
            if (msg.Type == EConfMsgType::Error) {
                Diag()->HasConfigurationErrors = true;
            }
            *Display()->NewConfMsg(msg.Type, msg.Kind, Diag()->Where.back().second, msg.Row, msg.Column) << msg.Message.Str();
        }
    }
}

void TConfMsgManager::FlushEvents(ui32 owner) const {
    if (Events.contains(owner)) {
        for (const auto& event : Events.at(owner)) {
            if (NYMake::TraceEnabled(event.Type)) {
                NYMake::Trace(event.Event);
            }
        }
    }
}

bool TConfMsgManager::IsDelayed() const {
    return Delayed;
}

void TConfMsgManager::DisableDelay() {
    Y_ASSERT(Delayed);
    Delayed = false;
    TracesDeduplicator.Clear();
}

void TConfMsgManager::EnableDelay() {
    Y_ASSERT(!Delayed);
    Delayed = true;
}

void TConfMsgManager::Erase(ui32 owner) {
    Messages.erase(owner);
    DupSrcMap.erase(owner);
    Events.erase(owner);
}

bool TConfMsgManager::EraseMessagesByKind(const TStringBuf var, ui32 owner) {
    auto eraseOwnerMessages = [](const TStringBuf& var, TVector<TConfigureMessage>& messages) {
        auto size = messages.size();
        EraseIf(messages, [&var](const TConfigureMessage& message) {
            return message.Kind == var;
        });
        return size > messages.size();
    };

    if (!owner) {
        bool someErased = false;
        for (auto& [_, messages] : Messages) {
            someErased |= eraseOwnerMessages(var, messages);
        }
        return someErased;
    } else {
        auto it = Messages.find(owner);
        if (it != Messages.end()) {
            return eraseOwnerMessages(var, it->second);
        }
        return false;
    }
}

bool TConfMsgManager::HasMessagesByKind(const TStringBuf var, ui32 owner) const {
    const auto it = Messages.find(owner);
    if (it == Messages.end()) {
        return false;
    }
    const auto& messages = it->second;
    return FindIf(messages, [&var](const TConfigureMessage& message) {
        return message.Kind == var;
    }) != messages.end();
}

void TConfMsgManager::Save(TMultiBlobBuilder& builder) {
    TString confMsgsData;
    {
        TStringOutput output(confMsgsData);
        Save(&output, Messages);
        output.Finish();
    }
    builder.AddBlob(new TBlobSaverMemory(TBlob::Copy(confMsgsData.data(), confMsgsData.size())));

    TString dupSrcData;
    {
        TStringOutput output(dupSrcData);
        Save(&output, DupSrcMap);
        output.Finish();
    }
    builder.AddBlob(new TBlobSaverMemory(TBlob::Copy(dupSrcData.data(), dupSrcData.size())));

    TString confEventsData;
    {
        TStringOutput output(confEventsData);
        Save(&output, Events);
        output.Finish();
    }
    builder.AddBlob(new TBlobSaverMemory(TBlob::Copy(confEventsData.data(), confEventsData.size())));
}

template<typename Container>
void TConfMsgManager::Save(IOutputStream* output, THashMap<ui32, Container>& confValues) {
    TSaver<typename Container::value_type> saver;
    for (const auto& [owner, values] : confValues) {
        for (const auto& value : values) {
            if constexpr (requires { std::is_convertible_v<decltype(value.IsPersistent), bool>; }) {
                if (!value.IsPersistent) {
                    continue;
                }
            }
            saver.Values.emplace_back(owner, value);
        }
    }
    saver.Save(output);
    confValues.clear();
}

void TConfMsgManager::Load(const TBlob& multi) {
    TSubBlobs blobs(multi);
    {
        TMemoryInput input(blobs[0].Data(), blobs[0].Length());
        Load(&input, Messages);
    }
    {
        TMemoryInput input(blobs[1].Data(), blobs[1].Length());
        Load(&input, DupSrcMap);
    }
    {
        TMemoryInput input(blobs[2].Data(), blobs[2].Length());
        Load(&input, Events);
    }
}

template<typename Container>
void TConfMsgManager::Load(IInputStream* input, THashMap<ui32, Container>& confValues) {
    TSaver<typename Container::value_type> saver;
    saver.Load(input);
    for (const auto& value : saver.Values) {
        if constexpr (requires { confValues[value.ElemId].push_back(value.Value); }) {
            confValues[value.ElemId].push_back(value.Value);
        } else if constexpr (requires { confValues[value.ElemId].insert(value.Value); }) {
            confValues[value.ElemId].insert(value.Value);
        } else {
            Y_ASSERT(0);
        }
    }
}

TConfMsgManager* ConfMsgManager() {
    auto ctx = CurrentContext<TExecContext>;
    if (ctx && ctx->ConfMsgManager) {
        return ctx->ConfMsgManager.get();
    }
    return Singleton<TConfMsgManager>();
}

void TConfMsgManager::ClearTopLevelMessages() {
    Messages[0].clear();
    Events[0].clear();
}
