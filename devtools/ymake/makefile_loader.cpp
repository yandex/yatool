#include "makefile_loader.h"

#include "autoincludes_conf.h"
#include "builtin_macro_consts.h"
#include "module_loader.h"
#include "prop_names.h"
#include "ymake.h"
#include "diag_reporter.h"

#include <devtools/ymake/lang/makefile_reader.h>
#include <devtools/ymake/lang/resolve_include.h>
#include <devtools/libs/yaplatform/platform.h>

#include <devtools/ymake/diag/trace.h>
#include <devtools/ymake/diag/manager.h>

#include <library/cpp/json/json_reader.h>

#include <util/folder/pathsplit.h>
#include <util/memory/blob.h>
#include <util/string/cast.h>
#include <util/string/join.h>
#include <util/string/split.h>
#include <util/string/subst.h>
#include <util/string/vector.h>

const TString AUTOUPDATED = TString("AUTOUPDATED");


namespace {
    void ReportModuleNoEnd(TStringBuf moduleName, TStringBuf makefileName) {
        TString what = TString::Join("Missing [[alt1]]END[[rst]] macro at the end of [[alt1]]", moduleName, "[[rst]] in [[imp]]", makefileName, "[[rst]]");
        TRACE(S, NEvent::TMakeSyntaxError(what, ToString(makefileName)));
        YConfErr(Syntax) << what << Endl;
    }

    void ReportNoModule(TStringBuf makefileName) {
        TString what = TString::Join("No module defined in [[imp]]", makefileName, "[[rst]]");
        TRACE(S, NEvent::TMakeSyntaxError(what, ToString(makefileName)));
        YConfErr(Syntax) << what << Endl;
    }

    inline bool IsInternalStatement(const TStringBuf& name) {
        return name.StartsWith('_');
    }
}

TDirParser::TDirParser(TYMake& yMake, TFileView dir, TStringBuf makefile, TPropValues& modules,
        TPropValues& recurses, TPropValues& testRecurses, IContentProvider* provider)
    : TEvalContext(yMake.Conf.Conditions)
    , YMake(yMake)
    , Conf(yMake.Conf)
    , Names(yMake.Names)
    , Module(nullptr)
    , Dir(dir.GetTargetStr())
    , Makefile(makefile)
    , Modules(modules)
    , Recurses(recurses)
    , TestRecurses(testRecurses)
    , ModuleCount(0)
    , NextSubModule(0)
    , Provider(provider)
{
    InitModuleVars(Vars(), Conf.CommandConf, GetMakefileId(), dir);
    auto [it, _] = UniqIncludes.insert({Makefile, {}});
    IncludeStack.emplace_back(it->first);
    LeaveModuleScope();
}

void TDirParser::EnterModuleScope() {
    Vars().AssignVarLookupHook([](const TYVar&, const TStringBuf&) {});
}

void TDirParser::LeaveModuleScope() {
    Vars().AssignVarLookupHook([](const TYVar& var, const TStringBuf& name) {
        if (var.ModuleScopeOnly) {
            YConfErr(UserErr) << "Cannot use variable " << name << " outside of a module";
        }
    });
}

void TDirParser::Load() {
    TScopedContext context(GetMakefileId(), Makefile);
    try {
        ReadMakeFile();
    } catch (TIncludeLoopException& e) {
        TRACE(L, e.GetEvent());
        YConfErr(Misconfiguration) << e.what() << Endl;
        // TODO: mark dir with error
    } catch (yexception& e) {
        TRACE(S, NEvent::TMakeSyntaxError(e.what(), Makefile));
        YConfErr(Syntax) << e.what() << Endl;
        // TODO: mark dir with error
    }
}

bool TDirParser::UserStatement(const TStringBuf& name, const TVector<TStringBuf>& args) {
    return UserStatementImpl(name, args, /* prohibitInternal */ true);
}

