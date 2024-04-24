#include "spdx.h"

#include <devtools/ymake/common/split.h>

#include <util/generic/algorithm.h>
#include <util/generic/array_ref.h>

namespace {

    enum class ETokenType {
        LBrk,
        RBrk,
        And,
        Or,
        With,
        LicId
    };

    std::pair<TStringBuf, TStringBuf> SplitToken(TStringBuf licenseExpr) noexcept {
        while (!licenseExpr.empty() && std::isspace(licenseExpr[0])) {
            licenseExpr = licenseExpr.substr(1);
        }
        if (licenseExpr.empty()) {
            return {};
        }

        if (EqualToOneOf(licenseExpr.front(), '(', ')')) {
            return {licenseExpr.substr(0, 1), licenseExpr.substr(1)};
        }

        size_t splitPos = 0;
        while (splitPos < licenseExpr.size() && !std::isspace(licenseExpr[splitPos]) && !EqualToOneOf(licenseExpr[splitPos], '(', ')')) {
            ++splitPos;
        }
        return {licenseExpr.substr(0, splitPos), licenseExpr.substr(splitPos)};
    }

    enum class Op {
        None,
        Or,
        And
    };

    struct TParserStackFrame {
        TVector<NSPDX::TLicenseProps> SubexprProps;
        size_t LastSubexprSize = 0;
        Op UnhandledOp = Op::Or;
    };

    TParserStackFrame StackReduce(TParserStackFrame prev, TParserStackFrame top) {
        switch (std::exchange(prev.UnhandledOp, Op::None)) {
            case Op::Or:
                prev.SubexprProps.insert(prev.SubexprProps.end(), top.SubexprProps.begin(), top.SubexprProps.end());
                prev.LastSubexprSize = top.SubexprProps.size();
                break;
            case Op::And: {
                Y_ASSERT(prev.SubexprProps.size() >= prev.LastSubexprSize); // No LHS for `... AND (Subexpr)` must be handled before stack push
                const auto last_ref = TArrayRef<NSPDX::TLicenseProps>{prev.SubexprProps}.last(prev.LastSubexprSize);
                const TVector<NSPDX::TLicenseProps> last{last_ref.begin(), last_ref.end()};
                prev.SubexprProps.resize(prev.SubexprProps.size() - prev.LastSubexprSize);
                for (const auto& prop: top.SubexprProps) {
                    for (const auto& last_elem: last) {
                        prev.SubexprProps.push_back(last_elem);
                        prev.SubexprProps.back().Static |= prop.Static;
                        prev.SubexprProps.back().Dynamic |= prop.Dynamic;
                    }
                }
                prev.LastSubexprSize *= top.SubexprProps.size();
                break;
            }
            case Op::None:
                throw NSPDX::TExpressionError() << "Missing operator between ')' and '('";
                break;
        }
        return prev;
    }

}

namespace NSPDX {

