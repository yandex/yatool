#pragma once

#include <library/cpp/containers/comptrie/comptrie_builder.h>

#include <util/folder/path.h>
#include <util/generic/string.h>

class MD5;

/// @brief loads autoinclude paths for current build
TCompactTrieBuilder<char, TString> LoadAutoincludes(const TVector<TFsPath>& configs, TStringBuf lintersMakeFilename, MD5& confData);
