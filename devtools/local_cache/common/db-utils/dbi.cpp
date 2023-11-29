#include "dbi.h"

#include <util/generic/scope.h>
#include <util/string/split.h>
#include <util/string/strip.h>

namespace NCachesPrivate {
    using namespace NSQLite;

    template <typename It>
    struct TIsAsciiSpaceAdapterOrSemicolon {
        bool operator()(const It& it) const noexcept {
            return IsAsciiSpace(*it) || *it == ';';
        }
    };

    template <typename It>
    TIsAsciiSpaceAdapterOrSemicolon<It> IsAsciiSpaceOrSemicolonAdapter(It) {
        return {};
    }

    // Primitive parser of .sql resource files.
    // Intended to keep db logic better localized, smaller amount of C++ stuff intermingled.
    //
    // _Severely_ restricted syntax, sequence of:
    //   -- STMT: <name>
    //   <SINGLE_SQL_STMT>;
    TSmallVec<std::pair<TString, TString>> GetDbSeq(TString f) {
        TSmallVec<std::pair<TString, TString>> out;

        TString stmtName;
        TStringStream stmtBuf;
        bool spaceOnlySeen = true;
        constexpr char MARKER[] = "-- STMT: ";
        for (auto v : StringSplitter(f).Split('\n')) {
            TStringBuf token(v.Token());
            token = StripString(token);

            if (token.StartsWith(MARKER)) {
                Y_ABORT_UNLESS(spaceOnlySeen);
                stmtName = StripStringRight(token.substr(sizeof(MARKER) - 1));
                continue;
            }

            if (!token.StartsWith("--") && !token.Empty()) {
                spaceOnlySeen = false;
            }

            if (stmtName.empty()) {
                Y_ABORT_UNLESS(spaceOnlySeen);
            } else {
                auto outToken = token;
                bool isComment = false;
                if (token.StartsWith("--")) {
                    outToken = StripStringRight(v.Token(), IsAsciiSpaceOrSemicolonAdapter(v.Token().begin()));
                    isComment = true;
                } else {
                    outToken = v.Token();
                }
                stmtBuf << outToken;

                if (token.EndsWith(";") && !isComment) {
                    out.emplace_back(std::make_pair(stmtName, stmtBuf.Str()));
                    stmtName.clear();
                    stmtBuf.clear();
                    spaceOnlySeen = true;
                } else {
                    stmtBuf << Endl;
                }
            }
        }
        Y_ABORT_UNLESS(stmtName.empty() && stmtBuf.empty() || spaceOnlySeen);
        return out;
    }

    // Simple sequence w/o names.
    TSmallVec<TString> GetDbAnonymousSeq(TString f) {
        TSmallVec<TString> out;

        TStringStream stmtBuf;
        bool spaceOnlySeen = true;
        for (auto v : StringSplitter(f).Split('\n')) {
            TStringBuf token(v.Token());
            token = StripString(token);

            if (!token.StartsWith("--") && !token.Empty()) {
                spaceOnlySeen = false;
            }

            auto outToken = token;
            bool isComment = false;
            if (token.StartsWith("--")) {
                outToken = StripStringRight(v.Token(), IsAsciiSpaceOrSemicolonAdapter(v.Token().begin()));
                isComment = true;
            } else {
                outToken = v.Token();
            }
            stmtBuf << outToken;

            if (token.EndsWith(";") && !isComment) {
                out.emplace_back(stmtBuf.Str());
                stmtBuf.clear();
                spaceOnlySeen = true;
            } else {
                stmtBuf << Endl;
            }
        }
        Y_ABORT_UNLESS(stmtBuf.empty() || spaceOnlySeen);
        return out;
    }

    TStmtSeq::TStmtSeq(TSQLiteDB& db, const TVector<TStringBuf>& resources) {
        Y_DEFER {
            for (auto& s : Stmts_) {
                s.ResetHard();
            }
        };
        for (auto& resource : resources) {
            for (auto& v : GetDbAnonymousSeq(NResource::Find(resource))) {
                Stmts_.emplace_back(TSQLiteStatement(db, v));
                Stmts_.back().ResetHard();
                Stmts_.back().Execute();
            }
        }
    }
}
