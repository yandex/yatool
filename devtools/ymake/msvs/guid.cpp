#include "guid.h"

#include <devtools/ymake/common/npath.h>

#include <library/cpp/digest/md5/md5.h>

#include <util/generic/guid.h>
#include <util/string/printf.h>
#include <util/system/hi_lo.h>
#include <util/system/yassert.h>

namespace NYMake {
    namespace NMsvs {

        namespace {

            TGUID NaiveGuid(const TStringBuf& s) {
                TGUID guid;
                MD5 ctx;
                static_assert(sizeof(guid.dw) == 16, "something wrong in this world");
                ctx.Update(s.data(), s.size());
                ctx.Final((unsigned char*)guid.dw);
                return guid;
            }

        }

        TString GenGuid(const TStringBuf& s) {
            TGUID guid = NaiveGuid(s);
            return Sprintf("%08X-%04X-%04X-%04X-%04X%08X", guid.dw[0], Hi16(guid.dw[1]).Get(), Lo16(guid.dw[1]).Get(), Hi16(guid.dw[2]).Get(), Lo16(guid.dw[2]).Get(), guid.dw[3]);
        }

        TString GenGuid(const TModuleNode& module) {
            Y_ASSERT(module.Name.IsType(NPath::ERoot::Build));
            return GenGuid(module.LongName());
        }

    }
}
