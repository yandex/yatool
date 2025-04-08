#include "platform_map.h"
#include "platform.h"

#include <library/cpp/digest/md5/md5.h>
#include <library/cpp/json/json_reader.h>

#include <util/generic/algorithm.h>
#include <util/string/builder.h>
#include <util/string/cast.h>
#include <util/string/split.h>

namespace NYa {
    TPlatformMap MappingFromJsonString(TStringBuf mapping) {
        NJson::TJsonValue jsonConfig;
        try {
            NJson::ReadJsonTree(mapping, &jsonConfig, true);
        } catch (const yexception& e) {
            throw TPlatformMappingError() << "Cannot parse json: " << e.what();
        }

        NJson::TJsonValue byPlatform;
        if (!jsonConfig.GetValue("by_platform", &byPlatform) || !byPlatform.IsMap()) {
            throw TPlatformMappingError() << "'by_platform' is not found or it's value is not an object";
        }

        TPlatformMap result;
        for (const auto& [platform, dest] : byPlatform.GetMap()) {
            NJson::TJsonValue uri;
            if (!dest.GetValue("uri", &uri) || !uri.IsString()) {
                throw TPlatformMappingError() << "Wrong platform '" << platform << "' description: 'uri' is not found or it's value is not a string";
            }
            TString canonizedPlatform = NYa::CanonizePlatform(platform).AsString();
            NJson::TJsonValue stripPrefixVal;
            unsigned long long stripPrefix = 0;
            if (dest.GetValue("strip_prefix", &stripPrefixVal)) {
                if (!stripPrefixVal.IsUInteger()) {
                    throw TPlatformMappingError() << "Wrong platform '" << platform << "' description: 'strip_prefix' value should be integral";
                }
                stripPrefix = stripPrefixVal.GetUInteger();
                if (stripPrefix > Max<ui16>()) {
                    throw TPlatformMappingError() << "Wrong platform '" << platform << "' description: 'strip_prefix' value of " << stripPrefix << " is too big";
                }
            }
            result.emplace(canonizedPlatform, TResourceDesc{uri.GetString(), static_cast<ui32>(stripPrefix)});
        }

        if (result.empty()) {
            throw TPlatformMappingError() << "Platform mapping is empty";
        }

        return result;
    }

    TString ResourceDirName(const NYa::TResourceDesc& desc) {
        return ResourceDirName(desc.Uri, desc.StripPrefix);
    }

    TString HttpResourceDirName(TStringBuf uri) {
        constexpr TStringBuf integrityPrefix{"integrity="};

        size_t hashIndex = uri.find('#');
        if (hashIndex == std::string::npos) {
            throw yexception() << "Wrong uri. No '#' symbol is found: " << uri;
        } else if (hashIndex == uri.size() - 1) {
            throw yexception() << "Wrong uri. '#' symbol must be followed by resource meta: " << uri;
        }

        TStringBuf metaStr = uri.substr(hashIndex + 1);
        if (metaStr.find('=') == std::string::npos) {
            // backward compatibility with old md5-scheme
            return ToString(metaStr);
        }

        for (const auto& it : StringSplitter(metaStr).Split('&')) {
            TStringBuf pair = it.Token();
            if (!pair.StartsWith(integrityPrefix)) {
                continue;
            }
            TStringBuf integrity = pair.substr(integrityPrefix.size());
            return MD5::Calc(integrity);
        }

        throw yexception() << "Wrong uri. Meta should include integrity: " << uri;
    }

    TString DockerResourceDirName(TStringBuf uri, TStringBuf dockerPrefix) {
        TStringBuf digest = uri.substr(uri.rfind(":") + 1);
        TStringBuf fqdn = uri.substr(dockerPrefix.size() + 2);
        fqdn = fqdn.substr(0, fqdn.find('/'));
        return ToString(fqdn) + "-" + ToString(digest.substr(0, 12));
    }

    TString ResourceDirName(TStringBuf uri, ui32 stripPrefix) {
        constexpr TStringBuf sbrPrefix{"sbr:"};
        constexpr TStringBuf httpPrefix{"http:"};
        constexpr TStringBuf httpsPrefix{"https:"};
        constexpr TStringBuf dockerPrefix{"docker:"};

        TString dirName;
        if (uri.StartsWith(sbrPrefix)) {
            dirName = uri.substr(sbrPrefix.size());
        } else if (uri.StartsWith(dockerPrefix)) {
            dirName = DockerResourceDirName(uri, dockerPrefix);
        } else if (uri.StartsWith(httpPrefix) || uri.StartsWith(httpsPrefix)) {
            dirName = HttpResourceDirName(uri);
        } else {
            throw yexception() << "Wrong uri. No known schema is found: " << uri;
        }
        if (stripPrefix) {
            dirName += "-" + ToString(stripPrefix);
        }
        return dirName;
    }

    TString ResourceVarName(TStringBuf baseName, const NYa::TResourceDesc& desc) {
        return ResourceVarName(baseName, desc.Uri, desc.StripPrefix);
    }

    namespace {
        TString EncodeUriForVar(TStringBuf uri) {
            if (uri.size() < 48) {
                // This allows up to 160bit hex + 7 chars for schema + ':'
                auto good = AllOf(uri, [](char c) {
                    return isalnum(c) || c == ':' || c == '_' || c == '-';
                });
                if (good) {
                    return ToString(uri);
                }
            }
            return ToString(THash<TStringBuf>()(uri));
        }
    }

    TString ResourceVarName(TStringBuf baseName, TStringBuf uri, ui32 stripPrefix) {
        auto varName = TStringBuilder{} << baseName << "-" << EncodeUriForVar(uri);
        if (stripPrefix) {
            varName << "-" << stripPrefix;
        }
        return varName;
    }
}
