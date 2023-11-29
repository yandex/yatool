#include "eval_context.h"
#include "makefile_reader.h"
#include "makelists/makefile_lang.h"
#include "resolve_include.h"

#include <devtools/ymake/common/memory_pool.h>
#include <devtools/ymake/common/npath.h>
#include <devtools/ymake/diag/display.h>
#include <devtools/ymake/diag/manager.h>
#include <devtools/ymake/diag/trace.h>
#include <devtools/ymake/options/roots_options.h>
#include <devtools/ymake/symbols/symbols.h>

#include <util/string/cast.h>

using NYndex::TYndex;

// read lists with support for includes
class TMakeFileReader : ISimpleMakeListVisitor {
public:
    TMakeFileReader(TEvalContext* context, const TRootsOptions& conf, IContentProvider* provider, TYndex& yndex)
        : Context(context)
        , Conf(conf)
        , ContentProvider(provider)
        , Yndex(yndex)
    {
    }

    void Statement(const TStringBuf& command, TVector<TStringBuf>& args, const TVisitorCtx& ctx, const TSourceRange& range) override {
        if (NPath::IsTypedPath(ctx.GetLocation().Path) && NPath::IsType(ctx.GetLocation().Path, NPath::Source)) {
            TStringBuf file = NPath::CutType(ctx.GetLocation().Path);
            if (file.at(0) == NPath::PATH_SEP) {
                file.Skip(1);
            }

            Yndex.AddReference(
                ToString(command),
                ToString(file),
                range
            );
        }

        Context->SetCurrentLocation(command, ctx.GetLocation());
        if (!Context->ShouldSkip(command)) {
            // TODO: move includes processing to context
            //       - This will align processing of all statements
            //       - This will allow to process multimodules without reparse by replayng statements sequence
            //       - This wiil allow move skipping logic completely to context as well simplifying interface and improving separation of concerns
            if (command == TStringBuf("INCLUDE")) {
                if (args.empty()) {
                    TString where = TString::Join(NPath::CutType(ctx.GetLocation().Path).data(), ":", ToString(ctx.GetLocation().Row),
                                      ":", ToString(ctx.GetLocation().Column));
                    TString what = TString::Join("[[alt1]]", command, "[[rst]] without arguments is invalid");
                    TRACE(S, NEvent::TMakeSyntaxError(what, where));
                    YConfErrPrecise(Syntax, ctx.GetLocation().Row, ctx.GetLocation().Column) << what << Endl;
                    return;
                }
                TString name = EvalExpr(Context->Vars(), args[0]);
                YDIAG(LEX) << "make: include " << name << Endl;
                this->Include(name, ctx.GetLocation().Path);
                return;
            }
            bool r = false;
            try {
                r = Context->OnStatement(command, args, *Pool);
            } catch (TIncludeLoopException&) {
                throw; // If we reached here the INCLUDEs path include re-parse for multimodule or something similar. Let actual INCLUDE handle this exception.
            } catch (yexception& e) {
                TString where = TString::Join(NPath::CutType(ctx.GetLocation().Path).data(), ":", ToString(ctx.GetLocation().Row),
                                      ":", ToString(ctx.GetLocation().Column));
                TString what = TString::Join(e.what(), "\n", ctx.Here(80));
                TRACE(S, NEvent::TMakeSyntaxError(what, where));
                YConfErr(Syntax) << "Syntax error in " << where << ": " << what << Endl;
            }
            if (!r) {
                YConfWarn(ToDo) << "language - unprocessed statement: " << command << Endl;
            }
        }
    }

    void Error(const TStringBuf& message, const TVisitorCtx& ctx) override {
        TString where = TString::Join(NPath::CutType(ctx.GetLocation().Path), ":", ToString(ctx.GetLocation().Row), ":", ToString(ctx.GetLocation().Column));
        TString what = ctx.Here(200);
        TRACE(S, NEvent::TMakeSyntaxError(what, where));
        ythrow TLangError() << message << where << ": \n" << what;
    }

    void Parse(const TString& path) {
        Run(path);
    }

private:
    void Run(const TString& path) {
        ReadMakeList(path, ContentProvider->Content(path), this, Pool.Get());
    }

    void Include(const TString& incname, const TStringBuf& from) {
        TString path;
        if (NPath::TSplitTraits::IsAbsolutePath(incname)) {
            path = Conf.CanonPath(incname);
        } else {
            // resolve source path relative to current file
            path = ResolveIncludePath(incname, from);
        }
        auto inc = Context->OnInclude(path, from);
        if (!inc.Ignored()) {
            Run(path);
        }
    }

private:
    TEvalContext* Context;
    const TRootsOptions& Conf;
    IContentProvider* ContentProvider;
    TAutoPtr<IMemoryPool> Pool = IMemoryPool::Construct();
    TYndex& Yndex;
};

void ReadMakeFile(const TString& path, const TRootsOptions& bconf, IContentProvider* provider, TEvalContext* result, TYndex& yndex) {
    YDIAG(Mkfile) << "== reading file: " << path << Endl;
    TMakeFileReader reader(result, bconf, provider, yndex);
    reader.Parse(path);
}
