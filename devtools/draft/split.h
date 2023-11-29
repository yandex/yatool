#pragma once
#include <util/generic/strbuf.h>
#include <util/string/ascii.h>

namespace NDev {

/// @todo: generalize splitter
/// @todo: provide unittest

template <typename TIter>
struct TIterOps {
    TIter operator++(int) {
        TIter& self = (TIter&)(*this);
        TIter prev = self;
        ++self;
        return prev;
    }
};

// return stripped lines with Begin and End iterators with no copy
struct TLines {
    TLines(TStringBuf str, char delim = '\n')
        : Begin(str, delim)
        , End()
    {
    }

    struct TIterator
        : public TIterOps<TIterator>
    {
        TStringBuf Str;
        char Delim;

        TIterator(TStringBuf str, char delim)
            : Str(str)
            , Delim(delim)
        {
            Skip();
        }
        TIterator() {
        }

        void Skip() {
            while (Str.size() && IsAsciiSpace(Str[0]))
                Str.Skip(1);
            while (IsAsciiSpace(Str.back()))
                Str.Chop(1);
        }
        bool operator==(const TIterator& r) const {
            return !Str.size() && !r.Str.size();
        }
        bool operator!=(const TIterator& r) const {
            return !(*this == r);
        }
        TStringBuf operator*() const {
            TStringBuf h;
            TStringBuf t;
            if (Str.TrySplit(Delim, h, t))
                return h;
            return Str;
        }
        TIterator& operator++() {
            TStringBuf h;
            TStringBuf t;
            if (Str.TrySplit(Delim, h, t)) {
                Str = t;
                Skip();
            } else
                Str.Clear();
            return *this;
        }
    };

public:
    const TIterator Begin;
    const TIterator End;
    TIterator begin() const {
        return Begin;
    }
    TIterator end() const {
        return End;
    }
};

// analog to TLines that skips newlines in quoted strings
struct TQuotedLines {
    TQuotedLines(TStringBuf str)
        : Begin(str)
        , End()
    {
    }
    struct TIterator
        : public TIterOps<TIterator>
    {
        TStringBuf Str;
        size_t TokEnd;

        TIterator(TStringBuf str)
            : Str(str)
            , TokEnd(0)
        {
            Step();
        }
        TIterator() {
        }

        void Step() {
            Skip();
            if (Str.size())
                GetLine();
            else
                TokEnd = 0;
        }
        void Skip() {
            while (Str.size() && IsAsciiSpace(Str[0]))
                Str.Skip(1);
            // while (IsAsciiSpace(Str.back()))
            //    Str.Chop(1);
        }
        void GetLine() {
            Y_ASSERT(Str.size() && !IsAsciiSpace(Str[0]));
            TokEnd = 0;
            // fetch words
            while (TokEnd < Str.size()) {
                TokEnd = GetToken(TokEnd);
                while (TokEnd < Str.size()) {
                    // until whitespace contains newline
                    if (Str[TokEnd] == '\n')
                        break;
                    if (!IsAsciiSpace(Str[TokEnd]))
                        break;
                    ++TokEnd;
                }
                if (TokEnd < Str.size() && Str[TokEnd] == '\n')
                    break;
            }
        }
        size_t GetToken(size_t start) {
            TStringBuf str = Str.SubStr(start);
            Y_ASSERT(str.size() && !IsAsciiSpace(str[0]));
            size_t tokEnd;
            if (str[0] == '\'' || str[0] == '"') {
                const char quote = str[0];
                tokEnd = 1;
                for (tokEnd = 1; tokEnd < str.size(); ++tokEnd) {
                    if (str[tokEnd] == '\\') {
                        ++tokEnd;
                    } else if (str[tokEnd] == quote) {
                        ++tokEnd;
                        break;
                    }
                }
            } else {
                for (tokEnd = 0; tokEnd < str.size(); ++tokEnd) {
                    if (IsAsciiSpace(str[tokEnd]))
                        break;
                }
            }
            return tokEnd + start;
        }
        bool operator==(const TIterator& r) const {
            return !Str.size() && !r.Str.size();
        }
        bool operator!=(const TIterator& r) const {
            return !(*this == r);
        }
        TStringBuf operator*() const {
            return Str.Head(TokEnd);
        }
        TIterator& operator++() {
            Str.Skip(TokEnd);
            Step();
            return *this;
        }
    };

public:
    const TIterator Begin;
    const TIterator End;
};

// support (isspace) word sequences with quotes, e.g.:
// hello "world" ! "echo \\\"hello \n\\\"world\" 'quotes "string" '
struct TWords {
    TWords(TStringBuf str)
        : Begin(str)
        , End()
    {
    }

    struct TIterator
        : public TIterOps<TIterator>
    {
        TStringBuf Str;
        size_t TokEnd;

        TIterator(TStringBuf str)
            : Str(str)
            , TokEnd(0)
        {
            Step();
        }
        TIterator() {
        }

        void Step() {
            Skip();
            if (Str.size())
                GetToken();
            else
                TokEnd = 0;
        }
        void Skip() {
            while (Str.size() && IsAsciiSpace(Str[0]))
                Str.Skip(1);
        }
        void GetToken() {
            Y_ASSERT(Str.size() && !IsAsciiSpace(Str[0]));
            if (Str[0] == '\'' || Str[0] == '"') {
                const char quote = Str[0];
                TokEnd = 1;
                for (TokEnd = 1; TokEnd < Str.size(); ++TokEnd) {
                    if (Str[TokEnd] == '\\') {
                        ++TokEnd;
                    } else if (Str[TokEnd] == quote) {
                        ++TokEnd;
                        break;
                    }
                }
            } else {
                for (TokEnd = 0; TokEnd < Str.size(); ++TokEnd) {
                    if (IsAsciiSpace(Str[TokEnd]))
                        break;
                }
            }
        }
        bool operator==(const TIterator& r) const {
            return !Str.size() && !r.Str.size();
        }
        bool operator!=(const TIterator& r) const {
            return !(*this == r);
        }
        TStringBuf operator*() const {
            return Str.Head(TokEnd);
        }
        TIterator& operator++() {
            Str.Skip(TokEnd);
            Step();
            return *this;
        }
        using TIterOps::operator++;
    };

public:
    const TIterator Begin;
    const TIterator End;
};

}
