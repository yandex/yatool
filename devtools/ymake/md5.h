#pragma once

#include "md5_debug.h"

#include <devtools/ymake/common/md5sig.h>

#include <library/cpp/digest/md5/md5.h>

class TMd5SigValue;

class TMd5UpdateValue {
public:
    friend class TMd5Value;

    inline TMd5UpdateValue(const TMd5SigValue& value);

    inline TMd5UpdateValue(const void* data, size_t size)
        : Data_(data), Size_(size)
    {}

    inline TMd5UpdateValue(const TStringBuf& value)
        : Data_(value.data()), Size_(value.size())
    {}

    inline TMd5UpdateValue(const TString& value)
        : Data_(value.data()), Size_(value.size())
    {}

    const void* Data() const {
        return Data_;
    }

    size_t Size() const {
        return Size_;
    }

private:
    const void* Data_;
    size_t Size_;
    const TNodeValueDebugOnly* Source_ = nullptr;
};

class TMd5Value : public TNodeValueDebugOnly {
private:
    MD5 Md5_;

public:
    friend void LogMd5ChangeImpl(const TMd5Value& value, const TNodeValueDebugOnly* source, TStringBuf reason);
    friend class TMd5SigValue;

    TMd5Value(TStringBuf valueName)
        : TNodeValueDebugOnly(valueName)
    {
        LogMd5Change(*this, nullptr, "TMd5Value::TMd5Value(TStringBuf)"sv);
    }

    TMd5Value(TNodeDebugOnly nodeDebug, TStringBuf valueName)
        : TNodeValueDebugOnly(nodeDebug, valueName)
    {
        LogMd5Change(*this, nullptr, "TMd5Value::TMd5Value(TNodeDebugOnly, TStringBuf)"sv);
    }

    TMd5Value(const TMd5Value& other)
        : TNodeValueDebugOnly(other, "TMd5Value::TMd5Value"sv)
        , Md5_(other.Md5_)
    {
        LogMd5Change(*this, &other, "TMd5Value::TMd5Value(const TMd5Value&)"sv);
    }

    TMd5Value& operator=(const TMd5Value& other) {
        TNodeValueDebugOnly::operator=(other);
        Md5_ = other.Md5_;
        LogMd5Change(*this, &other, "TMd5Value::operator="sv);
        return *this;
    }

    void Update(const void* data, size_t length, TStringBuf reason) {
        Md5_.Update(data, length);
        LogMd5Change(*this, nullptr, reason);
    }

    void Update(const TMd5UpdateValue& updateValue, TStringBuf reason) {
        Md5_.Update(updateValue.Data_, updateValue.Size_);
        LogMd5Change(*this, updateValue.Source_, reason);
    }

    TString ToBase64() const {
        MD5 copy = Md5_;
        TMd5Sig sig;
        copy.Final(sig.RawData);
        return Md5SignatureAsBase64(sig);
    }
};

class TMd5SigValue : public TNodeValueDebugOnly {
private:
    TMd5Sig Md5Sig_;

    friend void LogMd5ChangeImpl(const TMd5SigValue& value, const TNodeValueDebugOnly* source, TStringBuf reason);

public:
    friend class TMd5UpdateValue;
    friend class TMd5Value;

    TMd5SigValue(TNodeDebugOnly nodeDebug, TStringBuf valueName)
        : TNodeValueDebugOnly(nodeDebug, valueName)
    {
        LogMd5Change(*this, nullptr, "TMd5SigValue::TMd5SigValue(TNodeDebugOnly, TStringBuf)");
    }

    TMd5SigValue(TStringBuf valueName)
        : TNodeValueDebugOnly(valueName)
    {
        LogMd5Change(*this, nullptr, "TMd5SigValue::TMd5SigValue(TStringBuf)");
    }

    TMd5SigValue(const TMd5SigValue& other)
        : TNodeValueDebugOnly(other, "TMd5SigValue::TMd5SigValue"sv)
        , Md5Sig_(other.Md5Sig_)
    {
        LogMd5Change(*this, &other, "TMd5SigValue::TMd5SigValue"sv);
    }

    TMd5SigValue& operator= (const TMd5SigValue& other) {
        TNodeValueDebugOnly::operator=(other);
        Md5Sig_ = other.Md5Sig_;
        LogMd5Change(*this, &other, "TMd5SigValue::operator="sv);
        return *this;
    }

    void SetRawData(const ui8 rawData[16], TStringBuf reason) {
        memcpy(Md5Sig_.RawData, rawData, sizeof(Md5Sig_.RawData));
        LogMd5Change(*this, nullptr, reason);
    }

    bool operator< (const TMd5SigValue& other) const {
        return Md5Sig_ < other.Md5Sig_;
    }

    bool Empty() const {
        return Md5Sig_ == TMd5Sig{};
    }

    void CopyFrom(const TMd5Value& field) {
        MD5 md5Copy = field.Md5_;
        md5Copy.Final(Md5Sig_.RawData);
        LogMd5Change(*this, &field, "TMd5SigValue::CopyFrom"sv);
    }

    void MoveFrom(TMd5Value&& field) {
        field.Md5_.Final(Md5Sig_.RawData);
        LogMd5Change(*this, &field, "TMd5SigValue::MoveFrom"sv);
    }

    TString ToBase64() const {
        return Md5SignatureAsBase64(Md5Sig_);
    }

    const ui8* GetRawData() const {
        return Md5Sig_.RawData;
    }

    const TMd5Sig& GetRawSig() const {
        return Md5Sig_;
    }

    Y_SAVELOAD_DEFINE(Md5Sig_);
};

inline TMd5UpdateValue::TMd5UpdateValue(const TMd5SigValue& value)
    : Data_(value.Md5Sig_.RawData)
    , Size_(sizeof(value.Md5Sig_.RawData))
    , Source_(&value)
{
}
