#include "command_helpers.h"

namespace {
    TVector<TString> splitString(TStringBuf str, const TString &delimiter, bool cleanup) {
        TVector<TString> tokens;
        TString token;

        TString serviceChars = " \\'\"&";
        char quote = ' ';
        bool inQuotes = false;
        bool inEscape = false;

        for (size_t i = 0; i < str.size(); ++i) {
            if (str.compare(i, delimiter.size(), delimiter) == 0 && !inEscape && !inQuotes) {
                if (!token.empty()) {
                    tokens.push_back(std::move(token));
                    token.clear();
                }

                i += delimiter.size() - 1;
                continue;
            }
            if (!inEscape && str[i] == '\\') {
                inEscape = true;
                if (!cleanup) token += str[i];
                continue;
            }
            if (inEscape && !inQuotes && serviceChars.find(str[i], 0) == TString::npos) {
                token += '\\';
            }
            if (inEscape && inQuotes && str[i] != '\\' && str[i] != quote) {
                token += '\\';
            }
            if (inEscape) {
                token += str[i];
                inEscape = false;
                continue;
            }
            if (inQuotes && str[i] == quote) {
                if (!cleanup) token += str[i];
                inQuotes = false;
                quote = ' ';
                continue;
            }
            if (!inQuotes && (str[i] == '"' || str[i] == '\'')) {
                if (!cleanup) token += str[i];
                inQuotes = true;
                quote = str[i];
                continue;
            }
            token += str[i];
        }

        if (inQuotes) {
            throw std::runtime_error{"Invalid shell command"};
        }

        if (!token.empty()) {
            tokens.push_back(std::move(token));
        }

        return tokens;
    }
}

TVector<TVector<TString>> SplitCommandsAndArgs(TStringBuf cmd) {
    TVector<TVector<TString>> res;
    auto split = splitString(cmd, "&&", false);
    res.reserve(split.size());
    for(auto& str : split)
        res.push_back(splitString(str, " ", true));
    return res;
}

TVector<TString> SplitCommands(TStringBuf cmd) {
    TVector<TString> res;
    auto split = splitString(cmd, "&&", false);
    res.reserve(split.size());
    for(auto& str : split) {
        auto args = splitString(str, " ", false);
        res.emplace_back();
        for (size_t i = 0; i != args.size(); ++i) {
            res.back() += args[i];
            if (i + 1 != args.size())
                res.back() += " ";
        }
    }
    return res;
}

TVector<TString> SplitArgs(TStringBuf cmd) {
    return splitString(cmd, " ", true);
}
