#include "call_args_parser.h"

#include <util/generic/enum_cast.h>

#include <fmt/format.h>

namespace {

using TArgsSpan = std::span<const TStringBuf>;

inline TArgsSpan UnsafeTakeN(TArgsSpan& args, size_t count) noexcept {
    auto res = args.subspan(0, count);
    args = args.subspan(count);
    return res;
}

inline TArgsSpan TakeArr(TArgsSpan& args, const TSignature& sign) noexcept {
    size_t pos = 0;
    while (pos < args.size() && !sign.HasKeyword(args[pos])) {
        ++pos;
    }
    auto res = args.subspan(0, pos);
    args = args.subspan(pos);
    return res;
}

inline auto MakeError(TArgsParseError::ECase errCase, TArgDefIdx defIdx, size_t pos) noexcept {
    return std::unexpected(TArgsParseError{errCase, defIdx, pos});
}

inline TParsedCallArgs::TResult<TArgsSpan> TakeScalarKwValue(TArgDefIdx defIdx, TArgsSpan& args, const TSignature& sign) {
    if (args.empty() || sign.HasKeyword(args.front())) {
        return MakeError(TArgsParseError::MissingScalarKWValue, defIdx, 0);
    }

    return UnsafeTakeN(args, 1);
}

}

TStringBuf ArgDefName(const TSignature& sign, TArgDefIdx idx) noexcept {
    if (idx == VarargIdx) {
        return sign.GetVarargName();
    }

    const auto rawIdx = ToUnderlying(idx);
    if (rawIdx < 0) {
        return sign.GetKeyword(-rawIdx - 1);
    }

    return sign.ScalarPositionalArgs()[rawIdx];
}

const TKeyword* KeywordData(const TSignature& sign, TArgDefIdx idx) noexcept {
    const auto rawIdx = ToUnderlying(idx);
    return rawIdx < 0 ? &sign.GetKeywordData(-rawIdx - 1) : nullptr;
}

TString TArgsParseError::Message(const TSignature& sign, std::span<const TStringBuf> args) const {
    TString res;
    switch (Case_) {
        case ExtraPositional:
            res = fmt::format("More positional arguments passed than expected. Can't handle arg: '{}'.", args[ArgPos_]);
            break;
        case MissingPositional:
            res = fmt::format("Value for '{}' argument is missing.", ArgDefName(sign, ArgDef_));
            break;
        case MissingScalarKWValue:
            res = fmt::format("Missing value for scalar keyword '{}'.", ArgDefName(sign, ArgDef_));
            break;
        case MixingArraysKWAndPositionals:
            res = fmt::format(
                "Mixing positional arguments with named arrays is error prone and forbidden.\n"
                "\tGot positional arg '{}' after named array '{}' has been started.",
                args[ArgPos_],
                ArgDefName(sign, ArgDef_)
            );
            break;
        case DuplicatedKeyword:
            res = fmt::format("Keyword '{}' appears more than once and is not an array.", ArgDefName(sign, ArgDef_));
            break;
    }
    return res;
}

TParsedCallArgs::value_type TParsedCallArgs::ParseNext() noexcept {
    TParsedCallArgs::value_type res;
    if (Args_.empty() && NextPosIdx_ < LastPosIdx_) {
        res = MakeError(TArgsParseError::MissingPositional, static_cast<TArgDefIdx>(NextPosIdx_), CurrentArgPos());
        Clear();
        return res;
    }

    if (auto kw = Sign_->GetKeywordData(Args_.front())) {
        Args_ = Args_.subspan(1);
        res = DeduplicateKeyword(*kw).and_then([&](TArgDefIdx idx) -> TParsedCallArgs::value_type {
            switch (kw->Kind) {
            case TKeyword::Flag:
                return std::pair{idx, TArgsSpan{}};
            case TKeyword::Scalar:
                return TakeScalarKwValue(idx, Args_, *Sign_).transform(
                    [idx](TArgsSpan val) { return std::pair{idx, val}; }
                );
            case TKeyword::Array:
                return std::pair{idx, TakeArr(Args_, *Sign_)};
            }
        });
    } else {
        res = IncrementPositional().transform([&](TArgDefIdx idx){
            if (idx == VarargIdx) {
                return std::pair{idx, TakeArr(Args_, *Sign_)};
            } else {
                return std::pair{idx, UnsafeTakeN(Args_, 1)};
            }
        });
    }

    if (!res) {
        Clear();
    }
    return res;
}

TParsedCallArgs::TResult<TArgDefIdx> TParsedCallArgs::DeduplicateKeyword(const TKeyword& kw) noexcept {
    if (KWSet_[kw.Pos] && kw.Kind != TKeyword::Array) {
        return MakeError(TArgsParseError::DuplicatedKeyword, KWIdx(kw), CurrentArgPos());
    }

    if (kw.Kind == TKeyword::Array) {
        LastArr_ = KWIdx(kw);
    }
    KWSet_[kw.Pos] = true;
    return KWIdx(kw);
}

TParsedCallArgs::TResult<TArgDefIdx> TParsedCallArgs::IncrementPositional() noexcept {
    if (LastArr_ != TArgDefIdx{}) {
        return MakeError(TArgsParseError::MixingArraysKWAndPositionals, LastArr_, CurrentArgPos());
    }
    if (NextPosIdx_ == LastPosIdx_ && Sign_->HasVararg()) {
        return VarargIdx;
    } else if (NextPosIdx_ == Sign_->GetNumUsrArgs()) {
        return MakeError(TArgsParseError::ExtraPositional, {}, CurrentArgPos());
    }

    return static_cast<TArgDefIdx>(NextPosIdx_++);
}
