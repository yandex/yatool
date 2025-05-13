lexer grammar CmdLexer;

fragment ALPHA: [A-Za-z_];
fragment ALNUM: [A-Za-z_0-9];
fragment ID: ALPHA ALNUM*;
fragment SUBST: '$';
fragment SPACE: [\p{White_Space}];

LPAREN: '(';
RPAREN: ')';

TEXT_RAW: ('\\' . | ~([&'"\\] | [$()\p{White_Space}]))+ | '$'; // FIXME: the last part is a workaround for "$.server" etc. in makefiles
SQSTR_BEGIN: '\'' -> pushMode(SQSTR);
DQSTR_BEGIN: '"' -> pushMode(DQSTR);

TEXT_NOP: SUBST LPAREN ~[()]* RPAREN;
TEXT_VAR: SUBST ID;
TEXT_XFM: SUBST '{' -> pushMode(EVALUATION);

CMD_SEP: '&&';
ARG_SEP: SPACE+;

//

mode SQSTR;
SQSTR_END: '\'' -> popMode;
SQSTR_RAW: ('\\' . | ~[$'\\])+;
SQSTR_NOP: SUBST LPAREN ~[()]* RPAREN;
SQSTR_VAR: SUBST ID;
SQSTR_XFM: SUBST '{' -> pushMode(EVALUATION);

//

mode DQSTR;
DQSTR_END: '"' -> popMode;
DQSTR_RAW: ('\\' . | ~[$"\\])+;
DQSTR_NOP: SUBST LPAREN ~[()]* RPAREN;
DQSTR_VAR: SUBST ID;
DQSTR_XFM: SUBST '{' -> pushMode(EVALUATION);

//

mode EVALUATION;

XFM_END: '}' -> popMode;

MOD_ARG: '=' -> pushMode(MODARG);
MOD_SEP: ';';
MOD_END: ':';
STRING: '"' ('\\' . | ~["\\])* '"';
IDENTIFIER: ID;

//

mode MODARG;

MOD_ARG_VALUE_TEXT: ('\\' [$;:\\] | ~[$;:\\])+;
MOD_ARG_VALUE_VARIABLE: '$' ID;
MOD_ARG_VALUE_EVALUATION: '${' ID '}';
MOD_ARG_MOD_SEP: ';' -> popMode, type(MOD_SEP);
MOD_ARG_MOD_END: ':' -> popMode, type(MOD_END);
