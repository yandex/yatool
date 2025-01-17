#pragma once

#include <endian.h>

#if __BYTE_ORDER == __BIG_ENDIAN
    #define _ISbit(bit)   (1 << (bit))
#else
    #define _ISbit(bit)   ((bit) < 8 ? ((1 << (bit)) << 8) : ((1 << (bit)) >> 8))
#endif

enum {
    _ISupper = _ISbit (0),    /* UPPERCASE.  */
    _ISlower = _ISbit (1),    /* lowercase.  */
    _ISalpha = _ISbit (2),    /* Alphabetic.  */
    _ISdigit = _ISbit (3),    /* Numeric.  */
    _ISxdigit = _ISbit (4),   /* Hexadecimal numeric.  */
    _ISspace = _ISbit (5),    /* Whitespace.  */
    _ISprint = _ISbit (6),    /* Printing.  */
    _ISgraph = _ISbit (7),    /* Graphical.  */
    _ISblank = _ISbit (8),    /* Blank (usually SPC and TAB).  */
    _IScntrl = _ISbit (9),    /* Control character.  */
    _ISpunct = _ISbit (10),   /* Punctuation.  */
    _ISalnum = _ISbit (11)    /* Alphanumeric.  */
};
