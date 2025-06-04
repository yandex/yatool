#include <library/cpp/pybind/ptr.h>

#include <util/generic/algorithm.h>
#include <util/generic/hash.h>
#include <util/generic/scope.h>
#include <util/generic/strbuf.h>
#include <util/generic/vector.h>
#include <util/string/cast.h>
#include <util/system/yassert.h>

#include <contrib/libs/re2/re2/re2.h>
#include <contrib/libs/yaml/include/yaml.h>

#include <Python.h>

#include <cstdio>
#include <ranges>

namespace {
    inline TStringBuf YamlStringToStringBuf(const yaml_char_t* data) {
        Y_ASSERT(data != nullptr);
        return data ? TStringBuf{reinterpret_cast<const char*>(data)} : TStringBuf{};
    }

    inline TStringBuf YamlStringToStringBuf(const yaml_char_t* data, size_t size) {
        Y_ASSERT(data);
        return data ? TStringBuf{reinterpret_cast<const char*>(data), size} : TStringBuf{};
    }

    enum class EValueType {
        String,
        Null,
        Bool,
        Int,
        IntBase10,
        IntBase16,
        IntBase8,
        Float,
        Infinity,
        Nan,
    };

    // constexpr TStringBuf YMAKE_YAML_BINARY_TAG = "tag:yaml.org,2002:binary"_sb;
    // constexpr TStringBuf YMAKE_YAML_TIMESTAMP_TAG = "tag:yaml.org,2002:timestamp"_sb;
    constexpr TStringBuf YMAKE_YAML_OMAP_TAG = "tag:yaml.org,2002:omap"_sb;
    constexpr TStringBuf YMAKE_YAML_PAIRS_TAG = "tag:yaml.org,2002:pairs"_sb;
    constexpr TStringBuf YMAKE_YAML_SET_TAG = "tag:yaml.org,2002:set"_sb;

    static const THashMap<TStringBuf, EValueType> Tag2TypeMap = {
        {YAML_NULL_TAG, EValueType::Null},
        {YAML_BOOL_TAG, EValueType::Bool},
        {YAML_STR_TAG, EValueType::String},
        {YAML_INT_TAG, EValueType::Int},
        {YAML_FLOAT_TAG, EValueType::Float},
    };

    inline EValueType ScalarTypeFromTag(TStringBuf tag) {
        if (auto iter = Tag2TypeMap.find(tag); iter != Tag2TypeMap.end()) {
            return iter->second;
        }
        return EValueType::String;
    }

    // Regexp for types according to https://yaml.org/spec/1.2.2/#103-core-schema
    static const re2::RE2 RegExpNull = "null|Nul|NULL|~|";
    static const re2::RE2 RegExpBool = "true|True|TRUE|false|False|FALSE";
    static const re2::RE2 RegExpIntBase10 = "[-+]?[0-9]+";
    static const re2::RE2 RegExpIntBase16 = "0x[0-9a-fA-F]+";
    static const re2::RE2 RegExpIntBase8 = "0o[0-7]+";
    static const re2::RE2 RegExpNumber = "[-+]?(\\.[0-9]+|[0-9]+(\\.[0-9]*)?)([eE][-+]?[0-9]+)?";
    static const re2::RE2 RegExpInfinity = "[-+]?(\\.inf|\\.Inf|\\.INF)";
    static const re2::RE2 RegExpNan = "\\.nan|\\.NaN |\\.NAN";

    inline EValueType ScalarTypeFromValue(TStringBuf value) {
        if (re2::RE2::FullMatch(value, RegExpNull)) {
            return EValueType::Null;
        } else if (re2::RE2::FullMatch(value, RegExpBool)) {
            return EValueType::Bool;
        } else if (re2::RE2::FullMatch(value, RegExpIntBase10)) {
            return EValueType::IntBase10;
        } else if (re2::RE2::FullMatch(value, RegExpIntBase16)) {
            return EValueType::IntBase16;
            // } else if (re2::RE2::FullMatch(value, RegExpIntBase8)) {
            //     return EValueType::IntBase8;
        } else if (re2::RE2::FullMatch(value, RegExpNumber)) {
            return EValueType::Float;
        } else if (re2::RE2::FullMatch(value, RegExpInfinity)) {
            return EValueType::Infinity;
        } else if (re2::RE2::FullMatch(value, RegExpNan)) {
            return EValueType::Nan;
        }
        return EValueType::String;
    }

    struct TState {
        const yaml_node_t* Node = nullptr;
        TStringBuf Tag{};
    };

