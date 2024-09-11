#pragma once

#include "empty_parser.h"

struct TRagelInclude {
    enum class EKind {
        Native,
        Cpp,
    };

    template <typename TStringType>
    TRagelInclude(TStringType&& include, EKind kind)
        : Include(std::forward<TStringType>(include))
        , Kind(kind)
    {
    }

    bool operator==(const TRagelInclude& other) const {
        return other.Include == Include && other.Kind == Kind;
    }

    TString Include;
    EKind Kind;
};

class TRagelIncludesParser: public TIncludesParserBase {
public:
    TRagelIncludesParser();
    void Parse(IContentHolder& file, TVector<TRagelInclude>& includes) const;

protected:
    void ScanIncludes(TVector<TRagelInclude>& nativeIncludes, IContentHolder& incFile) const;
    bool ParseNativeIncludeLine(TStringBuf& lineBuf, TString& inc, IContentHolder& incFile) const override;
};
