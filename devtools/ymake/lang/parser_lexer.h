#pragma once

#include <util/generic/strbuf.h>
#include <util/generic/vector.h>
#include <util/generic/yexception.h>

namespace NYMake::NParsing {
    class TLexerError: public yexception {
    };

    enum class EMode {
        Command,
        SingleQuoted,
        DoubleQuoted,
        Evaluation,
        ModifierArgument,
        Config
    };
    enum class EToken {
        End,
        Raw,
        Noop,
        Variable,
        EvaluationBegin,
        LeftParen,
        RightParen,
        SingleQuote,
        DoubleQuote,
        CommandSeparator,
        Space,
        EvaluationEnd,
        ModifierArgument,
        ModifierSeparator,
        ModifierEnd,
        Identifier,
        String,
        ModifierText,
        ModifierVariable,
        ModifierEvaluation,
        Eol,
        Comment,
        DocComment,
        Symbol
    };
    struct TToken {
        EToken Kind = EToken::End;
        TStringBuf Text;
        size_t Line = 1;
        size_t Column = 0;
        size_t Offset = 0;
    };

    // Tokens borrow the input. Only the semantic result owns strings.
    // Config mode consumes UTF-8 already validated by the configuration loader.
    class TLexer {
    public:
        explicit TLexer(TStringBuf source, EMode mode = EMode::Command);
        TToken Next();
        size_t Position() const noexcept {
            return Position_;
        }
        TStringBuf Source() const noexcept {
            return Source_;
        }
        void Advance(size_t size);
        TToken Location() const noexcept {
            return {EToken::End, {}, Line_, Column_, Position_};
        }

    private:
        void PushMode(EMode mode) {
            ModeStack_.push_back(Mode_);
            Mode_ = mode;
        }

        void PopMode() {
            Mode_ = ModeStack_.back();
            ModeStack_.pop_back();
        }

        TStringBuf Source_;
        size_t Position_ = 0;
        size_t Line_ = 1;
        size_t Column_ = 0;
        EMode Mode_;
        TVector<EMode> ModeStack_;
    };
} // namespace NYMake::NParsing
