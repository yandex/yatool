#pragma once

#include "common_display/display.h"

#include <util/system/spinlock.h>
#include <util/stream/output.h>
#include <util/generic/hash.h>

using namespace NCommonDisplay;

enum class EConfMsgType {
    Error,
    Warning,
    Info,
    Debug,
    Count
};

class TDisplay : private TNonCopyable {
private:
    TLockedStream* Stream;
    EConfMsgType Cutoff = EConfMsgType::Count;
    using TMsgType = std::pair<TStringBuf, TStringBuf>;
    static const TMsgType msgTypesAsString[4];

public:
    typedef TAutoPtr<IOutputStream> TStreamMessage;
    TStreamMessage NewConfMsg(EConfMsgType type, TStringBuf msg, TStringBuf path = TStringBuf(), size_t row = 0, size_t column = 0);

    void SetStream(TLockedStream* stream = nullptr);
    void SetCutoff(EConfMsgType val) noexcept {Cutoff = val;}

private:
    TStreamMessage PrepareStream(EConfMsgType msgType, TStringBuf sub, TStringBuf path, size_t row = 0, size_t column = 0);
};

TDisplay* Display();

// Use these for user-intended messages, that are not related to graph nodes
// and should not be persisted in graph cache
#define YErr() (*Display()->NewConfMsg(EConfMsgType::Error, TStringBuf()))
#define YWarn() (*Display()->NewConfMsg(EConfMsgType::Warning, TStringBuf()))
#define YInfo() (*Display()->NewConfMsg(EConfMsgType::Info, TStringBuf()))
//TODO(kikht): YDebug should probably be replaced with YDIAG
#define YDebug() (*Display()->NewConfMsg(EConfMsgType::Debug, TStringBuf()))