bool TDirParser::UserStatementImpl(const TStringBuf& name, const TVector<TStringBuf>& args, bool prohibitInternal) {
    if (prohibitInternal && IsInternalStatement(name)) {
        YConfErrPrecise(Syntax, GetStatementRow(name), GetStatementColumn(name)) << "internal command [[alt1]]" << name << "[[rst]] is not allowed to appear in ya.make" << Endl;
        return true;
    }

    if (KnownStatement(name, args)) {
        return true;
    }

    auto i = Conf.BlockData.find(name);
    const TBlockData* data = i ? &i->second : nullptr;
    if (!Module || ReadModuleContentOnly && ReadModuleContentOnlyDone) {
        if (DirStatement(name, args)) {
            return true;
        }

        if (MiscStatement(name, args)) {
            return true;
        }

        if (IsKnownGarbageStatement(name)) {
            YConfInfo(Garbage) << "ymake doesn't make use of " << name << " statement\n";
            return true;
        }

        if (!IsBlockDataModule(data)) {
            YConfErrPrecise(Syntax, GetStatementRow(name), GetStatementColumn(name)) << "unexpected command [[alt1]]" << name << "[[rst]] outside of a module" << Endl;
            return true;
        }

        if (ModuleCount++ > 0) {
            YConfErrPrecise(Syntax, GetStatementRow(name), GetStatementColumn(name)) << "unexpected command [[alt1]]" << name << "[[rst]]. Only one module per ya make list is permitted." << Endl;
            return true;
        }

        bool definedModule = ModuleStatement(name, args, Vars(), OrigVars()); // might affect module name
        if (!definedModule) {
            YConfErrPrecise(Syntax, GetStatementRow(name), GetStatementColumn(name)) << "module cannot be constructed from [[alt1]]" << name << "[[rst]] statement" << Endl;
            return true;
        }

        AssertEx(Module != nullptr, "Defined module left unassigned");

        ModulesInDir.push_back(Module);
        SetCurrentNamespace(&Module->GetVars());
        SetOriginalVars(&Module->GetOrigVars());
        CheckModuleSemantics();
    } else if (IsBlockDataModule(data)) {
        TString what = TString::Join("module [[alt1]]", name, "[[rst]] inside module ", Module->GetModuleConf().Name, ". Maybe you need to place ", name, " after END()?");
        TRACE(S, NEvent::TMakeSyntaxError(what, Makefile));
        YConfErrPrecise(Syntax, GetStatementRow(name), GetStatementColumn(name)) << what << ". Skip [[alt1]]" << name << "[[rst]] macro." << Endl;
        return true;
    } else if (DeclareExternalsByJson(name, args)) {
        return true;
    } else {
        if (name != TStringBuf("END")) {
            if (Module->ProcessGlobStatement(name, args, Vars(), OrigVars(), {GetStatementRow(name), GetStatementColumn(name)})) {
                // error message for empty args for *GLOB macros are reported inside ProcessGlobStatement
                if (!args.empty()) {
                    Conf.Conditions.RecalcVars(TString::Join("$", args.front()), Vars(), OrigVars());
                }
                return true;
            }

            TStringBuf effectiveName = name;
            bool added = false;
            if (const auto it = Module->GetModuleConf().MacroAliases.find(name)) {
                effectiveName = it->second;
                i = Conf.BlockData.find(effectiveName);
                data = i ? &i->second : nullptr;
                added = Module->AddStatement(effectiveName, args);

            } else {
                added = Module->AddStatement(effectiveName, args);
            }

            if (added) {
                Module->ProcessMakelistStatement(effectiveName, args);

                if (!IsFileExtRule(data)) {
                    Module->ProcessModuleMacroCalls(effectiveName, args, !IncludeStack.empty() && NPath::Basename(IncludeStack.back()) == LINTERS_MAKE_INC);
                }

                DataStatement(effectiveName, args);
            }
        } else {
            if (ReadModuleContentOnly) {
                if (NextSubModule > 0) {
                    YConfErr(Syntax) << "XXX_FOR constructs are cannot be used with multimodules " << Dir << Endl;
                }
                ReadModuleContentOnlyDone = true;
                return true;
            }

            bool isNukedModule = false;
            if (NextSubModule > 0 && !Reparse) {
                Y_ASSERT(NextSubModule == 1);
                if (!DefaultTags.empty()) {
                    const TStringBuf var = Conf.CommandConf.Get1(NVariableDefs::VAR_EXCLUDE_SUBMODULES);
                    if (var) {
                        for (const TStringBuf tmp : StringSplitter(GetCmdValue(var)).Split(' ').SkipEmpty()) {
                            DefaultTags.erase(TString(tmp));
                        }
                    }
                }
                if (DefaultTags.empty()) {
                    YConfWarnPrecise(Syntax, GetStatementRow(name), GetStatementColumn(name)) << "All available variants of multimodule discarded in " << Dir << Endl;
                }
                if (!DefaultTags.contains(SubModules[0].Tag)) {
                    YDIAG(VV) << "Destroying banned 1st instance of multimodule " << Module->GetName() << Endl;
                    NukeModule();
                    isNukedModule = true;
                    NextSubModule = 0;
                }
                EraseIf(SubModules, [this](const auto& mod) { return !this->DefaultTags.contains(mod.Tag); });
            }
            if (isNukedModule) {
                // Skip the following conditions
            } else {
                size_t prefixLen;
                TString LintersMake;
                if (Conf.AutoincludePathsTrie.FindLongestPrefix(Dir + NPath::PATH_SEP_S, &prefixLen, &LintersMake)) {
                    if (TFsPath(Conf.RealPathEx(LintersMake)).Exists()) {
                        auto includeCtr = OnInclude(LintersMake, Makefile);
                        Y_ASSERT(!includeCtr.Ignored());
                        Vars().SetStoreOriginals(NVariableDefs::VAR_MODULE_COMMON_CONFIGS_DIR, ToString(NPath::Parent(LintersMake)), OrigVars());
                        ReadMakeFile(LintersMake);
                    } else {
                        TRACE(P, NEvent::TInvalidFile(LintersMake, {Dir}, TString{"File not found"}));
                        YConfErr(Misconfiguration) << LINTERS_MAKE_INC << " file not found at [[imp]]" << Dir << Endl;
                    }
                }
                // Process module EPILOGUE statement if there is one
                if (const auto& epilogue = Module->GetModuleConf().Epilogue; !epilogue.empty()) {
                    UserStatementImpl(epilogue, TVector<TStringBuf>(), /* prohibitInternal */ false);
                }

                YDIAG(VV) << "PrintMakeFileMap for module " << Module->GetName() << ":" << Endl;
                Module->PrintMakeFileMap();
                ClearModule();
            }
            if (Reparse) {
                ReparseEnd = true;
                return true;
            }
            if (NextSubModule < SubModules.size()) {
                Y_ASSERT(!ModulePath.empty());

                // TODO(spreis): disallow module start and END in different files
                TString reparsePath = ModulePath != IncludeStack.back() ? Makefile : TString(IncludeStack.back());
                while (NextSubModule < SubModules.size()) {
                   ++NextSubModule;
                   Reparse = true;
                   ReparseEnd = false;
                   // TODO(spreis): This can be replaced by replay of recorded statements as soon as
                   // INCLUDE statement processing is moved from makefile_reader to eval_context.
                   // Basically even now ShouldSkip() sees all statements and might record the trace for replay
                   // (if agrs were added as 2nd argument).
                   // However replay itself now should have been done in makefile_reader to process INCLUDEs which
                   // would be strange to say the least.
                   ReadMakeFile(reparsePath);
                }
                Reparse = false;
                NextSubModule = 0;
                SubModules.clear();
                ModulePath.Clear();
            }
        }
    }
    return true;
}

void TDirParser::NukeModule() {
    Y_ASSERT(Module && !ModulesInDir.empty() && ModulesInDir.back() == Module);
    ModulesInDir.pop_back();
    RestoreDirNamespace();
    TModule& toDestroy = Module->GetModule();
    LeaveModuleScope();
    delete Module;
    Module = nullptr;
    YMake.Modules.Destroy(toDestroy);
}

void TDirParser::SaveModule(TModuleDef* module) {
    TDepsCacheId modId = TDepsCacheId::None;
    if (!Discarded)  {
        modId = module->Commit();
    }
    if (modId != TDepsCacheId::None) {
        Modules.Push(modId);
    } else {
        NukeModule();
    }
}

void TDirParser::ClearModule() {
    RestoreDirNamespace();
    LeaveModuleScope();
    SaveModule(Module);
    Module = nullptr;
}

