#pragma once

#include <util/generic/hash.h>
#include <util/generic/string.h>
#include <util/generic/vector.h>
#include <util/system/platform.h>


namespace NYa {
    struct TLegacyPlatform {
        TString Os;
        TString Arch;
    };

    class TCanonizedPlatform {
    public:
        constexpr static TStringBuf ANY_PLATFORM = "any";
        constexpr static TStringBuf DEFAULT_ARCH = "x86_64";
        constexpr static TStringBuf PLATFORM_SEP = "-";

        TCanonizedPlatform();
        explicit TCanonizedPlatform(TStringBuf canonizedString);
        TCanonizedPlatform(TStringBuf os, TStringBuf arch);
        TCanonizedPlatform(const TCanonizedPlatform& other) = default;
        TCanonizedPlatform(TCanonizedPlatform&& other) = default;

        TCanonizedPlatform& operator=(const TCanonizedPlatform& other) = default;
        TCanonizedPlatform& operator=(TCanonizedPlatform&& other) noexcept = default;

        bool operator==(const TCanonizedPlatform& other) const = default;

        inline size_t Hash() const noexcept {
            return CombineHashes(THash<TString>{}(Os_), THash<TString>{}(Arch_));
        }

        TString AsString() const;
        inline TString Os() const {return Os_;}
        inline TString Arch() const {return Arch_;}
    private:
        void Check() const;

        TString Os_;
        TString Arch_;
    };

    class TUnsupportedPlatformError: public yexception {
    public:
        TUnsupportedPlatformError(TStringBuf platform) {
            *this << "'" << platform << "' is not supported";
        }
    };

    class TPlatformSpecificationError: public yexception {
    public:
        using yexception::yexception;
    };
}

template<>
struct THash<NYa::TCanonizedPlatform> {
    inline size_t operator()(const NYa::TCanonizedPlatform& val) const noexcept {
        return val.Hash();
    }
};

namespace NYa {
    using TPlatformReplacements = THashMap<TCanonizedPlatform, TVector<TCanonizedPlatform>>;

    const TPlatformReplacements& DefaultPlatformReplacements();

    TString CurrentOs();
    TString CurrentArchitecture();
    TLegacyPlatform CurrentPlatform();
    bool IsDarwinArm64();
    TCanonizedPlatform MyPlatform();

    TCanonizedPlatform CanonizePlatform(TString platform);

    // Note: If platformReplacements is null default PLATFORM_REPLACEMENTS will be used. To disable platform replacements pass empty hash
    TString MatchPlatform(
        const TCanonizedPlatform& expect,
        const TVector<TString>& platforms,
        const TPlatformReplacements* platformReplacements = nullptr);
}
