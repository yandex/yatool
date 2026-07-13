#pragma once

#include <util/generic/vector.h>
#include <util/generic/strbuf.h>
#include <util/generic/string.h>
#include <util/string/split.h>


class TSplitString {
    using TParts = TVector<TStringBuf>;
public:
    TSplitString() = default;
    TSplitString(const TSplitString&) = default;
    TSplitString(TSplitString&&) = default;
    ~TSplitString() = default;

    TSplitString(const TString& str)
        : Data(str)
    {
    }
    TSplitString(const TStringBuf& str)
        : Data(TString{str})
    {
    }


    TSplitString& operator=(const TString& str) {
        Data = str;
        Parts.clear();
        return *this;
    }
    TSplitString& operator=(const TStringBuf& str) {
        Data = TString{str};
        Parts.clear();
        return *this;
    }

    void Split(char delim) {
       StringSplitter(Data).Split(delim).SkipEmpty().Collect(&Parts);
    }

    void SplitByString(const char* delim) {
       StringSplitter(Data).SplitByString(delim).SkipEmpty().Collect(&Parts);
    }

    void SplitBySet(const char* delim) {
       StringSplitter(Data).SplitBySet(delim).SkipEmpty().Collect(&Parts);
    }

    operator TString() {
        return Data;
    }

    operator TString() const {
        return Data;
    }

    operator TParts() {
        return Parts;
    }

    operator TParts() const {
        return Parts;
    }

private:
    TString Data;
    TParts Parts;
};