bool TDirParser::ShouldSkip(TStringBuf command) const {
    if (TEvalContext::ShouldSkip(command)) {
        // Under untaken branch
        return true;
    }
    if (BranchTaken()) {
        if (!Module && Reparse) {
            // For multimodule we should
            // - find module position
            // - find a file in INCLUDEs that contains module
            // - skip everything after END
            // This could be simplified with replay, see TODO at the end of UserStatementImpl.
            if (ReparseEnd) {
                return true;
            }
            bool moduleFile = ModulePath == IncludeStack.back();
            if (moduleFile) {
                return ModulePos != GetCurrentLocation().Pos;
            } else {
                return command != "INCLUDE"sv;
            }
         }
         if (ReadModuleContentOnly && (!Module || ReadModuleContentOnlyDone)) {
            // For GO_TEST_FOR and alikes we (unfortunately) should allow basically everything
            // Otherwise IFs may not properly
            return command.StartsWith(NMacro::RECURSE) || command.StartsWith(NMacro::PARTITIONED_RECURSE);
        }
    }
    return false;
}

void TDirParser::RefineSubModules(const TStringBuf& name, const TVector<TStringBuf>& args) {
    if (!Reparse) {
        Y_ASSERT(NextSubModule == 1);
        Y_ASSERT(name == TStringBuf("ONLY_TAGS") ||
                 name == TStringBuf("EXCLUDE_TAGS") ||
                 name == TStringBuf("INCLUDE_TAGS"));

        if (name == TStringBuf("ONLY_TAGS")) {
            DefaultTags.clear();
        }

        bool isExcludeTags = (name == TStringBuf("EXCLUDE_TAGS"));

        // Validate that listed tags are applicable to current multimodule
        for (const auto tag : args) {
            if (!AnyOf(SubModules, [tag](const TSubModule& mod) { return mod.Tag == tag; })) {
                YConfWarnPrecise(Syntax, GetStatementRow(name), GetStatementColumn(name)) << "No variant tagged " << tag << " in this multimodule" << Endl;
            } else if (isExcludeTags) {
                DefaultTags.erase(TString{tag});
            } else {
                DefaultTags.insert(TString{tag});
            }
        }
    }
}

bool TDirParser::MessageStatement(const TStringBuf& name, const TVector<TStringBuf>& args, const TVars& vars, const TBuildConfiguration& conf) {
    if (name == "MESSAGE") {
        bool fatal = false;
        TVector<TStringBuf> margs(args); // inefficient code is OK here
        if (!margs.empty() && margs[0] == "STATUS") {
            margs.erase(margs.begin());
        }

        if (!margs.empty() && margs[0] == "FATAL_ERROR") {
            fatal = true;
            margs.erase(margs.begin());
        }

        TString msg = TCommandInfo(conf, nullptr, nullptr).SubstMacroDeeply(nullptr, JoinStrings(margs.begin(), margs.end(), " "), vars, false);
        if (fatal) {
            YConfErr(UserErr) << msg << Endl;
        } else {
            YConfWarn(UserWarn) << msg << Endl;
        }
        return true;
    }
    return false;
}


bool TDirParser::KnownStatement(const TStringBuf& name, const TVector<TStringBuf>& args) {
    if (MessageStatement(name, args, Vars(), Conf)) {
        // Do nothing
    } else if (name == "INCLUDE_ONCE") {
        bool once = true;

        if (args.size() > 0) {
            if (args.size() > 1) {
                YConfErrPrecise(Syntax, GetStatementRow(name), GetStatementColumn(name)) << "[[alt1]]" << name << "[[rst]] shall have 1 argument at most"  << Endl;
            }
            if (!IsTrue(args[0])) {
                once = !IsFalse(args[0]);
                if (once) {
                    YConfErrPrecise(Syntax, GetStatementRow(name), GetStatementColumn(name)) << "[[alt1]]" << name << "[[rst]] unexpected value [[imp]]" <<
                                                                                                 args[0] << "[[rst]] expected boolean constant" << Endl;
                }
            }
        }

        auto inc = UniqIncludes.find(IncludeStack.back());
        Y_ASSERT(!IncludeStack.empty() && inc != UniqIncludes.end());
        if (inc->second.Once != TIncludeInfo::DEFAULT_ONCE && inc->second.Once != once) {
             YConfErrPrecise(UserErr, GetStatementRow(name), GetStatementColumn(name)) << "[[alt1]]" << name << "[[rst]] attempt to alter behavior that was previously set explicitly" << Endl;
        }
        inc->second.Once = once;

    } else if (name == "NO_BUILD_IF" || name == "BUILD_ONLY_IF") {
        bool only = name == "BUILD_ONLY_IF";
        bool show = only;
        bool fatal = args[0] == "FATAL_ERROR";
        bool strict = args[0] == "STRICT" || fatal;
        bool explicit_mode = args[0] == "WARNING" || strict;
        // Use STRICT by default if no explicit mode is specified
        strict = strict || (!fatal && !explicit_mode);

        const auto beg = args.begin() + explicit_mode;
        TString reason;
        for (auto it = beg; it != args.end(); ++it) {
            auto arg = *it;
            if (Vars().IsTrue(arg)) {
                show = !only;
                reason = TString::Join("[[alt1]]", arg, "[[rst]]");
                break;
            }
        }
        if (show) {
            if (only) {
                reason = TString::Join("it is built only if one of [[alt1]](", JoinStrings(beg, args.end(), ", "), ")[[rst]]");
            }
            TString modality = strict ? "[[warn]]WILL NOT[[rst]] be built with its RECURSEs" : "[[warn]]SHOULD NOT[[rst]] be built";
            TString msg = TString::Join(name, ": [[imp]]", Dir, "[[rst]] ", modality, ", because ", reason, " is true");
            if (fatal) {
                YConfErr(UserErr) << msg << Endl;
            } else {
                YConfWarn(UserWarn) << msg << Endl;
            }
            Discarded = Discarded || strict;
        }
    } else if (name == "SUBSCRIBER") {
        Owners.insert(args.begin(), args.end());
    } else if (name == NMacro::VERSION) {
        Vars().SetStoreOriginals("MODVER", JoinStrings(args.begin(), args.end(), "."), OrigVars());
        if (Module) {
            Module->VersionSet(true);
        }
    } else if ((name == "ORIGINAL_SOURCE") || (name == "NEED_REVIEW") || (name == "NEED_CHECK") || (name == "NO_NEED_CHECK")) {
        // No operations.
    } else if (name == "ONLY_TAGS" || name == "EXCLUDE_TAGS" || name == "INCLUDE_TAGS") {
          if (SubModules.empty()) {
              YConfErrPrecise(Syntax, GetStatementRow(name), GetStatementColumn(name)) << "Macro: " << name << " allowed only in multimodule"  << Endl;
          } else {
              RefineSubModules(name, args);
          }
    } else if (name == "SET_RESOURCE_URI_FROM_JSON" || name == "SET_RESOURCE_MAP_FROM_JSON") {
        SetResourceXXXFromJson(name, args);
    } else {
        return false;
    }

    return true;
}

