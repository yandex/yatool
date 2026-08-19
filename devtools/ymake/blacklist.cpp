#include "blacklist.h"

#include <devtools/ymake/common/npath.h>
#include <devtools/ymake/diag/manager.h>

#include <library/cpp/digest/md5/md5.h>

#include <util/folder/path.h>
#include <util/generic/strbuf.h>
#include <util/generic/string.h>
#include <util/generic/yexception.h>
#include <util/stream/file.h>
#include <util/string/builder.h>

const TString* TBlackList::IsValidPath(TStringBuf path) const {
    if (NPath::IsTypedPath(path)) {
        path = NPath::CutType(path);
    }
    return NBlacklist::TSvnBlacklist::IsValidPath(path);
}

void TBlackList::Load(const TFsPath& sourceRoot, const TVector<TStringBuf>& lists, MD5& confHash) {
    Clear();
    for (const auto path : lists) {
        try {
            TFileInput file(sourceRoot / path);
            TString content = file.ReadAll();
            confHash.Update(content.data(), content.size());
            LoadFromString(content, path);
        } catch (const TFileError& e) {
            YConfErr(BadFile) << "Error while reading blacklist file " << path << ": " << e.what() << Endl;
        }
    }
}

void TBlackList::OnParserDiagnostic(const NBlacklist::TBlacklistDiagnostic& diagnostic) {
    if (diagnostic.Kind == EParserErrorKind::AbsolutePath || diagnostic.Kind == EParserErrorKind::InvalidPath) {
        NBlacklist::TSvnBlacklist::OnParserDiagnostic(diagnostic);
        return;
    }

    const auto statusName = [](NBlacklist::EBlacklistRuleStatus status) -> TStringBuf {
        return status == NBlacklist::EBlacklistRuleStatus::Blacklisted ? "positive" : "inverse";
    };
    TStringBuilder message;
    message << (diagnostic.Kind == EParserErrorKind::ConflictingRule ? "Conflicting" : "Duplicate")
            << " blacklist rule for normalized path [[alt1]]" << diagnostic.Path << "[[rst]]: [[imp]]"
            << diagnostic.CurrentSource.File << ':' << diagnostic.CurrentSource.Line << "[[rst]] declares "
            << statusName(diagnostic.CurrentStatus) << " rule [[alt1]]" << diagnostic.CurrentSource.RawRule
            << "[[rst]]; earlier declaration at [[imp]]" << diagnostic.PreviousSource.File << ':'
            << diagnostic.PreviousSource.Line << "[[rst]] is " << statusName(diagnostic.PreviousStatus)
            << " rule [[alt1]]" << diagnostic.PreviousSource.RawRule << "[[rst]].";

    if (diagnostic.Severity == NBlacklist::EBlacklistDiagnosticSeverity::Error) {
        YConfErr(Syntax) << message << " The blacklist file will not be applied." << Endl;
    } else {
        YConfWarn(Syntax) << message << " The later declaration takes precedence." << Endl;
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