    class TYamlBuilder {
    public:
        TYamlBuilder();
        ~TYamlBuilder();

        PyObject* BuildFromFile(FILE*);
        PyObject* BuildFromString(PyObject*);
        PyObject* BuildFromStream(PyObject*);

    private:
        PyObject* BuildNode(const yaml_node_t* node);
        PyObject* BuildNodeImpl(const yaml_node_t* node);
        PyObject* BuildMappingNode(const yaml_node_t*);
        PyObject* BuildSequenceNode(const yaml_node_t*);
        PyObject* BuildScalarNode(const yaml_node_t*);

        PyObject* BuildMapNode(const yaml_node_t*);
        PyObject* BuildSetNode(const yaml_node_t*);
        PyObject* BuildOrderedMapNode(const yaml_node_t*);
        PyObject* BuildPairNode(const yaml_node_t*);

        yaml_parser_t Parser_{};
        yaml_document_t Doc_{};
        TVector<TState> Stack_;
        THashMap<const yaml_node_t*, PyObject*> Cache_;
    };

    TYamlBuilder::TYamlBuilder() {
        memset(&Parser_, 0, sizeof(Parser_));
        yaml_parser_initialize(&Parser_);

        memset(&Doc_, 0, sizeof(Doc_));
    }

    TYamlBuilder::~TYamlBuilder() {
        yaml_parser_delete(&Parser_);
        yaml_document_delete(&Doc_);
    }

    PyObject* TYamlBuilder::BuildFromFile(FILE* file) {
        Y_ASSERT(Stack_.empty());
        Y_ASSERT(Cache_.empty());

        PyObject* result = nullptr;
        yaml_parser_set_input_file(&Parser_, file);
        if (!yaml_parser_load(&Parser_, &Doc_)) {
            PyErr_SetString(PyExc_RuntimeError, "Failed to load YAML document");
            result = nullptr;
        } else {
            result = BuildNode(yaml_document_get_root_node(&Doc_));
        }

        Y_ASSERT(Stack_.empty());
        for (auto& item : Cache_) {
            Py_XDECREF(item.second);
        }
        Cache_.clear();

        return result;
    }

    PyObject* TYamlBuilder::BuildFromString(PyObject* stringObj) {
        NPyBind::TPyObjectPtr bytesObj{stringObj};
        if (PyUnicode_Check(stringObj)) {
            NPyBind::TPyObjectPtr o{PyUnicode_AsUTF8String(stringObj), true};
            if (o == nullptr) {
                return nullptr;
            }
            bytesObj.Reset(o);
        }
        if (!PyBytes_Check(bytesObj.Get())) {
            PyErr_SetString(PyExc_RuntimeError, "Unexpected type of object");
            return nullptr;
        }

        PyObject* result = nullptr;
        yaml_parser_set_input_string(&Parser_, (yaml_char_t*)PyBytes_AS_STRING(bytesObj.Get()), PyBytes_GET_SIZE(bytesObj.Get()));
        if (!yaml_parser_load(&Parser_, &Doc_)) {
            PyErr_SetString(PyExc_RuntimeError, "Failed to load YAML document");
            result = nullptr;
        } else {
            result = BuildNode(yaml_document_get_root_node(&Doc_));
        }

        Y_ASSERT(Stack_.empty());
        for (auto& item : Cache_) {
            Py_XDECREF(item.second);
        }
        Cache_.clear();

        return result;
    }

    PyObject* TYamlBuilder::BuildFromStream(PyObject* streamObj) {
        NPyBind::TPyObjectPtr valueObj{PyObject_CallMethod(streamObj, "read", nullptr), true};
        if (valueObj == nullptr) {
            return nullptr;
        }
        // FIXME(snermolaev): we can do better here... by means of yaml_parser_set_input
        return BuildFromString(valueObj.Get());
    }

    PyObject* TYamlBuilder::BuildNode(const yaml_node_t* node) {
        if (node == nullptr) {
            Py_RETURN_NONE;
        }

        if (auto iter = Cache_.find(node); iter != Cache_.end()) {
            Py_XINCREF(iter->second);
            return iter->second;
        }

        PyObject* result = BuildNodeImpl(node);
        Py_XINCREF(result);
        Cache_[node] = result;
        return result;
    }

