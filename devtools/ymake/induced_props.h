#pragma once

#include <devtools/ymake/common/uniq_vector.h>
#include <devtools/ymake/compact_graph/dep_graph.h>
#include <devtools/ymake/compact_graph/dep_types.h>
#include <devtools/ymake/diag/debug_log.h>

#include <util/generic/bitmap.h>
#include <util/generic/hash.h>

#include "induced_props_debug.h"
#include "node_debug.h"

enum EVisitIntent {
    EVI_InducedDeps = 0,  // "ParsedIncls"
    EVI_CommandProps = 1, // "Cmd"
    EVI_ModuleProps = 2,  // "Mod"
    EVI_GetModules = 3,
    EVI_MaxId = 4,
};

#define FOR_ALL_INTENTS(name) \
for (EVisitIntent name = static_cast<EVisitIntent>(0); name < EVI_MaxId; name = static_cast<EVisitIntent>(name + 1))

EVisitIntent IntentByName(const TStringBuf& intent, bool check = true);

inline char IntentToChar(EVisitIntent intent) {
    Y_ASSERT(0 <= intent && intent < EVI_MaxId);
    return "ICMG"[intent];
}

class TPropertyType {
public:
    constexpr explicit TPropertyType(ui64 repr) : Repr_(repr) {}

    TPropertyType() = default;
    TPropertyType(TSymbols& symbols, EVisitIntent intent, TStringBuf name) {
        Repr_ = (ui64)symbols.CommandConf.Add(name) << 16 | (ui64)intent;
    }

    constexpr ui64 GetRepr() const {
        return Repr_;
    }

    constexpr bool operator== (const TPropertyType& that) const {
        return this->Repr_ == that.Repr_;
    }

    constexpr EVisitIntent GetIntent() const {
        return static_cast<EVisitIntent>(Repr_ & 0xFFFF);
    }

    TStringBuf GetName(const TDepGraph& graph) const {
        return graph.Names().CommandConf.GetName(Repr_ >> 16).GetStr();
    }

    TString Dump(const TDepGraph& graph) const {
        return TString{IntentToChar(GetIntent())} + ':' + GetName(graph);
    }

private:
    ui64 Repr_ = 0;

public:
    Y_SAVELOAD_DEFINE(Repr_);
};

template<>
struct THash<TPropertyType> {
    size_t operator()(const TPropertyType& propType) {
        return THash<ui64>{}(propType.GetRepr());
    }
};

struct TIndDepsRule {
    enum class EAction {
        Use,
        Pass
    };

    typedef TVector<std::pair<TPropertyType, EAction>> TActions;
    TActions Actions;
    bool PassInducedIncludesThroughFiles = false;
    bool PassNoInducedDeps = false;

    TIndDepsRule(std::initializer_list<TPropertyType> types = {}, EAction act = EAction::Use) {
        Actions.reserve(types.size());
        for (const auto& type: types) {
            Actions.push_back(std::make_pair(type, act));
        }
    }

    void InsertUseActionsTo(THashSet<TPropertyType>& target) const;
};

class TIntents {
public:
    TIntents() {}

    explicit TIntents(std::initializer_list<EVisitIntent> intents) {
        for (auto intent: intents) {
            Add(intent);
        }
    }

    bool Empty() const {
        return IntentBits_ == 0;
    }

    bool NonEmpty() const {
        return !Empty();
    }

    bool Has(EVisitIntent intent) const {
        return IntentBits_ & IntentBit(intent);
    }

    TIntents operator| (const TIntents& other) const {
        TIntents result{*this};
        result.IntentBits_ |= other.IntentBits_;
        return result;
    }

    void Add(EVisitIntent intent) {
        IntentBits_ |= IntentBit(intent);
    }

    void Add(const TIntents& intents) {
        IntentBits_ |= intents.IntentBits_;
    }

    TIntents operator& (const TIntents& other) const {
        TIntents result{*this};
        result.IntentBits_ &= other.IntentBits_;
        return result;
    }

    bool operator== (const TIntents& other) const {
        return IntentBits_ == other.IntentBits_;
    }

    void Remove(EVisitIntent intent) {
        IntentBits_ &= ~IntentBit(intent);
    }

    void Remove(const TIntents& intents) {
        IntentBits_ &= ~intents.IntentBits_;
    }

    TIntents Without(const TIntents& intents) const {
        TIntents result{*this};
        result.Remove(intents);
        return result;
    }

    static TIntents None() {
        return TIntents{};
    }

