#include "cpp_plugins.h"

#include <devtools/ymake/lang/plugin_facade.h>
#include <devtools/ymake/plugins/plugin_go_fake_output_handler.h>
#include <devtools/ymake/plugins/resource_handler/impl.h>

namespace NYMake {
    namespace NPlugins {
        void RegisterCppPlugins() {
            TPluginResourceHandler::RegisterMacro();
            TPluginGoFakeOutputHandler::RegisterMacro();
        }
    } // end of namespace NPlugins
} // end of namespace NYMake

