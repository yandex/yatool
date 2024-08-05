#pragma once

#include "base.h"

#include <devtools/ymake/include_parsers/go_import_parser.h>
#include <devtools/ymake/vars.h>

#include <util/generic/set.h>
#include <util/generic/strbuf.h>
#include <util/generic/string.h>
#include <util/generic/vector.h>

class TGoImportProcessor: public TIncludeProcessorBase {
public:
    explicit TGoImportProcessor(const TEvaluatorBase& vars, TSymbols& symbols);
    ui32 Version() const override { return 1 + CommonVersion; }
    void ProcessIncludes(TAddDepAdaptor& node,
                         TModuleWrapper& module,
                         TFileView incFileName,
                         const TVector<TParsedFile>& includes,
                         bool fromOutputIncludes = false) const;
    void ProcessOutputIncludes(TAddDepAdaptor& node,
                               TModuleWrapper& module,
                               TFileView incFileName,
                               const TVector<TString>& includes) const override;

private:
    void ProcessImport(const TString& import, TString& resolved, bool is_std, bool is_std_cmd) const;

    TSymbols& Symbols;

    TString StdLibPrefix;
    TString StdCmdPrefix;
    TString ArcadiaProjectPrefix;
    TString ContribProjectPrefix;
    TSet<TString> SkipImports;
};