TStringBuf TDirParser::GetOwners() const {
    if (!Owners.empty()) {
        return Names.CommandConf.GetStoredName(FormatProperty("OWNER", JoinSeq(" ", Owners))).GetStr();
    }
    else {
        return {};
    }
}

bool TDirParser::ModuleStatement(const TStringBuf& name, TArrayRef<const TStringBuf> args, TVars& vars, TOriginalVars& orig) {
    if (orig.contains("DLL_FOR_DIR")) {
        const auto origIt = orig.find("DLL_FOR_ARGS");
        CheckEx(origIt != orig.end(), "DLL_FOR_ARGS variable is not defined (while DLL_FOR_DIR variable is defined)");
        TVector<TStringBuf> dllArgs;
        Split(origIt->second, " ", dllArgs);
        return DeclStatement("DLL", dllArgs, vars, orig);
    } else if (orig.contains("PY_PROTOS_FOR_DIR")) {
        return DeclStatement("PY_PACKAGE", args, vars, orig);
    } else if (orig.contains("GO_TEST_FOR_DIR")) {
        return DeclStatement("GO_TEST", args, vars, orig);
    } else if (orig.contains("TS_TEST_FOR_DIR")) {
        return DeclStatement("TS_TEST", args, vars, orig);
    } else if (name == "UNITTEST_FOR" || name == "JTEST_FOR") {
        if (args.size() < 1) {
            TString what = TString::Join("[[alt1]]", name, "[[rst]] without arguments is invalid.");
            TRACE(S, NEvent::TMakeSyntaxError(what, Makefile));
            YConfErrPrecise(Syntax, GetStatementRow(name), GetStatementColumn(name)) << what << ". Skip [[alt1]]" << name << "[[rst]] macro." << Endl;
            return false;
        }
        vars.SetValue("UNITTEST_DIR", args[0]);
        args = args.subspan(1);
    }

    if (NextSubModule > 0) {
        return DeclStatement(SubModules[NextSubModule - 1].Name, args, vars,  orig);
    } else {
        auto i = Conf.BlockData.find(name);
        if (IsBlockDataMultiModule(&i->second)) {
            MultiModuleName = i->second.ModuleConf->Name;
            for (const auto& sub : i->second.ModuleConf->OrderedSubModules) {
                SubModules.push_back({sub.first, sub.second->Name});
                if (sub.second->IncludeTag) {
                    DefaultTags.insert(sub.first);
                }
            }
            AssertEx(NextSubModule == 0, "Entering nested submodule");
            Reparse = false;
            ReparseEnd = false;
            ModulePath = IncludeStack.back();
            ModulePos = GetCurrentLocation().Pos;
            ++NextSubModule;
            return DeclStatement(SubModules[0].Name, args, vars, orig);
        }
    }

    return DeclStatement(name, args, vars, orig);
}

bool TDirParser::DeclStatement(const TStringBuf& name, TArrayRef<const TStringBuf> args, TVars& vars, TOriginalVars& orig) {
    auto i = Conf.BlockData.find(name);
    if (IsBlockDataModule(&i->second)) {
        CheckEx(!Module, "module inside module");
        TModuleConf& conf = *(i->second.ModuleConf.Get());
        Module = new TModuleDef(YMake, YMake.Modules.Create(Dir, Makefile, conf.Tag), conf);
        EnterModuleScope();
        if (NextSubModule > 0) {
            Module->GetModule().SetFromMultimodule();
        }
        Module->InitModule(name, args, vars, orig);
        return true;
    }
    return false;
}

TVector<TString> TDirParser::GetDirsFromArgs(const TStringBuf& statementName,
                                 const TVector<TStringBuf>& args,
                                 std::function<TString (TStringBuf)> dirBuilder) {
    TVector<TString> dirs;
    for (const auto& arg : args) {
        if (!arg) {
            YConfWarn(UserErr) << "Empty argument in [[alt1]]" << statementName << "[[rst]]" << Endl;
            Y_ASSERT(arg);
            continue;
        }
        const TString dirName = dirBuilder(arg);
        if (!dirName.empty()) {
            dirs.push_back(dirName);
        }
    }
    return dirs;
}

TVector<TString> TDirParser::GetRecurseDirs(const TStringBuf& statementName,
                                            const TVector<TStringBuf>& args) {
    TVector<TString> dirs;
    if (statementName == NMacro::RECURSE || statementName == NMacro::RECURSE_FOR_TESTS) {
        dirs = GetDirsFromArgs(
            statementName, args,
            [this](TStringBuf arg) { return NPath::GenPath(Dir, arg); });
    } else if (statementName == NMacro::RECURSE_ROOT_RELATIVE) {
        dirs = GetDirsFromArgs(
            statementName, args,
            [](TStringBuf arg) { return NPath::ConstructYDir(arg, TStringBuf(), ConstrYDirDiag); });
    }
    else {
        YConfErrPrecise(Syntax, GetStatementRow(statementName), GetStatementColumn(statementName))
            << "unexpected command [[alt1]]" << statementName << "[[rst]]. Unknown form of RECURSE." << Endl;
    }
    return dirs;
}

void TDirParser::ReportFailOnRecurse(const TVector<TString>& takenRecurseDirs,
                                     const TVector<TString>& ignoredRecurseDirs) {
    NEvent::TFailOnRecurse event;
    *event.MutableTakenDirs() = {takenRecurseDirs.begin(), takenRecurseDirs.end()};
    *event.MutableIgnoredDirs() = {ignoredRecurseDirs.begin(), ignoredRecurseDirs.end()};
    TRACE(P, event);
}

