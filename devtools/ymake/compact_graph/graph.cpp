#include "graph.h"

template <>
void Out<TNodeId>(IOutputStream& os, TTypeTraits<TNodeId>::TFuncParam v) {
    os << ToUnderlying(v);
}
