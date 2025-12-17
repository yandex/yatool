#include "dbg.h"

void AssertNoCall() {
    Y_ASSERT(false && "Should not be called");
}
