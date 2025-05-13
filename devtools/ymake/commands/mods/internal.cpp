#include "common.h"
#include <fmt/format.h>
#include <util/generic/overloaded.h>

using namespace NCommands;

namespace {

    //
    //
    //

    class TDbgFail: public TBasicModImpl {

    private:

        enum class EMode {Abort, Exception};

    public:

        TDbgFail(): TBasicModImpl({.Id = EMacroFunction::DbgFail, .Name = "dbgfail", .Arity = 0, .CanPreevaluate = true, .CanEvaluate = true}) {
        }

        TMacroValues::TValue Preevaluate(
            [[maybe_unused]] const TPreevalCtx& ctx,
            [[maybe_unused]] const TVector<TMacroValues::TValue>& args
        ) const override {
            DoTheThing(args);
        }

        TTermValue Evaluate(
            [[maybe_unused]] std::span<const TTermValue> args,
            [[maybe_unused]] const TEvalCtx& ctx,
            [[maybe_unused]] ICommandSequenceWriter* writer
        ) const override {
            DoTheThing(args);
        }

    private:

        [[noreturn]] void DoTheThing(auto& args) const {
            auto [mode, msg] = UnpackArgs(args);
            switch(mode) {
                case EMode::Abort:
                    Y_ABORT("%s", msg.c_str());
                case EMode::Exception:
                    throw std::runtime_error(msg);
            }
            Y_ABORT();
        }

        std::tuple<EMode, std::string> UnpackArgs(auto& args) const {
            if (args.size() == 1)
                return {EMode::Exception, ToString(args[0])};
            else if (args.size() == 2)
                return {ParseMode(ExpectString(args[0])), ToString(args[1])};
            else
                FailArgCount(args.size(), "1-2");
        }

        EMode ParseMode(const auto& mode) const {
            if (mode == "a" || mode == "abort")
                return EMode::Abort;
            else if (mode == "e" || mode == "exception")
                return EMode::Exception;
            else
                throw std::runtime_error{fmt::format("Unknown failure mode in {}: {}", ::ToString(Id), mode)};
        }

        std::string ExpectString(const TMacroValues::TValue& val) const {
            if (auto str = std::get_if<std::string_view>(&val))
                return std::string(*str);
            return std::visit([&](auto& x) -> std::string {throw TBadArgType(Name, x);}, val);
        }

        std::string ExpectString(const TTermValue& val) const {
            if (auto str = std::get_if<TString>(&val))
                return std::string(*str);
            return std::visit([&](auto& x) -> std::string {throw TBadArgType(Name, x);}, val);
        }

        std::string ToString(const TMacroValues::TValue& val) const {
            return std::visit(TOverloaded{
                [&](std::string_view x) -> std::string {
                    return std::string(x);
                },
                [&](const std::vector<std::string_view>& x) -> std::string {
                    return fmt::format("{}", fmt::join(x, ", "));
                },
                [&](auto& x) -> std::string {
                    throw TBadArgType(Name, x);
                }
            }, val);
        }

        std::string ToString(const TTermValue& val) const {
            return std::visit(TOverloaded{
                [&](const TString& x) -> std::string {
                    return std::string(x);
                },
                [&](const TVector<TString>& x) -> std::string {
                    return fmt::format("{}", fmt::join(x, ", "));
                },
                [&](auto& x) -> std::string {
                    throw TBadArgType(Name, x);
                }
            }, val);
        }

    } Y_GENERATE_UNIQUE_ID(Mod);

    //
    //
    //

    // TODO deprecate & remove
    class TComma: public TBasicModImpl {
        public:
            TComma(): TBasicModImpl({.Id = EMacroFunction::Comma, .Name = "comma", .Arity = 1, .CanEvaluate = true}) {
            }
            TTermValue Evaluate(
                [[maybe_unused]] std::span<const TTermValue> args,
                [[maybe_unused]] const TEvalCtx& ctx,
                [[maybe_unused]] ICommandSequenceWriter* writer
            ) const override {
                CheckArgCount(args);
                return ",";
            }
        } Y_GENERATE_UNIQUE_ID(Mod);

}
