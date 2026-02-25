#include "signature_conversion.h"

#include "raii.h"

#include <devtools/ymake/options/static_options.h>

namespace {

TStringBuf StrContent(PyObject* pystr) noexcept {
    Y_ASSERT(PyUnicode_Check(pystr));
    Py_ssize_t size = 0;
    const char *data = PyUnicode_AsUTF8AndSize(pystr, &size);
    return {data, static_cast<size_t>(size)};
}

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

}

namespace NYMake::NPy {

std::expected<TSignature, ESignatureDeductionError> DeduceConfSignature(PyObject& func, PyTypeObject& unitType) noexcept {
    PyObject* signature = PyFunction_GetAnnotations(&func);
    if (!signature)
        return std::unexpected(ESignatureDeductionError::MissingTypeHints);

    PyObject *key=nullptr, *val=nullptr;
    Py_ssize_t pos = 0;
    if (!PyDict_Next(signature, &pos, &key, &val) || !IsUnitTypeAnnotation(*val, unitType))
        return std::unexpected(ESignatureDeductionError::MissingUnitArg);

    if (PyFunction_GetDefaults(&func))
        return std::unexpected(ESignatureDeductionError::IndistinguishableKwArg);
    auto kwargs = PyFunction_GetKwDefaults(&func);

    bool varargFound = false;
    TVector<TString> positionals;
    TSignature::TKeywords keywords;
    while (PyDict_Next(signature, &pos, &key, &val)) {
        auto defaultVal = kwargs ? PyDict_GetItemWithError(kwargs, key) : nullptr;
        if (PyErr_Occurred())
            return std::unexpected(ESignatureDeductionError::PyException);

        if (defaultVal) {
            if (IsFlagArgTypeAnnotation(*val)) {
                if (Py_IsTrue(defaultVal))
                    return std::unexpected(ESignatureDeductionError::WrongFlagDefault);
                keywords.AddFlagKeyword(TString{StrContent(key)}, {}, {});
            } else if (IsScalarArgTypeAnnotation(*val))
                keywords.AddScalarKeyword(TString{StrContent(key)}, StrContent(defaultVal), {});
            else if (IsArrayArgTypeAnnotation(*val))
                keywords.AddArrayKeyword(TString{StrContent(key)}, {});
            else
                return std::unexpected(ESignatureDeductionError::WrongArgType);
            continue;
        }

        if (IsScalarArgTypeAnnotation(*val)) {
            if (varargFound)
                return std::unexpected(ESignatureDeductionError::PositionalAfterVararg);
            positionals.push_back(TString{StrContent(key)});
        } else if (IsArrayArgTypeAnnotation(*val)) {
            positionals.push_back(TString{StrContent(key)} + NStaticConf::ARRAY_SUFFIX);
            varargFound = true;
        } else
            return std::unexpected(ESignatureDeductionError::WrongArgType);
    }

    return TSignature{positionals, std::move(keywords)};
}

}
