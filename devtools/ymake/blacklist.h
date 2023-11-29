#pragma once

#include <devtools/common/blacklist/blacklist.h>

#include <library/cpp/digest/md5/md5.h>

#include <util/folder/path.h>
#include <util/generic/strbuf.h>

class TBlackList: public NBlacklist::TSvnBlacklist {
public:
    using EParserErrorKind = NBlacklist::EParserErrorKind;

    const TString* IsValidPath(TStringBuf path) const;
    void OnParserError(EParserErrorKind kind, TStringBuf path, TStringBuf file) override;
    void Load(const TFsPath& sourceRoot, const TVector<TStringBuf>& lists, MD5& confData);
};
