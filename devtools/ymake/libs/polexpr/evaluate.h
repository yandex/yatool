#pragma once

#include "expression.h"

#include <util/generic/string.h>
#include <util/generic/vector.h>
#include <util/stream/output.h>

namespace NPolexpr {
    namespace NDetail {
        struct TFuncFrame {
            TExpression::TNode FuncNode;
            ui16 FirstArg;
            ui16 LastArg;
        };
    }

    template<typename FVisitor>
    inline size_t VisitFnArgs(const TExpression& expr, size_t pos, TFuncId expected, FVisitor visitor) {
        auto& fNode = expr.GetNodes()[pos];
        if (fNode.GetType() != TExpression::TNode::EType::Function)
            return pos;
        auto fId = TFuncId::FromRepr(fNode.GetIdx());
        if (fId.GetIdx() != expected.GetIdx())
            return pos;
        auto fArity = fNode.GetArity();
        ++pos;
        for (size_t arg = 0; arg != fArity; ++arg)
            pos = visitor(pos);
        return pos;
    }

    struct TOptionalFnArgs {
        size_t FirstArgOrFnPos;
        size_t ArgCount;
    };
    inline TOptionalFnArgs GetFnArgs(const TExpression& expr, size_t pos, TFuncId expected) {
        auto& fNode = expr.GetNodes()[pos];
        if (fNode.GetType() != TExpression::TNode::EType::Function)
            return {pos, 0};
        auto fId = TFuncId::FromRepr(fNode.GetIdx());
        if (fId.GetIdx() != expected.GetIdx())
            return {pos, 0};
        return {++pos, fNode.GetArity()};
    }

    template<typename TValue, typename TEvalFunc>
    requires (
        std::is_convertible_v<std::invoke_result_t<TEvalFunc, TConstId>, TValue> &&
        std::is_convertible_v<std::invoke_result_t<TEvalFunc, EVarId>, TValue> &&
        std::is_convertible_v<std::invoke_result_t<TEvalFunc, TFuncId, std::span<const TValue>>, TValue>
    )
    std::pair<TValue, size_t> Evaluate(
        const TExpression& expr,
        size_t pos,
        TEvalFunc&& eval
    ) {
        TVector<NDetail::TFuncFrame> funcStack;
        TVector<TValue> argStack;
        TVector<TValue> storesStack;

        auto nodes = expr.GetNodes();
        while (pos < nodes.size()) {
            if (argStack.size() == 1 && funcStack.empty())
                break;
            auto& node = nodes[pos++];
            switch (node.GetType()) {
                case TExpression::TNode::EType::Constant:
                    argStack.push_back(eval(TConstId::FromRepr(node.GetIdx())));
                    break;
                case TExpression::TNode::EType::Variable:
                    argStack.push_back(eval(static_cast<EVarId>(node.GetIdx())));
                    break;
                case TExpression::TNode::EType::Function:
                    funcStack.push_back(
                        {.FuncNode = node,
                         .FirstArg = static_cast<ui16>(argStack.size()),
                         .LastArg =
                             static_cast<ui16>(argStack.size() + node.GetArity())});
                    break;
                case TExpression::TNode::EType::Backref:
                    argStack.push_back(storesStack[storesStack.size() - node.GetIdx()]);
                    break;
            }
            if (node.IsReferenced() && node.GetType() != TExpression::TNode::EType::Function) {
                storesStack.push_back(argStack.back());
            }

            while (!funcStack.empty() &&
                   funcStack.back().LastArg == argStack.size()) {
                const auto& func = TFuncId::FromRepr(funcStack.back().FuncNode.GetIdx());
                const size_t firstArgPos = funcStack.back().FirstArg;
                const bool isReferenced = funcStack.back().FuncNode.IsReferenced();
                auto args = std::span{argStack}.subspan(firstArgPos);
                funcStack.pop_back();
                TValue res = eval(func, args);
                argStack.erase(argStack.begin() + firstArgPos, argStack.end());
                argStack.push_back(std::move(res));
                if (isReferenced) {
                    storesStack.push_back(argStack.back());
                }
            }
        }

        Y_ASSERT(argStack.size() == 1 && funcStack.empty());
        return {argStack.front(), pos};
    }

    template<typename TValue, typename TEvalFunc>
    TValue Evaluate(const TExpression& expr, TEvalFunc&& eval) {
        auto result = Evaluate<TValue, TEvalFunc>(expr, 0, std::move(eval));
        Y_ASSERT(result.second == expr.GetNodes().size());
        return std::move(result.first);
    }

