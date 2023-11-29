#pragma once

#include "file.h"
#include "error.h"

#include <util/generic/hash.h>
#include <util/generic/strbuf.h>
#include <util/generic/string.h>
#include <util/generic/yexception.h>
#include <util/system/types.h>

namespace NYMake {
    namespace NMsvs {
        struct TObj {
            TString FlatName;
            TString IntDir;

            TString Path() const;
        };

        struct TLib {
            TString LibPath;

            const TString& Path() const {
                return LibPath;
            }
        };

        template <typename TFileType>
        class TFilePool {
        private:
            THashMap<TNodeId, TFileType> Map;

        public:
            void Add(const TFile& file, const TFileType& obj) {
                auto inserted = Map.insert({file.Id(), obj});
                if (!inserted.second) {
                    ythrow TMsvsError() << "File already added to pool: " << file.Name;
                }
            }

            TString Path(const TFile& file) const {
                auto found = Map.find(file.Id());
                if (found == Map.end()) {
                    ythrow TMsvsError() << "File not found in pool: " << file.Name;
                }
                return found->second.Path();
            }

            bool Contains(const TFile& file) const {
                auto found = Map.find(file.Id());
                return found != Map.end();
            }
        };
    }
}

