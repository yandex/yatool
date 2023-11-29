#pragma once

#include "configuration.h"
#include "file.h"
#include "misc.h"

#include <devtools/ymake/symbols/symbols.h>

#include <devtools/ymake/mkcmd.h>

#include <util/generic/hash_set.h>
#include <util/generic/strbuf.h>
#include <util/generic/string.h>
#include <util/generic/vector.h>
#include <util/system/types.h>
#include <util/generic/flags.h>

namespace NYMake {
    namespace NMsvs {
        class TModuleNode: public TFile {
        public:
            TModuleNode(const TYMake& yMake, TNodeId id)
                : TFile(yMake, id)
            {
            }

            // Names with no extensions
            TStringBuf ShortName() const;
            TStringBuf LongName() const;

            TString Srcdir() const;
            TString Bindir() const;
            TFiles GlobalObjs() const;
        };

        using TNodeIds = TVector<TNodeId>;
        using TModules = TSet<TModuleNode, TLessByName>; // We always need them sorted

        class TModule: public TModuleNode {
        public:
            enum EKind {
                K_UNKNOWN,
                K_PROGRAM,
                K_LIBRARY,
                K_DLL,
            };
            enum class CxxFlavor : ui8 {
                Unknown = 0,
                C = 1,
                Cpp = 2,
                Mixed = 3,
            };

        public:
            using TFileFlagsMap = THashMap<TNodeId, TString, TIdentity>;
            using TFileOutSuffixMap = THashMap<TNodeId, TString, TIdentity>;
            TFiles Files;
            TFileFlagsMap FileFlags;
            TFileOutSuffixMap FileOutSuffix;
            THashSet<TNodeId> FileNodesWithBuildCmd;
            THashSet<TNodeId> FileNodesSrcsGlobal;

        private:
            TMakeCommand ModuleCmd;
            TMakeCommand SampleFileCmd;
            const ::TModule* Module;
            TYMake& YMake;
            TString IncludeVarName;
            bool IsGlobalLibModule = false;
            TNodeId ModuleId = 0;
            TFlags<CxxFlavor> Cxx = CxxFlavor::Unknown;

        public:
            TModule(TYMake& yMake, TNodeId id, TNodeId modId, const TNodeIds& extraFiles, bool globalSrcsAsNormal);

            EKind Kind() const;
            TString IncludeString(const TStringBuf& sep) const;
            TNodeIds RetrievePeers() const; // do query graph
            TFiles FilterFiles(const TStringBuf& ext, NPath::ERoot root = NPath::Unset) const;

            bool FileWithBuildCmd(const TFile& file) const;
            bool FileGlobal(const TNodeId fileId) const;

            TString CFlags(EConf conf);
            TString LinkFlags(EConf conf);
            TVector<TString> LinkStdLibs();

            bool IsFakeModule() const { return !IsGlobalLibModule && Module->IsFakeModule(); }
            TNodeId GetModuleId() const { return ModuleId; }

            bool IsGlobalModule() const { return IsGlobalLibModule; }

        private:
            void InitSampleFileCmd();
        };

        class TModuleSlnInfo: public TModuleNode {
        public:
            TNodeIds Peers;

        public:
            TModuleSlnInfo(const TModuleNode& mod, const TNodeIds& peers)
                : TModuleNode(mod)
                , Peers(peers)
            {
            }
        };
    }
}
