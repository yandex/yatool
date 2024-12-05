#pragma once

#include <devtools/ymake/common/uniq_vector.h>
#include <devtools/ymake/polexpr/expression.h>
#include <devtools/ymake/symbols/file_store.h>

namespace NCommands {

    struct TCompiledCommand {
        struct TInput {
            TStringBuf Name;
            ELinkType Context = ELinkType::ELT_Default;
            bool IsGlob = false;
            TInput(TStringBuf name) : Name(name) {}
            operator TStringBuf() const { return Name; }
        };
        struct TOutput {
            TStringBuf Name;
            bool IsTmp = false;
            bool NoAutoSrc = false;
            bool NoRel = false;
            bool ResolveToBinDir = false;
            TOutput(TStringBuf name): Name(name) {}
            operator TStringBuf() const { return Name; }
        };
        template<typename TLink>
        class TLinks:
            public TUniqContainerImpl<TLink, TStringBuf, 32, TVector<TLink>, true> // basically, TUniqVector<TLink> with IsIndexed=true
        {
        public:
            ui32 Base = 0;
            ui32 CollectCoord(TStringBuf s) {
                return this->Push(TLink(s)).first + Base;
            }
            template<typename FUpdater>
            void UpdateCoord(ui32 coord, FUpdater upd) {
                this->Update(coord - Base, upd);
            }
        };
        using TInputs = TLinks<TInput>;
        using TOutputs = TLinks<TOutput>;

        NPolexpr::TExpression Expression;
        TInputs Inputs;
        TOutputs Outputs;
    };

}