    static TIntents All() {
        TIntents all;
        all.IntentBits_ = AllIntentBits_;
        return all;
    }

    TString Dump() const;

    Y_SAVELOAD_DEFINE(IntentBits_);

private:
    ui8 IntentBits_ = 0;
    static_assert(sizeof(IntentBits_) * 8 >= EVI_MaxId);

    static ui8 IntentBit(EVisitIntent intent) {
        return 1 << intent;
    }

    static_assert(sizeof(ui64) * 8 > EVI_MaxId);
    constexpr static ui8 AllIntentBits_ = static_cast<ui8>((static_cast<ui64>(1) << EVI_MaxId) - 1);
};


inline IOutputStream& operator<< (IOutputStream& out, const TIntents& intents) {
    out << '[';
    for (size_t i  = 0; i < EVI_MaxId; ++i) {
        EVisitIntent intent = static_cast<EVisitIntent>(i);
        if (intents.Has(intent)) {
            out << IntentToChar(intent);
        }
    }
    out << ']';
    return out;
}

inline TString TIntents::Dump() const {
    TStringStream s;
    s << *this;
    return s.Str();
}


enum class EPropertyAdditionType {
    NotSet,
    Copied,
    FromNode,
    Created
};

struct TPropsNodeListDebug : public TNodeDebug {
    TPropsNodeListDebug(const TNodeDebug& debugNode, TPropertyType type) : TNodeDebug(debugNode), DebugType(type) {}
    TPropertyType DebugType;
};

using TPropsNodeListDebugOnly = TDebugOnly<TPropsNodeListDebug>;

struct TPropertySourceDebug {
    TPropertySourceDebug() = default;
    explicit TPropertySourceDebug(EPropertyAdditionType type) : Node(TDepsCacheId::None), Type(type) {}
    TPropertySourceDebug(TDepsCacheId node, EPropertyAdditionType type) : Node(node), Type(type) {}
    TPropertySourceDebug(TDepTreeNode node, EPropertyAdditionType type) : Node(MakeDepsCacheId(node.NodeType, node.ElemId)), Type(type) {}
    TPropertySourceDebug(TNodeDebug source, EPropertyAdditionType type) : Node(source.DebugNode) , Type(type) {}
    TDepsCacheId Node = TDepsCacheId::None;
    EPropertyAdditionType Type = EPropertyAdditionType::NotSet;
};

using TPropertySourceDebugOnly = TDebugOnly<TPropertySourceDebug>;

struct TPropertyTypeLog {
    TPropertyType Value;
    TString String;

    TPropertyTypeLog() = default;
    TPropertyTypeLog(const TDepGraph& graph, TPropertyType type)
        : Value(type)
    {
        TStringOutput out{String};
        out << IntentToChar(Value.GetIntent()) << ':' << Value.GetName(graph);
    }

    inline void Save(IOutputStream* s) const {
        ::Save(s, Value);
        ::SaveDebugValue(s, String);
    }

    inline void Load(IInputStream* s) {
        ::Load(s, Value);
        ::LoadDebugValue(s, String);
    }
};

namespace NDebugEvents::NProperties {
    struct TAddEvent {
        TNodeIdLog Node;
        TNodeIdLog SourceNode;
        TPropertyTypeLog PropType;
        TNodeIdLog PropNode;
        bool IsNew;
        EPropertyAdditionType AdditionType;

        Y_SAVELOAD_DEFINE(Node, SourceNode, PropType, PropNode, IsNew, AdditionType);

        TAddEvent() = default;
        TAddEvent(
            const TDepGraph& graph,
            TDepsCacheId node,
            TDepsCacheId sourceNode,
            TPropertyType propType,
            TDepsCacheId propNode,
            bool isNew,
            EPropertyAdditionType additionType
        );
    };

    struct TClearEvent {
        TNodeIdLog Node;
        TPropertyTypeLog PropType;

        Y_SAVELOAD_DEFINE(Node, PropType);

        TClearEvent() = default;
        TClearEvent(const TDepGraph& graph, TDepsCacheId id, TPropertyType type);
    };

    struct TReadEvent {
        TNodeIdLog Node;
        TPropertyTypeLog PropType;

        Y_SAVELOAD_DEFINE(Node, PropType);

        TReadEvent() = default;
        TReadEvent(const TDepGraph& graph, TDepsCacheId id, TPropertyType type);
    };

    struct TUseEvent {
        TNodeIdLog UserNode;
        TNodeIdLog SourceNode;
        TPropertyTypeLog PropType;
        TNodeIdLog PropNode;
        TStringLogEntry Note;

