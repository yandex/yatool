#include "configuration.h"

#include <util/generic/strbuf.h>
#include <util/generic/string.h>
#include <util/system/yassert.h>

namespace NYMake {
    namespace NMsvs {
        namespace {
            const TStringBuf CONFIGURATIONS[] = {
                TStringBuf(),          // C_UNSET
                TStringBuf("Debug"),     // C_DEBUG
                TStringBuf("Release"),   // C_RELEASE
            };

            const TStringBuf BUILD_MODE_MARKERS[] = {
                TStringBuf("unset"),     // C_UNSET
                TStringBuf("debug"),     // C_DEBUG
                TStringBuf("release"),   // C_RELEASE
            };

            inline const TStringBuf& BuildModeMarker(EConf conf) {
                size_t i = static_cast<size_t>(conf);
                Y_ASSERT(i < Y_ARRAY_SIZE(BUILD_MODE_MARKERS));
                return BUILD_MODE_MARKERS[i];
            }

            inline size_t ResolveBTSpecEntity(TStringBuf s, TString& resolved, EConf conf) {
                if ((s.size() < 5) || (s[0] != '@') || (s[1] != '[')) {
                    return 0;
                }
                s.Skip(2);
                size_t split = s.find('|');
                if ((split == TStringBuf::npos) || !split) {
                    return 0;
                }
                TStringBuf marker = s.SubStr(0, split);
                s.Skip(split + 1);
                size_t value_end = s.find(']');
                if (value_end == TStringBuf::npos) {
                    return 0;
                }
                resolved = s.SubStr(0, value_end);
                size_t parsed = marker.size() + resolved.size() + 4;
                if (marker != BuildModeMarker(conf)) {
                    resolved.clear();
                }
                return parsed;
            }
        }

        const TStringBuf& Configuration(EConf conf) {
            size_t i = static_cast<size_t>(conf);
            Y_ASSERT(i < Y_ARRAY_SIZE(CONFIGURATIONS));
            return CONFIGURATIONS[i];
        }

        TString ResolveBuildTypeSpec(const TStringBuf& s, EConf conf) {
            size_t at_pos = s.find('@');
            if (at_pos == TStringBuf::npos) {
                return TString{s};
            }
            TString rs(s);
            while (at_pos != TStringBuf::npos) {
                size_t rend = at_pos + 1;
                TString resolved;
                size_t size = ResolveBTSpecEntity(TStringBuf(rs).Skip(at_pos), resolved, conf);
                if (size) {
                    rs.replace(at_pos, size, resolved);
                    rend = at_pos + resolved.size();
                }
                at_pos = rs.find('@', rend);
            }
            return rs;
        }
    }
}
