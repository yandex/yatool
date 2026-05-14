#pragma once

#include "raii.h"
#include "signature_conversion.h"

#include <devtools/ymake/lang/call_signature.h>

#include <expected>

namespace NYMake::NPy {

class TFFIMacro {
public:
    class TCallError: public std::runtime_error {
    public:
        TCallError(const std::string& msg): std::runtime_error{msg} {}
    };

    TFFIMacro() noexcept = default;

    static std::expected<TFFIMacro, ESignatureDeductionError> Wrap(OwnedRef<> func, PyTypeObject& unitType) noexcept {
        auto sign = DeduceConfSignature(*func, unitType);
        if (!sign)
            return std::unexpected(sign.error());
        return TFFIMacro{std::move(func), std::move(sign.value())};
    }

    OwnedRef<> Call(PyObject& unit, std::span<const TStringBuf> args);

    TStringBuf Name() const noexcept;
    TString DocText() const;
    PyObject* Impl() const noexcept {
        return Func_.get();
    }

private:
    TFFIMacro(OwnedRef<>&& func, TSignature&& sign) noexcept
        : Func_{std::move(func)}
        , Sign_{std::move(sign)}
    {}

private:
    OwnedRef<> Func_;
    TSignature Sign_;
};

}
