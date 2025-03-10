#pragma once

#include "ids.h"

#include <util/generic/vector.h>
#include <util/ysaveload.h>

#include <span>

namespace NPolexpr {
    class TVariadicCallBuilder;

    class TExpression {
    public:
        friend class TVariadicCallBuilder;

        enum class ERef: ui32 {};
        enum class ERefId: ui32 {};
        class TRefsRegistry;
        class TSubexprRef;

        class TNode {
        public:
            enum class EType { Constant = 0,
                               Variable = 1,
                               Function = 2,
                               Backref = 3 };
        public:
            TNode() noexcept = default;

            constexpr EType GetType() const noexcept {
                return static_cast<EType>(Attrs.Type);
            }

            constexpr ui32 GetIdx() const noexcept {
                return GetType() == EType::Function ? TFuncId::FromRepr(Attrs.Idx).GetIdx()
                                                    : Attrs.Idx;
            }

            constexpr ui16 GetArity() const noexcept {
                return GetType() == EType::Function ? TFuncId::FromRepr(Attrs.Idx).GetArity()
                                                    : 0;
            }

            constexpr bool IsReferenced() const noexcept {return Attrs.Referenced;}
            constexpr void SetReferenced() noexcept {Attrs.Referenced = true;}

            // TODO? other node types, too
            TConstId AsConst() const {
                Y_ASSERT(GetType() == EType::Constant);
                return TConstId::FromRepr(GetIdx());
            }

            EVarId AsVar() const {
                Y_ASSERT(GetType() == EType::Variable);
                return static_cast<NPolexpr::EVarId>(GetIdx());
            }

            void Assign(TConstId id) {
                Attrs.Type = static_cast<ui32>(EType::Constant);
                Attrs.Idx = id.GetRepr();
            }

            void Assign(EVarId id) {
                Attrs.Type = static_cast<ui32>(EType::Variable);
                Attrs.Idx = static_cast<ui32>(id);
            }

            static TNode Backref(ERefId id) noexcept {
                return TNode{EType::Backref, false, static_cast<ui32>(id)};
            }

            static TNode Constant(TConstId id) noexcept {
                return TNode{EType::Constant, false, id.GetRepr()};
            }

            static TNode Var(EVarId id) noexcept {
                return TNode{EType::Variable, false, static_cast<ui32>(id)};
            }

            static TNode Function(TFuncId id) noexcept {
                return TNode{EType::Function, false, id.GetRepr()};
            }

            Y_SAVELOAD_DEFINE(Attrs.AllBits);

        private:
            union TNodeAttrs {
                ui32 AllBits = 0;
                struct {
                    ui32 Type : 2;
                    ui32 Referenced: 1;
                    ui32 Idx : 29;
                };
            } Attrs;

            constexpr TNode(EType EType, bool store, ui32 idx) noexcept {
                Attrs.Type = static_cast<ui32>(EType);
                Attrs.Referenced = store;
                Attrs.Idx = idx;
            }
        };

        Y_SAVELOAD_DEFINE(Expr);

    public:
        ui32 Tag() const {
            return 0;
        }

        void Append(TNode node) {
            Expr.push_back(node);
        }

        void Append(TConstId id) {
            Expr.push_back(TNode::Constant(id));
        }
        ERef Append(TRefsRegistry& refs, TConstId id);

        void Append(EVarId id) {
            Expr.push_back(TNode::Var(id));
        }
        ERef Append(TRefsRegistry& refs, EVarId id);

        void Append(TFuncId id) {
            Expr.push_back(TNode::Function(id));
        }
        TSubexprRef Append(TRefsRegistry& refs, TFuncId id);

        void Append(const TRefsRegistry& refs,  ERef ref);

        std::span<const TNode> GetNodes() const noexcept {
            return Expr;
        }

    private:
        TVector<TNode> Expr;
    };

    class TExpression::TRefsRegistry {
    public:
        TExpression::ERef MakeRef() noexcept {
            return static_cast<ERef>(NextRefNum++);
        }
        TExpression::ERefId CalcRefId(ERef ref) const noexcept {
            return static_cast<ERefId>(NextRefNum - static_cast<ui32>(ref));
        }

    private:
        ui32 NextRefNum = 0;
    };

    inline TExpression::ERef TExpression::Append(TRefsRegistry& refs, TConstId id) {
        auto node = TNode::Constant(id);
        node.SetReferenced();
        Expr.push_back(node);
        return refs.MakeRef();
    }

    inline TExpression::ERef TExpression::Append(TRefsRegistry& refs, EVarId id) {
        auto node = TNode::Var(id);
        node.SetReferenced();
        Expr.push_back(node);
        return refs.MakeRef();
    }

    class TExpression::TSubexprRef {
    public:
        TSubexprRef(TRefsRegistry& registry) noexcept: Registry{registry} {}
        ERef Finish() const noexcept {return Registry.MakeRef();}
    private:
        TRefsRegistry& Registry;
    };

    inline TExpression::TSubexprRef TExpression::Append(TRefsRegistry& refs, TFuncId id) {
        auto node = TNode::Function(id);
        node.SetReferenced();
        Expr.push_back(node);
        return TSubexprRef{refs};
    }

    inline void TExpression::Append(const TRefsRegistry& refs,  ERef ref) {
        Expr.push_back(TNode::Backref(refs.CalcRefId(ref)));
    }

}
