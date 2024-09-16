#pragma once

#include <util/generic/flags.h>

enum class ETraceEvent: ui64 {
    L = 0x001,
    U = 0x002,
    E = 0x008,
    D = 0x010,
    S = 0x020,
    P = 0x040,
    C = 0x200,
    d = L | U | E | D | S | P | C,

    H = 0x004,
    G = 0x080,
    T = 0x100,

    a = ~0u,
    A = a,
};
using TTraceEvents = TFlags<ETraceEvent>;
constexpr TFlags<ETraceEvent> operator| (ETraceEvent l, ETraceEvent r) noexcept {
    return TFlags<ETraceEvent>{l} | r;
}
