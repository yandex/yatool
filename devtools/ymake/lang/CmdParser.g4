parser grammar CmdParser;

options { tokenVocab = CmdLexer; }

main: argSep? cmds? EOF;
cmds: cmd (cmdSep cmd)* cmdSep?;
cmd: arg (argSep arg)* argSep?;
arg: (termsR | termsSQ | termsDQ)+; // Raw, Single-Quoted, Double-Quoted

argSep: ARG_SEP;
cmdSep: CMD_SEP ARG_SEP?;

termsR: (termR | termV | termS)+;
termsSQ: SQSTR_BEGIN (termSQR | termSQV | termSQS)* SQSTR_END;
termsDQ: DQSTR_BEGIN (termDQR | termDQV | termDQS)* DQSTR_END;

termR: TEXT_RAW | TEXT_NOP;
termV: TEXT_VAR;
termS: TEXT_EVL ((subModifier MOD_SEP)* subModifier MOD_END)? subBody EVL_END;

termSQR: SQSTR_RAW | SQSTR_NOP;
termSQV: SQSTR_VAR;
termSQS: SQSTR_EVL ((subModifier MOD_SEP)* subModifier MOD_END)? subBody EVL_END;

termDQR: DQSTR_RAW | DQSTR_NOP;
termDQV: DQSTR_VAR;
termDQS: DQSTR_EVL ((subModifier MOD_SEP)* subModifier MOD_END)? subBody EVL_END;

subModifier: subModKey (MOD_ARG subModValue)?;
subModKey: IDENTIFIER;
subModValue: (subModValueT | subModValueV | subModValueE)*;
subModValueT: MOD_ARG_VALUE_TEXT;
subModValueV: MOD_ARG_VALUE_VARIABLE;
subModValueE: MOD_ARG_VALUE_EVALUATION;
subBody: subBodyIdentifier | subBodyString;
subBodyIdentifier: IDENTIFIER;
subBodyString: STRING;
