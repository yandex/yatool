#pragma once

#include <util/generic/hash.h>
#include <util/system/defaults.h>
#include <util/ysaveload.h>
#include <compare>

struct TFileElemId;
struct TCmdElemId;
struct TElemId {
    using TUnderlying = ui32;
    constexpr TElemId() noexcept: Value_() {}
    constexpr explicit TElemId(TUnderlying value) noexcept: Value_(value) {}
    constexpr explicit operator bool() const { return !!Value_; }
    constexpr TUnderlying Raw() const { return Value_; }
    constexpr std::strong_ordering operator<=>(const TElemId&) const noexcept = default;

private:
    TUnderlying Value_;
};

struct TFileElemId: public TElemId {
    using TElemId::TElemId;
    constexpr std::strong_ordering operator<=>(const TFileElemId&) const noexcept = default;
};

struct TCmdElemId: public TElemId {
    using TElemId::TElemId;
    constexpr std::strong_ordering operator<=>(const TCmdElemId&) const noexcept = default;
};

Y_FORCE_INLINE constexpr TElemId::TUnderlying RawElemId(TElemId elemId) {
    return elemId.Raw();
}

Y_FORCE_INLINE constexpr TFileElemId AssumeFile(TElemId elemId) {
    return TFileElemId(elemId.Raw());
}

Y_FORCE_INLINE constexpr TCmdElemId AssumeCmd(TElemId elemId) {
    return TCmdElemId(elemId.Raw());
}

struct TElemIdIdentityHash {
    size_t operator()(TElemId id) const noexcept {
        return id.Raw();
    }
};

struct TFileElemIdIdentityHash {
    size_t operator()(TFileElemId id) const noexcept {
        return id.Raw();
    }
};

struct TCmdElemIdIdentityHash {
    size_t operator()(TCmdElemId id) const noexcept {
        return id.Raw();
    }
};

template <>
struct THash<TElemId> {
    size_t operator()(TElemId id) const noexcept {
        return THash<ui32>()(id.Raw());
    }
};

template <>
struct THash<TFileElemId> {
    size_t operator()(TFileElemId id) const noexcept {
        return THash<ui32>()(id.Raw());
    }
};

template <>
struct THash<TCmdElemId> {
    size_t operator()(TCmdElemId id) const noexcept {
        return THash<ui32>()(id.Raw());
    }
};

template <>
class TSerializer<TElemId> {
public:
    static inline void Save(IOutputStream* rh, TElemId id) {
        ::Save(rh, RawElemId(id));
    }

    static inline void Load(IInputStream* rh, TElemId& id) {
        TElemId::TUnderlying rawId;
        ::Load(rh, rawId);
        id = TElemId(rawId);
    }
};

template <>
class TSerializer<TFileElemId> {
public:
    static inline void Save(IOutputStream* rh, TFileElemId id) {
        ::Save(rh, RawElemId(id));
    }

    static inline void Load(IInputStream* rh, TFileElemId& id) {
        TElemId::TUnderlying rawId;
        ::Load(rh, rawId);
        id = TFileElemId(rawId);
    }
};

template <>
class TSerializer<TCmdElemId> {
public:
    static inline void Save(IOutputStream* rh, TCmdElemId id) {
        ::Save(rh, RawElemId(id));
    }

    static inline void Load(IInputStream* rh, TCmdElemId& id) {
        TElemId::TUnderlying rawId;
        ::Load(rh, rawId);
        id = TCmdElemId(rawId);
    }
};

inline IOutputStream& operator<< (IOutputStream& out, TElemId elemId) {
    out << RawElemId(elemId);
    return out;
}

inline IOutputStream& operator<< (IOutputStream& out, TFileElemId elemId) {
    out << RawElemId(elemId);
    return out;
}

inline IOutputStream& operator<< (IOutputStream& out, TCmdElemId elemId) {
    out << RawElemId(elemId);
    return out;
}
