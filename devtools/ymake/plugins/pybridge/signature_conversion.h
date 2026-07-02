#pragma once

#include <devtools/ymake/lang/call_signature.h>

#include <Python.h>

#include <expected>

namespace NYMake::NPy {

enum class ESignatureDeductionError {
    PyException,
    MissingTypeHints,
    MissingUnitArg,
    WrongArgType,
    WrongReturnType,
    WrongFlagDefault,
    PositionalAfterVararg,
    KwArgWithoutDefaults,
    IndistinguishableKwArg,
};

std::expected<TSignature, ESignatureDeductionError> DeduceConfSignature(PyObject& func, PyTypeObject& unitType);

}
