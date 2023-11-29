#pragma once

#include "expression.h"

#include <util/generic/vector.h>
#include <util/generic/hash.h>

#include <algorithm>

namespace NPolexpr {

    template<typename T, typename TValue>
    concept ExpressionReducer = requires(T t, TFuncId func, TValue val) {
        {t.Condition(func)} -> std::same_as<bool>;
        {t.Evaluate(EVarId{})} -> std::same_as<TValue>;
        {t.Evaluate(func, std::span<TConstId>{})} -> std::same_as<TValue>;
        {t.Wrap(val)} -> std::same_as<TConstId>;
    };

    namespace NDetail {

        using TNodePos = size_t; // position in TExpression::Expr
        using TPosRange = std::pair<TNodePos, TNodePos>;

        inline TPosRange CutFirst(TNodePos n, TPosRange& r) {
            auto mid = r.first + n;
            Y_ASSERT(mid <= r.second);
            auto result = TPosRange(r.first, mid);
            r.first = mid;
            return result;
        }

        template<typename TValue, ExpressionReducer<TValue> TReducer>
        class TReduceIf {

        public:

            TReduceIf(const TExpression& expr, TReducer reducer)
                : Expr(expr)
                , Reducer(std::move(reducer))
            {
            }

            TExpression Run() {

                if (Expr.GetNodes().empty())
                    return {};

                auto range = TPosRange(0, Expr.GetNodes().size());
                auto tree = Unflatten(range);
                Y_ASSERT(range.second == range.first);

                TreeIx.resize(Expr.GetNodes().size());
                UpdateTreeIndex(tree);

                ReduceIf(tree);

                for (auto&& ref : Refs) {
                    ref.NewId = NewRefCount;
                    if (!ref.Removed)
                        ++NewRefCount;
                }
                Flatten(tree);

                return std::move(Result);

            }

        private: // args

            const TExpression& Expr;
            TReducer Reducer;

            TExpression Result;

        private: // state

            template<typename TNode>
            struct TTree {
                TNode Node;
                TVector<TTree> Children;
            };

            struct TExprTreeNode {
                TPosRange Range;
                TExpression::TNode Head;
            };
            using TExprTree = TTree<TExprTreeNode>;

            using TRefId = size_t;

            struct TRefInfo {

                // .first is the position of the referenced node;
                // .second is the position of the next non-descendant node,
                //         which is also the first node that sees the reference as `backref(1)`;
                TPosRange Range = {static_cast<TNodePos>(-1), static_cast<TNodePos>(-1)};

                TVector<TNodePos> Backrefs;

                TRefId NewId = static_cast<TRefId>(-1);
                bool Removed = false;

            };

            TVector<TExprTree*> TreeIx; // indexed by TNodePos

            // N.B. Refs[*].Range.second is a monotonically nondecreasing sequence
            THashMap<TNodePos, TRefId> RefIx;
            TVector<TRefInfo> Refs; // indexed by TRefId
            TRefId NewRefCount = 0;

        private: // implementation pieces

            TExprTree Unflatten(TPosRange& range) {
                Y_ASSERT(range.first < range.second);
                auto headPos = range.first;
                auto& head = Expr.GetNodes()[headPos];
                auto result = TExprTree{.Node = {.Head = head}};
                switch (head.GetType()) {
                    case TExpression::TNode::EType::Constant:
                        result.Node.Range = CutFirst(1, range);
                        break;
                    case TExpression::TNode::EType::Variable:
                        result.Node.Range = CutFirst(1, range);
                        break;
                    case TExpression::TNode::EType::Function: {
                        auto argRange = range;
                        CutFirst(1, argRange);
                        for (int i = 0; i != head.GetArity(); ++i)
                            result.Children.push_back(Unflatten(argRange));
                        result.Node.Range = CutFirst(argRange.first - range.first, range);
                        break;
                    }
                    case TExpression::TNode::EType::Backref: {
                        result.Node.Range = CutFirst(1, range);
                        Refs[Refs.size() - head.GetIdx()].Backrefs.push_back(headPos);
                        break;
                    }
                }
                if (head.IsRefereced()) {
                    RefIx[headPos] = Refs.size();
                    Refs.push_back({.Range = result.Node.Range});
                }
                return result;
            }

            void UpdateTreeIndex(TExprTree& cursor) {
                TreeIx[cursor.Node.Range.first] = &cursor;
                for (auto&& child : cursor.Children)
                    UpdateTreeIndex(child);
            }

            void ReduceIf(TExprTree& cursor) {
                auto& head = AsNodes(cursor).front();
                if (
                    head.GetType() == TExpression::TNode::EType::Function &&
                    Reducer.Condition(TFuncId::FromRepr(head.GetIdx()))
                )
                    Reduce(cursor);
                else
                    for (auto&& child : cursor.Children)
                        ReduceIf(child);
            }