bool TDirParser::DirStatement(const TStringBuf& name, const TVector<TStringBuf>& args) {
    TVector<TString> takenRecurseDirs, ignoredRecurseDirs;
    if (name.StartsWith(NMacro::RECURSE)) {
        takenRecurseDirs = GetRecurseDirs(name, args);
        if (Conf.ShouldFailOnRecurse()) {
            ReportFailOnRecurse(takenRecurseDirs, ignoredRecurseDirs);
            return false;
        }
        for (const auto& dir : takenRecurseDirs) {
            AddSubdir(dir, name);
        }
    } else if (name.StartsWith(TStringBuf("PARTITIONED_RECURSE"))) {
        size_t argIndex = 0;
        TVector<TStringBuf> newArgs(Reserve(args.size()));
        TStringBuf balancingConfigArg;
        while (argIndex < args.size()) {
            if (args[argIndex] == "BALANCING_CONFIG") {
                if (argIndex != args.size() - 1) {
                    argIndex++;
                    balancingConfigArg = args[argIndex];
                }
            } else {
                newArgs.push_back(args[argIndex]);
            }
            argIndex++;
        }
        const TStringBuf macrosName = name.SubStr(TStringBuf("PARTITIONED_").size());
        const TStringBuf partitionIndexAsStr = Vars().EvalValue("RECURSE_PARTITION_INDEX");
        const TStringBuf partitionsCountAsStr = Vars().EvalValue("RECURSE_PARTITIONS_COUNT");
        if (!IsNumber(partitionsCountAsStr) || !IsNumber(partitionIndexAsStr)) {
            DirStatement(macrosName, newArgs);
        } else {
            const size_t partitionIndex = FromString<size_t>(partitionIndexAsStr);
            const size_t partitionsCount = FromString<size_t>(partitionsCountAsStr);
            const TVector<TStringBuf> partitionDirs = PartitionRecurse(
                name, newArgs, partitionIndex, partitionsCount, balancingConfigArg);
            YDebug()
                << "RECURSE partitioning applied for partition " << partitionIndex << " of " << partitionsCount << ". "
                << "Config '"<< balancingConfigArg << "'. "
                << "Selected: " << JoinStrings(partitionDirs.begin(), partitionDirs.end(), ", ")
                << Endl;
            if (Conf.ShouldFailOnRecurse()) {
                const TSet<TString> partitionDirsAux(partitionDirs.begin(), partitionDirs.end());
                auto allRecurseDirs = GetRecurseDirs(macrosName, newArgs);
                std::transform(allRecurseDirs.begin(), allRecurseDirs.end(),
                               allRecurseDirs.begin(), NPath::CutType);
                for (const auto& dir: allRecurseDirs) {
                    if (partitionDirsAux.contains(dir)) {
                        takenRecurseDirs.push_back(dir);
                    } else {
                        ignoredRecurseDirs.push_back(dir);
                    }
                }
                ReportFailOnRecurse(takenRecurseDirs, ignoredRecurseDirs);
                return false;
            }
            DirStatement(macrosName, partitionDirs);
        }
    } else {
        return false;
    }

    return true;
}

bool TDirParser::MiscStatement(const TStringBuf& name, const TVector<TStringBuf>& args) {
    if (name == "DLL_FOR" || name == "PY_PROTOS_FOR" || name == "GO_TEST_FOR" || name == "TS_TEST_FOR") {
        if (ModuleCount++ > 0) {
            YConfErrPrecise(Syntax, GetStatementRow(name), GetStatementColumn(name)) << "unexpected command [[alt1]]" << name << "[[rst]]. Only one module per ya make list is permitted." << Endl;
        }

        if (name == "PY_PROTOS_FOR") {
            CheckNumArgs(name, args, 1, ": dir_for <args for module in dir_for>");
            AddSubdir(ArcPath("contrib/libs/protobuf/python"), NMacro::RECURSE); //XXX: remove it ASAP, use PEERDIR from PACKAGE to PACKAGE instead
        } else {
            CheckMinArgs(name, args, 1, ": dir_for <args for module in dir_for>");
        }

        TVector<TStringBuf> margs;
        if (name == "DLL_FOR") {
            size_t argIndex = 0;
            while (argIndex < args.size()) {
                if (args[argIndex] == "EXPORTS") {
                    if (argIndex != args.size() - 1) {
                        argIndex++;
                        Vars().SetStoreOriginals("EXPORTS_FILE", TString(args[argIndex]), OrigVars());
                        Conf.Conditions.RecalcVars("$EXPORTS_FILE", Vars(), OrigVars());
                    } else {
                        YWarn() << "Exported file not specified after EXPORTS keyword" << Endl;
                    }
                } else {
                    margs.push_back(args[argIndex]);
                }
                argIndex++;
            }
        } else {
            margs = args;
        }

        TString dir_for = NPath::ConstructYDir(args[0], TStringBuf(), ConstrYDirDiag);
        CheckEx(!dir_for.empty() && dir_for != Dir, name << ": the current directory (" << dir_for << ") can not be used as an argument");
        Vars().SetStoreOriginals(name, "yes", OrigVars());
        Vars().SetStoreOriginals(TString::Join(name, "_DIR"), dir_for, OrigVars());
        Vars().SetStoreOriginals(TString::Join(name, "_ARGS"), EvalExpr(Vars(), JoinStrings(margs.begin() + 1, margs.end(), " ")), OrigVars());
        bool saveReadModuleContentOnly = ReadModuleContentOnly;
        bool saveReadModuleContentOnlyDone = ReadModuleContentOnlyDone;

        if (name == "GO_TEST_FOR") {
            ReadModuleContentOnly = true;
            Vars().SetStoreOriginals(TStringBuf("GO_TEST_IMPORT_PATH"), ToString(args[0]), OrigVars());
            Vars().SetStoreOriginals(TStringBuf("_GO_IMPORT_PATH"), ToString(args[0]), OrigVars());
            Vars().SetAppendStoreOriginals(NMacro::SRCDIR, ToString(args[0]), OrigVars());
        }

        if (name == "TS_TEST_FOR") {
            ReadModuleContentOnly = true;
            Vars().SetStoreOriginals(TStringBuf("TS_TEST_FOR_PATH"), ToString(args[0]), OrigVars());
            Vars().SetAppendStoreOriginals(NMacro::SRCDIR, ToString(args[0]), OrigVars());
        }

        TString makelib = NPath::GenSourcePath(dir_for, "ya.make");

        YDIAG(DG) << "MakeLib dep: " << makelib << Endl;
        auto includeCtr = OnInclude(makelib, Makefile);
        Y_ASSERT(!includeCtr.Ignored());
        ReadMakeFile(makelib);
        if (ReadModuleContentOnly) {
            if (Y_UNLIKELY(!Module)) {
                ReportNoModule(makelib);
            } else if (Y_UNLIKELY(!ReadModuleContentOnlyDone)) {
                ReportModuleNoEnd(Module->GetModuleConf().Name, makelib);
            }
            ReadModuleContentOnly = saveReadModuleContentOnly;
            ReadModuleContentOnlyDone = saveReadModuleContentOnlyDone;
        }
        return true;
    }

    return false;
}

