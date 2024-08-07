#include "blacklist.h"

#include <devtools/ymake/common/npath.h>
#include <devtools/ymake/diag/manager.h>

#include <library/cpp/digest/md5/md5.h>

#include <util/folder/path.h>
#include <util/generic/strbuf.h>
#include <util/generic/string.h>
#include <util/generic/yexception.h>
#include <util/stream/file.h>

const TString* TBlackList::IsValidPath(TStringBuf path) const {
    if (NPath::IsTypedPath(path)) {
        path = NPath::CutType(path);
    }
    return NBlacklist::TSvnBlacklist::IsValidPath(path);
}

void TBlackList::Load(const TFsPath& sourceRoot, const TVector<TStringBuf>& lists, MD5& confHash, MD5& anotherConfHash, bool addToAnother) {
    Clear();
    for (const auto path : lists) {
        try {
            TFileInput file(sourceRoot / path);
            TString content = file.ReadAll();
            confHash.Update(content.data(), content.size());
            if (addToAnother) {
                anotherConfHash.Update(content.data(), content.size());
            }
            LoadFromString(content, path);
        } catch (const TFileError& e) {
            YConfErr(BadFile) << "Error while reading blacklist file " << path << ": " << e.what() << Endl;
        }
    }
}

void TBlackList::OnParserError(EParserErrorKind kind, TStringBuf path, TStringBuf file) {
    switch (kind) {
        case EParserErrorKind::AbsolutePath:
            YConfWarn(Syntax) << "Absolute path in black list file [[imp]]"
                << ArcPath(file) << "[[rst]]. This path [[alt1]]" << path
                << "[[rst]] << will be skipped." << Endl;
            break;
        case EParserErrorKind::InvalidPath:
            YConfWarn(Syntax) << "Invalid path in black list file [[imp]]"
                << ArcPath(file) << "[[rst]]. This path [[alt1]]" << path
                << "[[rst]] will be skipped." << Endl;
            break;
        default:
            break;
    }
}
