#pragma once

#include <library/cpp/containers/comptrie/comptrie_builder.h>

#include <util/folder/path.h>
#include <util/generic/string.h>

const TString LINTERS_MAKE_INC = TString("linters.make.inc");

TVector<TString> LoadAutoincludeRoots(const TVector<TFsPath>& configs);
TCompactTrieBuilder<char, TString> BuildAutoincludeTrie(const TVector<TString>& roots);
TString FindAutoinclude(const TCompactTrieBuilder<char, TString>& trie, TStringBuf dir);
