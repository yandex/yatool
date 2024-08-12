#pragma once

#include <devtools/ymake/compact_graph/dep_types.h>
#include <devtools/ymake/compact_graph/graph.h>

struct TDeps;
struct TIndDepsRule;
struct TModAddData;
class TDirs;
struct TPropsNodeList;
class TPropertiesState;
class TResolveFile;

// This class enables unified interface for adding new deps in old and new graph representations
class TAddDepAdaptor : public TNonCopyable {
public:
    EMakeNodeType NodeType = EMNT_Deleted;
    ui64 ElemId = 0;
    TNodeId UpdNode = TNodeId::Invalid;

    TAddDepAdaptor() noexcept = default;
    virtual ~TAddDepAdaptor() = default;

    virtual void AddDepIface(EDepType /* depType */, EMakeNodeType /* elemNodeType */, TStringBuf /* elemName */) = 0;
    virtual void AddDepIface(EDepType /* depType */, EMakeNodeType /* elemNodeType */, ui64 /* elemId */) = 0;
    virtual void AddDeps(const TDeps& /* deps */) = 0;
    virtual bool AddUniqueDep(EDepType /* depType */, EMakeNodeType /* elemNodeType */, ui64 /* elemId */) = 0;
    virtual bool AddUniqueDep(EDepType /* depType */, EMakeNodeType /* elemNodeType */, TStringBuf /* elemName */) = 0;
    virtual bool HasAnyDeps() const = 0;
    virtual void AddParsedIncls(TStringBuf /* type */, const TVector<TResolveFile>& /* files */) = 0;
    virtual void AddDirsToProps(const TDirs& /* dirs */, TStringBuf /* propName */) = 0;
    virtual void AddDirsToProps(const TVector<ui32>& /* dirIds */, TStringBuf /* propName */) = 0;
    virtual void AddDirsToProps(const TPropsNodeList& /* props */, TStringBuf /* propName */) = 0;
    virtual TAddDepAdaptor& AddOutput(ui64 /* fileId */, EMakeNodeType /* defaultType */, bool addToOwn = true) = 0;
    virtual TPropertiesState& GetProps() = 0;
    virtual TModAddData& GetModuleData() = 0;
    virtual const TIndDepsRule* SetDepsRuleByName(TStringBuf /* name */) = 0;
};
