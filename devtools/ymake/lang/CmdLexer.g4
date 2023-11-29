lexer grammar CmdLexer;

fragment ALPHA: [A-Za-z_];
fragment ALNUM: [A-Za-z_0-9];

TEXT_RAW: ('\\' . | ~[&$'"\\\p{White_Space}])+;
SQSTR_BEGIN: '\'' -> pushMode(SQSTR);
DQSTR_BEGIN: '"' -> pushMode(DQSTR);

TEXT_NOP: '$(' ~[)]* ')';
TEXT_VAR: '$' ALPHA ALNUM*;
TEXT_EVL: '${' -> pushMode(EVALUATION);

CMD_SEP: '&&';
ARG_SEP: [\p{White_Space}]+;

//

mode SQSTR;
SQSTR_END: '\'' -> popMode;
SQSTR_RAW: ('\\' . | ~[$'\\])+;
SQSTR_NOP: '$(' ~[)]* ')';
SQSTR_VAR: '$' ALPHA ALNUM*;
SQSTR_EVL: '${' -> pushMode(EVALUATION);

//

mode DQSTR;
DQSTR_END: '"' -> popMode;
DQSTR_RAW: ('\\' . | ~[$"\\])+;
DQSTR_NOP: '$(' ~[)]* ')';
DQSTR_VAR: '$' ALPHA ALNUM*;
DQSTR_EVL: '${' -> pushMode(EVALUATION);

//

mode EVALUATION;

EVL_END: '}' -> popMode;

MOD_ARG: '=' -> pushMode(MODARG);
MOD_SEP: ';';
MOD_END: ':';
STRING: '"' ('\\' . | ~["\\])* '"';
IDENTIFIER: ALPHA ALNUM*;

//

mode MODARG;

MOD_ARG_VALUE_TEXT: ('\\' [$;:\\] | ~[$;:\\])+;
MOD_ARG_VALUE_VARIABLE: '$' ALPHA ALNUM*;
MOD_ARG_VALUE_EVALUATION: '${' ALPHA ALNUM* '}';
MOD_ARG_MOD_SEP: ';' -> popMode, type(MOD_SEP);
MOD_ARG_MOD_END: ':' -> popMode, type(MOD_END);
