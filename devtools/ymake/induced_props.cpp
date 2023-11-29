#include "induced_props.h"

void TIndDepsRule::InsertUseActionsTo(THashSet<TPropertyType>& target) const {
    for (const auto& [type, action] : Actions) {
        if (action == EAction::Use) {
            target.insert(type);
        }
    }
}

EVisitIntent IntentByName(const TStringBuf& intent, bool check) {
    // TODO: make a hash here
    if (intent == TStringBuf("ParsedIncls"))
        return EVI_InducedDeps;
    if (intent == TStringBuf("Cmd"))
        return EVI_CommandProps;
    if (intent == TStringBuf("Mod"))
        return EVI_ModuleProps;
    if (intent == TStringBuf("GetMod"))
        return EVI_GetModules;
    Y_ASSERT(!check);
    return EVI_MaxId;
}

