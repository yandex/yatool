#pragma once

#include <util/generic/hash.h>
#include <util/generic/hash_set.h>
#include <util/generic/string.h>

struct TUsedReservedVars {
    using TSet = THashSet<TString>;
    using TMap = THashMap<TString, TSet>;
    THolder<TSet> FromCmd;
    THolder<TMap> FromVars;

    static void Expand(TMap& what, const TMap& how) {
        for (auto& var : how)
            what[var.first].insert(var.second.begin(), var.second.end());
    }

    static void Expand(TSet& what, const TMap& how) {
        TSet news;
        ExpandOnce(what, news, how);
        while (true) {
            size_t exsize = what.size();
            what.insert(news.begin(), news.end());
            if (what.size() == exsize)
                break;
            auto olds = std::exchange(news, {});
            ExpandOnce(olds, news, how);
        }
    }

private:
    static void ExpandOnce(const TSet& what, TSet& where, const TMap& how) {
        for (auto& item : what)
            if (auto expansion = how.find(item); expansion != how.end())
                where.insert(expansion->second.begin(), expansion->second.end());
    }
};
