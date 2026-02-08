#pragma once

#include "macro_string.h"

#include <devtools/ymake/compact_graph/dep_types.h>
#include <devtools/ymake/lang/properties.h>

#include <util/generic/fwd.h>
#include <util/generic/hash.h>
#include <util/generic/hash_set.h>
#include <util/generic/string.h>
#include <util/generic/vector.h>
#include <util/string/type.h>
#include <util/string/vector.h>
#include <util/system/types.h>
#include <util/system/yassert.h>
#include <util/ysaveload.h>

#include <functional>
#include <utility>

struct TYVar;

TStringBuf Get1(const TYVar* var);
TStringBuf Eval1(const TYVar* var);
TString GetAll(const TYVar* var);
TString EvalAll(const TYVar* var);

namespace NYMake {
    inline bool IsTrue(TStringBuf b) {
        // string is true if it is not false and not empty
        if (IsFalse(b) || b.empty())
            return false;
        return true;
    }

    static const TStringBuf RESOURCE_GLOBAL_SUFFIX = "_RESOURCE_GLOBAL";
    inline bool IsGlobalResource(const TStringBuf var) {
        return var.EndsWith(RESOURCE_GLOBAL_SUFFIX);
    }
} // namespace NYMake

struct TVars;
struct TUpdEntryStats;
typedef std::pair<const TDepsCacheId, TUpdEntryStats>* TUpdEntryPtr;

/// identify and dereference variables
TString EvalExpr(const TVars& vars, const TStringBuf& expr);
TString EvalExpr(const TVars& vars, const TVector<TStringBuf>& args);
TString EvalExpr(const TVars& vars, const TVector<TString>& args);

struct TVarStr {
    TString Name;
    union {
        ui64 AllFlags;
        struct { // 64 bits used
            ui32 IsPathResolved : 1;
            ui32 ResolveToBinDir : 1; // Output's flag only
            ui32 NoTransformRelativeBuildDir : 1;
            ui32 NoRel : 1;           // see in TMacro
            ui32 NotFound : 1;
            ui32 IsMacro : 1;
            ui32 IsDir : 1;
            ui32 DirAllowed : 1;
            // ^^^^^^^^^^^^^^^^^^^^
            // 8bits up until here

            ui32 HasPrefix : 1;    // fully-qualified value
            ui32 NoAutoSrc : 1;
            ui32 IsTmp : 1;        // TODO: handle differently from output?
            ui32 IsOutputFile : 1; // Output of a command
            ui32 ForIncludeOnly : 1;
            ui32 IsGlobal : 1;
            ui32 FromLocalVar : 1; // tools derived from macro parameters must be added to command root
            ui32 AddToIncl : 1;    // see in TMacro
            // ^^^^^^^^^^^^^^^^^^^^
            // 16bits up until here

            ui32 AsStdout : 1;     // for local vars only
            ui32 IsAuto : 1;       // for auto inputs
            ui32 WorkDir : 1;      // for local vars only
            ui32 SetEnv : 1;       // for local vars only
            ui32 RawInput : 1;     // for module build cmd
            //XXX: ya build legacy
            ui32 NeedSubst : 1;    // SubstMacro must recursively call expansion
            ui32 AddToModOutputs : 1;
            ui32 ByExtFailed : 1;  // diagnostics helper
            // ^^^^^^^^^^^^^^^^^^^^
            // 24bits up until here

            ui32 Result : 1;
            ui32 IsOutput : 1;
            ui32 Main : 1;
            ui32 IsYPath : 1;
            ui32 ResolveToModuleBinDirLocalized : 1;
            ui32 HasPeerDirTags : 1; // Name can be prefixed with comma separated tags
            ui32 ResourceUri : 1;  // for local vars only
            ui32 TaredOut : 1;
            // ^^^^^^^^^^^^^^^^^^^^
            // 32bits up until here

