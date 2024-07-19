#include "blacklist.h"

#include <util/digest/numeric.h>
#include <util/folder/pathsplit.h>
#include <util/generic/hash.h>
#include <util/generic/set.h>
#include <util/generic/strbuf.h>
#include <util/generic/string.h>
#include <util/string/split.h>
#include <util/string/strip.h>
#include <util/system/compat.h>

namespace NBlacklist {
    Y_FORCE_INLINE size_t ComputeIndex(TStringBuf path) {
        static_assert(sizeof(TStringBuf::value_type) == 1);
        Y_ASSERT(!path.empty());
        size_t index = path[0];
        if (path.size() > 1) {
            index |= (static_cast<size_t>(path[1]) << 8);
        }
        return index;
    }

    class TBucket {
    public:
        void Add(TStringBuf path) {
            if (!path.empty()) {
                Prefixes.emplace_back(path);
            }
        }

        const TString* IsValidPath(TStringBuf path) const {
            if (auto pathSize = path.size(); pathSize > 0) {
                for (auto& prefix : Prefixes) {
                    auto size = prefix.size();
                    if (pathSize == size || pathSize > size && path[size] == TPathSplitUnix::MainPathSep) {
                        if (path.StartsWith(prefix)) {
                            return &prefix;
                        }
                    }
                }
            }

            return nullptr;
        }
    private:
        TVector<TString> Prefixes;
    };

    TSvnBlacklist::TSvnBlacklist() : Buckets(256 * 256) {
        static_assert(sizeof(TStringBuf::value_type) == 1);
    }

    TSvnBlacklist::~TSvnBlacklist() {
    }

    void TSvnBlacklist::Clear() {
        BlackSet.clear();
        for (auto& p : Buckets) {
            p.Reset(nullptr);
        }
        IsValidHash = false;
    }

    void TSvnBlacklist::LoadFromString(TStringBuf content, TStringBuf file) {
        auto func = [this, file](TStringBuf token) {
            const auto pos = token.find('#');
            token = StripString(token.Head(pos));
            if (!token.empty()){
                TPathSplitUnix pathSplit(token);
                if (pathSplit.IsAbsolute) {
                    OnParserError(EParserErrorKind::AbsoultePath, token, file);
                } else if (pathSplit.empty() || pathSplit[0] == TStringBuf(".") || pathSplit[0] == TStringBuf("..")) {
                    OnParserError(EParserErrorKind::InvalidPath, token, file);
                } else {
                    TString path = pathSplit.Reconstruct();
                    if (path.empty()) {
                        OnParserError(EParserErrorKind::InvalidPath, token, file);
                    } else {
                        if (auto [_, ok] = BlackSet.emplace(path); ok) {
                            auto& bucket = Buckets[ComputeIndex(path)];
                            if (bucket.Get() == nullptr) {
                                bucket = MakeHolder<TBucket>();
                            }
                            bucket->Add(path);
                            IsValidHash = false;
                        }
                    }
                }
            }
        };
        StringSplitter(content).SplitBySet("\n\r").SkipEmpty().Consume(func);
    }

    const TString* TSvnBlacklist::IsValidPath(TStringBuf path) const {
        if (path.empty()) {
            return nullptr;
        }

        if (auto& bucket = Buckets[ComputeIndex(path)]; bucket) {
            return bucket->IsValidPath(path);
        }

        return nullptr;
    }

    size_t TSvnBlacklist::GetHash() const {
        if (IsValidHash) {
            return Hash;
        }

        Hash = 0;
        TSet<TStringBuf> set(BlackSet.cbegin(), BlackSet.cend());
        for (const auto& elem : BlackSet) {
            Hash = CombineHashes(Hash, ComputeHash(elem));
        }
        IsValidHash = true;
        return Hash;
    }

    void TSvnBlacklist::OnParserError(EParserErrorKind, TStringBuf, TStringBuf) {
        // Default implementation: do nothing
    }

} // NBlacklist
