#pragma once

#include "module_state.h"
#include "module_wrapper.h"
#include "add_dep_adaptor.h"

#include <util/generic/vector.h>
#include <util/generic/flags.h>

class TModule;

enum class EPeerOption {
    NoOption = 0b000,
    AddIncl = 0b001,
    MaterialGhost = 0b010,
    VirtualGhost = 0b100
};

constexpr TFlags<EPeerOption> operator| (EPeerOption l, EPeerOption r) noexcept {
    return TFlags<EPeerOption>{l} | r;
}

/// @brief Module directories management in-graph and off-graph
/// This class may add nodes and edges to graph representing relations to directories
/// including relevant property nodes.
class TModuleDirBuilder {
public:
    TModuleDirBuilder(TModule& module, TAddDepAdaptor& node, TDepGraph& graph)
        : Module(module)
        , Node(node)
        , Graph(graph)
    {
    }

    void UseModuleProps(TPropertyType propType, const TPropsNodeList& propsValues);
    void AddSrcProperty();

    void AddSrcdir(const TStringBuf& dir);
    void AddIncdir(const TStringBuf& dir, EIncDirScope scope = EIncDirScope::Local, bool checkDir = true, TLangId langId = TModuleIncDirs::C_LANG);
    void AddPeerdir(const TStringBuf& dir, TFlags<EPeerOption> addFlags = {});
    void AddDepends(const TVector<TStringBuf>& args);
    void AddDataPath(TStringBuf path);

protected:
    TModule& Module;
    TAddDepAdaptor& Node;
    TDepGraph& Graph;

private:
    void AddMissingDir(TStringBuf dir);
};
