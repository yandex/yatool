
#include "exception.h"

#include <util/generic/vector.h>

namespace NYexport {

TYExportException::TYExportException() {
    BackTrace_.Capture();
}

TYExportException::TYExportException(const std::string& what) : TYExportException() {
    *this << what;
}

const TBackTrace* TYExportException::BackTrace() const noexcept {
    return &BackTrace_;
}

TVector<TString> TYExportException::GetCallStack() const {
    char tmpBuf[1024];
    TVector<TString> trace;
    trace.reserve(BackTrace_.size());
    for (size_t i = 0; i < BackTrace_.size(); ++i) {
        TResolvedSymbol rs = ResolveSymbol(const_cast<void*>(BackTrace_.data()[i]), tmpBuf, sizeof(tmpBuf));
        trace.push_back(rs.Name);
    }
    return trace;
}

}
