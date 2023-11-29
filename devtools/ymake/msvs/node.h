#pragma once

#include <util/generic/algorithm.h>
#include <util/generic/string.h>
#include <util/generic/function.h>
#include <util/system/types.h>

#include <devtools/ymake/compact_graph/dep_graph.h>
#include <devtools/ymake/ymake.h>

namespace NYMake {
    namespace NMsvs {


        class TGraphNode: public TConstDepNodeRef {
        public:
            const TFileView Name;
            const TBuildConfiguration& Conf;

        protected:
            const TDepGraph& Graph() const {
                return TDepGraph::Graph(*this);
            }

        public:
            TGraphNode(const TBuildConfiguration& conf, const TConstDepNodeRef& node)
                : TConstDepNodeRef(node)
                , Name(TDepGraph::GetFileName(*this))
                , Conf(conf)
            {
            }

            TGraphNode(const TYMake& yMake, TNodeId id)
                : TGraphNode(yMake.Conf, yMake.Graph[id])
            {
            }
        };

        struct TLessByName {
            bool operator()(const TGraphNode& l, const TGraphNode& r) const {
                return l.Name < r.Name;
            }
        };

        //        using TLessByName = TFunctorWrapper<decltype(NMsvs::LessByName), NMsvs::LessByName>;
    }
}
