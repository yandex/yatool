#pragma once
#include <util/generic/string.h>

namespace NNetNs {
#if defined(_linux_)
    void IfUp(const TString& ifname, const TString& ip, const TString& netmask);
#endif
}