            ui16 OutInclsFromInput: 1;
            ui16 OutputInThisModule: 1;  // Dynamic mark for vars created as outputs in current module
            ui16 StructCmdForVars: 1;
            ui16 IsGlob: 1;
            ui16 : 0;              // Start next word
            // ^^^^^^^^^^^^^^^^^^^^
            // 48bits up until here

            ui16 CurCoord : 16;    // TODO: don't crash if a command has 65536+ inputs?
        };
    };

public:
    TVarStr() = default;

    TVarStr(const TStringBuf& name)
        : Name(TString{name})
        , AllFlags(0)
    {
    }

    TVarStr(const TString& name)
        : Name(name)
        , AllFlags(0)
    {
    }

    TVarStr(const TStringBuf& name, bool hasPrefix, bool isPathResolved)
        : Name(name)
        , AllFlags(0)
    {
        IsPathResolved = isPathResolved;
        HasPrefix = hasPrefix;
    }

    TVarStr(const TString& name, bool hasPrefix, bool isPathResolved)
        : Name(name)
        , AllFlags(0)
    {
        IsPathResolved = isPathResolved;
        HasPrefix = hasPrefix;
    }

    void MergeFlags(const TVarStr& from) {
        Y_ASSERT(!from.CurCoord);
        AllFlags |= from.AllFlags;
    }

    bool operator==(const TVarStr& var) const {
        return Name == var.Name && AllFlags == var.AllFlags;
    }

    Y_SAVELOAD_DEFINE(
        Name,
        AllFlags
    );
};

static_assert(sizeof(TVarStr) == sizeof(TString) + sizeof(ui64), "union part of TVarStr must fit 64 bit");

template<>
struct hash<TVarStr> {
    inline size_t operator() (const TVarStr& var) const {
        return CombineHashes(hash<TString>()(var.Name), hash<ui64>()(var.AllFlags));
    }
};

struct TYVar: public TVector<TVarStr> {
    const TYVar* BaseVal = nullptr;
    mutable TUpdEntryPtr EntryPtr = nullptr; // element state in UpdIter, valid only for vars serving as TCommandInfo::Cmd
    union {
        ui32 Flags;
        struct {
            ui32 GenFromFile : 1;
            ui32 DontExpand : 1;
            ui32 IsReservedName : 1;
            ui32 HasBlockData : 1;
            mutable ui32 AddCtxFilled : 1;
            ui32 NoInline : 1;
            ui32 ModuleScopeOnly : 1;
            ui32 FakeDeepReplacement : 1;
            ui32 DontParse : 1;
        };
    };
    ui32 Id; // tmp hack! only used to return id from Lookup commands

    TYVar()
        : Flags(0)
        , Id(0)
    {
    }

    // name has prefix
    explicit TYVar(const TStringBuf& name)
        : Flags(0)
        , Id(0) // GetId(name)
    {
        push_back(TVarStr(name, true, false));
    }

    void SetSingleVal(const TStringBuf& s, bool hasPrefix) {
        clear();
        push_back(TVarStr(s, hasPrefix, false));
    }

    void SetSingleVal(const TStringBuf& name, const TStringBuf& val, ui32 id, int varsId = -1) {
        Id = varsId >= 0 ? varsId : id;
        clear();
        push_back(TVarStr(FormatCmd(id, name, val), true, false));
    }

    void AddToSingleVal(const TStringBuf& s) {
        if (empty()) {
            push_back(TVarStr(s));
            return;
        }
        TString& n = (*this)[0].Name;
        if (!n) {
            n.assign(s);
        } else {
            n += " ";
            n += s;
        }
    }

    void DelFromSingleVal(const TStringBuf& s) { //remove substring from value
        if (empty() || s.empty())
            return;
        TString& n = (*this)[0].Name;
        if (!n)
            return;
        size_t pos = n.find(s);
        while (pos != TString::npos) {
            n.assign(n.substr(0, pos) + n.substr(pos + s.size(), n.size() - (pos + s.size())));
            pos = n.find(s);
        }
    }

