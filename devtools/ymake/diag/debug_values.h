#pragma once

#include <util/generic/hash.h>
#include <util/generic/vector.h>
#include <util/ysaveload.h>

class IDebugValues {
public:
    virtual ~IDebugValues() {}

    virtual void Reset() = 0;
};

class TDebugValuesReset {
public:
    static void Register(IDebugValues* debugValues) {
        Instance()->DebugValuesRegister_.push_back(debugValues);
    }

    static void Reset() {
        for (IDebugValues* debugValues : Instance()->DebugValuesRegister_) {
            debugValues->Reset();
        }
    }

private:
    static TDebugValuesReset* Instance() {
        return Singleton<TDebugValuesReset>();
    }

    TVector<IDebugValues*> DebugValuesRegister_;
};

template<typename TValue>
class TDebugValues : public IDebugValues {
private:
    using TValuesIndices = THashMap<TValue, ui32>;
    using TValues = TVector<TValue>;

public:
    TDebugValues() {
        TDebugValuesReset::Register(this);
    }

    std::pair<bool, ui32> Store(const TValue& value) {
        auto [it, isNew] = ValuesIndices_.insert(typename TValuesIndices::value_type{value, Counter_});
        if (isNew) {
            return {true, Counter_++};
        }

        auto [_, index] = *it;
        return {false, index};
    }

    void Restore(const TValue& value) {
        Values_.push_back(value);
    }

    TValue Get(ui32 index) {
        return Values_.at(index);
    }

private:
    void Reset() override {
        Counter_ = 0;
        ValuesIndices_.clear();
        Values_.clear();
    }

    ui32 Counter_ = 0;
    TValuesIndices ValuesIndices_;
    TValues Values_;
};

template<typename TValue>
inline TDebugValues<TValue>* DebugValues() {
    return Singleton<TDebugValues<TValue>>();
}

template<typename TValue>
inline void SaveDebugValue(IOutputStream* s, const TValue& value) {
    auto [isNew, index] = DebugValues<TValue>()->Store(value);

    ::Save<bool>(s, isNew);
    if (isNew) {
        ::Save(s, value);
    } else {
        ::Save<ui32>(s, index);
    }
}

template<typename TValue>
inline void LoadDebugValue(IInputStream* s, TValue& value) {
    bool isNew;
    ::Load(s, isNew);

    if (isNew) {
        ::Load(s, value);
        DebugValues<TValue>()->Restore(value);
    } else {
        ui32 index;
        ::Load(s, index);
        value = DebugValues<TValue>()->Get(index);
    }
}

struct TStringLogEntry {
    TString Value;

    TStringLogEntry() = default;
    TStringLogEntry(const TString& value) : Value(value) {}

    inline void Save(IOutputStream* s) const {
        ::SaveDebugValue(s, Value);
    }

    inline void Load(IInputStream* s) {
        ::LoadDebugValue(s, Value);
    }
};
