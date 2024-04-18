#include "licenses_conf.h"

#include <devtools/ymake/common/npath.h>
#include <devtools/ymake/diag/dbg.h>
#include <devtools/ymake/diag/manager.h>
#include <devtools/ymake/spdx.h>

#include <library/cpp/digest/md5/md5.h>
#include <library/cpp/json/fast_sax/parser.h>
#include <library/cpp/json/json_reader.h>

#include <util/generic/iterator_range.h>
#include <util/generic/yexception.h>
#include <util/memory/blob.h>
#include <util/stream/file.h>
#include <util/string/builder.h>

namespace {
    void ParseRestrictedSection(const NJson::TJsonValue& propGroup, const TString& propName, THashMap<TString, TLicenseGroup>& licenseGroups) {
        if (!propGroup.IsMap()) {
            YConfErr(Misconfiguration) << "License property section with name " << propName << " should be a map." << Endl;
            return;
        }
        for (const auto& [license, linkingTypes] : propGroup.GetMap()) {
            if (!linkingTypes.IsMap()) {
                YConfErr(Misconfiguration) << "Linking types section by path " << propName << "." << license << "should be a map." << Endl;
                continue;
            }
            for (const auto& [linkingType, properties] : linkingTypes.GetMap()) {
                if (!properties.IsArray()) {
                    YConfErr(Misconfiguration) << "List of properties by path " << propName << "." << license << "." << linkingType << "should be an array." << Endl;
                    continue;
                }
                if (linkingType == "default") {
                    for (const auto& property : properties.GetArray()) {
                        if (!property.IsString()) {
                            YConfErr(Misconfiguration) << "Property type by path " << propName << "."  << license << "." << linkingType << " should be a string." << Endl;
                            continue;
                        }
                        licenseGroups[property.GetString()].Static.push_back(license);
                        licenseGroups[property.GetString()].Dynamic.push_back(license);
                    }
                } else if(linkingType == "static") {
                    for (const auto& property : properties.GetArray()) {
                        if (!property.IsString()) {
                            YConfErr(Misconfiguration) << "Property type by path " << propName << "."  << license << "." << linkingType << " should be a string." << Endl;
                            continue;
                        }
                        licenseGroups[property.GetString()].Static.push_back(license);
                    }
                } else if (linkingType == "dynamic") {
                    for (const auto& property : properties.GetArray()) {
                        if (!property.IsString()) {
                            YConfErr(Misconfiguration) << "Property type by path " << propName << "."  << license << "." << linkingType << " should be a string." << Endl;
                            continue;
                        }
                        licenseGroups[property.GetString()].Dynamic.push_back(license);
                    }
                } else {
                    YConfErr(Misconfiguration) << "Linking type " << linkingType << " is not supported. Linking type should be static, dynamic or default." << Endl;
                }
            }
        }
    }

    void ParseStandardSection(const NJson::TJsonValue& propGroup, const TString& propName, THashMap<TString, TLicenseGroup>& licenseGroups) {
        if (!propGroup.IsMap()) {
            YConfErr(Misconfiguration) << "License property section with name " << propName << " should be a map." << Endl;
            return;
        }
        auto& licenseGroup = licenseGroups[propName];
        for (const auto& [linkingType, licenses] : propGroup.GetMap()) {
            if (!licenses.IsArray()) {
                YConfErr(Misconfiguration) << "List of licenses by path " << propName << "." << linkingType << "should be an array." << Endl;
                continue;
            }
            if (linkingType == "default") {
                for (const auto& license : licenses.GetArray()) {
                    if (!license.IsString()) {
                        YConfErr(Misconfiguration) << "License type by path " << propName << "." << linkingType << " should be a string." << Endl;
                        continue;
                    }
                    licenseGroup.Static.push_back(license.GetString());
                    licenseGroup.Dynamic.push_back(license.GetString());
                }
            } else if (linkingType == "static") {
                for (const auto& license : licenses.GetArray()) {
                    if (!license.IsString()) {
                        YConfErr(Misconfiguration) << "License type by path " << propName << "." << linkingType << " should be a string." << Endl;
                        continue;
                    }
                    licenseGroup.Static.push_back(license.GetString());
                }
            } else if (linkingType == "dynamic") {
                for (const auto& license : licenses.GetArray()) {
                    if (!license.IsString()) {
                        YConfErr(Misconfiguration) << "License type by path " << propName << "." << linkingType << " should be a string." << Endl;
                        continue;
                    }
                    licenseGroup.Dynamic.push_back(license.GetString());
                }
            } else {
                YConfErr(Misconfiguration) << "Linking type " << linkingType << " is not supported. Linking type should be static, dynamic or default." << Endl;
            }
        }
    }
}

void LoadLicenses(const NJson::TJsonValue& json, THashMap<TString, TLicenseGroup>& licenseGroups) {
    if (!json.IsMap()) {
        YConfErr(Misconfiguration) << "License conf root section should be a map." << Endl;
        return;
    }
    for (const auto& [propName, propGroup] : json.GetMap()) {
        if (propName == "restricted") {
            ParseRestrictedSection(propGroup, propName, licenseGroups);
        } else {
            ParseStandardSection(propGroup, propName, licenseGroups);
        }
    }
}

THashMap<TString, TLicenseGroup> LoadLicenses(const TVector<TFsPath>& configs) {
    THashMap<TString, TLicenseGroup> licenseGroups;
    for (const TFsPath& path : configs) {
        YDIAG(Conf) << "Reading license conf file: " << path << Endl;
        try {
            TFileInput fileInput(path);
            NJson::TJsonValue json;
            ReadJsonTree(&fileInput, true, &json, true);
            LoadLicenses(json, licenseGroups);
        } catch (const TFileError& e) {
            YConfErr(BadFile) << "Error while reading license config " << path << ": " << e.what() << Endl;
        } catch (const NJson::TJsonException& e) {
            YConfErr(BadFile) << "License config " << path << " is invalid. Json syntax error: " << e.what() << Endl;
        }
    }
    return licenseGroups;
}
