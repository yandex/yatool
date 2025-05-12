#pragma once

#include "ya_conf_json_types.h"

#include <util/generic/string.h>
#include <util/folder/path.h>

namespace NYa {
    THashMap<TString, TString> Environ(bool(*filter)(const TString& key, const TString& val) = nullptr);

    class IConfig : public TNonCopyable {
    public:
        virtual TFsPath HomeDir() const = 0;
        virtual TFsPath MiscRoot() const = 0;
        virtual TFsPath LogsRoot() const = 0;
        virtual TFsPath TmpRoot() const= 0;
        virtual TFsPath ToolRoot() const = 0;
        virtual int ToolCacheVersion() const = 0;
        // Get arcadia root from a current working dir and only as a last resort use YA_SOURCE_ROOT.
        virtual TFsPath ArcadiaRoot() const = 0;
        virtual const NYaConfJson::TYaConf& YaConf() const = 0;
        virtual const NYaConfJson::TFormula& YaConfFormula(const TFsPath& arcadiaPath) const = 0;
        virtual ~IConfig() = default;
    };

    const IConfig& GetConfig();

    namespace NPrivate {
        // For test purpose return non-singleton object
        THolder<IConfig> GetConfigImpl();
    }
}