void TDirParser::DataStatement(const TStringBuf& name, const TVector<TStringBuf>& args) {
    if (name != "DATA") {
        return;
    }

    enum class TAutoUp {No, Maybe, Seen} autoUp = TAutoUp::No;
    auto reportMissingAutoUpConfig = [&](TStringBuf arg) {
            if (autoUp == TAutoUp::Seen) {
                YConfErrPrecise(Misconfiguration, GetStatementRow(name), GetStatementColumn(name)) << "[[alt1]]" << AUTOUPDATED << "[[rst]] in [[alt1]]" << name << "[[rst]] section should be followed by configuration name, found: " << arg << Endl;
            }
        };

    TVector<TStringBuf> dataFilesPathes;
    for (const auto& data : args) {
        if (data.StartsWith(DATA_SBR_PREFIX)) {
            reportMissingAutoUpConfig(data);
            autoUp = TAutoUp::Maybe;
            continue;
        }
        if (data.StartsWith(DATA_EXT_PREFIX)) {
            TString path = TString(data.SubStr(DATA_EXT_PREFIX.length()));
            auto dataPath = NPath::GenPath(Dir, path);
            auto fullPath = Conf.RealPathEx(dataPath);
            auto externalPath = fullPath + ".external";

            if (!TFsPath(fullPath).Exists() && !TFsPath(externalPath).Exists()) {
                TRACE(P, NEvent::TInvalidFile(TString{path + ".external"}, {Dir}, TString{"File not found"}));
                YConfErrPrecise(Misconfiguration, GetStatementRow(name), GetStatementColumn(name)) << "Neither actual data nor .external file exists for [[imp]]" << path << "[[rst]] inside [[alt1]]" << name << "[[rst]] section." << Endl;
            }

            reportMissingAutoUpConfig(data);
            autoUp = TAutoUp::Maybe;
            continue;
        }

        if (data.StartsWith(DATA_ARC_PREFIX)) {
            dataFilesPathes.push_back(data.SubStr(DATA_ARC_PREFIX.length()));
            reportMissingAutoUpConfig(data);
            autoUp = TAutoUp::No;
        } else if (data == AUTOUPDATED) {
            reportMissingAutoUpConfig(data);
            if (autoUp == TAutoUp::Maybe) {
                autoUp = TAutoUp::Seen;
            } else {
                YConfErrPrecise(Misconfiguration, GetStatementRow(name), GetStatementColumn(name))
                     << "[[alt1]]" << AUTOUPDATED << "[[rst]] in [[alt1]]" << name << "[[rst]] section should have path with [[imp]]" << DATA_SBR_PREFIX << "[[rst]] before it, ignored" << Endl;
            }
        } else if (autoUp == TAutoUp::Seen) {
            autoUp = TAutoUp::No;
        } else {
            YConfErrPrecise(Misconfiguration, GetStatementRow(name), GetStatementColumn(name))
                << "Path [[imp]]" << data << "[[rst]] in [[alt1]]" << name << "[[rst]] section should start with one of the following prefixes: [[imp]]"
                << DATA_ARC_PREFIX << ", " << DATA_SBR_PREFIX << ", " << DATA_EXT_PREFIX << "[[rst]]" << Endl;
            autoUp = TAutoUp::No;
        }
    }
    reportMissingAutoUpConfig("<none>");
    Y_ASSERT(Module);
    Module->AddStatement(NMacro::_DATA_FILES, dataFilesPathes);
}

void TDirParser::AddSubdir(const TStringBuf& dir, const TStringBuf& name) {
    YDIAG(DG) << "Sub-directory dep: " << dir << "( " << name << ")" << Endl;

    auto storedDir = Names.FileConf.GetStoredName(dir);
    if (!Names.FileConf.CheckDirectory(storedDir)) {
        TRACE(P, NEvent::TInvalidRecurse(TString{dir}));
        YConfErrPrecise(BadDir, GetStatementRow(name), GetStatementColumn(name)) << "[[imp]]" << name << "[[rst]] to non-directory [[imp]]" << dir << "[[rst]]" << Endl;
        return;
    }

    auto dirElemId = storedDir.GetElemId();
    if (name == "RECURSE_FOR_TESTS") {
        TestRecurses.Push(MakeDepFileCacheId(dirElemId));
    } else {
        Recurses.Push(MakeDepFileCacheId(dirElemId));
    }
}

TVector<TStringBuf> TDirParser::PartitionRecurse(TStringBuf name,
                                                 const TVector<TStringBuf>& args,
                                                 size_t index,
                                                 size_t count,
                                                 TStringBuf balancingConfig) {
    if (!balancingConfig.empty()) {
        TString balancingConfigArcPath = ArcPath(balancingConfig);
        auto balancingConfContent = Names.FileConf.GetFileByName(balancingConfigArcPath);
        auto includeCtr = OnInclude(balancingConfigArcPath, Makefile);
        Y_ASSERT(!includeCtr.Ignored());
        try {
            NJson::TJsonValue balancingConfig = ParseBalancingConf(*balancingConfContent);
            return PartitionWithBalancingConf(args, balancingConfig, index, count);
        } catch (const yexception& error) {
            YConfErrPrecise(Misconfiguration, GetStatementRow(name), GetStatementColumn(name))
                << "Unable to load balancing config [[imp]]" << balancingConfigArcPath
                << "[[rst]] inside [[alt1]]" << name << "[[rst]]: " << error.what() << Endl;
        }
    }
    return Partition(args, index, count);
}

