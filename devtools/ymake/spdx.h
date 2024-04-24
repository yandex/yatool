#pragma once

#include <devtools/ymake/diag/dbg.h>

#include <util/generic/fwd.h>

#include <bitset>

namespace NSPDX {

    struct TExpressionError: public TError {};

    using TPropSet = std::bitset<32>;

    enum class EPeerType {
        Static,
        Dynamic
    };

    struct TLicenseProps {
        TPropSet Static;
        TPropSet Dynamic;

        bool operator==(const TLicenseProps&) const noexcept = default;
        bool operator!=(const TLicenseProps&) const noexcept = default;

        const TPropSet& GetProps(EPeerType linkType) const noexcept {
            switch (linkType) {
                case EPeerType::Dynamic:
                    return Dynamic;
                case EPeerType::Static:
                    return Static;
            }
            Y_UNREACHABLE();
        }
    };

    TVector<TLicenseProps> ParseLicenseExpression(const THashMap<TString, TLicenseProps>& licenses, TStringBuf expr);

    using LicenseHandler = std::function<void(TStringBuf)>;
    void ForEachLicense(TStringBuf licenses, const LicenseHandler& handler);
    void ForEachLicense(const TVector<TString>& licenses, const LicenseHandler& handler);
}
