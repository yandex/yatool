#include "from_json.h"

#include <devtools/ya/cpp/lib/json_sax/reader.h>

#include <library/cpp/json/common/defs.h>
#include <library/cpp/json/fast_sax/parser.h>

namespace NYa::NEdl {
    using ::NJson::TJsonCallbacks;
    namespace {
        class TParserCallbacks: public TJsonCallbacks {
        public:
            TParserCallbacks(TLoaderPtr&& loader)
                : TJsonCallbacks(true)
            {
                StateStack_.push_back(START);
                LoaderStack_.push_back(std::move(loader));
            }

            bool OnNull() override {
                return SetValue(nullptr);
            }

            bool OnBoolean(bool val) override {
                return SetValue(val);
            }

            bool OnInteger(long long val) override {
                return SetValue(val);
            }

            bool OnUInteger(unsigned long long val) override {
                return SetValue(val);
            }

            bool OnString(const TStringBuf& val) override {
                return SetValue(val);
            }

            bool OnDouble(double val) override {
                return SetValue(val);
            }

            bool OnOpenArray() override {
                if (StateStack_.empty()) {
                    return false;
                }
                try {
                    switch (StateStack_.back()) {
                        case START:
                        case AFTER_MAP_KEY:
                            StateStack_.back() = IN_ARRAY; // instead of pop_back/push_back
                            LoaderStack_.back()->EnsureArray();
                            Path_.push_back(0u);
                            return true;
                        case IN_ARRAY:
                            std::get<size_t>(Path_.back())++;
                            LoaderStack_.push_back(LoaderStack_.back()->AddArrayValue());
                            LoaderStack_.back()->EnsureArray();
                            Path_.push_back(0u);
                            return true;
                        default:
                            return false;
                    }
                } catch (TLoaderError& e) {
                    Error_ = e.what();
                    return false;
                }
            }

            bool OnCloseArray() override {
                if (StateStack_.empty()) {
                    return false;
                }
                try {
                    if (StateStack_.back() == IN_ARRAY) {
                        StateStack_.pop_back();
                        LoaderStack_.back()->Finish();
                        LoaderStack_.pop_back();
                        Path_.pop_back();
                        RemoveMapKeyFromPath();
                        return true;
                    } else {
                        return false;
                    }
                } catch (TLoaderError& e) {
                    Error_ = e.what();
                    return false;
                }
            }

            bool OnOpenMap() override {
                if (StateStack_.empty()) {
                    return false;
                }
                try {
                    switch (StateStack_.back()) {
                        case START:
                        case AFTER_MAP_KEY:
                            StateStack_.back() = IN_MAP; // instead of pop_back/push_back
                            LoaderStack_.back()->EnsureMap();
                            return true;
                        case IN_ARRAY: {
                            StateStack_.push_back(IN_MAP);
                            std::get<size_t>(Path_.back())++;
                            LoaderStack_.push_back(LoaderStack_.back()->AddArrayValue());
                            LoaderStack_.back()->EnsureMap();
                            return true;
                        }
                        default:
                            return false;
                    }
                } catch (TLoaderError& e) {
                    Error_ = e.what();
                    return false;
                }
            }

            bool OnCloseMap() override {
                if (StateStack_.empty()) {
                    return false;
                }
                try {
                    if (StateStack_.back() == IN_MAP) {
                        StateStack_.pop_back();
                        LoaderStack_.back()->Finish();
                        LoaderStack_.pop_back();
                        RemoveMapKeyFromPath();
                        return true;
                    } else {
                        return false;
                    }
                } catch (TLoaderError& e) {
                    Error_ = e.what();
                    return false;
                }
            }

            bool OnMapKey(const TStringBuf& key) override {
                if (StateStack_.empty()) {
                    return false;
                }
                try {
                    if (StateStack_.back() == IN_MAP) {
                        LoaderStack_.push_back(LoaderStack_.back()->AddMapValue(key));
                        Path_.push_back(TString{key});
                        StateStack_.push_back(AFTER_MAP_KEY);
                        return true;
                    } else {
                        return false;
                    }
                } catch (TLoaderError& e) {
                    Error_ = e.what();
                    return false;
                }
            }

            bool OnEnd() override {
                return StateStack_.empty();
            }

            void OnError(size_t off, TStringBuf reason) override {
                TStringStream error;
                error << (Error_ ? Error_ : reason) << " at " << GetCurrentPath();
                TJsonCallbacks::OnError(off, error.Str());
            }

        private:
            template <class T>
            bool SetValue(const T value) {
                if (StateStack_.empty()) {
                    return false;
                }
                try {
                    switch (StateStack_.back()) {
                        case START:
                        case AFTER_MAP_KEY:
                            StateStack_.pop_back();
                            LoaderStack_.back()->SetValue(value);
                            LoaderStack_.pop_back();
                            RemoveMapKeyFromPath();
                            return true;
                        case IN_ARRAY:
                            std::get<size_t>(Path_.back())++;
                            LoaderStack_.back()->AddArrayValue()->SetValue(value);
                            return true;
                        default:
                            return false;
                    }
                } catch (TLoaderError& e) {
                    Error_ = e.what();
                    return false;
                }
            }

            void RemoveMapKeyFromPath() {
                if (!StateStack_.empty() && StateStack_.back() == IN_MAP) {
                    Path_.pop_back();
                }
            }

            TString GetCurrentPath() {
                TStringStream path{};
                path << "<ROOT>";
                for (const auto& p : Path_) {
                    if (std::holds_alternative<size_t>(p)) {
                        size_t idx = std::get<size_t>(p);
                        // To simplify code the path index is incremented before an element loading.
                        // idx == 0 - array just created (no elements)
                        // idx == 1 - the first element is loading (or successfully loaded).
                        // If error occurred we should report about element index equals to (idx - 1).
                        // If idx = 0 and error happened, this is a syntax error (unexpected end of stream or unknown token).
                        if (idx > 0) {
                            path << '[' << (idx - 1) << ']';
                        }
                    } else {
                        TStringBuf key = std::get<TString>(p);
                        if (key) {
                            path << '.' << std::get<TString>(p);
                        }
                    }
                }
                return path.Str();
            }

        private:
            enum TState {
                START,
                AFTER_MAP_KEY,
                IN_MAP,
                IN_ARRAY
            };

            TVector<TState> StateStack_;
            TVector<TLoaderPtr> LoaderStack_;
            TString Error_;
            TVector<std::variant<size_t, TString>> Path_;
        };

    }

    void LoadJson(TStringBuf in, TLoaderPtr&& loader) {
        TParserCallbacks callbacks{std::move(loader)};
        ::NJson::ReadJsonFast(in, &callbacks);
    }

    void LoadJson(IInputStream& in, TLoaderPtr&& loader) {
        TParserCallbacks callbacks{std::move(loader)};
        NYa::NJson::ReadJson(in, &callbacks);
    }
}