TIncludeController TDirParser::OnInclude(TStringBuf incFile, TStringBuf fromFile) {
    // Add all transitive closure of includes into root ya.make
    auto [it, added] = UniqIncludes.insert({TString(incFile), {!!Module, NextSubModule, Reparse}});
    if (added) {
        Includes.emplace_back(it->first);
        if (Names.FileConf.YPathExists(incFile, EPathKind::File).Empty()) {
            YConfErr(BadFile) << "file [[alt1]]" << incFile << "[[rst]] specified in INCLUDE command (in "
                              << fromFile << ") doesn't exist." << Endl;
            // return normally to allow parser additionally handle failure for selective checkout
        }
    } else {
        if (!it->second.Register(!!Module, NextSubModule, Reparse)) {
            return TIncludeController();
        }
    }
    return TIncludeController(IncludeStack, it->first);
}

void TDirParser::CheckModuleSemantics() {
    if (!Module->GetBuildConf().RenderSemantics) {
        return;
    }
    bool noSem = !Module->GetModuleConf().HasSemantics;
    auto& module = Module->GetModule();
    if (!Conf.ForeignOnNoSem) {
        if (noSem && !module.IsSemIgnore()) {
            Module->SetLateConfErrNoSem();
        }
    } else {
        if (!noSem) {
            const auto& exportLang = YMake.GetExportLang();
            const auto& moduleLang = module.GetLang();
            noSem = !exportLang.empty() && !moduleLang.empty() && exportLang != moduleLang;
        }
        if (noSem) {
            if (module.IsFinalTarget()) {
                module.SetSemForeign();
            }
            module.SetSemIgnore();
        }
    }
}

void TDirParser::CheckModuleEnd() {
    if (Y_UNLIKELY(Module)) {
        ReportModuleNoEnd(Module->GetModuleConf().Name, Makefile);
        YConfInfo(Syntax) << "Automatically add END macro" << Endl;
        while (Module) {
            UserStatement("END", TVector<TStringBuf>());
        }
    }
}

void TDirParser::ReadMakeFile(const TString& makefile) {
    auto moduleCount = ModuleCount;
    ModuleCount = 0;
    ::ReadMakeFile(makefile, Conf, Provider, this, YMake.Yndex);
    ModuleCount = moduleCount;
}


void TDirParser::ApplyDiscard() {
    if (bool failOnRecurse = Conf.ShouldFailOnRecurse(); Discarded || failOnRecurse) {
        // Don't perform deep cleanup of modules here:
        // - If Discard condition was held at module's END processing
        //   then module will not be committed and added to the lists.
        // - Otherwise it will be committed deep into internal structures and there is no
        //   uncommit feature scraping all references to the module. Let it stay there:
        //   the module will be unavailable for build provindig execly what we need.
        ModulesInDir.clear();
        Modules.clear();
        ModuleCount = 0;
        MultiModuleName = {};
        if (!failOnRecurse) {
            Recurses.clear();
            TestRecurses.clear();
        }
    }
}

void TDirParser::ReadMakeFile() {
    ReadMakeFile(Makefile);
    CheckModuleEnd();
    ApplyDiscard();
}

ui64 TDirParser::GetMakefileId() const {
    return Names.FileConf.GetId(Makefile);
}


void TDirParser::SetResourceXXXFromJson(const TStringBuf name, const TVector<TStringBuf>& args) {
    CheckNumArgs(name, args, 2);
    const TStringBuf varName = args[0];
    const TStringBuf fileName = args[1];
    try {
        if (name == "SET_RESOURCE_URI_FROM_JSON") {
            TVector<TString> value = GetResourceUriValue(fileName);
            Vars().SetStoreOriginals(varName, JoinSeq(' ', value), OrigVars());
        } else if (name == "SET_RESOURCE_MAP_FROM_JSON") {
            TVector<TString> value = GetResourceMapValue(fileName);
            Vars().SetStoreOriginals(varName, JoinSeq(' ', value), OrigVars());
        } else {
            Y_ABORT("Internal error: unexpected macro %s", TString(name).c_str());
        }
    } catch (const yexception& e) {
        ReportPlatformResourceError(name, e);
    }
}

TVector<TString> TDirParser::GetResourceUriValue(const TStringBuf fileName) {
    NYa::TPlatformMap platformMapping = LoadPlatformMapping(fileName);
    TString targetPlatform = TString(Vars().EvalValue("CANONIZED_TARGET_PLATFORM"));
    if (const NYa::TResourceDesc* desc = platformMapping.FindPtr(targetPlatform)) {
        TVector<TString> result{desc->Uri};
        if (desc->StripPrefix) {
            result.push_back("STRIP_PREFIX");
            result.push_back(ToString(desc->StripPrefix));
        }
        return result;
    }
    return {};
}

TVector<TString> TDirParser::GetResourceMapValue(const TStringBuf fileName) {
    NYa::TPlatformMap platformMapping = LoadPlatformMapping(fileName);
    TVector<TString> result;
    for (const auto& [platform, desc] : platformMapping) {
        result.push_back(desc.Uri);
        result.push_back("FOR");
        result.push_back(to_upper(platform));
        if (desc.StripPrefix) {
            result.push_back("STRIP_PREFIX");
            result.push_back(ToString(desc.StripPrefix));
        }
    }
    return result;
}

static TConfigurationError PlatformMappingError(const TStringBuf fileName) {
    return TConfigurationError() << "Unable to load jsonFile [[imp]]" << fileName << "[[rst]]: ";
}

NYa::TPlatformMap TDirParser::LoadPlatformMapping(const TStringBuf fileName) {
    TString arcRelPath = ResolveIncludePath(fileName, Makefile);
    auto fileContent = Names.FileConf.GetFileByName(arcRelPath);
    const auto& data = fileContent->GetFileData();
    if (data.NotFound || data.IsDir) {
        throw PlatformMappingError(fileContent->GetAbsoluteName()) << "Doesn't exist or is not a file";
    }

    NYa::TPlatformMap result;
    try {
        result = std::move(NYa::MappingFromJsonString(fileContent->GetContent()));
    } catch (const NYa::TPlatformMappingError& e) {
        throw PlatformMappingError(fileContent->GetAbsoluteName()) << e.what();
    }
    if (data.CantRead) {
        throw PlatformMappingError(fileContent->GetAbsoluteName()) << "Cannot read file";
    }
    auto includeCtr = OnInclude(arcRelPath, Makefile);
    Y_ASSERT(!includeCtr.Ignored());
    return result;
}

