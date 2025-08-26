#pragma once

#include "conf.h"

#include <devtools/ymake/compact_graph/dep_types.h>
#include <devtools/ymake/compact_graph/dep_graph.h>

#include <devtools/ymake/symbols/symbols.h>
#include <devtools/ymake/diag/stats.h>

#include <util/memory/blob.h>
#include <util/generic/hash.h>
#include <util/generic/strbuf.h>

class TYMake;
class TModuleDef;
class TModule;
struct TNodeAddCtx;
struct TModuleGlobInfo;

class TGeneralParser {
public:
    using TModuleConstraints = std::function<void(const TModuleDef&)>;

    explicit TGeneralParser(TYMake& yMake);
    ~TGeneralParser() = default;

    // ProcessFile may change file type, currently from File to MissingFile
    void ProcessFile(TFileView name, TNodeAddCtx& node, TAddIterStack& stack, TFileHolder& fileContent, TModule* mod);
    void ProcessCommand(TCmdView name, TNodeAddCtx& node, TAddIterStack& stack);

    bool NeedUpdateFile(ui64 fileId, EMakeNodeType type, TFileHolder& fileContent);

    EMakeNodeType DirectoryType(TFileView dirname) const;
    TNodeRelocationMap RelocatedNodes;

    void ReportStats();
private:
    EMakeNodeType ProcessDirectory(TFileView dirname, TNodeAddCtx& node);
    void AddCommandNodeDeps(TNodeAddCtx& node);
    void ProcessMakeFile(TFileView name, TNodeAddCtx& node);
    void ProcessBuildCommand(TStringBuf name, TNodeAddCtx& nodei, TAddIterStack& stack);
    void ProcessProperty(TStringBuf name, TNodeAddCtx& node, TAddIterStack& stack);
    void ProcessCmdProperty(TStringBuf name, TNodeAddCtx& node, TAddIterStack& stack);
    void RelocateFile(TNodeAddCtx& node, TStringBuf newName, EMakeNodeType newType, TDGIterAddable& iterAddable);
    void RelocateFile(TNodeAddCtx& node, ui32 id, EMakeNodeType newType, TDGIterAddable& iterAddable);

private:
    TYMake& YMake;
    TDepGraph& Graph;
    const TBuildConfiguration& Conf;
    TModuleConstraints ModuleConstraintsChecker;
    TCachedFileConfContentProvider YaMakeContentProvider;
    NStats::TGeneralParserStats Stats{"ya.make parsing stats"};
};

TFileView MakefileNodeNameForDir(TFileConf& fileConf, TFileView dir);
TFileView MakefileNodeNameForModule(const TFileConf& fileConf, const TModule& module);
void PopulateGlobNode(TNodeAddCtx& node, const TModuleGlobInfo& globInfo);
void CreateGlobVar2PatternDeps(ui32 globVarElemId, const TVector<ui32>& globPatternElemIds, TYMake& YMake, TUpdIter& UpdIter, TModule* Module);
