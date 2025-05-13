grammar  TConf;

options {
    language=Cpp;
}

@parser::header
{
#include "TConfLexer.h"
#include <util/generic/string.h>
#include <util/stream/output.h>
}

@parser::members
{
    enum class EScopeKind {
        None = 0,
        Macro,
        Module,
        MultiModule,
        Foreach
    };

    inline bool IsNone(EScopeKind kind) {
        return kind == EScopeKind::None;
    }
    inline bool IsMacro(EScopeKind kind) {
        return kind == EScopeKind::Macro;
    }
    inline bool IsModule(EScopeKind kind) {
        return kind == EScopeKind::Module;
    }
    inline bool IsMultiModule(EScopeKind kind) {
        return kind == EScopeKind::MultiModule;
    }
    inline bool IsForeach(EScopeKind kind) {
        return kind == EScopeKind::Foreach;
    }

    enum class EForeachBlockKind {
        None = 0,
        Sections,
        Call
    };

    inline bool IsForeachNone(EForeachBlockKind kind) {
        return kind == EForeachBlockKind::None;
    }
    inline bool IsForeachSections(EForeachBlockKind kind) {
        return kind == EForeachBlockKind::Sections;
    }
    inline bool IsForeachCall(EForeachBlockKind kind) {
        return kind == EForeachBlockKind::Call;
    }

    EScopeKind ScopeKind = EScopeKind::None;
    EForeachBlockKind ForeachBlockKind = EForeachBlockKind::None;
    int BlockLevel = 0;
    bool InMultiModule = false;
}

entries:
    SP? entry ( EOL SP? entry )* EOF
    ;

entry:
    ( importStmt | stmt | macroDef | moduleDef | multiModuleDef )
    ;

stmt:
    (
    { BlockLevel == 1 && (IsMacro(ScopeKind) || IsModule(ScopeKind) || IsMultiModule(ScopeKind)) }? propStmt |
    { BlockLevel == 2 && InMultiModule && IsModule(ScopeKind) }? propStmt |
    { IsMultiModule(ScopeKind) }? moduleDefStmt |
    { IsMacro(ScopeKind) || IsModule(ScopeKind) }? callStmt |
    { IsMacro(ScopeKind) }? foreachStmt |
    { IsForeach(ScopeKind) && IsForeachNone(ForeachBlockKind) }? ( callStmt | extStmt ) |
    { IsForeach(ScopeKind) && IsForeachSections(ForeachBlockKind) }? extStmt |
    { !IsForeach(ScopeKind) && !IsMultiModule(ScopeKind) }? ( simpleStmt | whenStmt | selectStmt ) |
    comment | docComment )?
    ;

importStmt:
    '@import' SP string
    ;

propStmt:
    '.' propName SP? assignOp propValue
    ;

propName:
    ident
    ;

propValue:
    ( ~'\n' )*
    ;

comment:
    COMMENT
    ;

docComment:
    DOC_COMMENT
    ;

simpleStmt:
    assignStmt
    ;

simpleBlockStmt:
    { ++BlockLevel; } simpleBlock { --BlockLevel; }
    ;

simpleBlock:
    '{' SP? ( EOL SP? ( comment | simpleStmt | whenStmt | selectStmt )? )* EOL SP? '}' SP?
    ;

assignStmt:
    ident SP? assignOp rvalue
    ;

assignOp:
    ( '=' | ':=' | '+=' | '-=' )
    ;

rvalue:
    ( ~'\n' | RVAL_SYMBOL | '\\' '\r'? '\n' )*
    ;

callStmt:
    macroName SP? '(' SP? actualArgs SP? ')' SP?
    ;

actualArgs:
    ( ~'\n' )*
    ;

blockStmt:
    block
    ;

block:
    { ++BlockLevel; } '{' SP? ( EOL SP? stmt )* EOL SP? '}' { --BlockLevel; } SP?
    ;

whenStmt:
    whenClause ( ( EOL SP? comment? )+ elseWhenClause )* ( ( EOL SP? comment? )+ otherwiseClause )?
    ;

whenClause:
    'when' SP? '(' SP? logicExpr SP? ')' SP? simpleBlockStmt
    ;

elseWhenClause:
    'elsewhen' SP? '(' SP? logicExpr SP? ')' SP? simpleBlockStmt
    ;

otherwiseClause:
    'otherwise' SP? simpleBlockStmt
    ;

selectStmt:
    'select' SP? '(' SP? varRef SP? ')' SP? '{' SP? ( ( EOL SP? comment? )+ alternativeClause )+ ( ( EOL SP? comment? )+ defaultClause )? EOL SP? '}' SP?
    ;

