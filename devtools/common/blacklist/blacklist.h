#pragma once

#include <util/generic/hash_set.h>
#include <util/generic/ptr.h>
#include <util/generic/strbuf.h>
#include <util/generic/string.h>
#include <util/generic/vector.h>

namespace NBlacklist {

    enum class EParserErrorKind {
        Ok = 0,
        AbsolutePath,
        InvalidPath,
    };

    class TBucket;

    class TSvnBlacklist {
    public:
        TSvnBlacklist();
        virtual ~TSvnBlacklist();

        void Clear();
        bool Empty() const noexcept { return BlackSet.empty(); }
        void LoadFromString(TStringBuf content, TStringBuf file);
        const TString* IsValidPath(TStringBuf path) const;
        size_t GetHash() const;

        virtual void OnParserError(EParserErrorKind kind, TStringBuf path, TStringBuf file);
    private:
        THashSet<TString> BlackSet;
        TVector<THolder<TBucket>> Buckets;
        mutable size_t Hash = 0;
        mutable bool IsValidHash = false;
    };

} // NBlacklist