    PyObject* TYamlBuilder::BuildNodeImpl(const yaml_node_t* node) {
        Y_ASSERT(node != nullptr);

        Stack_.emplace_back(node, YamlStringToStringBuf(node->tag));
        Y_DEFER {
            Stack_.pop_back();
        };

        switch (node->type) {
            case YAML_NO_NODE:
                Py_RETURN_NONE;
            case YAML_MAPPING_NODE:
                if (Stack_.size() > 1 && EqualToOneOf((Stack_.rbegin() + 1)->Tag, YMAKE_YAML_OMAP_TAG, YMAKE_YAML_PAIRS_TAG)) {
                    return BuildPairNode(node);
                } else {
                    return BuildMappingNode(node);
                }
            case YAML_SEQUENCE_NODE:
                return BuildSequenceNode(node);
            case YAML_SCALAR_NODE:
                return BuildScalarNode(node);
        }

        PyErr_SetString(PyExc_TypeError, "Unexpected YAML node type");
        return nullptr;
    }

    PyObject* TYamlBuilder::BuildMappingNode(const yaml_node_t* node) {
        Y_ASSERT(node != nullptr && node->type == YAML_MAPPING_NODE);

        const auto tag = Stack_.back().Tag;

        if (tag == YAML_DEFAULT_MAPPING_TAG) {
            return BuildMapNode(node);
        } else if (tag == YMAKE_YAML_OMAP_TAG) {
            return BuildOrderedMapNode(node);
        } else if (tag == YMAKE_YAML_SET_TAG) {
            return BuildSetNode(node);
        } else {
            Y_ASSERT(0);
        }

        Py_RETURN_NONE;
    }

    PyObject* TYamlBuilder::BuildSequenceNode(const yaml_node_t* node) {
        Y_ASSERT(node != nullptr && node->type == YAML_SEQUENCE_NODE);

        const auto& items = &node->data.sequence.items;
        size_t size = items->top - items->start;
        NPyBind::TPyObjectPtr seq{PyList_New(size), true};
        if (seq == nullptr) {
            return nullptr;
        }

        int i = 0;
        for (const auto& item : std::ranges::subrange{items->start, items->top}) {
            yaml_node_t* elem_node = yaml_document_get_node(&Doc_, item);
            NPyBind::TPyObjectPtr elem{BuildNode(elem_node), true};
            if (elem == nullptr) {
                return nullptr;
            }
            PyList_SET_ITEM(seq.Get(), i++, elem.RefGet());
        }

        return seq.RefGet();
    }

    PyObject* TYamlBuilder::BuildScalarNode(const yaml_node_t* node) {
        Y_ASSERT(node != nullptr && node->type == YAML_SCALAR_NODE);

        auto tag = Stack_.back().Tag;
        // FIXME(snermolaev): get type of scalar
        tag = tag.empty() ? tag : TStringBuf{};
        if (tag.empty()) {
            if (node->data.scalar.style == YAML_PLAIN_SCALAR_STYLE) {
                tag = "?"_sb;
            } else {
                tag = "!"_sb;
            }
        }
        const auto value = YamlStringToStringBuf(node->data.scalar.value, node->data.scalar.length);
        EValueType valueType = EValueType::String;
        if (tag == "?"_sb) {
            valueType = ScalarTypeFromValue(value);
        } else {
            valueType = ScalarTypeFromTag(tag);
        }
        switch (valueType) {
            case EValueType::String:
                return PyUnicode_FromStringAndSize(value.data(), value.size());
            case EValueType::Null:
                Py_RETURN_NONE;
            case EValueType::Bool:
                if (EqualToOneOf(value, "true"_sb, "True"_sb, "TRUE"_sb)) {
                    Py_RETURN_TRUE;
                } else {
                    assert(EqualToOneOf(value, "false"_sb, "False"_sb, "FALSE"_sb));
                    Py_RETURN_FALSE;
                }
            case EValueType::Int: {
                long longValue{};
                if (TryFromString(value, longValue)) {
                    return PyLong_FromLong(longValue);
                } else {
                    PyErr_Format(PyExc_RuntimeError, "Failed to convert `%s` to long", TString{value}.c_str());
                    return nullptr;
                }
            }
            case EValueType::IntBase10: {
                long longValue{};
                if (TryIntFromString<10>(value, longValue)) {
                    return PyLong_FromLong(longValue);
                } else {
                    PyErr_Format(PyExc_RuntimeError, "Failed to convert `%s` to long", TString{value}.c_str());
                    return nullptr;
                }
            }
            case EValueType::IntBase16: {
                long ulongValue{};
                if (TryIntFromString<16>(value.SubStr(2), ulongValue)) {
                    return PyLong_FromUnsignedLong(ulongValue);
                } else {
                    PyErr_Format(PyExc_RuntimeError, "Failed to convert `%s` to long", TString{value}.c_str());
                    return nullptr;
                }
            }
            case EValueType::IntBase8: {
                unsigned long ulongValue{};
                if (TryIntFromString<8>(value.SubStr(2), ulongValue)) {
                    return PyLong_FromUnsignedLong(ulongValue);
                } else {
                    PyErr_Format(PyExc_RuntimeError, "Failed to convert `%s` to long", TString{value}.c_str());
                    return nullptr;
                }
            }
            case EValueType::Float: {
                double doubleValue{};
                if (TryFromString(value, doubleValue)) {
                    return PyFloat_FromDouble(doubleValue);
                } else {
                    PyErr_Format(PyExc_RuntimeError, "Failed to convert `%s` to float", TString{value}.c_str());
                    return nullptr;
                }
            }
            case EValueType::Infinity: {
                return value.front() == '-' ? PyFloat_FromDouble(-Py_HUGE_VAL) : PyFloat_FromDouble(Py_HUGE_VAL);
            }
            case EValueType::Nan: {
                return PyFloat_FromDouble(Py_NAN);
            }
            default:
                Y_ASSERT(0);
        }
        Py_RETURN_NONE;
    }

