#pragma once

#include <library/cpp/json/json_value.h>

#include <util/folder/path.h>
#include <util/generic/hash.h>
#include <util/generic/strbuf.h>
#include <util/generic/string.h>
#include <util/generic/vector.h>

class MD5;

struct TLicenseGroup {
    TVector<TString> Static;
    TVector<TString> Dynamic;
};

/// @brief loads all licenses configuration for current build
THashMap<TString, TLicenseGroup> LoadLicenses(const TVector<TFsPath>& configs, MD5& confData);

void LoadLicenses(const NJson::TJsonValue& json, THashMap<TString, TLicenseGroup>& licenseGroups);
