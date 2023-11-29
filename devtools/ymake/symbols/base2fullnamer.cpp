#include "base2fullnamer.h"

#include <devtools/ymake/common/path_definitions.h>

#include <util/system/yassert.h>

TBase2FullNamer::TBase2FullNamer()
    : Fullname_()
    , PathLen_(0)
{}

void TBase2FullNamer::SetPath(const TStringBuf dirName) {
    size_t cap = 2048; // enough for almost all systems for one time allocation
    if (dirName.size() > cap) cap = dirName.size();
    cap += 1 + MAX_BASENAME_LEN;
    if (Fullname_.capacity() < cap) {
        Fullname_.reserve(cap);
    }
    Fullname_ = dirName;
    if (Fullname_.back() != NPath::PATH_SEP) {
        Fullname_.append(1, NPath::PATH_SEP);
    }
    PathLen_ = Fullname_.size();
}

TStringBuf TBase2FullNamer::GetFullname(const TStringBuf basename) {
    Y_ASSERT(PathLen_ != 0);
    Fullname_.resize(PathLen_);
    Fullname_.append(basename);
    return TStringBuf(Fullname_);
}
