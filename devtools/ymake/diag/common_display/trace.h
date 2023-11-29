#pragma once

#include <google/protobuf/message.h>

#include <util/generic/string.h>

namespace NYMake {
    using TProtoMessage = ::google::protobuf::Message;

    struct TProxyEvent {
        size_t ID;
        const TProtoMessage* Msg;

        template <class T>
        inline TProxyEvent(const T& ev)
            : ID(ev.ID)
            , Msg(&ev)
        {
        }

        inline operator const TProtoMessage&() const noexcept {
            return *Msg;
        }
    };

    TString EventToStr(const TProtoMessage& ev);

    void Trace(const TProxyEvent& ev);
    void Trace(const TString& ev);
}
