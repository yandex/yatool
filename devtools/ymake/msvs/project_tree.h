#pragma once

#include "module.h"

#include <util/generic/strbuf.h>
#include <util/generic/string.h>
#include <util/generic/vector.h>
#include <util/generic/utility.h>
#include <util/generic/yexception.h>

#include <functional>

namespace NYMake {
    namespace NMsvs {

        class TProjectTree {
        public:
            using TNodes = TVector<size_t>;

            struct TNode {
                enum EType: ui8 {
                    T_DIR = 0,
                    T_PROJECT,
                    T_ROOT,
                };

                size_t Parent;
                const EType Type;
                const TStringBuf Name;
                const TStringBuf Path;

                TNodes Children;

                TString Guid() const;
            };

        private:
            TVector<TNode> Pool = {TNode{0, TNode::EType::T_ROOT, {}, {}, {}}};
            bool Sorted = true;

        public:
            void Traverse(std::function<bool (const TNode&)> handle, bool flat = true) const;
            const TNode& GetParent(const TNode& node, bool flat = true) const;

            const TNode& Add(const TStringBuf& path);
            const TNode& Add(const TModuleNode& module);
            void Sort();
        };

        struct TProjectTreeError: public yexception {
            const TProjectTree::TNode Node;

            explicit TProjectTreeError(const TProjectTree::TNode& node);
        };

        TString GenGuid(TProjectTree::TNode::EType projectType);
    }
}
