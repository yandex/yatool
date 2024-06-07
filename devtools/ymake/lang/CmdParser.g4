parser grammar CmdParser;

options { tokenVocab = CmdLexer; }

main: argSep* cmdSep* scr? EOF;
scr: cmd (cmdSep+ cmd)* cmdSep*;
cmd: arg (argSep+ arg)* argSep*;
arg: (termsRO | termsSQ | termsDQ)+;
callArg: (termsRA | termsSQ | termsDQ)+;

argSep: ARG_SEP;
cmdSep: CMD_SEP ARG_SEP?;

// raw command sub-argument, raw call sub-argument, single-quoted string, double-quoted string
termsRO: (termO | termC | termV | termX)+;
termsRA: (termA | termC | termV | termX)+;
termsSQ: SQSTR_BEGIN (termSQR | termSQV | termSQX)* SQSTR_END;
termsDQ: DQSTR_BEGIN (termDQR | termDQV | termDQX)* DQSTR_END;

// a term for a...
// raw command argument, raw call argument, call, variable, transformation
termO: (TEXT_RAW | TEXT_NOP | LPAREN | RPAREN)+;
termA: (TEXT_RAW | TEXT_NOP)+;
termC: TEXT_VAR LPAREN argSep? (callArg (argSep callArg)*)? argSep? RPAREN;
termV: TEXT_VAR;
termX: TEXT_XFM ((xModifier MOD_SEP)* xModifier MOD_END)? xBody XFM_END;

termSQR: SQSTR_RAW | SQSTR_NOP;
termSQV: SQSTR_VAR;
termSQX: SQSTR_XFM ((xModifier MOD_SEP)* xModifier MOD_END)? xBody XFM_END;

termDQR: DQSTR_RAW | DQSTR_NOP;
termDQV: DQSTR_VAR;
termDQX: DQSTR_XFM ((xModifier MOD_SEP)* xModifier MOD_END)? xBody XFM_END;

xModifier: xModKey (MOD_ARG xModValue)?;
xModKey: IDENTIFIER;
xModValue: (xModValueT | xModValueV | xModValueE)*;
xModValueT: MOD_ARG_VALUE_TEXT;
xModValueV: MOD_ARG_VALUE_VARIABLE;
xModValueE: MOD_ARG_VALUE_EVALUATION;
xBody: xBodyIdentifier | xBodyString;
xBodyIdentifier: IDENTIFIER;
xBodyString: STRING;
