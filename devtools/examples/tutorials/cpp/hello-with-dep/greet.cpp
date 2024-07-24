#include "greet.h"

#include <fmt/format.h>

#include <util/generic/strbuf.h>
#include <util/stream/output.h>

void greet(IOutputStream& dest, TStringBuf name) {
    dest << fmt::format("Hello {}", name) << Endl;
}
