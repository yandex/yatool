#pragma once

#include "incldep.h"

#include <devtools/ymake/symbols/symbols.h>

struct TParsedFile {
    enum class EKind {
        Import,
        Include
    };

    template <typename TStringType>
    TParsedFile(TStringType&& parsedFile, EKind kind)
        : ParsedFile(std::forward<TStringType>(parsedFile))
        , Kind(kind)
    {
    }

    bool operator==(const TParsedFile& other) const {
        return other.ParsedFile == ParsedFile && other.Kind == Kind;
    }

    TString ParsedFile;
    EKind Kind;
};

class TGoImportParser {
public:
    void Parse(IContentHolder& file, TVector<TParsedFile>& parsedFiles);
};

