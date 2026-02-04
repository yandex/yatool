#pragma once

#include "config_conditions.h"

#include <devtools/ymake/diag/dbg.h>
#include <devtools/ymake/diag/trace.ev.pb.h>
#include <devtools/ymake/lang/makelists/statement_location.h>
#include <devtools/ymake/vars.h> //FIXME: missing PEERDIR

#include <util/memory/segmented_string_pool.h>
#include <util/generic/algorithm.h>
#include <util/generic/deque.h>
#include <util/generic/ptr.h>
#include <util/generic/vector.h>
#include <util/generic/yexception.h>

struct IMemoryPool;

class TIncludeLoopException: public yexception {
public:
    TIncludeLoopException(const TVector<TStringBuf>& stack) {
        *this << "include loop detected: " << JoinStrings(stack.begin(), stack.end(), "\n -> ");
        Event.SetLoopId(0);
        TString type = ToString(EMNT_MakeFile);
        for (const auto& item : stack) {
            NEvent::TLoopItem& loopItem = *Event.AddLoopNodes();
            loopItem.SetName(TString(item));
            loopItem.SetType(type);
        }
    }

    NEvent::TLoopDetected& GetEvent() {
        return Event;
    }

private:
    NEvent::TLoopDetected Event;
};

class TIncludeController {
public:
    TIncludeController(TVector<TStringBuf>& stack, TStringBuf file)
        : Stack(&stack)
    {
        Stack->emplace_back(file);
        if (Find(*Stack, file) != Stack->end() - 1) {
            throw TIncludeLoopException(*Stack);
        }
    }

    // Constructor for ignored include
    explicit TIncludeController()
        : Stack(nullptr)
    {
    }

    bool Ignored() const {
        return Stack == nullptr;
    }

    TIncludeController(const TIncludeController&) = delete;
    TIncludeController& operator=(const TIncludeController&) = delete;

    TIncludeController(TIncludeController&&) = default;
    TIncludeController& operator=(TIncludeController&&) = default;


    ~TIncludeController() {
        if (Stack) {
            Stack->pop_back();
        }
    }

private:
    TVector<TStringBuf>* Stack;
};

/// make script evaluation context
/// handles core language constructs
///  var: SET SET_APPEND DEFAULT ENABLE DISABLE
/// cond: IF ELSEIF ELSE ENDIF
class TEvalContext {
private:
    using TSSPool = IMemoryPool;
    using TExtraSetters = std::function<bool(TStringBuf, const TVector<TStringBuf>&)>;

    TCondition& Condition;
    TVars LocalVars;
    TVars* CurrentNs;
    TOriginalVars OriginalVars; //stores original value of variables that can be recalculated in config conditions
    TOriginalVars* CurrentOriginal;
    TExtraSetters NsExtraSetters;
    enum EIfState {
        IfProcessing,      // IF() is false AND all previous ELSEIF() are false, searching some true or ELSE()
        IfProcessed,       // IF() is true OR some previous ELSEIF() is true - already processed
        IfProcessedByElse, // before ELSE() all IF/ELSEIF are false, processed ELSE()
        IfSkipedElseIf,    // last macro was ELSEIF() and it was skipped
        IfSkipedElse,      // last macro was ELSE() and it was skipped
    };
    TVector<EIfState> IfState;
    size_t SkipIfs;

    /// calculate boolean expression
    typedef TDeque<TStringBuf> TTokStream;
    const TString VarExpr(const TStringBuf& arg, bool just_parse);
    // in sequence of lowering priority
    // just_parse means value is known and we need just parse the rest of expr
    const TString PrimExpr(TTokStream& args, bool just_parse);
    const TString UnaryExpr(TTokStream& args, bool just_parse); // DEFINED
    bool BinaryExpr(TTokStream& args, bool just_parse);         // EQUAL STREQUAL MATCHES STARTS_WITH ENDS_WITH
    bool NotExpr(TTokStream& args, bool just_parse);            // NOT
    bool AndExpr(TTokStream& args, bool just_parse);            // AND
    bool OrExpr(TTokStream& args, bool just_parse);             // OR
    bool BoolExpr(const TVector<TStringBuf>& args, bool just_parse);

    bool VarStatement(const TStringBuf& name, TVector<TStringBuf>& args, TSSPool& pool);
    bool CondStatement(const TStringBuf& name, TVector<TStringBuf>& args, TSSPool& pool);

protected:
    THashMap<const void*, TStatementLocation> StatementContext;
    const TStatementLocation*  CurrentLocation = nullptr;

    /// called on unknown statements, reimplement
    virtual bool UserStatement(const TStringBuf& /* name */, const TVector<TStringBuf>& /* args */) {
        // throw
        return false;
    }
    void StartNamespace(TVars& vars, TOriginalVars& orig, TExtraSetters&& setters = {}) {
        CurrentNs = &vars;
        CurrentOriginal = &orig;
        NsExtraSetters = std::move(setters);
    }
    void RestoreDirNamespace() {
        CurrentNs = &LocalVars;
        CurrentOriginal = &OriginalVars;
        NsExtraSetters = {};
    }

    bool IsCondStatement(TStringBuf command) const;

    bool BranchTaken() const {
        return IfState.empty() || IfState.back() == IfProcessed || IfState.back() == IfProcessedByElse;
    }

public:
    TEvalContext(TCondition& condition)
        : Condition(condition)
        , CurrentNs(&LocalVars)
        , CurrentOriginal(&OriginalVars)
        , SkipIfs(0)
    {
    }
    virtual ~TEvalContext() {
    }
    virtual bool ShouldSkip(TStringBuf command) const;

    TVars& Vars() {
        return *CurrentNs;
    }
    const TVars& Vars() const {
        return *CurrentNs;
    }
    TOriginalVars& OrigVars() {
        return *CurrentOriginal;
    }

    const TStatementLocation& GetCurrentLocation() const {
        Y_ASSERT(CurrentLocation != nullptr);
        return *CurrentLocation;
    }

    virtual TIncludeController OnInclude(TStringBuf incFile, TStringBuf fromFile) = 0;

    // dereference and reconstruct
    void Deref(TVector<TStringBuf>& args, TSSPool& pool);

    // perform escaping of arguments to macro calls
    void EscapeArgs(TVector<TStringBuf>& args, TSSPool& pool);

    void SetCurrentLocation(const TStringBuf statement, const TStatementLocation& location);
    size_t GetStatementRow(const TStringBuf statement);
    size_t GetStatementColumn(const TStringBuf statement);


    /// called by lexer
    bool OnStatement(const TStringBuf& name, TVector<TStringBuf>& args, TSSPool& pool) {
        return CondStatement(name, args, pool);
    }
    static bool SetStatement(const TStringBuf& name, const TVector<TStringBuf>& args, TVars& vars, TOriginalVars& orig);
};
