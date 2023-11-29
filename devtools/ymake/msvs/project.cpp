#include "project.h"

#include <util/generic/array_size.h>
#include <util/system/types.h>
#include <util/system/yassert.h>

namespace NYMake {
    namespace NMsvs {
        namespace {
            struct TModuleKindData {
                TStringBuf ConfigurationType;
                TStringBuf TargetExtention;
            };

            TModuleKindData MODULE_KIND_DATA[] = {
                {TStringBuf(), TStringBuf(".unk")},                 // K_UNKOWN
                {TStringBuf("Application"), TStringBuf(".exe")},      // K_PROGRAM
                {TStringBuf("StaticLibrary"), TStringBuf(".lib")},    // K_LIBRARY
                {TStringBuf("DynamicLibrary"), TStringBuf(".dll")},   // K_DLL
            };
        }

        const TStringBuf& TProject::ConfigurationType() const {
            size_t i = static_cast<size_t>(Module.Kind());
            Y_ASSERT(i < Y_ARRAY_SIZE(MODULE_KIND_DATA));
            return MODULE_KIND_DATA[i].ConfigurationType;
        }

        // TODO: get extentions from our build system
        const TStringBuf& TProject::TargetExtention() const {
            size_t i = static_cast<size_t>(Module.Kind());
            Y_ASSERT(i < Y_ARRAY_SIZE(MODULE_KIND_DATA));
            return MODULE_KIND_DATA[i].TargetExtention;
        }
    }
}