    PyObject* TYamlBuilder::BuildMapNode(const yaml_node_t* node) {
        Y_ASSERT(node != nullptr && node->type == YAML_MAPPING_NODE);

        NPyBind::TPyObjectPtr dict{PyDict_New(), true};
        if (dict == nullptr) {
            return nullptr;
        }

        const auto& pairs = node->data.mapping.pairs;
        for (const auto& p : std::ranges::subrange{pairs.start, pairs.top}) {
            yaml_node_t* key_node = yaml_document_get_node(&Doc_, p.key);
            NPyBind::TPyObjectPtr keyObj{BuildNode(key_node), true};
            if (keyObj == nullptr) {
                return nullptr;
            }

            yaml_node_t* value_node = yaml_document_get_node(&Doc_, p.value);
            NPyBind::TPyObjectPtr valueObj{BuildNode(value_node), true};
            if (valueObj == nullptr) {
                return nullptr;
            }

            if (PyDict_SetItem(dict.Get(), keyObj.Get(), valueObj.Get()) < 0) {
                return nullptr;
            }
        }

        return dict.RefGet();
    }

    PyObject* TYamlBuilder::BuildSetNode(const yaml_node_t* node) {
        Y_ASSERT(node != nullptr && node->type == YAML_MAPPING_NODE);

        NPyBind::TPyObjectPtr set{PySet_New(nullptr), true};
        if (set == nullptr) {
            return nullptr;
        }

        const auto& pairs = node->data.mapping.pairs;
        for (const auto& p : std::ranges::subrange{pairs.start, pairs.top}) {
            yaml_node_t* key_node = yaml_document_get_node(&Doc_, p.key);
            NPyBind::TPyObjectPtr keyObj{BuildNode(key_node), true};
            if (keyObj == nullptr) {
                return nullptr;
            }

            yaml_node_t* value_node = yaml_document_get_node(&Doc_, p.value);
            auto value = YamlStringToStringBuf(value_node->data.scalar.value, value_node->data.scalar.length);
            if (value_node->type != YAML_SCALAR_NODE || ScalarTypeFromValue(value) != EValueType::Null) {
                return nullptr;
            }

            if (PySet_Add(set.Get(), keyObj.Get()) < 0) {
                return nullptr;
            }
        }

        return set.RefGet();
    }

    PyObject* TYamlBuilder::BuildOrderedMapNode(const yaml_node_t* node) {
        return BuildSequenceNode(node);
    }

    PyObject* TYamlBuilder::BuildPairNode(const yaml_node_t* node) {
        Y_ASSERT(node != nullptr && node->type == YAML_MAPPING_NODE);

        const auto& pairs = node->data.mapping.pairs;
        size_t size = pairs.top - pairs.start;
        if (size != 1) {
            PyErr_SetString(PyExc_RuntimeError, "Unexpected ...");
            return nullptr;
        }

        const yaml_node_t* key_node = yaml_document_get_node(&Doc_, pairs.start->key);
        NPyBind::TPyObjectPtr keyObj{BuildNode(key_node), true};
        if (keyObj == nullptr) {
            return nullptr;
        }

        const yaml_node_t* value_node = yaml_document_get_node(&Doc_, pairs.start->value);
        NPyBind::TPyObjectPtr valueObj{BuildNode(value_node), true};
        if (valueObj == nullptr) {
            return nullptr;
        }

        NPyBind::TPyObjectPtr pair{PyTuple_Pack(2, keyObj.Get(), valueObj.Get()), true};
        if (pair == nullptr) {
            return nullptr;
        }

        return pair.RefGet();
    }
} // namespace

