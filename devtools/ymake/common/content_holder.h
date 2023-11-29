#pragma once

#include <util/generic/strbuf.h>
#include <util/generic/string.h>

class IContentHolder : private TNonCopyable {
public:
    virtual ~IContentHolder() = default;
    virtual TStringBuf GetContent() = 0;
    virtual TStringBuf GetAbsoluteName() = 0;       // for diagnostics and debug messages
};

class TStringContentHolder : public IContentHolder {
    TString Content;
    TString Name;

public:
    template <typename T, typename U>
    TStringContentHolder(T&& content, U&& name)
        : Content(std::forward<T>(content))
        , Name(std::forward<U>(name))
    {}

    ~TStringContentHolder() = default;

    TStringBuf GetContent() override {
        return Content;
    }

    TStringBuf GetAbsoluteName() override {
        return Name;
    }
};