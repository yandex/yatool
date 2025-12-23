#pragma once

#include <util/system/types.h>
#include <util/system/yassert.h>
#include <format>

namespace NPolexpr {

    enum class EVarId: ui32 {};

    class TConstId {
    public:
        constexpr TConstId(ui32 storage, ui32 idx) noexcept
            : Val{(storage << IDX_BITS) | idx}
        {
            Y_ABORT_UNLESS(idx < (1 << IDX_BITS));
            Y_ASSERT(storage < (1 << STORAGE_BITS));
        }

        constexpr ui32 GetStorage() const noexcept {
            return Val >> IDX_BITS;
        }
        constexpr ui32 GetIdx() const noexcept {
            return Val & ~(~0u << IDX_BITS);
        }

        constexpr ui32 GetRepr() const noexcept {
            return Val;
        }
        constexpr static TConstId FromRepr(ui32 repr) noexcept {
            return TConstId{repr};
        }

    private:
        constexpr TConstId(ui32 repr) noexcept
            : Val{repr}
        {
        }

    public:
        constexpr static ui32 IDX_BITS = 25;
        constexpr static ui32 STORAGE_BITS = 4;

    private:
        ui32 Val;
    };

    class TFuncId {
    public:

        constexpr TFuncId(auto arity, auto idx)
            : Val{(ui32(arity) << IDX_BITS) | ui32(idx)}
        {
            if (!(static_cast<ui32>(idx) < (1 << IDX_BITS))) [[unlikely]]
                throw std::runtime_error(std::format("Bad function id {}", idx));
            if (!(static_cast<ui32>(arity) < (1 << ARITY_BITS))) [[unlikely]]
                throw std::runtime_error(std::format("Too many arguments for function {}: {}/{}", idx, arity, (1 << ARITY_BITS) - 1));
        }

        constexpr ui16 GetArity() const noexcept {
            return static_cast<ui16>(Val >> IDX_BITS);
        }
        constexpr ui32 GetIdx() const noexcept {
            return Val & ~(~0u << IDX_BITS);
        }

        constexpr ui32 GetRepr() const noexcept {
            return Val;
        }
        constexpr static TFuncId FromRepr(ui32 repr) noexcept {
            return TFuncId{repr};
        }

    private:
        constexpr TFuncId(ui32 repr) noexcept
            : Val{repr}
        {
        }

    public:
        constexpr static ui32 IDX_BITS = 13;
        constexpr static ui32 ARITY_BITS = 16;

    private:
        ui32 Val;
    };

}
