#pragma once

#include <util/folder/path.h>
#include <util/folder/pathsplit.h>


constexpr TStringBuf SRC_DIR = "$S"sv;
constexpr TStringBuf BLD_DIR = "$B"sv;
constexpr TStringBuf UNK_DIR = "$U"sv;
constexpr TStringBuf DUMMY_FILE = "$B/build/__dummy__"sv;

constexpr TStringBuf DATA_ARC_PREFIX = "arcadia/"sv;
constexpr TStringBuf DATA_SBR_PREFIX = "sbr://"sv;
constexpr TStringBuf DATA_EXT_PREFIX = "ext:"sv;

namespace NPath {

using TSplitTraits = TPathSplitTraitsLocal;
using TSplit = TPathSplitUnix;

enum ERoot : ui8 {
    Source,
    Build,
    Unset,
    Link
};

constexpr char SPECSYM = '$';
constexpr char PATH_SEP = TPathSplitTraitsUnix::MainPathSep;
constexpr char WIN_PATH_SEP = TPathSplitTraitsWindows::MainPathSep;
constexpr char ENUM_TO_TYPE[] = {'S', 'B', 'U', 'L'};
constexpr size_t ROOT_LENGTH = 2;                 // $ + Type
constexpr size_t PREFIX_LENGTH = ROOT_LENGTH + 1; // $ + Type + /
constexpr char EXT_PATH_L_DELIM = '(';
constexpr char EXT_PATH_R_DELIM = ')';

constexpr const TStringBuf PATH_SEP_S(&PATH_SEP, 1);

}
