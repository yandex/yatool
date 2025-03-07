#pragma once

#include <devtools/ymake/polexpr/expression.h>

namespace NPolexpr {
    /// Builder class for constructing variadic fucntion call. It tracks argument count
    /// inside Append functions and adjust function call node arity once Build function
    /// called.
    /// All Append functions provided have the same signature and semantic as
    /// TExpression::Append
    class TVariadicCallBuilder {
    public:
        /// Constructs builder which adds call to variadic function func to the
        /// expression expr. Once the object is constructed no Append functions
        /// calls are allowed on the object expr untill this->Build is called.
        TVariadicCallBuilder(TExpression& expr, TFuncId func)
            : Expr{expr}
            , CallNodeIdx{expr.Expr.size()}
        {
            expr.Append(func);
        }

        /// Constructs builder which adds call to variadic function func to the
        /// expression edited by outerBuilder. Once the object is constructed no
        /// Append functions calls are allowed on the object outerBuilder untill
        /// this->Build is called.
        TVariadicCallBuilder(TVariadicCallBuilder& outerBuilder, TFuncId func)
            : TVariadicCallBuilder{outerBuilder.Expr, func}
        {
            OuterBuilder = &outerBuilder;
        }

        template<typename TId>
        void Append(TId id) {
            Expr.Append(id);
            OnSubexpr(Expr.Expr.back().GetArity());
        }

        template<typename TId>
        auto Append(TExpression::TRefsRegistry& refs, TId id) {
            const auto res = Expr.Append(refs, id);
            OnSubexpr(Expr.Expr.back().GetArity());
            return res;
        }

        void Append(TExpression::TRefsRegistry& refs, TExpression::ERef id) {
            Expr.Append(refs, id);
            OnSubexpr(0);
        }

        /// Finalises variadic function call construction and releases expression
        /// or another bulder assed to this object constructor.
        template<typename TFuncName>
        void Build() {
            TExpression::TNode& callNode = Expr.Expr[CallNodeIdx];
            callNode = TExpression::TNode::Function(TFuncId{ArgsAdded, static_cast<TFuncName>(callNode.GetIdx())});
            if (OuterBuilder) {
                OuterBuilder->OnSubexpr(0);
            }
        }

        /// Finalises variadic function call construction and releases expression
        /// or another bulder assed to this object constructor.
        /// Returns reference to the constructed variadic call result which ca be
        /// reused in the same expression later.
        template<typename TFuncName>
        TExpression::ERef Build(TExpression::TRefsRegistry& registry) {
            Build<TFuncName>();
            Expr.Expr[CallNodeIdx].SetReferenced();
            return registry.MakeRef();
        }

    private:
        void OnSubexpr(ui16 arity) {
            --CurArgNodesRemains;
            CurArgNodesRemains += arity;
            if (CurArgNodesRemains == 0) {
                ++ArgsAdded;
                CurArgNodesRemains = 1;
            }
        }

    private:
        TExpression& Expr;
        size_t CallNodeIdx;
        TVariadicCallBuilder* OuterBuilder{nullptr};
        ui16 ArgsAdded{0};
        ui32 CurArgNodesRemains{1};
    };
}