alternativeClause locals[ const TString* selectVar ] @init { $selectVar = nullptr; } :
    string ( SP? '|' SP? string )* SP? '?' SP? simpleBlockStmt
    ;

defaultClause locals[ const TString* defaultCondition ] @init { $defaultCondition = nullptr; } :
    'default' SP? '?' SP? simpleBlockStmt
    ;

foreachStmt:
    'foreach' SP? '(' SP? ident SP? ':' SP? varRef SP? ')' SP?
    { auto saveScopeKind = ScopeKind; ScopeKind = EScopeKind::Foreach; ForeachBlockKind = EForeachBlockKind::None; } blockStmt { ScopeKind = saveScopeKind; }
    ;

extStmt:
    '[' SP? ext ( SP? ',' SP? ext )* SP? ']' SP? '=' rvalue
    ;

ext:
    '.' extName
    ;

extName:
    '*' | ident
    ;

macroDefSignature:
    { ScopeKind = EScopeKind::Macro; }  SP? macroName SP? '(' SP? ( formalArgs SP? )? ')'
    ;

macroDef:
    'macro' macroDefSignature SP? block { ScopeKind = EScopeKind::None; }
    ;

macroName:
    ident
    ;

formalArgs:
    vararg | formalArg ( SP? ',' SP? formalArg )* ( SP? ',' SP? vararg )?
    ;

vararg:
    argName '...'
    ;

formalArg:
    specArg | argName deepReplace? ( arraySpec | boolSpec ( SP? boolInit )? | SP? defaultInit )?
    ;

deepReplace:
    '{' modifiers '}'
    ;

modifiers:
    modifier ( ';' modifier )*
    ;

modifier:
    ( 'input' | 'output' | 'tool' | 'env' )
    ;

arraySpec:
    '[]'
    ;

boolSpec:
    '?'
    ;

boolInit:
    initTrue = string SP? ':' SP? initFalse = string
    ;

defaultInit:
    '=' SP? string
    ;

specArg:
    string
    ;

argName:
    ident
    ;

multiModuleDefSignature:
    { ScopeKind = EScopeKind::MultiModule; InMultiModule = true; } SP? moduleName
    ;

multiModuleDef:
    'multimodule' multiModuleDefSignature SP? block { InMultiModule = false; ScopeKind = EScopeKind::None; }
    ;

moduleDefStmt:
    moduleDef
    ;

moduleDefSignature:
    { ScopeKind = EScopeKind::Module; } SP? moduleName SP? ( ancestor SP? )?
    ;

moduleDef:
    'module' moduleDefSignature block { ScopeKind = InMultiModule ? EScopeKind::MultiModule : EScopeKind::None; }
    ;

moduleName:
    ident
    ;

ancestor:
    ':' SP? moduleName
    ;

logicExpr:
    logicOr | logicIn
    ;

logicOr:
    logicAnd ( SP? '||' SP? logicAnd )*
    ;

logicAnd:
    logicRel ( SP? '&&' SP? logicRel )*
    ;

logicRel:
    logicNot ( SP? relationOp SP? logicNot )?
    ;

logicNot:
    ( negationOp SP? )? logicTerm
    ;

logicTerm:
    logicVarRef | string | '(' SP? logicExpr SP? ')'
    ;

logicIn:
    ( logicVarRef | string ) SP inOp SP ( stringArray | varRef )
    ;

stringArray:
    '[' SP? ( string ( SP? ',' SP? string )* SP? )? ']'
    ;

logicVarRef:
    varRef
    ;

relationOp:
    '!=' | '=='
    ;

negationOp:
    '!'
    ;

inOp:
    'in'
    ;

varRef:
    '$' varName
    ;

varName:
    ident
    ;

ident:
    ID | 'macro' | 'module' | 'param' | 'input' | 'output' | 'tool' | 'in'
    ;

string:
    '"' stringContent '"'
    ;

stringContent:
    ( '\\' ~( '\n' ) | ~( '"' | '\n') )*
    ;

ID: [a-zA-Z_]([\-]*[a-zA-Z_0-9]+)*;
RVAL_SYMBOL: [0-9a-zA-Z_$(){}/\\[\]\-+=*?!@.,;:'"`|><~];
DOC_COMMENT: '###' ~[\n]*;
COMMENT: '#' ~[\n]*;
SP: ( ' ' | '\t' )+;
EOL: '\n';
RC: '\r' -> skip;
