#pragma once

#include "base.h"

#include <devtools/ymake/include_parsers/ts_import_parser.h>
#include <devtools/ymake/module_state.h>

class TTsImportProcessor: public TIncludeProcessorBase {
public:
    struct TTsConfig {
        TString RootDir;
        TString OutDir;
        bool SourceMap : 1;
        bool Declaration : 1;
        bool DeclarationMap : 1;
        bool PreserveJsx : 1;

        TTsConfig(const TModule& module):
            RootDir(NPath::SmartJoin(module.GetDir().GetTargetStr(), module.Get("TS_CONFIG_ROOT_DIR"))),
            OutDir(NPath::SmartJoin(module.GetDir().GetTargetStr(), module.Get("TS_CONFIG_OUT_DIR"))),
            SourceMap(module.Vars.IsTrue("TS_CONFIG_SOURCE_MAP")),
            Declaration(module.Vars.IsTrue("TS_CONFIG_DECLARATION")),
            DeclarationMap(module.Vars.IsTrue("TS_CONFIG_DECLARATION_MAP")),
            PreserveJsx(module.Vars.IsTrue("TS_CONFIG_PRESERVE_JSX")) {
        }
    };

    static TVector<TString> GenerateOutputPaths(TStringBuf sourcePath, const TTsConfig& cfg);

    TTsImportProcessor();

    void ProcessIncludes(TAddDepAdaptor& node,
                         TModuleWrapper& module,
                         TFileView incFileName,
                         const TVector<TString>& includes) const;

    void ProcessOutputIncludes(TAddDepAdaptor& node,
                               TModuleWrapper& module,
                               TFileView incFileName,
                               const TVector<TString>& includes) const override;

    void ProcessImports(TAddDepAdaptor& node,
                        TModuleWrapper& module,
                        TFileView incFileName,
                        const TVector<TString>& includes) const;
};
