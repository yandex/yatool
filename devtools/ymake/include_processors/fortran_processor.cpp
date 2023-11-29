#include "fortran_processor.h"

#include <devtools/ymake/add_dep_adaptor_inline.h>
#include <devtools/ymake/module_wrapper.h>

void TFortranIncludeProcessor::ProcessIncludes(TAddDepAdaptor& node,
                                               TModuleWrapper& module,
                                               TFileView incFileName,
                                               const TVector<TString>& includes) const {
    ResolveAndAddLocalIncludes(node, module, incFileName, includes, {}, LanguageId);
}
