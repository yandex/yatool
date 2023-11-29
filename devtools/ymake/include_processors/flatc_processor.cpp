#include "flatc_processor.h"

#include <devtools/ymake/add_dep_adaptor_inline.h>
#include <devtools/ymake/module_wrapper.h>

TFlatcIncludeProcessorBase::TFlatcIncludeProcessorBase(TStringBuf inducedFrom,
                                                       TStringBuf inducedTo)
    : InducedFrom(inducedFrom)
    , InducedTo(inducedTo)
{
}

void TFlatcIncludeProcessorBase::ProcessIncludes(TAddDepAdaptor& node,
                                                 TModuleWrapper& module,
                                                 TFileView incFileName,
                                                 const TVector<TString>& includes) const {
    ResolveAndAddLocalIncludes(node, module, incFileName, includes, {}, LanguageId);

    TVector<TString> induced(Reserve(includes.size()));
    for (const auto& incl : includes) {
        induced.emplace_back(MakeInduced(incl));
    }

    TVector<TResolveFile> resolvedInduced(Reserve(induced.size()));
    module.ResolveAsUnset(induced, resolvedInduced);

    /*
    Тут сейчас содержится костыль.

    Мы генерируем *.fbs.cpp и *.fbs.h файлы. Они являются различными output-ами
    одной команды, и fbs.cpp содержит include файла fbs.h. Мы должны были бы
    поставить соответствующую include-дугу, чтобы правильно учесть зависимости
    fbs.h файла при компиляции fbs.cpp. Но сейчас, поскольку fbs.cpp является
    основным output-ом команды, добавление такой дуги создаст цикл между основным
    и дополнительным output-ами, и мы такие циклы можем обрабатывать некорректно.

    Поэтому вместо добавления правильной дуги мы здесь индуцируем зависимости,
    которые реально относятся только к *.h файлам (конкретно *.fbs.h) и на *.cpp
    (конкретно, *.fbs.cpp). Таким образом, необходимые зависимости fbs.h становятся
    зависимостями fbs.cpp и корректно учитываются при его компиляции.
    */
    node.AddParsedIncls("h+cpp", resolvedInduced);
}

TString TFlatcIncludeProcessorBase::MakeInduced(const TString& include) const {
    TString induced(include);
    SubstGlobal(induced, InducedFrom, InducedTo);
    return induced;
}
