#include "cpp_plugins.h"

#include <devtools/ymake/lang/plugin_facade.h>
#include <devtools/ymake/plugins/resource_handler/impl.h>

namespace NYMake {
    namespace NPlugins {
        void RegisterCppPlugins(TBuildConfiguration& conf) {
            TPluginResourceHandler::RegisterMacro(conf);
        }
    } // end of namespace NPlugins
} // end of namespace NYMake

