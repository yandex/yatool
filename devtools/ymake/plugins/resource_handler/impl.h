#pragma once

#include <devtools/ymake/lang/plugin_facade.h>

#include <util/system/src_location.h>

class TBuildConfiguration;

namespace NYMake {
    namespace NPlugins {
        class TPluginResourceHandler: public TMacroImpl {
        private:
            bool IsSemanticsRendering = false;

        public:
            TPluginResourceHandler(bool isSemanticsRendering)
                : IsSemanticsRendering(isSemanticsRendering)
            {
            }

            void Execute(TPluginUnit& unit, const TVector<TStringBuf>& params) override;

            static void RegisterMacro(TBuildConfiguration& conf);
        };
    } // namespace NPlugins
} // namespace NYMake
