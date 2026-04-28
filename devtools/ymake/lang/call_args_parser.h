#pragma once

#include "call_signature.h"

#include <bitset>
#include <expected>

enum class TArgDefIdx: int {};
inline TArgDefIdx KWIdx(const TKeyword& key) noexcept {
    return static_cast<TArgDefIdx>(-1 - static_cast<int>(key.Pos));
}
constexpr TArgDefIdx VarargIdx = static_cast<TArgDefIdx>(std::numeric_limits<int>::max());

class TArgsParseError {
public:
    enum class ECase {
        ExtraPositional,
        MissingPositional,
        MissingScalarKWValue,
        MixingArraysKWAndPositionals,
        DuplicatedKeyword
    };
    using enum ECase;

    TArgsParseError(ECase errCase, TArgDefIdx argDef, size_t arg) noexcept
        : Case_{errCase}
        , ArgDef_{argDef}
        , ArgPos_(arg)
    {}

    ECase ErrorCase() const noexcept {
        return Case_;
    }

    TString Message(const TSignature& sign, std::span<const TStringBuf> args) const;

private:
    ECase Case_;
    TArgDefIdx ArgDef_;
    size_t ArgPos_;
};

class TParsedCallArgs {
public:
    template<typename Val>
    using TResult = std::expected<Val, TArgsParseError>;

    using value_type = TResult<std::pair<TArgDefIdx, std::span<const TStringBuf>>>;
    struct sentinel {};
    class iterator;

    TParsedCallArgs(const TSignature& sign, std::span<const TStringBuf> args) noexcept
        : Sign_{&sign}
        , Args_{args}
        , LastPosIdx_{Sign_->GetNumUsrArgs() - (Sign_->HasVararg() ? 1 : 0)}
        , ArgsTotal_{Args_.size()}
    {}

    iterator begin() noexcept;
    sentinel end() const noexcept {return {};}

    bool Empty() const noexcept {
        return Args_.empty() && NextPosIdx_ == LastPosIdx_;
    }

    void Clear() noexcept {
        Args_ = {};
        NextPosIdx_ = LastPosIdx_;
    }

    value_type ParseNext() noexcept;

private:
    TResult<TArgDefIdx> DeduplicateKeyword(const TKeyword& kw) noexcept;
    TResult<TArgDefIdx> IncrementPositional() noexcept;
    size_t CurrentArgPos() const noexcept {return ArgsTotal_ - Args_.size();}

private:
    const TSignature* Sign_;
    std::span<const TStringBuf> Args_;
    std::bitset<128> KWSet_;
    size_t NextPosIdx_ = 0;
    size_t LastPosIdx_ = 0;
    size_t ArgsTotal_ = 0;
    TArgDefIdx LastArr_ = {};
};

class TParsedCallArgs::iterator {
public:
    using value_type = TParsedCallArgs::value_type;
    using difference_type = std::ptrdiff_t;

    iterator() noexcept = default;
    iterator(TParsedCallArgs& parser) noexcept
        : Parser_{&parser}
    {
        ++(*this);
    }

    const value_type& operator* () const noexcept {
        return Current_;
    }

    iterator& operator++() noexcept {
        if (Parser_->Empty()) {
            Parser_ = nullptr;
        } else {
            Current_ = Parser_->ParseNext();
        }
        return *this;
    }

    iterator operator++(int) noexcept {
        auto res = *this;
        ++(*this);
        return res;
    }

    bool operator==(TParsedCallArgs::sentinel) const noexcept {
        return Parser_ == nullptr;
    }

private:
    value_type Current_{};
    TParsedCallArgs* Parser_ = nullptr;
};

inline TParsedCallArgs::iterator TParsedCallArgs::begin() noexcept {
    return {*this};
}