    bool SetOption(TStringBuf name) {
        if (name == NProperties::GEN_FROM_FILE) {
            GenFromFile = true;
            return true;
        } else if (name == NProperties::NO_EXPAND) {
            DontExpand = true;
            return true;
        }
        return false;
    }

    template <class V>
    void Append(V& args, bool hasPrefix) {
        for (typename V::const_iterator i = args.begin(); i != args.end(); ++i)
            push_back(TVarStr(*i, hasPrefix, false));
    }

    template <class M>
    void InsertNamesTo(M& m) const {
        for (const_iterator i = begin(); i != end(); ++i)
            m.insert(i->Name);
    }

    template <class V>
    void Assign(V& args) {
        clear();
        reserve(args.size());
        for (typename V::const_iterator i = args.begin(); i != args.end(); ++i)
            push_back(*i);
    }

    void Append(const TYVar& vars) {
        insert((*this).end(), vars.begin(), vars.end());
    }

    void AppendUnique(const TYVar& vars) {
        THashSet<TVarStr> uniqVars;
        uniqVars.insert(this->begin(), this->end());
        for (auto& var : vars) {
            if (!uniqVars.contains(var)) {
                this->push_back(var);
                uniqVars.insert(this->back());
            }
        }
    }

    void Load(IInputStream* input) {
        BaseVal = nullptr;
        ::Load(input, Flags);
        ::Load(input, Id);
        ::Load(input, static_cast<TVector<TVarStr>&>(*this));
    }

    void Save(IOutputStream* output) const {
        Y_ASSERT(BaseVal == nullptr);
        ::Save(output, Flags);
        ::Save(output, Id);
        ::Save(output, static_cast<const TVector<TVarStr>&>(*this));
    }
};

struct TOriginalVars: public THashMap<TString, TString> { //store base values of vars for current ya.make
public:
    void SetAppend(const TStringBuf& name, const TString& val) {
        if ((*this).contains(name) && !(*this)[name].empty()) {
            (*this)[name] += " " + val;
        } else
            (*this)[name] = val;
    }

    void SetValue(const TStringBuf& name, const TString& val) {
        (*this)[name] = val;
    }
};

struct TVars: public THashMap<TString, TYVar> {
public:
    const TVars* Base;
    ui64 Id;

private:
    std::function<void(const TYVar&, const TStringBuf&)> VarLookupHook;

public:
    explicit TVars(const TVars* base = nullptr)
        : Base(base)
        , Id(0)
        , VarLookupHook([](const TYVar&, const TStringBuf&) {})
    {
    }

    void AssignVarLookupHook(const std::function<void(const TYVar&, const TStringBuf&)>& hook) {
        VarLookupHook = hook;
    }

    void Clear() {
        clear();
    }

    void Add1Sp(const TStringBuf& k, const TStringBuf& args) {
        (*this)[k].AddToSingleVal(args);
    }

    void Del1Sp(const TStringBuf& k, const TStringBuf& args) {
        (*this)[k].DelFromSingleVal(args);
    }

    value_type& SetValue(const TStringBuf& key, const TStringBuf& args, const TYVar* baseVal = nullptr) {
        auto [pos, _] = emplace(key, TYVar{});
        pos->second.SetSingleVal(FormatCmd(Id, key, args), true);
        pos->second.BaseVal = baseVal ? baseVal : Base ? Base->Lookup(key) : nullptr;
        return *pos;
    }

    void SetPathResolvedValue(const TStringBuf key, const TStringBuf args) {
        TYVar& yvar = (*this)[key];
        yvar.SetSingleVal(FormatCmd(Id, key, args), true);
        yvar[0].IsPathResolved = true;
    }

    void SetAppend(const TStringBuf& name, const TStringBuf& value) {
        if (Contains(name) && GetCmdValue(Get1(name)).size()) {
            Add1Sp(name, value);
        } else {
            ResetAppend(name, value);
        }
    }

