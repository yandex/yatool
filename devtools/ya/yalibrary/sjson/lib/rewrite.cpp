#include "rewrite.h"
#include "encoder.h"
#include "native_input.h"

#include <devtools/libs/json_sax/reader.h>
#include <library/cpp/pybind/ptr.h>
#include <util/generic/algorithm.h>
#include <util/generic/hash_set.h>
#include <util/generic/string.h>
#include <util/generic/vector.h>
#include <util/stream/input.h>
#include <util/string/cast.h>

namespace NSJson {
    namespace {
        using NPyBind::TPyObjectPtr;

        bool ValidateArguments(PyObject* rootKey, PyObject* replacements) {
            if (rootKey != Py_None && !PyBytes_Check(rootKey)) {
                PyErr_SetString(PyExc_TypeError, "root_key must be bytes or None");
                return false;
            }
            if (!PyDict_Check(replacements)) {
                PyErr_SetString(PyExc_TypeError, "replacements must be a dict");
                return false;
            }
            PyObject *key, *value;
            Py_ssize_t pos = 0;
            while (PyDict_Next(replacements, &pos, &key, &value)) {
                if (!PyBytes_Check(key)) {
                    PyErr_SetString(PyExc_TypeError, "replacement keys must be bytes");
                    return false;
                }
            }
            return true;
        }

        TStringBuf BytesView(PyObject* value) {
            Y_ENSURE(value && PyBytes_Check(value), "Binary stream / encoded JSON expected");
            return {PyBytes_AS_STRING(value), static_cast<size_t>(PyBytes_GET_SIZE(value))};
        }

        class TInput : public IInputStream {
        public:
            explicit TInput(PyObject* stream)
                : Read_(PyObject_GetAttrString(stream, "read"), true)
            {
                Y_ENSURE(Read_.Get() && PyCallable_Check(Read_.Get()), "read() required");
            }

            size_t DoRead(void* data, size_t size) override {
                TPyObjectPtr value(PyObject_CallFunction(Read_.Get(), "n", static_cast<Py_ssize_t>(size)), true);
                auto bytes = BytesView(value.Get());
                Y_ENSURE(bytes.size() <= size, "read() returned too many bytes");
                memcpy(data, bytes.data(), bytes.size());
                return bytes.size();
            }

        private:
            TPyObjectPtr Read_;
        };

        class TOutput {
        public:
            explicit TOutput(PyObject* stream)
                : Write_(PyObject_GetAttrString(stream, "write"), true)
                , Stream_(stream)
            {
                Y_ENSURE(Write_.Get() && PyCallable_Check(Write_.Get()), "write() required");
                Buffer_.reserve(65536);
            }

            void Write(TStringBuf value) {
                while (!value.empty()) {
                    const size_t size = Min(value.size(), 65536 - Buffer_.size());
                    Buffer_.append(value.data(), size);
                    value = value.SubStr(size);
                    if (Buffer_.size() == 65536) Flush();
                }
            }

            void Object(PyObject* value) {
                // Do not encode patches with dumps(): inputs/result/conf can
                // themselves be large. The encoder has its own bounded output.
                Flush();
                Encode(value, Stream_);
            }

            void Flush() {
                if (!Buffer_.empty()) {
                    Send(Buffer_);
                    Buffer_.clear();
                }
            }

            void String(TStringBuf value) {
                Write("\"");
                size_t begin = 0;
                static const char hex[] = "0123456789abcdef";
                for (size_t i = 0; i < value.size(); ++i) {
                    const auto c = static_cast<unsigned char>(value[i]);
                    if (c >= 0x20 && c != '"' && c != '\\') continue;
                    Write(value.SubStr(begin, i - begin));
                    if (c == '"') {
                        Write("\\\"");
                    } else if (c == '\\') {
                        Write("\\\\");
                    } else {
                        const char escaped[] = {'\\', 'u', '0', '0', hex[c >> 4], hex[c & 15]};
                        Write(TStringBuf(escaped, sizeof(escaped)));
                    }
                    begin = i + 1;
                }
                Write(value.SubStr(begin));
                Write("\"");
            }

        private:
            void Send(TStringBuf value) {
                TPyObjectPtr bytes(PyBytes_FromStringAndSize(value.data(), value.size()), true);
                Y_ENSURE(bytes.Get());
                TPyObjectPtr result(PyObject_CallFunctionObjArgs(Write_.Get(), bytes.Get(), nullptr), true);
                Y_ENSURE(result.Get(), "write() failed");
            }

            TPyObjectPtr Write_;
            PyObject* Stream_;
            TString Buffer_;
        };

        class TRewriter : public ::NJson::TJsonCallbacks {
        public:
            TRewriter(PyObject* output, PyObject* rootKey, PyObject* replacements)
                : TJsonCallbacks(true)
                , Output_(output)
                , Extract_(rootKey != Py_None)
                , RootKey_(Extract_ ? TString(BytesView(rootKey)) : TString())
                , Replacements_(replacements)
                , Selected_(!Extract_)
            {
            }

            bool OnNull() override { return Scalar("null"); }
            bool OnBoolean(bool value) override { return Scalar(value ? "true" : "false"); }
            bool OnInteger(long long value) override { return Scalar(ToString(value)); }
            bool OnUInteger(unsigned long long value) override { return Scalar(ToString(value)); }

            bool OnDouble(double value) override {
                if (IgnoreScalar()) return true;
                // Match Python's float representation, including negative zero.
                char* number = PyOS_double_to_string(value, 'r', 0, Py_DTSF_ADD_DOT_0, nullptr);
                Y_ENSURE(number);
                BeforeValue();
                Output_.Write(number);
                PyMem_Free(number);
                return true;
            }

