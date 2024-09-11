#include "ragel_parser.h"
#include "cpp_includes_parser.h"

#include <devtools/ymake/diag/manager.h>

#include <util/generic/set.h>

TRagelIncludesParser::TRagelIncludesParser()
    : TIncludesParserBase("include", "#")
{
}

void TRagelIncludesParser::Parse(IContentHolder& file, TVector<TRagelInclude>& includes) const {
    ScanIncludes(includes, file);
}

void TRagelIncludesParser::ScanIncludes(TVector<TRagelInclude>& includes, IContentHolder& incFile) const {
    size_t i = 0;
    TStringBuf lineBuf;
    bool inSpecification = false;

    TSet<TString> uniqueSet;
    TStringBuf input = incFile.GetContent();

    {
        TVector<TString> cppIncludes;
        ScanCppIncludes(input, cppIncludes);
        for (const auto& include : cppIncludes) {
            includes.push_back(TRagelInclude(include, TRagelInclude::EKind::Cpp));
        }
    }

    while (input.ReadLine(lineBuf) && NeedParseLine(++i)) {
        size_t p = lineBuf.find_first_not_of(" \t");
        if (p == TString::npos) {
            continue;
        }

        lineBuf = lineBuf.SubStr(p);

        if (lineBuf.StartsWith("%%{")) {
            inSpecification = true;
        } else if (lineBuf.StartsWith("}%%")) {
            inSpecification = false;
        } else if (inSpecification && lineBuf.StartsWith(IncPrefix)) {
            TString inc;
            if (ParseNativeIncludeLine(lineBuf, inc, incFile)) {
                const bool inserted = uniqueSet.insert(inc).second;
                if (inserted) {
                    includes.push_back(TRagelInclude(inc, TRagelInclude::EKind::Native));
                }
            }
        }
    }
}

bool TRagelIncludesParser::ParseNativeIncludeLine(TStringBuf& lineBuf, TString& inc, IContentHolder& incFile) const {
    ChopIncludeComment(lineBuf, CommentSign);

    TVector<TStringBuf> parts;
    Split(lineBuf, " \t", parts);

    if (!IsPrefixMatches(parts, IncPrefix)) {
        return false;
    }

    TStringBuf incStr = parts.size() == 2 ? parts[1] : parts[2];

    //delete ; at the end
    if (incStr.back() == ';')
        incStr.Chop(1);

    char marker = incStr[0];

    try {
        // include FsmName "inputfile.rl";
        // The include statement can be used to draw in the statements of another FSM specification.
        // Both the name and input file are optional, however at least one must be given
        if (parts.size() == 2 && (isalpha(marker) || marker == '_')) {
            // input file is not provided
            return false;
        }
        AssertEx(marker == '"' && incStr.back() == '"', "file: " << incFile.GetAbsoluteName() << ": invalid line:\n"
                                                                 << lineBuf);

        TStringBuf target = incStr.SubStr(1, incStr.size() - 2);

        AssertEx(target.size(), "empty include: " << incFile.GetAbsoluteName());

        inc = TString{target};
        return true;
    } catch (TRuntimeAssertion& e) {
        YConfWarn(Incl) << "file:" << incFile.GetAbsoluteName() << " has wrong include:" << incStr << ". " << e.what() << Endl;
        return false;
    }
}
