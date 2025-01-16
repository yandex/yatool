#pragma once

#include <devtools/libs/yaplatform/platform.h>
#include <devtools/libs/yaplatform/platform_map.h>
#include <devtools/ya/cpp/lib/edl/common/loaders.h>

namespace NYa::NEdl {
    template <>
    class TLoader<TLegacyPlatform> : public TLoaderForRef<TLegacyPlatform> {
    public:
        using TLoaderForRef<TLegacyPlatform>::TLoaderForRef;

        inline void EnsureMap() override {
        }

        inline TLoaderPtr AddMapValue(TStringBuf key) override {
            if (key == "os") {
                return GetLoader(this->ValueRef_.Os);
            }
            if (key == "arch") {
                return GetLoader(this->ValueRef_.Arch);
            }

            ythrow TLoaderError() << "Unexpected map key '" << key << "'";
        }
    };

    template <>
    class TLoader<TCanonizedPlatform> : public TLoaderForRef<TCanonizedPlatform> {
    public:
        using TLoaderForRef<TCanonizedPlatform>::TLoaderForRef;

        inline void SetValue(const TStringBuf val) override {
            this->ValueRef_ = TCanonizedPlatform(val);
        }
    };

    template <>
    class TLoader<TResourceDesc> : public TLoaderForRef<TResourceDesc> {
    public:
        using TLoaderForRef<TResourceDesc>::TLoaderForRef;

        void EnsureMap() override {
        }

        TLoaderPtr AddMapValue(TStringBuf key) override {
            if (key == "uri") {
                return GetLoader(this->ValueRef_.Uri);
            }
            if (key == "strip_prefix") {
                return GetLoader(this->ValueRef_.StripPrefix);
            }

            ythrow TLoaderError() << "Unexpected map key '" << key << "'";
        }
    };
}
