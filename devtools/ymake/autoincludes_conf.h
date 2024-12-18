#pragma once

#include <library/cpp/containers/comptrie/comptrie_builder.h>

#include <util/folder/path.h>
#include <util/generic/string.h>

class MD5;
const TString LINTERS_MAKE_INC = TString("linters.make.inc");

/// @brief loads autoinclude paths for current build
TCompactTrieBuilder<char, TString> LoadAutoincludes(const TVector<TFsPath>& configs, MD5& confData);
