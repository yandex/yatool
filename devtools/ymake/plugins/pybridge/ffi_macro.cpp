#include "ffi_macro.h"
#include "str.h"

#include <devtools/ymake/lang/call_args_parser.h>

#include <util/generic/hash.h>
#include <util/generic/vector.h>

#include <fmt/format.h>

#include <Python.h>

#include <ranges>

namespace NYMake::NPy {

namespace {

[[noreturn]]
void ThrowPyCallError() {
    OwnedRef err{PyErr_GetRaisedException()};
    PyErr_DisplayException(err.get());
    OwnedRef errStr{PyObject_Str(err.get())};
    throw TFFIMacro::TCallError{std::string{StrContent(*errStr)}};
}

OwnedRef<> MakePyStr(TStringBuf str) {
    OwnedRef res{PyUnicode_FromStringAndSize(str.data(), str.size())};
    if (!res) {
        ThrowPyCallError();
    }
    return res;
}

OwnedRef<> MakePyTuple(std::span<const TStringBuf> values) {
    OwnedRef tuple{PyTuple_New(values.size())};
    if (!tuple) {
        ThrowPyCallError();
    }

    for (size_t i = 0; i < values.size(); ++i) {
        PyTuple_SetItem(tuple.get(), i, MakePyStr(values[i]).Release());
    }

    return tuple;
}

OwnedRef<> MakePyTuple(std::span<OwnedRef<>> values) {
    OwnedRef tuple{PyTuple_New(values.size())};
    if (!tuple) {
        ThrowPyCallError();
    }

    for (size_t i = 0; i < values.size(); ++i) {
        PyTuple_SetItem(tuple.get(), i, values[i].Release());
    }

    return tuple;
}

void StrDictSet(PyObject& dict, TStringBuf key, PyObject& val) {
    if (PyDict_SetItem(&dict, MakePyStr(key).get(), &val) != 0) {
        ThrowPyCallError();
    }
}

void StrDictTupleAppend(PyObject& dict, TStringBuf key, std::span<const TStringBuf> strs) {
    auto keyObj = MakePyStr(key);
    PyObject* prevVal = PyDict_GetItemWithError(&dict, keyObj.get());
    if (PyErr_Occurred()) {
        ThrowPyCallError();
    }

    auto newVal = MakePyTuple(strs);
    if (prevVal) {
        newVal = OwnedRef{PySequence_Concat(prevVal, newVal.get())};
        if (!newVal) {
            ThrowPyCallError();
        }
    }

    if (PyDict_SetItem(&dict, keyObj.get(), newVal.get()) != 0) {
        ThrowPyCallError();
    }
}

} // namespace

OwnedRef<> TFFIMacro::Call(PyObject& unit, std::span<const TStringBuf> args) {
    TVector<OwnedRef<>> posArgs;
    posArgs.reserve(args.size());
    posArgs.push_back(FromBorrowedRef(&unit));

    OwnedRef kwDict{PyDict_New()};
    if (!kwDict) {
        ThrowPyCallError();
    }

    for (const auto& arg : TParsedCallArgs{Sign_, args}) {
        if (!arg) {
            throw TCallError{fmt::format("Failed to call {}: {}", Name(), arg.error().Message(Sign_, args))};
        }

        if (auto kw = KeywordData(Sign_, arg->first)) {
            switch (kw->Kind) {
                case TKeyword::Flag:
                    StrDictSet(*kwDict, ArgDefName(Sign_, arg->first), *Py_True);
                    break;
                case TKeyword::Scalar:
                    Y_ASSERT(arg->second.size() == 1);
                    StrDictSet(*kwDict, ArgDefName(Sign_, arg->first), *MakePyStr(arg->second.front()));
                    break;
                case TKeyword::Array:
                    StrDictTupleAppend(*kwDict, ArgDefName(Sign_, arg->first), arg->second);
                    break;
            }
        } else {
            // Both regular positionals and varargs are handled here
            std::ranges::copy(arg->second | std::views::transform(MakePyStr), std::back_inserter(posArgs));
        }
    }

    OwnedRef res{PyObject_Call(Func_.get(), MakePyTuple(posArgs).get(), kwDict.get())};
    if (!res) {
        ThrowPyCallError();
    }
    return res;
}

TStringBuf TFFIMacro::Name() const noexcept {
    NYMake::NPy::OwnedRef name{PyObject_GetAttrString(Func_.get(), "__name__")};
    return StrContent(*name);
}

TString TFFIMacro::DocText() const {
    PyObject* doc = ((PyFunctionObject*)Func_.get())->func_doc;
    if (!doc || !PyUnicode_Check(doc)) {
        return {};
    }
    return TString{StrContent(*doc)};
}

}
