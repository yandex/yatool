#include "config.h"
#include "logger.h"
#include "ya_conf_json_loaders.h"

#include <devtools/ya/app_config/lib/config.h>
#include <devtools/ya/cpp/lib/edl/json/from_json.h>

#include <library/cpp/resource/resource.h>

#include <util/folder/path.h>
#include <util/generic/algorithm.h>
#include <util/generic/lazy_value.h>
#include <util/generic/hash_set.h>
#include <util/generic/singleton.h>
#include <util/stream/file.h>
#include <util/string/cast.h>
#include <util/string/type.h>
#include <util/system/env.h>
#include <util/system/user.h>

#ifndef _win32_
    #include <pwd.h>
#endif
#include <stdlib.h>

extern char **environ;

template<>
struct THash<TFsPath> {
    inline size_t operator()(const TFsPath& t) const noexcept {
        return THash<TString>()(t.GetPath());
    }
};

namespace NYa {
    const TString YA_CONF_JSON_FILE = "ya.conf.json";
    const TFsPath YA_CONF_JSON_PATH = TFsPath("build") / YA_CONF_JSON_FILE;
    const TString RES_FS_ROOT = "resfs/file";
    const TString YA_CONF_JSON_RESOURCE_KEY = RES_FS_ROOT + "/ya.conf.json";

    THashMap<TString, TString> Environ(bool(*filter)(const TString& key, const TString& val)) {
        THashMap<TString, TString> result{};
        char** curVar = environ;
        while (*curVar) {
            TString key{*curVar};
            TString val{};
            if (const size_t pos = key.find('='); pos != std::string::npos) {
                val = key.substr(pos + 1);
                key.resize(pos);
            }
            if (!filter || filter(key, val)) {
                result.emplace(key, val);
            }
            ++curVar;
        }
        return result;
    }

    namespace NPrivate {
        bool IsArcadiaRoot(const TFsPath& path) {
            return (path / ".arcadia.root").Exists() || (path / YA_CONF_JSON_PATH).Exists();
        }

        class TConfig : public IConfig {
        public:
            TConfig() {
                HomeDir_ = [this]() {return HomeDirImpl();};
                MiscRoot_ = [this]() {return MiscRootImpl();};
                LogsRoot_ = [this]() {return LogsRootImpl();};
                TmpRoot_ = [this]() {return TmpRootImpl();};
                ToolRoot_ = [this]() {return ToolRootImpl();};
                ToolCacheVersion_ = [this]() {return ToolCacheVersionImpl();};
                ArcadiaRoot_ = [this]() {return ArcadiaRootImpl();};
                YaConf_ = [this]() { return YaConfImpl();};
            }

            TFsPath HomeDir() const override  {
                return HomeDir_.GetRef();
            }

            TFsPath MiscRoot() const override  {
                return MiscRoot_.GetRef();
            }

            TFsPath LogsRoot() const override  {
                return LogsRoot_.GetRef();
            }

            TFsPath TmpRoot() const override  {
                return TmpRoot_.GetRef();
            }

            TFsPath ToolRoot() const override  {
                return ToolRoot_.GetRef();
            }

            int ToolCacheVersion() const override  {
                return ToolCacheVersion_.GetRef();
            }

            TFsPath ArcadiaRoot() const override {
                return ArcadiaRoot_.GetRef();
            }

            const NYaConfJson::TYaConf& YaConf() const override {
                return YaConf_.GetRef();
            }

            const NYaConfJson::TFormula& YaConfFormula(const TFsPath& arcadiaPath) const override {
                if (const NYaConfJson::TFormula* ptr = FormulaCache_.FindPtr(arcadiaPath)) {
                    return *ptr;
                }
                FormulaCache_.emplace(arcadiaPath, ConfigImpl<NYaConfJson::TFormula>(arcadiaPath));
                return FormulaCache_[arcadiaPath];
            }

        private:
            TFsPath HomeDirImpl() {
#ifdef _win32_
                if (TFsPath home = GetEnv("USERPROFILE")) {
                    return home;
                }
                if (TFsPath drive = GetEnv("HOMEDRIVE")) {
                    if (TFsPath path = GetEnv("HOMEPATH")) {
                        return drive / path;
                    }
                }
#else
                // When executed in yp pods, $HOME will point to the current snapshot work dir.
                // Read current home directory from /etc/passwd at first
                passwd* pw = nullptr;
                pw = getpwuid(getuid());
                if (pw) {
                    return pw->pw_dir;
                }
                if (TFsPath home = GetEnv("HOME")) {
                    return home;
                }
#endif
                throw yexception() << "Cannot find home dir";
            }

