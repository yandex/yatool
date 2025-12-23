#pragma once
#include <devtools/ymake/vars.h>

#include <library/cpp/fieldcalc/field_calc.h>
#include <library/cpp/fieldcalc/field_calc_int.h>

#include <util/generic/set.h>
#include <util/generic/string.h>
#include <util/ysaveload.h>

class TYmakeConfigConditionCalc: public TFieldCalculatorBase {
public:
    bool item_by_name(dump_item& it, const char* name) const override {
        if (*name == '$') {
            it = dump_item((resolve_fn_t)&TVars::EvalValue, name + 1);
            it.pack_id = 0;
            return true;
        } else {
            return TFieldCalculatorBase::item_by_name(it, name);
        }
    }
    void Compile(char** field_names, int field_count) {
        TFieldCalculatorBase::Compile(field_names, field_count);
    }
    bool CondById(TVars& vars, int condNumber) {
        const char* d = reinterpret_cast<const char*>(&vars);
        return TFieldCalculatorBase::CondById(&d, condNumber);
    }
    size_t CondsQuantity() {
        return +conditions;
    }
};

enum EMacroAssignOperator {
    EMAO_Set = 1,
    EMAO_Append = 2,
    EMAO_Define = 3, //set if not defined
    EMAO_Exclude = 4,
    EMAO_Unknown = 5,
};

inline EMacroAssignOperator GetOperator(const TString& aoperator) {
    if (aoperator == "=")
        return EMAO_Set;
    if (aoperator == "+=")
        return EMAO_Append;
    if (aoperator == ":=")
        return EMAO_Define;
    if (aoperator == "-=")
        return EMAO_Exclude;
    return EMAO_Unknown;
}

struct TConditionAction {
    TString MacroValue;
    TString MacroToChange;
    EMacroAssignOperator Aoperator;

    TConditionAction(const TString& var = "", const EMacroAssignOperator op = EMAO_Unknown, const TString& value = "")
        : MacroValue(value)
        , MacroToChange(var)
        , Aoperator(op)
    {
    }

    Y_SAVELOAD_DEFINE(
        MacroValue,
        MacroToChange,
        Aoperator
    );
};

typedef TMap<TString, TSet<TString>> TVar2Macros;
typedef TMap<TString, TSet<size_t>> TMacro2Conditions;

class TCondition {
public:
    void RecalcVars(const TString& variable, TVars& realVars, TOriginalVars& origVars);
    void RecalcAll(TVars& realVars);
    void AddActionForVariable(const TString& variable, const size_t& condNumber, const TConditionAction& action);
    void AddRawCondition(const TString& condition) { RawConditions.push_back(condition); }
    const TVector<TString>& GetRawConditions() const { return RawConditions; }
    void Clear();

    TYmakeConfigConditionCalc ConditionCalc; //all compiled conditions indexed by numbers

    Y_SAVELOAD_DEFINE(
        Var2Macros,
        Macro2Conditions,
        Condition2Action,
        RawConditions
    );
private:
    void ApplyCondition(size_t condNumber, TVars& realVars);
    TVar2Macros Var2Macros;
    TMacro2Conditions Macro2Conditions;
    TMap<size_t, TConditionAction> Condition2Action;
    TVector<TString> RawConditions;
};