    void ResetAppend(const TStringBuf& name, const TStringBuf& value) {
        const TYVar* baseVal;
        if (Base && (baseVal = Base->Lookup(name)) != nullptr && !baseVal->IsReservedName) {
            SetValue(name, TString::Join("$", name, " ", value), baseVal);
        } else {
            SetValue(name, value);
        }
    }

    void SetAppendStoreOriginals(const TStringBuf& name, const TString& val, TOriginalVars& orig) {
        SetAppend(name, val);
        orig.SetAppend(name, val);
    }

    void SetStoreOriginals(const TStringBuf& name, const TString& val, TOriginalVars& orig) {
        SetValue(name, val);
        orig.SetValue(name, val);
    }

    void SetAppend(const TStringBuf& name, const TVector<TStringBuf>& args) {
        SetAppend(name, EvalExpr(*this, args));
    }

    void RemoveFromScope(const TStringBuf& name) {
        erase(name);
    }

    void Append(const TStringBuf& k, const TVector<TStringBuf>& args) {
        (*this)[k].Append(args, false);
    }

    const TYVar* Lookup(const TStringBuf& name) const {
        auto varIt = find(name);
        if (varIt != end()) {
            TYVar* var = (TYVar*)&varIt->second;
            var->Id = Id; // tmp
            VarLookupHook(*var, name);
            return var;
        }

        if (Base) {
            auto var = Base->Lookup(name);
            if (var) {
                VarLookupHook(*var, name);
            }
            return var;
        }

        return nullptr;
    }

    const TVars* GetBase(ui64 id) const {
        //Cout << name << ": "  << (*this)[name] << Endl;
        const TVars* cur = this;
        while (cur->Id != id) {
            if (!(cur = cur->Base))
                return nullptr;
        }
        return cur;
    }

    TStringBuf Get1(const TStringBuf& name) const {
        return ::Get1(Lookup(name));
    }

    TString GetAll(const TStringBuf& name) const {
        return ::GetAll(Lookup(name));
    }

    TStringBuf EvalValue(const TStringBuf& name) const {
        return ::Eval1(Lookup(name));
    }

    TString EvalAll(const TStringBuf& name, bool defaultToName = false) const {
        auto var = Lookup(name);
        if (!var && defaultToName)
            return TString("$") + name;
        return ::EvalAll(var);
    }

    bool Has(const TStringBuf& name) const {
        return Lookup(name) != nullptr;
    }

    // Has without recurse
    bool Contains(const TStringBuf& name) const {
        if (contains(name))
            return true;
        return false;
    }

    bool IsTrue(const TStringBuf& name) const {
        const TYVar* val = Lookup(name);
        if (!val || val->empty())
            return false;
        if ((*val)[0].HasPrefix)
            return NYMake::IsTrue(GetCmdValue((*val)[0].Name));
        return NYMake::IsTrue((*val)[0].Name);
    }

    bool IsReservedName(const TStringBuf& name) const {
        const TYVar* val = Lookup(name);
        return val && val->IsReservedName;
    }

    bool IsIdDeeper(ui64 what, ui64 than) const {
        if (what == than)
            return false;
        const TVars* cur = this;
        while (cur) {
            if (cur->Id == what)
                return false;
            if (cur->Id == than)
                return true;
            cur = cur->Base;
        }
        Y_ASSERT(cur); // `what' and `than' must be from our vars hierarchy
        return false;
    }

    void Load(IInputStream* input) {
        Base = nullptr;
        ::Load(input, Id);
        ::Load(input, static_cast<THashMap<TString, TYVar>&>(*this));
    }

    void Save(IOutputStream* output) const {
        Y_ASSERT(Base == nullptr);
        ::Save(output, Id);
        ::Save(output, static_cast<const THashMap<TString, TYVar>&>(*this));
    }
};