            TFsPath MiscRootImpl() {
                if (TFsPath root = GetEnv("YA_CACHE_DIR")) {
                    return root;
                }
                return HomeDir() / ".ya";
            }

            TFsPath LogsRootImpl() {
                if (TFsPath root = GetEnv("YA_LOGS_ROOT")) {
                    return root;
                }
                return MiscRoot() / "logs";
            }

            TFsPath TmpRootImpl() {
                return MiscRoot() / "tmp";
            }

            int ToolCacheVersionImpl() {
                TString userName = GetUsername();
                bool robotEnv = userName.Contains("sandbox") || userName.Contains("teamcity");
                if (ArcadiaRoot() && !robotEnv && IsTrue(GetEnv("YA_TC", "1"))) {
                    return 4;
                }
                return 3;
            }

            TFsPath ToolRootImpl() {
                TFsPath toolRoot;
                if (TFsPath root = GetEnv("YA_CACHE_DIR_TOOLS")) {
                    toolRoot = root;
                } else {
                    toolRoot = MiscRoot() / "tools";
                }
                int cacheVersion = ToolCacheVersion();
                toolRoot /= "v" + IntToString<10>(cacheVersion);
                return toolRoot;
            }

            TFsPath ArcadiaRootImpl() {
                TFsPath path{TFsPath::Cwd()};
                while (true) {
                    if (IsArcadiaRoot(path)) {
                        return path;
                    }
                    TFsPath parent = path.Parent();
                    if (parent == path) {
                        break;
                    } else {
                        path = parent;
                    }
                }
                if (TFsPath root = GetEnv("YA_SOURCE_ROOT")) {
                    if (IsArcadiaRoot(root)) {
                        return root;
                    }
                }
                return {};
            }

            NYaConfJson::TYaConf YaConfImpl() {
                TVector<TFsPath> configDirs{};
                if (TFsPath yaToolConf = GetEnv("YA_TOOLS_CONFIG_PATH")) {
                    configDirs.push_back(yaToolConf / YA_CONF_JSON_FILE);
                }

                if (TFsPath extraPath = NConfig::ExtraConfRoot) {
                    configDirs.push_back(extraPath / YA_CONF_JSON_FILE);
                }
                configDirs.push_back(YA_CONF_JSON_PATH);
                return ConfigImpl<NYaConfJson::TYaConf>(configDirs, YA_CONF_JSON_RESOURCE_KEY);
            }

            template <class T>
            T ConfigImpl(const TVector<TFsPath> arcadiaPaths, const TString& resourcePath) const {
                TFsPath arcadiaRoot = ArcadiaRoot();
                T value{};

                if (!arcadiaRoot) {
                    const TString resFsPath = RES_FS_ROOT + "/" + resourcePath;
                    DEBUG_LOG << "Load config from resource: " << resourcePath << "\n";
                    TString data = NResource::Find(resourcePath);
                    NYa::NEdl::LoadJson(data, value);
                    return value;
                }

                for (const auto& arcadiaPath : arcadiaPaths) {
                    const TFsPath path = arcadiaRoot / arcadiaPath;

                    if (!path.Exists()) {
                        DEBUG_LOG << "Cannot load config (file doesn't exist): " << path << "\n";
                        continue;
                    }

                    DEBUG_LOG << "Load config from: " << path << "\n";
                    NYa::NEdl::LoadJsonFromFile(path.GetPath(), value);
                    return value;
                }

                throw yexception() << "Cannot find config " << YA_CONF_JSON_FILE;
            }

            template <class T>
            T ConfigImpl(const TFsPath& arcadiaPath) const {
                TVector<TFsPath> arcadiaPaths = {arcadiaPath};
                return ConfigImpl<T>(arcadiaPaths, arcadiaPath.GetPath());
            }
        private:
            TLazyValue<TFsPath> HomeDir_;
            TLazyValue<TFsPath> MiscRoot_;
            TLazyValue<TFsPath> LogsRoot_;
            TLazyValue<TFsPath> TmpRoot_;
            TLazyValue<TFsPath> ToolRoot_;
            TLazyValue<int> ToolCacheVersion_;
            TLazyValue<TFsPath> ArcadiaRoot_;
            TLazyValue<NYaConfJson::TYaConf> YaConf_;
            mutable THashMap<TFsPath, NYaConfJson::TFormula> FormulaCache_{};
        };

        // For test purpose
        THolder<IConfig> GetConfigImpl() {
            return MakeHolder<NPrivate::TConfig>();
        }
    }

    const IConfig& GetConfig() {
        return *Singleton<NPrivate::TConfig>();
    }
}
