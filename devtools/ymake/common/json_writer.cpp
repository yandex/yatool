#include "json_writer.h"

namespace {
    constexpr size_t UCharCount = 1ull << sizeof(unsigned char) * 8/*bit*/;

    using TReplace = const char*;
    using TReplaceTable = std::array<TReplace, UCharCount>;

    constexpr TReplaceTable CharReplaces() {
        TReplaceTable table{
            "\\u0000", "\\u0001", "\\u0002", "\\u0003", "\\u0004", "\\u0005", "\\u0006", "\\u0007",
            "\\b", "\\t", "\\n", "\\u000B", "\\f", "\\r", "\\u000E", "\\u000F",
            "\\u0010", "\\u0011", "\\u0012", "\\u0013", "\\u0014", "\\u0015", "\\u0016", "\\u0017",
            "\\u0018", "\\u0019", "\\u001A", "\\u001B", "\\u001C", "\\u001D", "\\u001E", "\\u001F",
        };
        table['"'] = "\\\"";
        table['\\'] = "\\\\";
        return table;
    }
}

NYMake::TJsonWriter::TJsonWriter(IOutputStream& out)
    : Out_(out)
    , Cur_(Buf_)
{}

NYMake::TJsonWriter::~TJsonWriter() {
    Flush();
}

void NYMake::TJsonWriter::WriteString(const TStringBuf& s) {
    WriteDirectly('"');
    if (!s.empty()) {
        static constexpr const auto CHAR_REPLACES = CharReplaces();
        const auto* pBeg = s.data();
        const auto* pEnd = pBeg + s.size();
        auto* pOfs = pBeg;
        auto* pCur = pBeg;
        while (pCur < pEnd) {
            const auto c = static_cast<unsigned char>(*pCur);
            const auto* charReplace = CHAR_REPLACES[c];
            if (charReplace) {
                if (pOfs < pCur) {
                    WriteDirectly(TStringBuf(pOfs, pCur - pOfs));
                }
                pOfs = pCur + 1;
                WriteDirectly(charReplace);
            }
            ++pCur;
        };
        if (pOfs < pEnd) {
            WriteDirectly(TStringBuf(pOfs, pEnd - pOfs));
        }
    }
    WriteDirectly('"');
}

void NYMake::TJsonWriter::WriteDirectly(const char c) {
    *Cur_++ = c;
    if (Cur_ >= Buf_ + sizeof(Buf_)) {
        DoFlushBuf();
    }
}

void NYMake::TJsonWriter::WriteDirectly(const TStringBuf& s) {
    auto size = s.size();
    auto longStr = size >= 256; //  string long enough for direct output to stream
    if (longStr || Cur_ + size >= Buf_ + sizeof(Buf_)) {
        FlushBuf();
    };
    if (longStr) { // don't copy long string to buffer, output it directly
        Out_ << s;
    } else {
        std::memcpy(Cur_, s.data(), size);
        Cur_ += size;
    }
}

void NYMake::TJsonWriter::WriteValue(const float value) {
    if (Y_UNLIKELY(!IsValidFloat(value))) {
        throw yexception() << "JSON writer: invalid float value: " << FloatToString(value);
    }
    WriteDirectly(FloatToString(value, PREC_NDIGITS, 6));
}

void NYMake::TJsonWriter::WriteValue(const double value) {
    if (Y_UNLIKELY(!IsValidFloat(value))) {
        throw yexception() << "JSON writer: invalid double value: " << FloatToString(value);
    }
    WriteDirectly(FloatToString(value, PREC_NDIGITS, 10));
}
