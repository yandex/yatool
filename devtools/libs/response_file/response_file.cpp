#include "response_file.h"

#include <util/folder/path.h>
#include <util/generic/yexception.h>
#include <util/stream/file.h>

#include <cstring>
#include <utility>

namespace NResponseFile {
    namespace {

        TUnbufferedFileInput OpenResponseFile(const TString& path) {
            try {
                return TUnbufferedFileInput(path);
            } catch (const yexception& error) {
                if (!TFsPath(path).Exists()) {
                    ythrow yexception() << "Response file '" << path << "' doesn't exist.";
                }
                ythrow yexception() << "Cannot read response file '" << path << "': " << error.what();
            }
        }

        class TExpander {
        public:
            TExpander(TVector<TString>& storage, const std::function<bool(TStringBuf)>& stopExpansion)
                : Storage_(storage)
                , StopExpansion_(stopExpansion)
            {
            }

            TVector<const char*> Expand(TConstArrayRef<const char*> args) {
                if (args.empty()) {
                    return {};
                }
                size_t storedIndex = Storage_.size();
                Result_.reserve(args.size() + 1);
                Result_.push_back(args.front());
                for (size_t index = 1; index < args.size(); ++index) {
                    ExpandArgument(args[index], 0, nullptr);
                }
                // Null entries correspond to newly stored arguments in insertion
                // order. Resolve them after growth stops, including for SSO strings.
                if (storedIndex != Storage_.size()) {
                    for (const char*& arg : Result_) {
                        if (!arg) {
                            arg = Storage_[storedIndex++].c_str();
                        }
                    }
                }
                return std::move(Result_);
            }

        private:
            void AppendArgument(const char* arg, TString* owned) {
                if (Result_.size() - 1 >= MaxExpandedArguments) {
                    ythrow yexception() << "Too many arguments after expanding response files (limit: "
                                        << MaxExpandedArguments << ").";
                }
                if (owned) {
                    Storage_.push_back(std::move(*owned));
                    Result_.push_back(nullptr);
                } else {
                    Result_.push_back(arg);
                }
            }

            void ExpandArgument(const char* arg, size_t nesting, TString* owned) {
                if (!Stopped_ && StopExpansion_ && StopExpansion_(arg)) {
                    Stopped_ = true;
                }
                if (!Stopped_ && arg[0] == '@' && arg[1] && arg[1] != '@') {
                    if (nesting >= MaxResponseFileNesting) {
                        ythrow yexception() << "Response file nesting exceeds limit of " << MaxResponseFileNesting << ".";
                    }
                    ExpandFile(arg + 1, nesting + 1);
                } else {
                    if (!Stopped_ && arg[0] == '@' && arg[1] == '@') {
                        if (owned) {
                            owned->erase(0, 1);
                        } else {
                            ++arg;
                        }
                    }
                    AppendArgument(arg, owned);
                }
            }

            void ExpandLine(TString& line, size_t nesting) {
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }
                if (!line.empty()) {
                    ExpandArgument(line.c_str(), nesting, &line);
                }
                line.clear();
            }

            void ExpandFile(const TString& path, size_t nesting) {
                auto input = OpenResponseFile(path);
                TString line;
                char buffer[8192];
                while (true) {
                    size_t bytesRead = 0;
                    try {
                        bytesRead = input.Read(buffer, sizeof(buffer));
                    } catch (const yexception& error) {
                        ythrow yexception() << "Cannot read response file '" << path << "': " << error.what();
                    }
                    if (bytesRead == 0) {
                        break;
                    }
                    if (std::memchr(buffer, '\0', bytesRead) != nullptr) {
                        ythrow yexception() << "Response file '" << path << "' contains a NUL byte.";
                    }

                    const char* begin = buffer;
                    const char* end = buffer + bytesRead;
                    while (begin != end) {
                        const char* newline = static_cast<const char*>(std::memchr(begin, '\n', end - begin));
                        if (newline == nullptr) {
                            line.append(begin, end);
                            break;
                        }
                        line.append(begin, newline);
                        ExpandLine(line, nesting);
                        begin = newline + 1;
                    }
                }
                if (!line.empty()) {
                    ExpandLine(line, nesting);
                }
            }

            TVector<TString>& Storage_;
            const std::function<bool(TStringBuf)>& StopExpansion_;
            TVector<const char*> Result_;
            bool Stopped_ = false;
        };

    } // namespace

    TVector<const char*> ExpandResponseFiles(
        TConstArrayRef<const char*> args,
        TVector<TString>& storage,
        const std::function<bool(TStringBuf)>& stopExpansion)
    {
        return TExpander{storage, stopExpansion}.Expand(args);
    }

} // namespace NResponseFile