void TDirParser::ReportPlatformResourceError(const TStringBuf name, const yexception& error, const TStringBuf varName) {
    TString forVar = varName ? Join("", "for [[alt2]]", varName, "[[rst]] ") : "";

    YConfErrPrecise(Misconfiguration, GetStatementRow(name), GetStatementColumn(name)) << "Command [[alt1]]" << name << "[[rst]] "
        << forVar << "failed with error: " << error.what() << Endl;
}

bool TDirParser::DeclareExternalsByJson(const TStringBuf& name, const TVector<TStringBuf>& args) {
    if (name == "DECLARE_EXTERNAL_RESOURCE_BY_JSON" || name == "DECLARE_EXTERNAL_HOST_RESOURCES_BUNDLE_BY_JSON") {
        Y_ASSERT(Module);
        CheckNumArgs(name, args, 2, 3);
        const TStringBuf varName = args[0];
        const TStringBuf fileName = args[1];
        const TStringBuf friendlyName = args.size() > 2 ? args[2] : varName;
        try {
            if (name == "DECLARE_EXTERNAL_RESOURCE_BY_JSON") {
                TVector<TString> value = GetResourceUriValue(fileName);
                if (!value) {
                    throw TConfigurationError() << "Unsupported platform";
                }
                TVector<TStringBuf> declArgs{};
                declArgs.reserve(value.size() + 1);
                declArgs.push_back(varName);
                declArgs.insert(declArgs.end(), value.begin(), value.end());
                Module->AddStatement("DECLARE_EXTERNAL_RESOURCE", declArgs);
            } else if (name == "DECLARE_EXTERNAL_HOST_RESOURCES_BUNDLE_BY_JSON") {
                TVector<TString> value = GetResourceMapValue(fileName);
                TVector<TStringBuf> declArgs{};
                declArgs.reserve(value.size() + 1);
                declArgs.push_back(varName);
                declArgs.insert(declArgs.end(), value.begin(), value.end());
                Module->AddStatement("DECLARE_EXTERNAL_HOST_RESOURCES_BUNDLE", declArgs);
            } else {
                Y_ABORT("Internal error: unexpected macro %s", TString(name).c_str());
            }
        } catch (const yexception& e) {
            ReportPlatformResourceError(name, e, friendlyName);
        }
    } else {
        return false;
    }
    return true;
}

void CheckNoArgs(const TStringBuf& name, const TVector<TStringBuf>& args) {
    CheckEx(!args.size(), "macro " << name << " can not have any arguments");
}

void CheckNumArgs(const TStringBuf& name, const TVector<TStringBuf>& args, size_t num, const char* descr) {
    CheckEx(args.size() == num, "macro " << name << ": " << args.size() << " args, must be " << num << descr);
}

void CheckMinArgs(const TStringBuf& name, const TVector<TStringBuf>& args, size_t least, const char* descr) {
    CheckEx(args.size() >= least, "macro " << name << ": " << args.size() << " args, must be at least " << least << descr);
}

void CheckNumArgs(const TStringBuf& name, const TVector<TStringBuf>& args, size_t least, size_t most) {
    if (!least) {
        CheckEx(args.size() <= most, "macro " << name << ": " << args.size() << " args, must be no more than " << most);
    } else {
        CheckEx(args.size() >= least && args.size() <= most,
                "macro " << name << ": " << args.size() << " args, must be " << least << ".." << most);
    }
}

NJson::TJsonValue ParseBalancingConf(TFileContentHolder& balancingConfContent) {
    auto content = balancingConfContent.GetContent();

    const auto& data = balancingConfContent.GetFileData();

    if (data.NotFound) {
        throw TConfigurationError() << " Path does not exist: " << balancingConfContent.GetAbsoluteName();
    }
    if (data.IsDir) {
        throw TConfigurationError() << " Path is not file: " << balancingConfContent.GetAbsoluteName();
    }
    Y_ASSERT(balancingConfContent.WasRead());
    if (data.CantRead) {
        throw TConfigurationError() << " Cannot read file: " << balancingConfContent.GetAbsoluteName();
    }

    TMemoryInput balancingConfigFile(content.data(), content.size());
    NJson::TJsonValue balancingConfig;
    if(!NJson::ReadJsonTree(&balancingConfigFile, &balancingConfig, false)) {
        throw TConfigurationError() << " Unable to read json tree from " << balancingConfContent.GetAbsoluteName();
    }
    return balancingConfig;
}

TVector<TStringBuf> Partition(const TVector<TStringBuf>& args, size_t index, size_t count) {
    count = Max<size_t>(1, count);
    const size_t chunkLength = Max<size_t>(1, (args.size() + count - 1) / count);
    const size_t startIndex = index * chunkLength;
    if (startIndex >= args.size()) {
        return {};
    }

    const size_t endIndex = Min(args.size(), startIndex + chunkLength);
    return TVector<TStringBuf>(args.begin() + startIndex, args.begin() + endIndex);
}

TVector<TStringBuf> PartitionWithBalancingConf(const TVector<TStringBuf>& newArgs, const NJson::TJsonValue& balancingConfig, size_t index, size_t count) {
    const NJson::TJsonValue* partitionConfig;
    if (!balancingConfig["partitions"].GetValuePointer(ToString(count), &partitionConfig)) {
        throw TConfigurationError() << " partitions section of balancing config does not contain appropriate balancing for " << count << " partitions." << Endl;
    }

    THashSet<TStringBuf> dirsOutsideOfConfig(newArgs.begin(), newArgs.end());
    TVector<TStringBuf> partitionDirs;
    for (const auto& partitionKv : partitionConfig->GetMapSafe()) {
        const size_t curIndex = FromString<size_t>(partitionKv.first);
        for (const auto& dir : partitionKv.second.GetArraySafe()) {
            const TString& dirName = dir.GetStringSafe();
            auto dirsOutIt = dirsOutsideOfConfig.find(dirName);
            if (curIndex == index && dirsOutIt) {
                partitionDirs.push_back(*dirsOutIt);
            }
            dirsOutsideOfConfig.erase(dirsOutIt);
        }
    }

    if (!dirsOutsideOfConfig.empty()) {
        TVector<TStringBuf> unmappedAll;
        for (const auto& arg : newArgs) {
            if (dirsOutsideOfConfig.contains(arg)) {
                unmappedAll.push_back(arg);
            }
        }
        for (const auto& unmappedDir : Partition(unmappedAll, index, count)) {
            partitionDirs.push_back(unmappedDir);
        }
    }
    return partitionDirs;
}
