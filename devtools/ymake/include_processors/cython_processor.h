#pragma once

#include "base.h"
#include <devtools/ymake/include_parsers/cython_parser.h>

class TSymbols;

class TCythonIncludeProcessor: public TIncludeProcessorBase {
public:
    explicit TCythonIncludeProcessor(TSymbols& symbols);
    void ProcessIncludes(TAddDepAdaptor& node,
                         TModuleWrapper& module,
                         TFileView incFileName,
                         const TVector<TCythonDep>& includes) const;
    void ProcessOutputIncludes(TAddDepAdaptor& node,
                               TModuleWrapper& module,
                               TFileView incFileName,
                               const TVector<TString>& includes) const override;

private:
    TLangId LanguageC;
    TLangId LanguageCython;
};
