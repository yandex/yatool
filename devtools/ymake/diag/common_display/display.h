#pragma once

#include <google/protobuf/message.h>

#include <util/system/spinlock.h>
#include <util/stream/output.h>

namespace NCommonDisplay{
    using TProtoMessage = ::google::protobuf::Message;

    class TLockedStream : private TNonCopyable {
    private:
        TAdaptiveLock Lock;
        TAtomicSharedPtr<IOutputStream> Output;

    public:
        TLockedStream();
        void SetStream(TAtomicSharedPtr<IOutputStream> stream);

        void Emit(const TStringBuf& str);
    };

    TLockedStream* LockedStream();
    void DisplayMsg(const TProtoMessage& msg);
}; //namespace NDisplay