            void Flatten(const TExprTree& cursor) {

                if (cursor.Node.Head.GetType() == TExpression::TNode::EType::Backref) {
                    auto refCnt = RefCountAt(cursor.Node.Range.first);
                    auto& ref = Refs[refCnt - cursor.Node.Head.GetIdx()];
                    auto newRefCnt = refCnt != Refs.size() ? Refs[refCnt].NewId : NewRefCount;
                    auto newBackref = newRefCnt - ref.NewId;
                    Result.Append(TExpression::TNode::Backref(static_cast<TExpression::ERefId>(newBackref)));
                } else
                    Result.Append(cursor.Node.Head);

                for (auto&& child : cursor.Children)
                    Flatten(child);

            }

            auto AsNodes(const TExprTree& cursor) {
                return Expr.GetNodes().subspan(
                    cursor.Node.Range.first,
                    cursor.Node.Range.second - cursor.Node.Range.first
                );
            }

            void Reduce(TExprTree& cursor) {

                for (auto&& child : cursor.Children) {
                    Reduce(child);
                    CleanUpRef(child);
                }

                if (cursor.Node.Head.GetType() == TExpression::TNode::EType::Backref) {
                    auto& ref = FollowBackref(cursor);
                    auto cnt = std::erase(ref.Backrefs, cursor.Node.Range.first); // TBD: O(N^2) ALERT
                    Y_ASSERT(cnt == 1);
                }

                cursor.Node.Head.Assign(Evaluate(cursor));
                Y_ASSERT(cursor.Node.Head.GetType() == TExpression::TNode::EType::Constant);

                for (auto&& child : cursor.Children)
                    TreeIx[child.Node.Range.first] = nullptr;
                cursor.Children = {};

            }

            void CleanUpRef(TExprTree& cursor) {
                if (!cursor.Node.Head.IsRefereced())
                    return;
                auto headContent = cursor.Node.Head.AsConst();
                auto& ref = Refs[RefIx.at(cursor.Node.Range.first)];
                Y_ASSERT(!ref.Removed);
                for (auto brPos : ref.Backrefs) {
                    auto brNode = TreeIx[brPos];
                    Y_ASSERT(brNode->Node.Head.GetType() == TExpression::TNode::EType::Backref);
                    brNode->Node.Head.Assign(headContent);
                }
                ref.Backrefs = {};
                ref.Removed = true;
                // in theory, we should also reset the cursor.Node.Head's "Referenced" flag here,
                // but this node is about to become irrelevant anyway
            }

            TConstId Evaluate(TExprTree& cursor) {
                auto& head = cursor.Node.Head;
                switch (head.GetType()) {
                    case TExpression::TNode::EType::Constant:
                        return head.AsConst();
                    case TExpression::TNode::EType::Variable:
                        return Reducer.Wrap(Reducer.Evaluate(static_cast<EVarId>(head.GetIdx())));
                    case TExpression::TNode::EType::Function: {
                        auto func = TFuncId::FromRepr(head.GetIdx());
                        TVector<TConstId> args;
                        args.reserve(cursor.Children.size());
                        for (auto&& child : cursor.Children)
                            args.push_back(child.Node.Head.AsConst());
                        return Reducer.Wrap(Reducer.Evaluate(func, std::span(args)));
                    }
                    case TExpression::TNode::EType::Backref: {
                        auto& ref = FollowBackref(cursor);
                        auto& refNode = *TreeIx[ref.Range.first];
                        Reduce(refNode); // TODO without forced reduction
                        head.Assign(refNode.Node.Head.AsConst());
                        return head.AsConst();
                    }
                }
            }

            TRefInfo& FollowBackref(const TExprTree& cursor) {
                Y_ASSERT(cursor.Node.Head.GetType() == TExpression::TNode::EType::Backref);
                auto refCnt = RefCountAt(cursor.Node.Range.first);
                return Refs[refCnt - cursor.Node.Head.GetIdx()];
            }

            TRefId RefCountAt(TNodePos pos) {
                return std::upper_bound(Refs.begin(), Refs.end(), pos,
                    [](TNodePos a, const TRefInfo& b) { return a < b.Range.second; }
                ) - Refs.begin();
            }

        };

    }

    template<
        typename TValue,
        ExpressionReducer<TValue> TReducer
    >
    TExpression ReduceIf(const TExpression& expr, TReducer reducer) {
        return NDetail::TReduceIf<TValue, TReducer>(expr, std::move(reducer)).Run();
    }

}