static PyTypeObject CSafeLoaderType = {
    .ob_base = PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "ymakeyaml.CSafeLoader",
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_DISALLOW_INSTANTIATION,
    .tp_doc = PyDoc_STR("Dummy type"),
};

PyDoc_STRVAR(LoadDocString, "Load single YAML document from file");

static PyObject*
YmakeyamlLoad(PyObject* /* self */, PyObject* args, PyObject* kwargs) {
    const char* keys[] = {
        "",       // stream
        "Loader", // fake loader - the only expected value is CSafeLoader
        nullptr,
    };
    PyObject* stream = nullptr;
    PyObject* loader = nullptr;
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O|$O", (char**)keys, &stream, &loader)) {
        return nullptr;
    }
    if (loader == nullptr || Py_IsNone(loader)) {
        PyErr_SetString(PyExc_RuntimeError, "Keyword `Loader` must be spcified in call to `ymakeyaml.load`");
        return nullptr;
    } else if (!PyType_Check(loader) || PyObject_IsSubclass(loader, (PyObject*)&CSafeLoaderType) != 1 || PyObject_IsSubclass((PyObject*)&CSafeLoaderType, loader) != 1) {
        PyErr_SetString(PyExc_RuntimeError, "The only value supported for keyword `Loader` is `ymakeyaml.CSafeLoader`");
        return nullptr;
    }

    if (PyObject_HasAttrString(stream, "read")) {
        TYamlBuilder builder;
        return builder.BuildFromStream(stream);
    }

    const char* data = nullptr;
    Py_ssize_t size = 0;
    PyObject* asUnicode = nullptr;
    if (PyUnicode_Check(stream)) {
        data = PyUnicode_AsUTF8AndSize(stream, &size);
        if (data == nullptr) {
            return nullptr;
        }
    } else if (PyBytes_Check(stream) || PyByteArray_Check(stream)) {
        asUnicode = PyUnicode_FromEncodedObject(stream, "utf-8", NULL);
        if (asUnicode == nullptr) {
            return nullptr;
        }
        data = PyUnicode_AsUTF8AndSize(asUnicode, &size);
        if (data == nullptr) {
            Py_DecRef(asUnicode);
            return nullptr;
        }
    } else {
        PyErr_SetString(PyExc_TypeError, "Expected string or UTF-8 encoded bytes or bytearray");
        return nullptr;
    }

    TString fileName{data, static_cast<size_t>(size)};
    Py_XDECREF(asUnicode);

    PyObject* obj = nullptr;
    try {
        FILE* file = std::fopen(fileName.c_str(), "r");
        if (file == nullptr) {
            PyErr_Format(PyExc_RuntimeError, "Failed to open YAML file: %s", fileName.c_str());
            return nullptr;
        }
        Y_DEFER {
            std::fclose(file);
        };

        TYamlBuilder builder;
        obj = builder.BuildFromFile(file);
    } catch (std::exception& e) {
        PyErr_Format(PyExc_RuntimeError, "Failed to load YAML document from file: %s", fileName.c_str());
    }

    return obj;
}

static PyMethodDef ymakeyaml_methods[] = {
    {"load", (PyCFunction)YmakeyamlLoad, METH_VARARGS | METH_KEYWORDS, LoadDocString},
    {NULL, NULL, 0, NULL},
};

static int
YmakeyamlExec(PyObject* mod) {
    if (PyModule_AddType(mod, &CSafeLoaderType) < 0) {
        return -1;
    }

    return 0;
}

static struct PyModuleDef_Slot ymakeyaml_slots[] = {
    {Py_mod_exec, (void*)YmakeyamlExec},
    {Py_mod_multiple_interpreters, Py_MOD_PER_INTERPRETER_GIL_SUPPORTED},
    {0, NULL},
};

static PyModuleDef ymakeyaml_module = {
    PyModuleDef_HEAD_INIT,               // m_base
    "ymakeyaml",                         // m_name
    PyDoc_STR("Simple version on yaml"), // m_doc
    0,                                   // m_size
    ymakeyaml_methods,                   // m_methods
    ymakeyaml_slots,                     // m_slots
    NULL,                                // m_traverse
    NULL,                                // m_clear
    NULL,                                // m_free
};

PyMODINIT_FUNC
PyInit_ymakeyaml() {
    return PyModuleDef_Init(&ymakeyaml_module);
}
