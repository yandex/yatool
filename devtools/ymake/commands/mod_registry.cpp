#include "mod_registry.h"

#include <devtools/ymake/config/config.h>
#include <devtools/ymake/diag/dbg.h>
#include <devtools/ymake/conf.h>
#include <devtools/ymake/exec.h>
#include <fmt/format.h>
#include <util/generic/overloaded.h>
#include <util/generic/yexception.h>

using namespace NCommands;

namespace {
    auto& ModDescriptions() {
        static THashMap<EMacroFunction, TModImpl*> modDescriptions;
        return modDescriptions;
    }
}

NCommands::TModImpl::TModImpl(TModMetadata metadata): TModMetadata(metadata) {
    Y_ABORT_IF(ModDescriptions().contains(Id));
    ModDescriptions().insert({Id, this});
}

NCommands::TModImpl::~TModImpl() {
    ModDescriptions().erase(Id);
}

NCommands::TModRegistry::TModRegistry() {
    THashSet<EMacroFunction> done;
    Descriptions.fill(nullptr);
    for (auto& m : ModDescriptions()) {
        auto& _m = *m.second;
        Y_ABORT_IF(done.contains(_m.Id));
        Y_ABORT_IF(Index.contains(_m.Name));
        done.insert(_m.Id);
        Descriptions.at(ToUnderlying(_m.Id)) = &_m;
        if (!_m.Internal)
            Index[_m.Name] = _m.Id;
    }
}

ui16 TModRegistry::FuncArity(EMacroFunction func) const {
    auto desc = At(func);
    if (Y_UNLIKELY(!desc))
        throw yexception() << "unknown modifier " << func;
    return desc->Arity;
}

NPolexpr::TFuncId TModRegistry::Func2Id(EMacroFunction func) const {
    return NPolexpr::TFuncId{FuncArity(func), static_cast<ui32>(func)};
}

EMacroFunction TModRegistry::Id2Func(NPolexpr::TFuncId id) const {
    return static_cast<EMacroFunction>(id.GetIdx());
}
