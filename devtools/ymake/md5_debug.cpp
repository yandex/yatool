#include "md5_debug.h"

#include "md5.h"

std::atomic<size_t> TNodeValueDebug::GlobalIndex_ = 1;

const TNodeValueDebug TNodeValueDebug::None{};

void LogMd5ChangeImpl(const TMd5Value& value, const TNodeValueDebugOnly* source, TStringBuf reason) {
    DEBUG_USED(value, source, reason);
    ui8 md5[16];
    MD5 copy = value.Md5_;
    copy.Final(md5);
    BINARY_LOG(UIDs, TMd5Change, value, source ? *source : TNodeValueDebug::None, reason, md5);
}

void LogMd5ChangeImpl(const TMd5SigValue& value, const TNodeValueDebugOnly* source, TStringBuf reason) {
    DEBUG_USED(value, source, reason);
    BINARY_LOG(UIDs, TMd5Change, value, source ? *source : TNodeValueDebug::None, reason, value.Md5Sig_.RawData);
}

namespace NDebugEvents {
    TMd5Change::TMd5Change(const TNodeValueDebug& destination, const TNodeValueDebug& source, TStringBuf reason, const ui8* md5)
        : Destination(destination), Source(source), Reason(TString{reason})
    {
        memcpy(Md5, md5, 16);
    }
}
