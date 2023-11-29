#include "md5sig.h"

TString Md5SignatureAsBase64(const TMd5Sig& s) {
    TString r = TString::Uninitialized(24);
    Base64EncodeUrl(r.begin(), s.RawData, sizeof(s.RawData));
    r.resize(22); // cut two useless equal signs '='
    return r;
}
