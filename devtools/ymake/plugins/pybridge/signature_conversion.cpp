#include "signature_conversion.h"

#include "raii.h"
#include "str.h"

#include <devtools/ymake/options/static_options.h>

namespace {

inline bool IsUnitTypeAnnotation(const PyObject& annotation, const PyTypeObject& unitType) noexcept {
    return &annotation == reinterpret_cast<const PyObject*>(&unitType);
}

inline bool IsFlagArgTypeAnnotation(const PyObject& annotation) noexcept {
    return &annotation == reinterpret_cast<const PyObject*>(&PyBool_Type);
}

inline bool IsScalarArgTypeAnnotation(const PyObject& annotation) noexcept {
    return &annotation == reinterpret_cast<const PyObject*>(&PyUnicode_Type);
}

bool IsArrayArgTypeAnnotation(PyObject& annotation) noexcept {
    if (!Py_IS_TYPE(&annotation, &Py_GenericAliasType))
        return false;

    NYMake::NPy::OwnedRef generic{PyObject_GetAttrString(&annotation, "__origin__")};
    if (generic.get() != reinterpret_cast<const PyObject*>(&PyTuple_Type))
        return false;

    NYMake::NPy::OwnedRef args{PyObject_GetAttrString(&annotation, "__args__")};
    Y_ASSERT(PyTuple_Check(args.get()));
    if (PyTuple_Size(args.get()) != 2)
        return false;
    if (!Py_IS_TYPE(PyTuple_GetItem(args.get(), 1), &PyEllipsis_Type))
        return false;
    auto itemType = PyTuple_GetItem(args.get(), 0);
    return itemType == reinterpret_cast<const PyObject*>(&PyUnicode_Type);
}

PyObject* UnwrapDecoratedFunction(PyObject& func) {
    PyObject* inner = &func;
    // The code bellow assumes that user decorators do not forget to use `@functools.wraps` properly.
    while (PyObject_HasAttrString(inner, "__wrapped__")) {
        inner = PyObject_GetAttrString(inner, "__wrapped__");
        Py_DECREF(inner); // The object is owned directly or indirectly by `func` argument. We do not need to keep strong reference to it
    }

    if (!PyFunction_Check(inner)) {
        PyErr_Format(PyExc_RuntimeError, "Can't deduce signature by object of type '%N'.", Py_TYPE(inner));
        return nullptr;
    }
    return inner;
}

}

namespace NYMake::NPy {

std::expected<TSignature, ESignatureDeductionError> DeduceConfSignature(
    PyObject& func,
    PyTypeObject& unitType,
    const THashSet<std::string>& ignoreArgs
) {
    auto nakedFunc = UnwrapDecoratedFunction(func);
    if (!nakedFunc)
        return std::unexpected(ESignatureDeductionError::PyException);

    PyObject* signature = PyFunction_GetAnnotations(nakedFunc);
    if (!signature)
        return std::unexpected(ESignatureDeductionError::MissingTypeHints);

    PyObject *key=nullptr, *val=nullptr;
    Py_ssize_t pos = 0;
    while (PyDict_Next(signature, &pos, &key, &val)) {
        if (!ignoreArgs.contains(StrContent(*key)))
            break;
    }
    if (!key || !IsUnitTypeAnnotation(*val, unitType))
        return std::unexpected(ESignatureDeductionError::MissingUnitArg);

    if (PyFunction_GetDefaults(nakedFunc))
        return std::unexpected(ESignatureDeductionError::IndistinguishableKwArg);
    auto kwargs = PyFunction_GetKwDefaults(nakedFunc);

    const auto& codeObj = *reinterpret_cast<PyCodeObject*>(PyFunction_GetCode(nakedFunc));
    const auto kwOnlyArgsNum = codeObj.co_kwonlyargcount;
    const auto kwOnlyWithDefaultsNum = kwargs ? PyDict_GET_SIZE(kwargs) : 0;
    if (kwOnlyWithDefaultsNum != kwOnlyArgsNum) {
        return std::unexpected(ESignatureDeductionError::KwArgWithoutDefaults);
    }

    const bool hasVararg = (codeObj.co_flags & CO_VARARGS);
    TVector<TString> positionals;
    TSignature::TKeywords keywords;
    while (PyDict_Next(signature, &pos, &key, &val)) {
        auto defaultVal = kwargs ? PyDict_GetItemWithError(kwargs, key) : nullptr;
        if (PyErr_Occurred())
            return std::unexpected(ESignatureDeductionError::PyException);

        const auto argName = StrContent(*key);
        if (argName == "return") {
            if (val == Py_None) {
                continue;
            }
            return std::unexpected(ESignatureDeductionError::WrongReturnType);
        }
        if (ignoreArgs.contains(argName))
            continue;

        if (defaultVal) {
            if (IsFlagArgTypeAnnotation(*val)) {
                if (Py_IsTrue(defaultVal))
                    return std::unexpected(ESignatureDeductionError::WrongFlagDefault);
                keywords.AddFlagKeyword(TString{argName}, {}, {});
            } else if (IsScalarArgTypeAnnotation(*val))
                keywords.AddScalarKeyword(TString{argName}, StrContent(*defaultVal), {});
            else if (IsArrayArgTypeAnnotation(*val))
                keywords.AddArrayKeyword(TString{argName}, {});
            else
                return std::unexpected(ESignatureDeductionError::WrongArgType);
            continue;
        }

        const bool isVararg = hasVararg && pos == codeObj.co_argcount + 1;
        if (IsScalarArgTypeAnnotation(*val)) {
            if (isVararg) {
                positionals.push_back(TString{argName} + NStaticConf::ARRAY_SUFFIX);
            } else {
                positionals.push_back(TString{argName});
            }
        } else if (IsArrayArgTypeAnnotation(*val) && isVararg) {
            // TODO(YMAKE-2151): remove support of `tuple[str, ...]` on `*args`
            positionals.push_back(TString{argName} + NStaticConf::ARRAY_SUFFIX);
        } else
            return std::unexpected(ESignatureDeductionError::WrongArgType);
    }

    return TSignature{positionals, std::move(keywords)};
}

}