    template<typename TGetNameFunc>
    requires(
        std::is_convertible_v<std::invoke_result_t<TGetNameFunc, TConstId>, std::string_view> &&
        std::is_convertible_v<std::invoke_result_t<TGetNameFunc, EVarId>, std::string_view> &&
        std::is_convertible_v<std::invoke_result_t<TGetNameFunc, TFuncId>, std::string_view>
    )
    void Print(IOutputStream& oss, const TExpression& expr, TGetNameFunc&& getName, size_t highlightBegin = -1, size_t highlightEnd = -1) {
        TVector<NDetail::TFuncFrame> funcStack;
        ui16 argsStack = 0;
        ui32 nextRefIdx = 0;
        TVector<ui32> refsStack;
        TVector<ui32> refsOrder;

        // highlighter
        struct THightlighter {
            const char *Prefix = "    ";
            const char *Indent = "  ";
            int Level = 0;
            size_t Pos = 0;
            size_t Depth = 0;
        };
        THightlighter hl;
        auto hlMaybeBegin = [&]() {
            if (hl.Pos != size_t(-1) && hl.Pos++ == highlightBegin) {
                oss << "[[bad]]";
                hl.Depth = funcStack.size();
            }
        };
        auto hlMaybeEnd = [&]() {
            if (hl.Pos != size_t(-1) && hl.Pos == highlightEnd && hl.Depth == funcStack.size())
                hl.Pos = size_t(-1), oss << "[[rst]]";
        };
        auto hlNewLine = [&]() {
            oss << "\n" << hl.Prefix;
            for (int i = 0; i != hl.Level; ++i)
                oss << hl.Indent;
        };

        auto prettify = highlightBegin != highlightEnd;
        if (prettify)
            oss << hl.Prefix;

        for (TExpression::TNode node : expr.GetNodes()) {
            if (prettify)
                hlMaybeBegin();

            if (node.IsReferenced()) {
                oss << "[$" << nextRefIdx++ << " = ";
            }
            switch (node.GetType()) {
                case TExpression::TNode::EType::Constant:
                    oss << getName(TConstId::FromRepr(node.GetIdx()));
                    ++argsStack;
                    break;
                case TExpression::TNode::EType::Variable:
                    oss << '$' << getName(static_cast<EVarId>(node.GetIdx()));
                    ++argsStack;
                    break;
                case TExpression::TNode::EType::Function:
                    oss << getName(TFuncId::FromRepr(node.GetIdx())) << '(';
                    funcStack.push_back(
                        {.FuncNode = node,
                         .FirstArg = argsStack,
                         .LastArg = static_cast<ui16>(argsStack + node.GetArity())});
                    if (prettify) {
                        if (funcStack.back().LastArg - funcStack.back().FirstArg > 1) {
                            ++hl.Level;
                            hlNewLine();
                        }
                    }
                    break;
                case TExpression::TNode::EType::Backref:
                    oss << "$" << refsOrder[refsOrder.size() - static_cast<ui32>(node.GetIdx())];
                    ++argsStack;
                    break;
            }
            if (node.IsReferenced()) {
                if (node.GetType() == TExpression::TNode::EType::Function) {
                    refsStack.push_back(nextRefIdx - 1);
                } else {
                    oss << ']';
                    refsOrder.push_back(nextRefIdx - 1);
                }
            }

            while (!funcStack.empty() && funcStack.back().LastArg == argsStack) {
                if (prettify) {
                    hlMaybeEnd();
                    if (funcStack.back().LastArg - funcStack.back().FirstArg > 1) {
                        --hl.Level;
                        hlNewLine();
                    }
                }
                argsStack = funcStack.back().FirstArg + 1;
                const bool isReferenced = funcStack.back().FuncNode.IsReferenced();
                funcStack.pop_back();
                oss << ')';
                if (isReferenced) {
                    oss << ']';
                    refsOrder.push_back(refsStack.back());
                    refsStack.pop_back();
                }
            }
            if (prettify)
                hlMaybeEnd();
            if (!funcStack.empty() && funcStack.back().FirstArg != argsStack) {
                if (prettify) {
                    oss << ",";
                    hlNewLine();
                } else
                    oss << ", ";
            }
        }
    }
}
