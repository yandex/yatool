#include "config_conditions.h"

#include <devtools/ymake/diag/manager.h>

void TCondition::Clear() {
    Var2Macros.clear();
    Macro2Conditions.clear();
    Condition2Action.clear();
    RawConditions.clear();
}

void TCondition::AddActionForVariable(const TString& variable, const size_t& condNumber, const TConditionAction& action) {
    //in Var2Macros insert variable -> action.MacroToChange;
    //in Macro2Conditions insert action.MacroToChange -> condNumber;
    Condition2Action.try_emplace(condNumber, action);
    const TString& macro = action.MacroToChange;
    Var2Macros[variable].insert(macro);
    Macro2Conditions[macro].insert(condNumber);
}

void TCondition::ApplyCondition(size_t condNumber, TVars& realVars) {
    TConditionAction& action = Condition2Action[condNumber];
    if (ConditionCalc.CondById(realVars, condNumber)) {
        switch (action.Aoperator) {
            case EMAO_Set:
                //YDIAG(V) << "Set pair: " << action.MacroToChange << " " << action.MacroValue << Endl;
                realVars.SetValue(action.MacroToChange, action.MacroValue);
                break;
            case EMAO_Append:
                realVars.SetAppend(action.MacroToChange, action.MacroValue);
                //YDIAG(V) << "Append: " << action.MacroToChange << "->" << action.MacroValue << Endl;
                break;
            case EMAO_Define:
                //YDIAG(V) << "Define: " << action.MacroToChange << " " << action.MacroValue << Endl;
                if (!realVars.Has(action.MacroToChange))
                    realVars.SetValue(action.MacroToChange, action.MacroValue);
                break;
            case EMAO_Exclude: //delete from var value substring;
                //realVars.Del1Sp(action.MacroToChange, action.MacroValue);
                YConfWarn(MacroUse) << "Exclude is deprecated in config for now. No action: " << action.MacroToChange << " " << action.MacroValue << Endl;
                break;
            default:
                YConfWarn(MacroUse) << "Unknown operator for " << action.MacroValue << " applied to " << action.MacroToChange << Endl;
        }
    }
    //YDIAG(V) << "After cond=" << condNumber << " var is: " << action.MacroToChange << "->" << realVars.Get1(action.MacroToChange) << Endl;
}

void TCondition::RecalcVars(const TString& variable, TVars& realVars, TOriginalVars& origVars) {
    TVar2Macros::iterator varIt = Var2Macros.find(variable); //there are some conds with this variable
    if (varIt == Var2Macros.end()) {
        return;
    }
    for (TSet<TString>::iterator macroIt = varIt->second.begin(); macroIt != varIt->second.end(); ++macroIt) {
        //get next macro that can be changed due to variable has changed
        TMacro2Conditions::iterator condIt = Macro2Conditions.find(*macroIt);

        if (condIt == Macro2Conditions.end())
            continue;

        if (realVars.Contains(*macroIt)) {
            if (origVars.find(*macroIt) != origVars.end()) { //if this variable was not set in scope before any conditions calculations, remove it
                realVars.ResetAppend(*macroIt, origVars[*macroIt]);
            } else {
                realVars.RemoveFromScope(*macroIt);
            }
        }
        for (TSet<size_t>::iterator condNumIt = condIt->second.begin(); condNumIt != condIt->second.end(); ++condNumIt) {
            size_t condNumber = *condNumIt;
            ApplyCondition(condNumber, realVars);
        }
    }
}

void TCondition::RecalcAll(TVars& realVars) {
    for (size_t i = 0; i < ConditionCalc.CondsQuantity(); ++i)
        ApplyCondition(i, realVars);
}
