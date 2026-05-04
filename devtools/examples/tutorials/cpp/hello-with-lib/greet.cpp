#include "greet.h"

#include <util/generic/strbuf.h>
#include <util/stream/output.h>

void greet(IOutputStream& dest, TStringBuf name) {
    dest << "Hello " << name << Endl;
}
