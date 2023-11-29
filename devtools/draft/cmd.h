#pragma once

#include "split.h"
#include <util/generic/vector.h>

namespace NDev {

struct TOsCommand {
    TVector<TString> Args; // Arg[0] is program
    TString Stdin;
    TString Stdout;
};
typedef TVector<TOsCommand> TOsCommandList;

inline void GetCommandList(TOsCommandList& result, const TString& str) {
    // todo: deal with quoted strings, check escapes
    typedef TQuotedLines TLines;
    TLines lines(str);
    for (TLines::TIterator line = lines.Begin; line != lines.End; ++line) {
        TOsCommand cmd;
        //
        TWords words(*line);
        for (TWords::TIterator word = words.Begin; word != words.End; ++word) {
            if ((*word)[0] == '<') {
                if (*word == "<") {
                    ++word;
                    Y_ASSERT(word != words.End);
                    cmd.Stdin = *word;
                } else
                    cmd.Stdin = (*word).Tail(1);
            } else if ((*word)[0] == '>') {
                if (*word == ">") {
                    ++word;
                    Y_ASSERT(word != words.End);
                    cmd.Stdout = *word;
                } else
                    cmd.Stdout = (*word).Tail(1);
            } else {
                TStringBuf w = *word;
                // strip quotes, plain exec doesn't need them
                if (w[0] == '"' || w[0] == '\'')
                    w = w.SubStr(1, w.size() - 2);
                cmd.Args.push_back(TString{w});
            }
        }
        result.push_back(cmd);
    }
}

}

