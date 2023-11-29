#pragma once

#include <devtools/ymake/compact_graph/graph.h>

#include <util/generic/strbuf.h>
#include <util/generic/string.h>
#include <util/generic/vector.h>
#include <util/system/types.h>

namespace NYMake {
    namespace NMsvs {
        using TNodeIds = TVector<TNodeId>;

        template <typename V, typename T, typename... P>
        inline void ToSortedData(V& result, const T& data, P&... pp) {
            for (const auto& d : data) {
                result.emplace(pp..., d);
            }
        }
    }
}