    TVector<TLicenseProps> ParseLicenseExpression(const THashMap<TString, TLicenseProps>& licenses, TStringBuf expr) {
        TParserStackFrame stackTop;
        TVector<TParserStackFrame> stackTail;

        TStringBuf tok;
        TStringBuf unhandledId;
        bool waitWithRhs = false;

        auto handleOp = [&](auto op, TStringBuf rhs) {
            if (waitWithRhs) {
                throw TExpressionError() << "No right argument for WITH operator";
            }
            if (op == Op::None) {
                return;
            }
            const auto it = licenses.find(rhs);
            if (it == licenses.end()) {
                throw TExpressionError() << "Unknown license " << rhs;
            }
            if (op == Op::And) {
                Y_ASSERT(stackTop.SubexprProps.size() >= stackTop.LastSubexprSize);
                for (auto& porp : TArrayRef<NSPDX::TLicenseProps>{stackTop.SubexprProps}.last(stackTop.LastSubexprSize)) {
                    porp.Dynamic |= it->second.Dynamic;
                    porp.Static |= it->second.Static;
                }
            } else {
                Y_ASSERT(op == Op::Or);
                stackTop.SubexprProps.push_back(it->second);
                stackTop.LastSubexprSize = 1;
            }
        };

        auto finalizeExpr = [&] {
            if (unhandledId.empty() && stackTop.UnhandledOp != Op::None) {
                throw TExpressionError() << "No right argument for " << (stackTop.UnhandledOp == Op::And ? "AND" : "OR") << " operator";
            }
            handleOp(std::exchange(stackTop.UnhandledOp, Op::None), std::exchange(unhandledId, {}));
        };

        // The only reason to put it into lambda is to handle implicit AND operation
        auto handleAnd = [&] {
            handleOp(std::exchange(stackTop.UnhandledOp, Op::And), std::exchange(unhandledId, {}));
        };

        for (std::tie(tok, expr) = SplitToken(expr); !tok.empty(); std::tie(tok, expr) = SplitToken(expr)) {
            const auto tokType
                = tok == "(" ? ETokenType::LBrk
                : tok == ")" ? ETokenType::RBrk
                : tok == "AND" ? ETokenType::And
                : tok == "OR" ? ETokenType::Or
                : tok == "WITH" ? ETokenType::With
                : ETokenType::LicId;
            switch (tokType) {
                case ETokenType::LBrk:
                    if (waitWithRhs) {
                        throw TExpressionError() << "No right argument for WITH operator";
                    }
                    if (!unhandledId.empty()) {
                        throw TExpressionError() << "Missing operator between '" << unhandledId << "' and '('";
                    }
                    stackTail.push_back(std::exchange(stackTop, {}));
                    break;
                case ETokenType::RBrk:
                    if (stackTail.empty()) {
                        throw TExpressionError() << "Closing bracket without matching opening bracket";
                    }
                    finalizeExpr();
                    stackTop = StackReduce(std::move(stackTail.back()), std::move(stackTop));
                    stackTail.pop_back();
                    break;
                case ETokenType::With:
                    if (unhandledId.empty()) {
                        throw TExpressionError() << "No left argument for WITH operator";
                    }
                    waitWithRhs = true;
                    break;

                case ETokenType::And:
                    if (unhandledId.empty() && stackTop.UnhandledOp != Op::None) {
                        throw TExpressionError() << "No left argument for AND operator";
                    }
                    handleAnd();
                    break;

                case ETokenType::Or:
                    if (unhandledId.empty() && stackTop.UnhandledOp != Op::None) {
                        throw TExpressionError() << "No left argument for OR operator";
                    }
                    handleOp(std::exchange(stackTop.UnhandledOp, Op::Or), std::exchange(unhandledId, {}));
                    break;

                case ETokenType::LicId: {
                    const auto lastUnhandled = std::exchange(unhandledId, tok);
                    if (std::exchange(waitWithRhs, false)) {
                        handleOp(std::exchange(stackTop.UnhandledOp, Op::None), lastUnhandled + TString{" WITH "} + unhandledId);
                        unhandledId = {};
                        continue;
                    } else if (!lastUnhandled.empty()) {
                        throw TExpressionError() << "Missing operator between '" << lastUnhandled << "' and '" << unhandledId << "' licenses";
                    } else if (stackTop.UnhandledOp == Op::None && !stackTop.SubexprProps.empty()) {
                        Y_ASSERT(lastUnhandled.empty());
                        throw TExpressionError() << "Missing operator before '" << unhandledId << "' license";
                    }
                    break;
                }
            }
        }
        if (!stackTail.empty()) {
            throw TExpressionError() << "No closing bracket for sub expression";
        }
        finalizeExpr();
        return stackTop.SubexprProps;
    }

    void ForEachLicense(TStringBuf licenses, const LicenseHandler& handler) {
        TString license;
        bool nextIsException = false;
        for (TStringBuf tok : SplitBySpace(licenses)) {
            const bool exceptMarker = tok == "WITH";
            if (!exceptMarker && !nextIsException && !license.empty()) {
                handler(std::exchange(license, {}));
            }
            if (!license.empty()) {
                license += ' ';
            }
            license += tok;
            nextIsException = exceptMarker;
        }
        if (!license.empty()) {
            handler(license);
        }
    }

    void ForEachLicense(const TVector<TString>& licenses, const LicenseHandler& handler) {
        for (const auto& license : licenses) {
            handler(license);
        }
    }
}
