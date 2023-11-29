#pragma once

#include "node.h"

#include <devtools/ymake/common/npath.h>

#include <util/generic/strbuf.h>
#include <util/generic/string.h>
#include <util/generic/vector.h>

namespace NYMake {
    namespace NMsvs {
        TString WindowsPath(TString unixPath);
        TString WindowsPathWithPrefix(const TString& prefix, TString unixPath);
        bool IsObjPath(TStringBuf str);
        bool IsObjPath(TFileView str);
        bool IsOObjPath(const TStringBuf& str);
        bool IsRegularSourcePath(TStringBuf str);
        bool IsRegularSourcePath(TFileView str);
        bool IsAsmSourcePath(const TStringBuf& str);
        bool IsCSourcePath(const TStringBuf& str);
        bool IsCuSourcePath(const TStringBuf& str);

        class TFile: public TGraphNode {
        public:
            TFile(const TBuildConfiguration& conf, const TConstDepNodeRef& node)
                : TGraphNode(conf, node)
            {
            }

            TFile(const TBuildConfiguration& conf, const TDepGraph& graph, TNodeId id)
                : TGraphNode(conf, graph[id])
            {
            }

            TFile(const TYMake& yMake, TNodeId id)
                : TGraphNode(yMake, id)
            {
            }

            inline NPath::ERoot Root() const {
                return Name.GetType();
            }

            inline TString NameAbs() const {
                return Conf.RealPath(Name);
            }

            inline TString NameAbsWin() const {
                return WindowsPath(NameAbs());
            }

            inline TStringBuf Ext() const {
                return Name.Extension();
            }

            inline bool IsObj() const {
                return IsObjPath(Name);
            }
            inline bool IsRegularSource() const {
                return IsRegularSourcePath(Name);
            }
        };

        using TFiles = TSet<TFile, TLessByName>; // We always need them sorted
    }
}