        Y_SAVELOAD_DEFINE(UserNode, SourceNode, PropType, PropNode, Note);

        TUseEvent() = default;
        TUseEvent(const TDepGraph& graph, TDepsCacheId userNode, TDepsCacheId sourceNode, TPropertyType propType, TDepsCacheId propNode, TString note);
    };
}

using TPropValues = TUniqVector<TDepsCacheId>;

struct TPropsNodeList
    : public TPropsNodeListDebugOnly
{
    TPropsNodeList(TPropsNodeListDebugOnly debug) : TPropsNodeListDebugOnly(debug) {}

    TPropValues::const_iterator begin() const {
        DebugLogRead();
        return Values_.begin();
    }

    TPropValues::const_iterator end() const {
        DebugLogRead();
        return Values_.end();
    }

    bool empty() const {
        DebugLogRead();
        return Values_.empty();
    }

    size_t size() const {
        DebugLogRead();
        return Values_.size();
    }

    void Clear() {
        BINARY_LOG(IPRP, NProperties::TClearEvent, DebugGraph, DebugNode, DebugType);
        Values_.clear();
    }

    void Push(TDepsCacheId propId, TPropertySourceDebugOnly sourceDebug) {
        bool isNew = Values_.Push(propId);
        DEBUG_USED(sourceDebug, isNew);
        BINARY_LOG(IPRP, NProperties::TAddEvent, DebugGraph, DebugNode, sourceDebug.Node, DebugType, propId, isNew, sourceDebug.Type);
    }

    void SetValues(TPropValues&& values, TPropertySourceDebugOnly sourceDebug) {
        Values_ = std::move(values);
        IF_BINARY_LOG(IPRP) {
            for (const auto& value : Values_) {
                DEBUG_USED(sourceDebug, value);
                BINARY_LOG(IPRP, NProperties::TAddEvent, DebugGraph, DebugNode, sourceDebug.Node, DebugType, value, true, sourceDebug.Type);
            }
        }
    }

    const TVector<TDepsCacheId>& Data() const {
        DebugLogRead();
        return Values_.Data();
    }

    // Только для отладочных целей. Не считается событием чтения актуальных свойств.
    const TVector<TDepsCacheId>& DataNotFinal() const {
        return Values_.Data();
    }

private:
    TPropValues Values_;

private:
    friend class TInducedProps;

    // Здесь не предполагается чтение актуальной информации о свойствах.
    // Этот метод используется для внутренней проверки состояния во время сбора свойств.
    bool HasNotFinal(TDepsCacheId id) const {
        return Values_.has(id);
    }

    void DebugLogRead() const {
        BINARY_LOG(IPRP, NProperties::TReadEvent, DebugGraph, DebugNode, DebugType);
    }
};

class TInducedProps
    : public THashMap<TPropertyType, TPropsNodeList>
    , public TNodeDebugOnly
{
public:
    TInducedProps(const TNodeDebugOnly& nodeDebug) : TNodeDebugOnly(nodeDebug) {}

    TPropsNodeList& Get(TPropertyType propType) {
        auto emplace_result = try_emplace(propType, TPropsNodeListDebugOnly{*this, propType});
        auto& [typeAndList, _] = emplace_result;
        auto& [__, propsList] = *typeAndList;
        return propsList;
    }

    bool HasType(TPropertyType propType) const {
        return find(propType) != end();
    }

    void AddType(TPropertyType propType, TPropertySourceDebugOnly sourceDebug) {
        auto [_, isNew] = try_emplace(propType, TPropsNodeListDebugOnly{*this, propType});
        DEBUG_USED(sourceDebug, isNew);
        BINARY_LOG(IPRP, NProperties::TAddEvent, DebugGraph, DebugNode, sourceDebug.Node, propType, TDepsCacheId::None, isNew, sourceDebug.Type);
    }

    bool HasAll(TPropertyType propType, const TVector<TDepsCacheId>& props) const {
        const auto it = find(propType);
        if (it == end()) {
            return false;
        }

        const TPropsNodeList& currentProps = it->second;
        auto alreadyAdded = [&](const auto& propValue) {
            return currentProps.HasNotFinal(propValue);
        };
        return AllOf(props, alreadyAdded);
    }

    void AddAll(TPropertyType propType, const TVector<TDepsCacheId>& props, TPropertySourceDebugOnly source) {
        TPropsNodeList& currentProps = Get(propType);
        for (const auto& prop : props) {
            currentProps.Push(prop, source);
        }
    }
};

