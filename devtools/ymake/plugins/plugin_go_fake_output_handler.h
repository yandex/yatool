#pragma once

#include <devtools/ymake/lang/plugin_facade.h>

#include <util/generic/strbuf.h>

class TBuildConfiguration;

namespace NYMake {
    namespace NPlugins {
        class TPluginGoFakeOutputHandler: public TMacroImpl {
        public:
            void Execute(TPluginUnit& unit, const TVector<TStringBuf>& params, TVector<TSimpleSharedPtr<TMacroCmd>>* result = nullptr) override;

            static void RegisterMacro(TBuildConfiguration& conf);
        };
    }
}

