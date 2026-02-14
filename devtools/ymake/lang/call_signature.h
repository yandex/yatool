#pragma once

#include <util/generic/string.h>
#include <util/generic/vector.h>
#include <util/ysaveload.h>

struct TKeyword {
    size_t From;
    size_t To;
    size_t Pos;
    TString DeepReplaceTo;
    TVector<TString> OnKwPresent;
    TVector<TString> OnKwMissing;
    TKeyword()
        : From(0)
        , To(0)
        , Pos(0)
    {
    }
    TKeyword(const TString& myName, size_t from, size_t to, const TString& deepReplaceTo, TStringBuf onKwPresent = {}, TStringBuf onKwMissing = {})
        : From(from)
        , To(to)
        , Pos(0)
        , DeepReplaceTo(deepReplaceTo)
    {
        if (onKwPresent.data() != nullptr) // "" from file is not NULL
            OnKwPresent.emplace_back(onKwPresent);
        else if (!From && !To)
            OnKwPresent.push_back(myName);
        if (onKwMissing.data() != nullptr)
            OnKwMissing.emplace_back(onKwMissing);
    }

    Y_SAVELOAD_DEFINE(
        From,
        To,
        Pos,
        DeepReplaceTo,
        OnKwPresent,
        OnKwMissing
    );
};

class TSignature {
public:
    class TKeywords {
    public:
        TKeywords() noexcept = default;
        ~TKeywords() noexcept = default;

        TKeywords(const TKeywords&) = delete;
        TKeywords& operator=(const TKeywords&) = delete;
        TKeywords(TKeywords&&) noexcept = default;
        TKeywords& operator=(TKeywords&&) noexcept = default;


        void AddKeyword(const TString& word, size_t from, size_t to, const TString& deepReplaceTo, const TStringBuf& onKwPresent = nullptr, const TStringBuf& onKwMissing = nullptr);
        void AddArrayKeyword(const TString& word, const TString& deepReplaceTo) {
            AddKeyword(word, 0, ::Max<ssize_t>(), deepReplaceTo);
        }
        void AddScalarKeyword(const TString& word, const TStringBuf& defaultVal, const TString& deepReplaceTo) {
            AddKeyword(word, 1, 1, deepReplaceTo, nullptr, defaultVal);
        }
        void AddFlagKeyword(const TString& word, const TStringBuf& setVal, const TStringBuf& unsetVal) {
            AddKeyword(word, 0, 0, {}, setVal, unsetVal);
        }

        bool Empty() const noexcept {
            return Collected_.empty();
        }

        TVector<std::pair<TString, TKeyword>> Take() && noexcept {
            return std::move(Collected_);
        }
    private:
        TVector<std::pair<TString, TKeyword>> Collected_;
    };

    TSignature() noexcept = default;
    TSignature(const TVector<TString>& cmd, TSignature::TKeywords&& kw);

    const TVector<TString>& ArgNames() const noexcept {
        return ArgNames_;
    }

    bool HasKeyword(TStringBuf arg) const {
        return std::ranges::binary_search(Keywords_, arg, std::less<>{}, &std::pair<TString, TKeyword>::first);
    }
    bool IsNonPositional() const {
        return !Keywords_.empty();
    }
    size_t GetKeyArgsNum() const {
        return Keywords_.size();
    }
    const TVector<std::pair<TString, TKeyword>>& GetKeywords() const noexcept {
        return Keywords_;
    }
    const TString& GetKeyword(size_t arrNum) const {
        Y_ASSERT(arrNum < Keywords_.size());
        return Keywords_[arrNum].first;
    }
    const TKeyword& GetKeywordData(size_t arrNum) const {
        Y_ASSERT(arrNum < Keywords_.size());
        return Keywords_[arrNum].second;
    }
    const TKeyword* GetKeywordData(TStringBuf name) const {
        const auto [first, last] = std::ranges::equal_range(Keywords_, name, std::less<>{}, &std::pair<TString, TKeyword>::first);
        return first != last ? &first->second : nullptr;
    }
    size_t Key2ArrayIndex(TStringBuf arg) const;

    // @return Variable positional argument name (i.e. for `Rest...` argument form signature returns `Rest`). Returns empty string
    //         for signature without variable positional argument.
    TStringBuf GetVarargName() const noexcept;
    bool HasVararg() const noexcept;

    bool HasUsrArgs() const noexcept {
        return NumUsrArgs_ != 0;
    }

    size_t GetNumUsrArgs() const noexcept {
        return NumUsrArgs_;
    }

    TString GetDeepReplaceTo(size_t arrNum) const {
        if (arrNum < Keywords_.size()) {
            return Keywords_[arrNum].second.DeepReplaceTo;
        }
        return {};
    }

    Y_SAVELOAD_DEFINE(
        ArgNames_,
        Keywords_,
        NumUsrArgs_
    );

private:
    TVector<TString> ArgNames_;
    // /me cries loudly because of absence of std::flat_map right here and right now.
    // Do not hesitate to remove this coment once proper time will come.
    TVector<std::pair<TString, TKeyword>> Keywords_;
    size_t NumUsrArgs_ = 0;
};