using TInducedPropsPtr = TSimpleSharedPtr<TInducedProps>;
using TUsingRules = TMaybe<std::reference_wrapper<const THashSet<TPropertyType>>>;

class TNodeProperties
    : private TSimpleSharedPtr<TInducedProps>
    , public TNodeDebugOnly
{
private:
    enum {
        ShouldAdd,
        AlreadyAdded
    };

public:
    TNodeProperties(const TNodeDebugOnly& nodeDebug) : TNodeDebugOnly(nodeDebug) {}

    TInducedProps::const_iterator begin() const {
        return Get() ? Get()->begin() : TInducedProps::const_iterator(nullptr);
    }

    TInducedProps::const_iterator end() const {
        return Get() ? Get()->end() : TInducedProps::const_iterator(nullptr);
    }

    TInducedProps::iterator begin() {
        return Get() ? Get()->begin() : TInducedProps::iterator(nullptr);
    }

    TInducedProps::iterator end() {
        return Get() ? Get()->end() : TInducedProps::iterator(nullptr);
    }

    TPropsNodeList& operator[](TPropertyType propType) {
        CreateIfNeeded();
        return Get()->Get(propType);
    }

    bool Empty() const {
        return Get() ? Get()->empty() : true;
    }

    TInducedProps::const_iterator Find(TPropertyType propType) const {
        return Get() ? Get()->find(propType) : TInducedProps::const_iterator(nullptr);
    }

    inline void AddProps(
        TPropertyType propType,
        const TVector<TDepsCacheId>& props,
        TPropertySourceDebugOnly source
    );
    inline void AddSimpleProp(TPropertyType propType, TPropertySourceDebugOnly sourceDebug);
    inline void SetPropValues(TPropertyType propType, TPropValues&& values, TPropertySourceDebugOnly sourceDebug);
    inline void CopyProps(const TNodeProperties& from, TIntents copyIntents, TUsingRules skipPropTypes);

    void Clear() {
        if (Get()) {
            Drop();
        }
    }

private:
    template<typename TAddedCheck>
    auto PrepareToAdd(TAddedCheck&& alreadyAdded) {
        if (CreateIfNeeded()) {
            return ShouldAdd;
        }

        if (Shared()) {
            if (alreadyAdded()) {
                return AlreadyAdded;
            } else {
                Unshare();
                return ShouldAdd;
            }
        }

        // Не проверяем, были ли все props уже добавлены, потому что при добавлении
        // каждой из них в TPropsNodeList всё равно будет такая же проверка на уникальность.
        return ShouldAdd;
    }

    bool CreateIfNeeded() {
        if (!Get()) {
            Reset(new TInducedProps(TNodeDebugOnly{*this}));
            return true;
        }
        return false;
    }

    bool Shared() const {
        return RefCount() > 1;
    }

    void Unshare() {
        Reset(new TInducedProps(*Get()));
    }
};

inline void TNodeProperties::AddProps(
    TPropertyType propType,
    const TVector<TDepsCacheId>& props,
    TPropertySourceDebugOnly source
)
{
    if (props.empty()) {
        return;
    }

    auto alreadyAddedCheck = [&]() {
        return Get()->HasAll(propType, props);
    };
    if (PrepareToAdd(alreadyAddedCheck) == ShouldAdd) {
        Get()->AddAll(propType, props, source);
    }
}

inline void TNodeProperties::AddSimpleProp(TPropertyType propType, TPropertySourceDebugOnly sourceDebug) {
    auto alreadyAddedCheck = [&]() {
        return Get()->HasType(propType);
    };
    if (PrepareToAdd(alreadyAddedCheck) == ShouldAdd) {
        Get()->AddType(propType, sourceDebug);
    }
}

inline void TNodeProperties::SetPropValues(TPropertyType propType, TPropValues&& values, TPropertySourceDebugOnly sourceDebug) {
    CreateIfNeeded();
    Get()->Get(propType).SetValues(std::move(values), sourceDebug);
}

inline void TNodeProperties::CopyProps(const TNodeProperties& from, TIntents copyIntents, TUsingRules skipPropTypes) {
    if (from.Empty()) {
        return;
    }

    TPropertySourceDebugOnly source{from, EPropertyAdditionType::Copied};

    for (const auto& [propType, propsValues] : *from) {
        if (!copyIntents.Has(propType.GetIntent())) {
            continue;
        }
        if (!skipPropTypes.Empty() && skipPropTypes->get().contains(propType)) {
            continue;
        }

        AddProps(propType, propsValues.Data(), source);
    }
}