            bool OnString(const TStringBuf& value) override {
                if (IgnoreScalar()) return true;
                BeforeValue();
                Output_.String(value);
                return true;
            }

            bool OnOpenMap() override { return Open(false); }
            bool OnOpenArray() override { return Open(true); }
            bool OnCloseMap() override { return Close(false); }
            bool OnCloseArray() override { return Close(true); }

            bool OnMapKey(const TStringBuf& key) override {
                if (Skipping_) return true;
                if (!Active_) {
                    if (Extract_ && Depth_ == 1) {
                        Selected_ = key == RootKey_;
                        Y_ENSURE(!Selected_ || !Found_, "Duplicate selected root key");
                    }
                    return true;
                }
                Key(key);
                if (Depth_ == TargetDepth_) {
                    TPyObjectPtr pyKey(PyBytes_FromStringAndSize(key.data(), key.size()), true);
                    Y_ENSURE(pyKey.Get());
                    if (auto* value = PyDict_GetItem(Replacements_, pyKey.Get())) {
                        Output_.Object(value);
                        Written_.insert(TString(key));
                        Skipping_ = true;
                    } else {
                        Y_ENSURE(!PyErr_Occurred());
                    }
                }
                return true;
            }

            bool OnEnd() override {
                Y_ENSURE(Found_ && !Active_ && !Skipping_ && Depth_ == 0, "Selected JSON object not found or incomplete");
                Output_.Flush();
                return true;
            }

        private:
            bool IgnoreScalar() {
                if (Skipping_) {
                    if (!SkipDepth_) Skipping_ = false;
                    return true;
                }
                Y_ENSURE(!Selected_, "Selected JSON value must be an object");
                return !Active_;
            }

            bool Scalar(TStringBuf value) {
                if (IgnoreScalar()) return true;
                BeforeValue();
                Output_.Write(value);
                return true;
            }

            bool Open(bool array) {
                ++Depth_;
                if (Skipping_) {
                    ++SkipDepth_;
                    return true;
                }
                if (Selected_) {
                    Y_ENSURE(!array && !Found_, "Selected JSON value must be an object");
                    Selected_ = false;
                    Active_ = Found_ = true;
                    TargetDepth_ = Depth_;
                }
                if (Active_) {
                    BeforeValue();
                    Output_.Write(array ? "[" : "{");
                    Stack_.push_back({array, true});
                }
                return true;
            }

            bool Close(bool array) {
                if (Skipping_) {
                    Y_ENSURE(SkipDepth_);
                    if (!--SkipDepth_) Skipping_ = false;
                } else if (Active_) {
                    if (Depth_ == TargetDepth_) {
                        PyObject *key, *value;
                        Py_ssize_t pos = 0;
                        while (PyDict_Next(Replacements_, &pos, &key, &value)) {
                            auto name = BytesView(key);
                            if (!Written_.contains(TString(name))) {
                                Key(name);
                                Output_.Object(value);
                            }
                        }
                        Active_ = false;
                    }
                    Output_.Write(array ? "]" : "}");
                    Stack_.pop_back();
                }
                --Depth_;
                return true;
            }

            void BeforeValue() {
                if (!Stack_.empty() && Stack_.back().Array) {
                    Comma();
                }
            }

            void Comma() {
                if (!Stack_.back().First) Output_.Write(",");
                Stack_.back().First = false;
            }

            void Key(TStringBuf key) {
                Comma();
                Output_.String(key);
                Output_.Write(":");
            }

            struct TFrame {
                bool Array;
                bool First;
            };

            TOutput Output_;
            bool Extract_;
            TString RootKey_;
            PyObject* Replacements_;
            bool Selected_;
            bool Active_{};
            bool Found_{};
            bool Skipping_{};
            size_t Depth_{};
            size_t TargetDepth_{};
            size_t SkipDepth_{};
            THashSet<TString> Written_;
            TVector<TFrame> Stack_;
        };
    }

    static PyObject* Rewrite(PyObject* input, PyObject* output, PyObject* rootKey, PyObject* replacements,
                             const TString* path) {
        // Check the native API contract before opening input or writing output.
        // The Python wrapper normalizes text keys to bytes before this boundary.
        if (!ValidateArguments(rootKey, replacements)) {
            return nullptr;
        }
        try {
            THolder<IInputStream> source;
            if (path) {
                source = MakeHolder<TNativeFileInput>(*path);
            } else {
                source = MakeHolder<TInput>(input);
            }
            TRewriter callbacks(output, rootKey, replacements);
            NYa::NJson::ReadJson(*source, &callbacks);
            Py_RETURN_NONE;
        } catch (const TFileError& error) {
            if (!PyErr_Occurred()) PyErr_SetString(PyExc_IOError, error.what());
        } catch (const yexception& error) {
            if (!PyErr_Occurred()) PyErr_SetString(PyExc_RuntimeError, error.what());
        } catch (...) {
            if (!PyErr_Occurred()) PyErr_SetString(PyExc_RuntimeError, "JSON stream rewrite failed");
        }
        return nullptr;
    }

    PyObject* RewriteObjectFromStream(PyObject* input, PyObject* output, PyObject* rootKey, PyObject* replacements) {
        return Rewrite(input, output, rootKey, replacements, nullptr);
    }

    PyObject* RewriteObjectFromFile(const TString& path, PyObject* output, PyObject* rootKey, PyObject* replacements) {
        return Rewrite(nullptr, output, rootKey, replacements, &path);
    }
}
