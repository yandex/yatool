#include "project_tree.h"

#include "guid.h"

#include <devtools/ymake/common/npath.h>

#include <util/generic/adaptor.h>
#include <util/generic/algorithm.h>
#include <util/generic/array_size.h>
#include <util/generic/strbuf.h>
#include <util/generic/string.h>
#include <util/generic/utility.h>
#include <util/generic/vector.h>
#include <util/system/defaults.h>
#include <util/system/yassert.h>

namespace NYMake {
    namespace NMsvs {

        namespace {

            const TStringBuf TYPE_GUIDS[] = {
                TStringBuf("2150E333-8FDC-42A3-9474-1A3956D46DE8"),  // T_DIR
                TStringBuf("8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942"),  // T_PROJECT
            };

            inline bool AddChild(TVector<TProjectTree::TNode>& pool, size_t& node,
                    const TStringBuf& name, const TStringBuf& path,
                    TProjectTree::TNode::EType type = TProjectTree::TNode::EType::T_DIR) {
                size_t parent = node;
                for (const auto& child: pool.at(parent).Children) {
                    if (pool.at(child).Name == name) {
                        if (pool.at(child).Type != type) {
                            ythrow TProjectTreeError(pool.at(child)) << "project/directory clash" << path;
                        }
                        node = child;
                        return false;
                    }
                }
                node = pool.size();
                pool.emplace_back(TProjectTree::TNode{parent, type, name, path, {}});
                pool.at(parent).Children.push_back(node);
                return true;
            }

            template <typename THandler>
            inline void TraverseNodes(THandler& handle, const TVector<TProjectTree::TNode>& pool, bool flat) {
                Y_ASSERT(pool);
                TVector<size_t> st(1, 0);
                st.reserve(pool.size());
                while (st) {
                    size_t node = st.back();
                    st.pop_back();
                    if (!flat || (pool.at(node).Children.size() != 1)) {
                        if (!handle(node)) {
                            break;
                        }
                    }
                    for (auto child: ::Reversed(pool.at(node).Children)) {
                        st.push_back(child);
                    }
                }
            }

            struct TNodeGetter {
                std::function<bool (const TProjectTree::TNode&)> Handle;
                const TVector<TProjectTree::TNode>& Pool;

                TNodeGetter(std::function<bool (const TProjectTree::TNode&)> handle,
                        const TVector<TProjectTree::TNode>& pool)
                    : Handle(handle)
                    , Pool(pool)
                {
                }

                bool operator ()(size_t node) const {
                    if (Pool.at(node).Type == TProjectTree::TNode::EType::T_ROOT) {
                        return true;
                    }
                    return Handle(Pool.at(node));
                }
            };

            struct TNodeKeyGetter {
                const TVector<TProjectTree::TNode>& Pool;

                TNodeKeyGetter(const TVector<TProjectTree::TNode>& pool)
                    : Pool(pool)
                {
                }

                inline const TStringBuf& operator ()(size_t node) const {
                    return Pool.at(node).Name;
                }
            };

            struct TNodeSorter {
                TVector<TProjectTree::TNode>& Pool;
                const TNodeKeyGetter NodeKeyGetter;

                TNodeSorter(TVector<TProjectTree::TNode>& pool)
                    : Pool(pool)
                    , NodeKeyGetter(pool)
                {
                }

                inline bool operator ()(size_t node) const {
                    ::SortBy(Pool.at(node).Children, NodeKeyGetter);
                    return true;
                }
            };

        };

        TString TProjectTree::TNode::Guid() const {
            return Path ? GenGuid(Path) : TString();
        }

        const TProjectTree::TNode& TProjectTree::GetParent(const TNode& node, bool flat) const {
            if (node.Type == TNode::EType::T_ROOT) {
                ythrow TProjectTreeError(node) << "cannot get parent of the root node";
            }
            size_t parent = node.Parent;
            if (flat) {
                while (Pool.at(parent).Children.size() <= 1) {
                    if (Pool.at(parent).Type == TNode::EType::T_ROOT) {
                        break;
                    }
                    parent = Pool.at(parent).Parent;
                }
            }
            return Pool.at(parent);
        }

        void TProjectTree::Traverse(std::function<bool (const TNode&)> handle, bool flat) const {
            Y_ASSERT(Sorted);
            TNodeGetter nodeGetter(handle, Pool);
            TraverseNodes(nodeGetter, Pool, flat);
        }

        const TProjectTree::TNode& TProjectTree::Add(const TStringBuf& path) {
            Y_ASSERT(!NPath::IsTypedPath(path));
            size_t node = 0;
            size_t bpos = 0;
            size_t len = 1;
            TStringBuf name;
            for (size_t pos = path.find(NPath::PATH_SEP); pos != TStringBuf::npos; pos = path.find(NPath::PATH_SEP, bpos)) {
                name = path.SubStr(bpos, pos - bpos);
                AddChild(Pool, node, name, path.Head(pos));
                ++len;
                bpos = pos + 1;
            }
            name = path.Tail(bpos);
            if (AddChild(Pool, node, name, path, TNode::EType::T_PROJECT)) {
                Sorted = false;
            }
            return Pool.at(node);
        }

        const TProjectTree::TNode& TProjectTree::Add(const TModuleNode& module) {
            Y_ASSERT(module.Name.IsType(NPath::ERoot::Build));
            return Add(module.LongName());
        }

        void TProjectTree::Sort() {
            if (Sorted) {
                return;
            }
            TNodeSorter nodeSorter(Pool);
            TraverseNodes(nodeSorter, Pool, false);
            Sorted = true;
        }

        TString GenGuid(TProjectTree::TNode::EType projectType) {
            size_t i = static_cast<size_t>(projectType);
            Y_ASSERT(i < Y_ARRAY_SIZE(TYPE_GUIDS));
            if (i < Y_ARRAY_SIZE(TYPE_GUIDS)) {
                return TString{TYPE_GUIDS[i]};
            }
            return TString();
        }

        TProjectTreeError::TProjectTreeError(const TProjectTree::TNode& node)
            : Node(node)
        {
        }

    }
}
