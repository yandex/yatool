#include "dep_types.h"

void TDeps::Add(EDepType depType, EMakeNodeType elemNodeType, ui64 elemId) {
    Y_ASSERT(!IsLocked());
    Deps.emplace_back(depType, elemNodeType, elemId);
    Uniq.insert(Back());
}

void TDeps::Add(const TDeps& what) {
    Y_ASSERT(!IsLocked());
    Deps.insert(end(), what.begin(), what.end()); // TODO: unique?
    Uniq.insert(what.Uniq.begin(), what.Uniq.end());
}

bool TDeps::AddUnique(EDepType depType, EMakeNodeType elemNodeType, ui64 elemId) {
    Y_ASSERT(elemId);
    Y_ASSERT(!IsLocked());
    TAddDepDescr dep = {depType, elemNodeType, elemId};
    if (Uniq.find(dep) == Uniq.end()) {
        Deps.emplace_back(dep);
        Uniq.insert(dep);
        return true;
    }
    return false;
}

void TDeps::Erase(iterator b, iterator e) {
    ForEach(b, e, [this](const auto& dep) {
        Uniq.erase(Uniq.find(dep));
    });
    Deps.erase(b, e);
}

void TDeps::Replace(size_t idx, EDepType depType, EMakeNodeType elemNodeType, ui64 elemId) {
    Uniq.erase(Uniq.find(Deps[idx]));
    Deps[idx] = {depType, elemNodeType, elemId};
    Uniq.insert(Deps[idx]);
}
