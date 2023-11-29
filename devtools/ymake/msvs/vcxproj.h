#pragma once

#include "module.h"
#include "obj.h"
#include "project_tree.h"

#include <devtools/ymake/ymake.h>

#include <devtools/ymake/compact_graph/query.h>
#include <devtools/ymake/compact_graph/iter_direct_peerdir.h>

#include <devtools/ymake/common/uniq_vector.h>
#include <devtools/ymake/diag/dbg.h>

#include <library/cpp/xml/document/xml-document.h>

#include <util/folder/path.h>
#include <util/generic/hash_set.h>
#include <util/generic/ptr.h>
#include <util/generic/strbuf.h>
#include <util/generic/string.h>
#include <util/generic/vector.h>
#include <util/system/types.h>

namespace NYMake {
    namespace NMsvs {
        // node's module & primary data
        struct TNodeInfoData : TEntryStatsData {
            //ui64 Module = 0; // offset
            TNodeId ToolModule = 0;         // for directories only
            bool ModuleRendered = false; // is module final target
            // bool IsMade = false; = HasBuildFrom

            TSimpleSharedPtr<TUniqVector<TNodeId>> Peers; // Peerdirs, recursively
            TSimpleSharedPtr<TUniqVector<TNodeId>> Tools; // Tooldirs
            explicit TNodeInfoData(bool isFile = false)
                : TEntryStatsData(isFile)
            {
            }
        };

        using TNodeInfo = TVisitorStateItem<TNodeInfoData>;

        class TFlatObjPool {
        private:
            THashSet<TString> Set;

        public:
            bool AddSource(const TStringBuf& sourcePath, TString& objName);
        };

        class TVcRender: public TDirectPeerdirsConstVisitor<TNodeInfo> {
        private:
            using TBase = TDirectPeerdirsConstVisitor<TNodeInfo>;
            THashMap<TNodeId, TNodeIds> Mod2Srcs; // directory-based heuristics

        public:
            enum EVcConf {
                Unset,
                Release,
                Debug
            };

            struct TStats {
                size_t NumProjects;
                size_t NumUpdatedProjects;
                size_t NumSolutions;

                TStats()
                    : NumProjects(0)
                    , NumUpdatedProjects(0)
                    , NumSolutions(0)
                {
                }
            };

            void Leave(TState& state);

            bool AcceptDep(TState& state) {
                auto& st = state.Top();

                auto dep = st.CurDep();
                auto depType = *dep;

                if ((depType == EDT_Search || depType == EDT_Property || depType == EDT_Group) && !IsModuleOwnNodeDep(dep)) {
                    return false;
                }
                if (depType == EDT_Search2 && !IsGlobalSrcDep(dep)) {
                    return false;
                }
                if (depType == EDT_Include && dep.To()->NodeType == EMNT_NonParsedFile) {
                    // Don't mine anything from generated files belonging to other modules
                    return false;
                }

                // TODO: switch to direct tooldirs
                if (IsDirectToolDep(dep)) {
                    return false;
                }

                return TBase::AcceptDep(state);
            }

        private:
            TYMake& YMake;
            TString Name;
            size_t Version;
            TString VersionString;
            TStats Stats;
            TSet<TModuleSlnInfo, TLessByName> ModSlnInfo;
            TProjectTree ProjectTree;
            TFsPath SolutionRoot;
            TString IntermediateDir;
            TFilePool<TObj> GlobalObjPool;
            TUniqVector<TNodeId> GlobalNodes;
            TFilePool<TLib> GlobalLibPool;
            TString AllProject;
            bool UseArcadiaToolchain;

            static constexpr const char* LLVMToolset = "llvm";

        private:
            void RenderVcxproj(TModule& module, const TSimpleSharedPtr<TUniqVector<TNodeId>>& peers, const TSimpleSharedPtr<TUniqVector<TNodeId>>& tools);
            void RenderAllVcxproj(const TStringBuf& title, const TStringBuf& path, const TStringBuf& guid);
            void RenderSln();
            void DisableFilters(const TStringBuf& path);

            TString InternalProjectDirPath(const TStringBuf& path) const;
            TString InternalProjectPath(const TStringBuf& path) const;
            TString ProjectDirPath(const TStringBuf& path) const;
            TString ProjectPath(const TStringBuf& path) const;
            TString MsvsProjectPath(const TStringBuf& path) const;
            TString RealProjectPath(const TStringBuf& path) const;
            TString RealProjectDirPath(const TStringBuf& path) const;
            TString SolutionFilename() const;
            TString GetPlatformToolset() const;
            TString GetToolsVersion() const;

        public:
            TVcRender(TYMake& yMake, const TFsPath& solutionRoot, const TStringBuf& name, size_t version);
            void Render();

            const TStats GetStats() const {
                return Stats;
            }
        };
    }
}
