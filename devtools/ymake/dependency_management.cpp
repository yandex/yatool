#include "dependency_management.h"
#include "module_restorer.h"
#include "module_state.h"
#include "module_store.h"
#include "prop_names.h"
#include "ymake.h"

#include <devtools/ymake/ymake.h>
#include <devtools/ymake/compact_graph/iter_direct_peerdir.h>
#include <devtools/ymake/compact_graph/peer_collector.h>
#include <devtools/ymake/diag/trace.h>
#include <library/cpp/json/fast_sax/parser.h>
#include <library/cpp/json/writer/json.h>
#include <util/string/split.h>
#include <fmt/format.h>

using namespace fmt::literals;

namespace {
    // Module vars sent to jbuild and ya test via dart file
    constexpr TStringBuf MANAGED_PEERS = "MANAGED_PEERS";
    constexpr TStringBuf MANAGED_PEERS_CLOSURE = "MANAGED_PEERS_CLOSURE";
    constexpr TStringBuf NON_NAMAGEABLE_PEERS = "NON_NAMAGEABLE_PEERS";
    constexpr TStringBuf DART_CLASSPATH = "DART_CLASSPATH";
    constexpr TStringBuf DART_CLASSPATH_DEPS = "DART_CLASSPATH_DEPS";

    // Global configuration vars
    constexpr TStringBuf MANAGEABLE_PEERS_ROOTS = "MANAGEABLE_PEERS_ROOTS";

    // Vars with input data
    constexpr TStringBuf ARGS_DELIM = "ARGS_DELIM";
    constexpr TStringBuf _FORCED_DEPENDENCY_MANAGEMENT_VALUE = "_FORCED_DEPENDENCY_MANAGEMENT_VALUE";
    constexpr TStringBuf _FORCED_DEPENDENCY_MANAGEMENT_EXCEPTIONS = "_FORCED_DEPENDENCY_MANAGEMENT_EXCEPTIONS";
    constexpr TStringBuf DEPENDENCY_MANAGEMENT_VALUE = "DEPENDENCY_MANAGEMENT_VALUE";
    constexpr TStringBuf EXCLUDE_VALUE = "EXCLUDE_VALUE";
    constexpr TStringBuf JAVA_DEPENDENCIES_CONFIGURATION_VALUE = "JAVA_DEPENDENCIES_CONFIGURATION_VALUE";
    constexpr TStringBuf IGNORE_JAVA_DEPENDENCIES_CONFIGURATION = "IGNORE_JAVA_DEPENDENCIES_CONFIGURATION";
    constexpr TStringBuf FORBID_DIRECT_PEERDIRS = "FORBID_DIRECT_PEERDIRS";
    constexpr TStringBuf FORBID_DEFAULT_VERSIONS = "FORBID_DEFAULT_VERSIONS";
    constexpr TStringBuf FORBID_CONFLICT = "FORBID_CONFLICT";
    constexpr TStringBuf FORBID_CONFLICT_DM = "FORBID_CONFLICT_DM";
    constexpr TStringBuf FORBID_CONFLICT_DM_RECENT = "FORBID_CONFLICT_DM_RECENT";
    constexpr TStringBuf REQUIRE_DM = "REQUIRE_DM";

    bool IsGhost(const TModule& parent, const TModule& peer) noexcept {
        return parent.GhostPeers.contains(peer.GetDirId());
    }

    template<typename TFunc>
    bool IterateJsonStrArray(TStringBuf array, TFunc&& handler) {
        struct TJsonCallbacks: NJson::TJsonCallbacks {
            bool OnNull() override {return false;}
            bool OnBoolean(bool) override {return false;}
            bool OnInteger(long long) override {return false;}
            bool OnUInteger(unsigned long long) override {return false;}
            bool OnDouble(double) override {return false;}
            bool OnOpenMap() override {return false;}
            bool OnMapKey(const TStringBuf&) override {return false;}
            bool OnMapKeyNoCopy(const TStringBuf&) override {return false;}
            bool OnCloseMap() override {return false;}

            bool OnString(const TStringBuf& val) override {
                Handler(val);
                return true;
            }

            bool OnOpenArray() override {
                return !std::exchange(InsideArray, true);
            }

            bool OnCloseArray() override {
                InsideArray = false;
                return true;
            }

            TJsonCallbacks(std::remove_reference_t<TFunc>& handlerArg): Handler{handlerArg} {}

            std::remove_reference_t<TFunc>& Handler;
            bool InsideArray = false;
        } callbacks{handler};

        return NJson::ReadJsonFast(array, &callbacks);
    }

    bool VersionLess(TStringBuf l, TStringBuf r) {
        auto litems = StringSplitter(l).Split('.');
        auto ritems = StringSplitter(r).Split('.');

        auto lit = litems.begin();
        auto rit = ritems.begin();
        for (; lit != litems.end() && rit != ritems.end(); ++lit, ++rit) {
            int lval, rval;
            if (TryFromString(*lit, lval) && TryFromString(*rit, rval)) {
                if (lval != rval) {
                    return lval < rval;
                }
            } else if (TStringBuf{*lit} != TStringBuf{*rit}) {
                return TStringBuf{*lit} < TStringBuf{*rit};
            }
        }

        // * if lit == end && rit == end then l and r are equal and false must be returned
        // * if lit != end && rit == end then l can't be less then r and false must be returned
        // this 2 cases are handled by the fact that [rit, end) is empty range and AnyOf(empty, predicate) is always false
        // * if lit == end && rit != end then l can't be greater than r but can be equal if all remainig version components
        //   of are equal to 0. Otherwise l < r and true must be returned.
        return AnyOf(rit, ritems.end(), [](TStringBuf part) {
            int partVal;
            return !TryFromString(part, partVal) || partVal != 0;
        });
    }

    template<typename TCollection, typename TPredicate>
    TString JoinStringsIf(const TCollection& collection, TStringBuf delim, TPredicate&& pred) {
        TStringBuilder result;
        for (const auto& item: collection) {
            if (!pred(item)) {
                continue;
            }
            if (!result.empty()) {
                result << delim;
            }
            result << item;
        }
        return result;
    }

    enum class EPathType {
        Artefact,
        Moddir
    };

    struct TDependencyManagementRules {
        using TLib = TStringBuf;
        using TLibWithVer = TStringBuf;
        using TLib2LibWithVer = THashMap<TLib, TLibWithVer>;
        using TModuleExcludes = THashSet<TLibWithVer>;
        using TLib2Excludes = THashMap<TLib, TModuleExcludes>;

        const TLib2LibWithVer& ForcedLib2LibWithVer; // Global forced libDir -> libVerDir
        const TLib2Excludes& ForcedLib2Excludes; // Global forced excludes
        TLib2LibWithVer Lib2Ver; // libDir -> libVerDir
        TVector<TStringBuf> Excludes;
        union {
            ui32 AllFlags = 0;
            struct { // 6 bits of 32 used
                ui32 ForbidDirectPeerdirs: 1;
                ui32 ForbidConflict: 1;
                ui32 ForbidConflictDM: 1;
                ui32 ForbidConflictDMRecent: 1;
                ui32 RequireDM: 1;
            };
        };

        explicit TDependencyManagementRules(const TLib2LibWithVer& forcedLib2Ver, const TLib2Excludes& forcedLib2Excludes)
            : ForcedLib2LibWithVer(forcedLib2Ver)
            , ForcedLib2Excludes(forcedLib2Excludes)
        {}
        TDependencyManagementRules() = delete;

        bool IsExcluded(TFileView path) const noexcept {
            return IsExcluded(path.GetTargetStr());
        }

        bool IsExcluded(TStringBuf path) const noexcept {
            path = NPath::CutAllTypes(path);
            return AnyOf(Excludes.begin(), Excludes.end(), [path](TStringBuf exclude) { return NPath::IsPrefixOf(exclude, path); });
        };

        TStringBuf GetRuleForPeer(TFileView peerDir, bool isDirect = false) const {
            // Firstly check peer for general DM, skip if transitive and excluded
            // If peer (or it parent) exists in Lib2Ver, then forced was skiped by exclude or no forced for peer
            auto peerDirName = peerDir.CutAllTypes();
            const auto peerLibIt = Lib2Ver.find(peerDirName);
            if ((!peerLibIt.IsEnd()) && ((isDirect) || (!IsExcluded(peerLibIt->second)))) {
                return peerLibIt->second;
            }

            // Then check parent of peer for general DM (when in peer used version, for example, PEERDIR(lib/1.0))
            auto parentDirName = NPath::Parent(peerDirName);
            const auto parentLibIt = Lib2Ver.find(parentDirName);
            if (!parentLibIt.IsEnd()) {
                if (isDirect) {
                    // Rule - direct peerdir must win
                    // For direct peer return peer as result, must overwrite DM
                    return peerDirName;
                } else if (!IsExcluded(parentLibIt->second)) { // skip if transitive and excluded
                    return parentLibIt->second; // For transitive peer return version from DM by parent
                }
            }

            // Found peer or it parent in forced always reply by forced version
            const auto peerForcedLibIt = ForcedLib2LibWithVer.find(peerDirName);
            if (!peerForcedLibIt.IsEnd()) {
                return peerForcedLibIt->second;
            }
            const auto parentForcedLibIt = ForcedLib2LibWithVer.find(parentDirName);
            if (!parentForcedLibIt.IsEnd()) {
                return parentForcedLibIt->second;
            }

            // Not found peer anywhere
            return {};
        }

        void AddDependencyManagement(TStringBuf libWithVer, const TModule& module, THashMap<TStringBuf, TString>& dmConfErrors) {
            Y_ASSERT(!NPath::IsTypedPath(libWithVer));

        // --- Check use proxy in DEPENDENCY_MANAGEMENT
            // Search excludes for this library with version
            auto libWithVerExcludeIt = ForcedLib2Excludes.find(libWithVer);
            auto allowWithVerForced = libWithVerExcludeIt.IsEnd(); // no excludes FORCED must be apply
            if (!allowWithVerForced) {
                // If exists library with version, search current project for exclude from forced
                allowWithVerForced = libWithVerExcludeIt->second.find(module.GetDir().CutAllTypes()).IsEnd();
            }

            // If enabled forced dependency for module
            if (allowWithVerForced) {
                auto forcedIt = ForcedLib2LibWithVer.find(libWithVer);
                if (!forcedIt.IsEnd()) { // library with version exists in forced
                    YConfWarn(ProxyInDM) << TStringBuilder()
                        << "Try use proxy " << libWithVer
                        << " in DM, conflict with forced dependency " << forcedIt->second
                        << " in " << module.GetDir().CutAllTypes() << "/ya.make"
                        << ", declaration skiped"
                        << Endl;
                    return;
                }
            }
            auto libWithVerIt = Lib2Ver.find(libWithVer);
            if (!libWithVerIt.IsEnd()) {
                YConfWarn(ProxyInDM) << TStringBuilder()
                    << "Try use proxy " << libWithVer
                    << " in DM, conflict with dependency " << libWithVerIt->second
                    << " in " << module.GetDir().CutAllTypes() << "/ya.make"
                    << ", declaration skiped"
                    << Endl;
                return;
            }
        // ===

            TStringBuf lib = NPath::Parent(libWithVer);

            // Search excludes for this library
            auto libExcludeIt = ForcedLib2Excludes.find(lib);
            auto allowForced = libExcludeIt.IsEnd(); // no excludes FORCED must be apply
            if (!allowForced) {
                // If exists library, search current project for exclude from forced
                allowForced = libExcludeIt->second.find(module.GetDir().CutAllTypes()).IsEnd();
            }

            // If enabled forced dependency for module
            if (allowForced) {
                auto forcedIt = ForcedLib2LibWithVer.find(lib);
                if (!forcedIt.IsEnd()) {
                    auto forcedLibWithVer = forcedIt->second;

                    // If exists DM error for lib, remove it with UserWarn message
                    auto errIt = dmConfErrors.find(forcedLibWithVer);
                    if (!errIt.IsEnd()) {
                        YConfWarn(GlobalDMViolation) << errIt->second;
                        // Previous DM error always remove: on success or replaced by new error
                        dmConfErrors.erase(errIt);
                    }

                    // Configuration error only if user dependency not copy of forced dependency
                    if (forcedLibWithVer != libWithVer) {
                        dmConfErrors.emplace(forcedLibWithVer, TStringBuilder()
                            << "Try overwrite forced dependency " << forcedLibWithVer
                            << " by " << libWithVer
                            << " in " << module.GetDir().CutAllTypes() << "/ya.make"
                            << Endl
                        );
                    } else {
                        dmConfErrors.emplace(forcedLibWithVer, TStringBuilder()
                            << "Overwrite forced dependency " << forcedLibWithVer
                            << " by same value in " << module.GetDir().CutAllTypes() << "/ya.make"
                            << Endl
                        );
                    }
                    return;
                }
            }
            Lib2Ver[lib] = libWithVer;
        }
    };

    class TDependencyManagementConf {
    private:
        using TLib = TDependencyManagementRules::TLib;
        using TLib2LibWithVer = TDependencyManagementRules::TLib2LibWithVer;
        using TModuleExcludes = TDependencyManagementRules::TModuleExcludes;
        using TLib2Excludes = TDependencyManagementRules::TLib2Excludes;

        const TStringBuf ArgsDelim;
        TVector<TStringBuf> Roots;
        TLib2LibWithVer ForcedLib2LibWithVer; // Global forced dependency management libDir -> libVerDir
        TLib2Excludes ForcedLib2Excludes; // Projects for exclude from forced dependency management by forced lib
        TString StrForEvalForced;

    public:
        TDependencyManagementConf(const TVars& globals)
            : ArgsDelim{GetArgsDelim(globals)}
        {
            IterateVarVals(MANAGEABLE_PEERS_ROOTS, globals, [&](TStringBuf root){
                Roots.push_back(root);
            });
            IterateVarVals(_FORCED_DEPENDENCY_MANAGEMENT_VALUE, globals, [&](TStringBuf libWithVer){
                Y_ASSERT(!NPath::IsTypedPath(libWithVer));
                ForcedLib2LibWithVer[NPath::Parent(libWithVer)] = libWithVer;
            }, &StrForEvalForced);
            enum class EReadExclude {
                WaitFor, // wait FOR item
                WaitForLib, // wait library without version after FOR
                WaitExclude // wait exclude for project
            };
            static constexpr TStringBuf FOR = "FOR";
            auto excludeState = EReadExclude::WaitFor;
            TStringBuf forLib;
            int excludesCount = 0;
            IterateVarVals(_FORCED_DEPENDENCY_MANAGEMENT_EXCEPTIONS, globals, [&](TStringBuf item){
                switch (excludeState) {
                    case EReadExclude::WaitFor: {
                        if (item == FOR) {
                            excludeState = EReadExclude::WaitForLib;
                        } else {
                            YConfErr(Misconfiguration) << "Wait FOR in " << _FORCED_DEPENDENCY_MANAGEMENT_EXCEPTIONS << " but get " << item;
                        }
                    }; break;
                    case EReadExclude::WaitForLib: {
                        if (item == FOR) {
                            YConfErr(Misconfiguration) << "Not found library for FOR in " << _FORCED_DEPENDENCY_MANAGEMENT_EXCEPTIONS;
                            excludeState = EReadExclude::WaitFor;
                        } else {
                            Y_ASSERT(!NPath::IsTypedPath(item));
                            forLib = item;
                            excludeState = EReadExclude::WaitExclude;
                            if (ForcedLib2LibWithVer.find(forLib).IsEnd()) {
                                YConfWarn(GlobalDMViolation) << "Project " << forLib << " absent in " << _FORCED_DEPENDENCY_MANAGEMENT_VALUE << ", exceptions for it has no effect";
                            }
                        }
                    }; break;
                    case EReadExclude::WaitExclude: {
                        if (item == FOR) {
                            if (!excludesCount) {
                                YConfErr(Misconfiguration) << "Not found excludes after FOR for " << forLib << " in " << _FORCED_DEPENDENCY_MANAGEMENT_EXCEPTIONS;
                            }
                            excludeState = EReadExclude::WaitForLib;
                            excludesCount = 0;
                        } else {
                            Y_ASSERT(!NPath::IsTypedPath(item));
                            ++excludesCount;
                            ForcedLib2Excludes[forLib].emplace(item);
                        }
                    }; break;
                }
            });
        }

        TDependencyManagementRules GetRules(const TModule& module, THashMap<TStringBuf, TString>& dmConfErrors) const {
            TDependencyManagementRules result(ForcedLib2LibWithVer, ForcedLib2Excludes);
            MergeRules(result, module, dmConfErrors);
            return result;
        }

        void MergeRules(TDependencyManagementRules& result, const TModule& module, THashMap<TStringBuf, TString>& dmConfErrors) const {
            if (!module.GetAttrs().RequireDepManagement) {
                return;
            }
            TScopedContext diagContext{module.GetName()};

            // Read user dependence management
            // Skip and Misconfiguration error on try overwrite forced in start module
            // Skip and UserWarn warning on try overwrite forced in NOT start module
            IterateJBuildMacroArgs(DEPENDENCY_MANAGEMENT_VALUE, module.Vars, [&](TStringBuf item) {
                if (std::none_of(Roots.begin(), Roots.end(), [item](TStringBuf root) { return NPath::IsPrefixOf(root, item); })) {
                    YConfErr(BadDep) << "[[alt1]]" << DEPENDENCY_MANAGEMENT_VALUE.SubString(0, DEPENDENCY_MANAGEMENT_VALUE.size() - 6/*_VALUE*/) << "[[rst]] for [[imp]]"
                                     << item
                                     << "[[rst]] which is outside of paths allowed for manageable dependencies"
                                     << Endl;
                }
                result.AddDependencyManagement(item, module, dmConfErrors);
            });

            IterateJBuildMacroArgs(EXCLUDE_VALUE, module.Vars, [&](TStringBuf item) {
                result.Excludes.push_back(item);
            });

            if (module.Get(IGNORE_JAVA_DEPENDENCIES_CONFIGURATION) != "yes") {
                IterateJBuildMacroArgs(JAVA_DEPENDENCIES_CONFIGURATION_VALUE, module.Vars, [&](TStringBuf item) {
                    if (item == FORBID_DIRECT_PEERDIRS) {
                        result.ForbidDirectPeerdirs = true;
                    } else if (item == FORBID_DEFAULT_VERSIONS) {
                        // This flag is unconditionaly enabled globaly
                        // and is going to be removed
                    } else if (item == FORBID_CONFLICT) {
                        result.ForbidConflict = true;
                    } else if (item == FORBID_CONFLICT_DM) {
                        result.ForbidConflictDM = true;
                    } else if (item == FORBID_CONFLICT_DM_RECENT) {
                        result.ForbidConflictDMRecent = true;
                    } else if (item == REQUIRE_DM) {
                        result.RequireDM = true;
                    } else {
                        const auto allFlagNames = {
                            FORBID_DIRECT_PEERDIRS,
                            FORBID_DEFAULT_VERSIONS, // TODO: remove
                            FORBID_CONFLICT,
                            FORBID_CONFLICT_DM,
                            FORBID_CONFLICT_DM_RECENT,
                            REQUIRE_DM
                        };
                        YConfErr(UserErr)
                            << "Unknown JAVA_DEPENDENCIES_CONFIGURATION value '" << item
                            << "'. Allowed only [" << JoinStrings(allFlagNames.begin(), allFlagNames.end(), "' ") << "]" << Endl;
                    }
                });
            }
        }

        bool IsContribWithVer(const TModule& module) const {
            const auto modDir = module.GetDir().CutAllTypes();
            return AnyOf(Roots.begin(), Roots.end(), [modDir](TStringBuf root) { return NPath::IsPrefixOf(root, modDir); });
        }

        const TLib2LibWithVer& GetForcedLib2Ver() const {
            return ForcedLib2LibWithVer;
        }

        const TLib2Excludes& GetForcedLib2Excludes() const {
            return ForcedLib2Excludes;
        }

    private:
        template <typename TFunc>
        void IterateJBuildMacroArgs(TStringBuf macroValVar, const TVars& vars, TFunc&& func) const {
            const TYVar* value = vars.Lookup(macroValVar);
            if (!value) {
                return;
            }

            for (const TVarStr& varStr : *value) {
                for (TStringBuf item : StringSplitter(GetPropertyValue(varStr.Name)).Split(' ')) {
                    if (item.empty() || (item[0] == '$' && item.Skip(1) == macroValVar) || item == ArgsDelim) {
                        continue;
                    }
                    func(item);
                }
            }
        }

        template <typename TFunc>
        void IterateVarVals(const TStringBuf varName, const TVars& globalVars, TFunc&& func, TString* strForEval = nullptr) const {
            const TYVar* vars = globalVars.Lookup(varName);
            if (!vars) {
                return;
            }

            for (const TVarStr& var : *vars) {
                auto vals = GetPropertyValue(var.Name);
                if (strForEval) {
                    vals = *strForEval = EvalExpr(globalVars, vals);
                }
                for (TStringBuf val : StringSplitter(vals).Split(' ')) {
                    if (!val.empty()) {
                        func(val);
                    }
                }
            }
        }

        static TStringBuf GetArgsDelim(const TVars& globals) {
            const auto var = globals.Get1(ARGS_DELIM);
            if (var.empty()) {
                return TStringBuf{};
            }
            return GetPropertyValue(var);
        }
    };

    enum class EPeerResolution {Unversioned, Default, Direct, Managed, Transitive};
    struct TResolvedPeer
    {
        TNodeId Id;
        EPeerResolution Resolution;
    };
    struct TResolutionInfo {
        unsigned MinDepth;
        TResolvedPeer Resolution;
    };

    class TConflictResolver {
    private:
        struct TResolutionRecord {
            TResolvedPeer Choice;
            TStringBuf Version;
            TVector<TStringBuf> ConflictVersions;
        };

    public:
        TConflictResolver(const TDependencyManagementRules& rules, const THashMap<TStringBuf, TNodeId>& libs, TFileConf& fileConf, bool forceSkipDepCfgChecks = false) noexcept
            : Rules{rules}
            , LibIds{libs}
            , ForceSkipDepCfgCheck{forceSkipDepCfgChecks}
            , FileConf(fileConf)
        {}

        TNodeId Resolve(const TResolvedPeer cur, const TModule& peer, unsigned depth) {
            if (cur.Resolution == EPeerResolution::Unversioned) {
                return AcceptResolution(cur.Id, cur, depth);
            }

            TFileView peerDir = peer.GetDir();
            const auto libName = FileConf.Parent(peerDir);
            const auto libVer = peerDir.Basename();

            const auto [resolution, isNew] = Resolutions.emplace(libName, TResolutionRecord{cur, libVer, {}});
            if (!isNew) {
                if ((Rules.ForbidConflict && resolution->second.Choice.Resolution != EPeerResolution::Managed)
                    || ((Rules.ForbidConflictDM || Rules.ForbidConflictDMRecent) && resolution->second.Choice.Resolution == EPeerResolution::Managed)) {
                    if (resolution->second.ConflictVersions.empty()) {
                        resolution->second.ConflictVersions.push_back(resolution->second.Version);
                    }
                    resolution->second.ConflictVersions.push_back(libVer);
                }
                if (resolution->second.Choice.Resolution == EPeerResolution::Managed) {
                    // All managed version must have replacements because the first one in prefix order might
                    // differ from the first one in bredth traversal.
                    AccumulatedPeers.emplace(cur.Id, TResolutionInfo{depth, resolution->second.Choice});
                }
                return TNodeId::Invalid;
            }

            const auto [resolve, dmVer] = FindExplicitResolution(peerDir, depth == 1);
            if (resolve == TNodeId::Invalid) {
                resolution->second.Choice.Resolution = (cur.Resolution == EPeerResolution::Managed ? EPeerResolution::Transitive : cur.Resolution);
                return AcceptResolution(cur.Id, resolution->second.Choice, depth);
            }

            if ((Rules.ForbidConflictDM || Rules.ForbidConflictDMRecent) && resolution->second.Choice.Id != resolve) {
                resolution->second.ConflictVersions.push_back(libVer);
            }
            resolution->second.Choice = {resolve, EPeerResolution::Managed};
            resolution->second.Version = dmVer;
            return AcceptResolution(cur.Id, resolution->second.Choice, depth);
        }

        THashMap<TNodeId, TResolutionInfo> Finalize() {
            if (!ForceSkipDepCfgCheck && Rules.ForbidConflict) {
                for (const auto& [lib, resolution]: Resolutions) {
                    if (resolution.ConflictVersions.empty() || resolution.Choice.Resolution == EPeerResolution::Managed) {
                        continue;
                    }
                    YConfErr(Misconfiguration)
                        << "[[alt1]]Auto resolved[[rst]] versions conflict: "
                        << JoinVectorIntoString(resolution.ConflictVersions, ", ")
                        << " ([[imp]]"
                        << lib.CutAllTypes() << NPath::PATH_SEP << resolution.Version
                        << "[[rst]] chosen)"
                        << Endl;
                }
            }

            if (!ForceSkipDepCfgCheck && Rules.ForbidConflictDM) {
                for (const auto& [lib, resolution]: Resolutions) {
                    if (
                        resolution.ConflictVersions.empty()
                        || resolution.Choice.Resolution != EPeerResolution::Managed) {
                        continue;
                    }
                    YConfErr(Misconfiguration)
                        << "[[alt1]]Different[[rst]] libraries versions in PEERDIRs: "
                        << JoinVectorIntoString(resolution.ConflictVersions, ", ")
                        << " ([[imp]]"
                        << lib.CutAllTypes() << NPath::PATH_SEP << resolution.Version
                        << "[[rst]] required by DEPENDENCY_MANAGEMENT)"
                        << Endl;
                }
            }

            if (!ForceSkipDepCfgCheck && Rules.ForbidConflictDMRecent) {
                for (const auto& [lib, resolution]: Resolutions) {
                    if (resolution.Choice.Resolution != EPeerResolution::Managed) {
                        continue;
                    }
                    const auto newerConflicts = JoinStringsIf(
                        resolution.ConflictVersions,
                        ", ",
                        [dmVer = resolution.Version](TStringBuf item) {
                            return VersionLess(dmVer, item);
                        });
                    if (newerConflicts.empty()) {
                        continue;
                    }
                    YConfErr(Misconfiguration)
                        << "[[alt1]]More recent[[rst]] libraries versions in PEERDIRs: "
                        << newerConflicts
                        << " ([[imp]]"
                        << lib.CutAllTypes() << NPath::PATH_SEP << resolution.Version
                        << "[[rst]] required by DEPENDENCY_MANAGEMENT)"
                        << Endl;
                }
            }

            return std::move(AccumulatedPeers);
        }

        const TDependencyManagementRules& GetRules() const noexcept {
            return Rules;
        }

    private:
        std::pair<TNodeId, TStringBuf> FindExplicitResolution(TFileView peerDir, bool isDirect) const {
            const auto rule = Rules.GetRuleForPeer(peerDir, isDirect);
            if (rule.empty()) {
                return {TNodeId::Invalid, TStringBuf{}};
            }
            const auto libIt = LibIds.find(rule);
            if (libIt.IsEnd()) {
                if (NPath::Parent(peerDir.CutAllTypes()) == NPath::Parent(rule)) {
                    // This situation when peerDir = some/VersionX but rule is some/VersionY
                    // And this mean peerDir blocked by forced dependency management
                    YConfErr(Misconfiguration)
                        << "[[alt1]]DEPENDENCY_MANAGEMENT[[rst]] " << (isDirect ? "direct" : "transitive") << " peerdir [[imp]]"
                        << peerDir
                        << "[[rst]] try overwrite forced dependency [[imp]]"
                        << rule
                        << "[[rst]]"
                        << Endl;
                } else {
                    YConfErr(BadDir)
                        << "[[alt1]]DEPENDENCY_MANAGEMENT[[rst]] replaces peerdir to [[imp]]"
                        << peerDir
                        << "[[rst]] with dependency to missing directory or directory without module [[imp]]"
                        << rule
                        << "[[rst]]"
                        << Endl;
                }
                return {TNodeId::Invalid, TStringBuf{}};
            }
            return {libIt->second, NPath::Basename(rule)};
        }

        TNodeId AcceptResolution(TNodeId conflictId, TResolvedPeer resolution, unsigned depth) {
            const auto [pos, _] = AccumulatedPeers.emplace(conflictId, TResolutionInfo{depth, resolution});
            if (resolution.Resolution == EPeerResolution::Managed) {
                // Resolution target must have trivial replacement because the first item one in prefix order might
                // differ from the first one in bredth traversal.
                AccumulatedPeers.emplace(pos->second.Resolution.Id, pos->second);
            }
            return pos->second.Resolution.Id;
        }

    private:
        const TDependencyManagementRules& Rules;
        const THashMap<TStringBuf, TNodeId>& LibIds;
        THashMap<TNodeId, TResolutionInfo> AccumulatedPeers;
        THashMap<TFileView, TResolutionRecord> Resolutions;
        bool ForceSkipDepCfgCheck = false;
        TFileConf& FileConf;
    };

    struct TManagedPeers {
        TVector<TResolvedPeer> Direct;
        NDetail::TPeersClosure Closure;
        TVector<TNodeId> UnmanageablePeers;
        TVector<TNodeId> UnmanageablePeersClosure;
    };

    struct CollectedModuleInfo {
        TVector<TNodeId> Peers;
        TUniqVector<TNodeId> UnmanageablePeers;
        bool UseExcludes = false;
    };

    class TDependencyManagementCollector {
    public:
        using TStateItem = TGraphIteratorStateItem<CollectedModuleInfo, true>;

        TDependencyManagementCollector(const TRestoreContext& restoreContext)
            : RestoreContext{restoreContext}
            , DMConf{restoreContext.Conf.CommandConf} {
        }

        void LogStats() const {
            YDebug()
                << "Modules requiring dependency management: " << ModulesWithDepMng
                << "; BFS traversals performed: " << BFSRuns
                << Endl;
        }

        bool Start(TStateItem& parentItem) {
            if (ManagedPeers.contains(parentItem.Node().Id())) {
                return false;
            }

            TModuleRestorer restorer{RestoreContext, parentItem.Node()};
            TModule* parent = restorer.RestoreModule();
            Y_ASSERT(parent);
            ModulesWithDepMng += parent->GetAttrs().RequireDepManagement;

            // Follow all modules regardles of PropagateDependencyManagement attribute in order to collect tools requiring DM
            return true;
        }

        void Finish(TStateItem& parentItem, void*) {
            TModule* parent = RestoreContext.Modules.Get(parentItem.Node()->ElemId);
            Y_ASSERT(parent);
            if (!parent->GetAttrs().RequireDepManagement) {
                return;
            }

            TScopedContext diagContext{parent->GetName()};

            if (DMConf.IsContribWithVer(*parent)) {
                ContribsDict.emplace(parent->GetDir().CutAllTypes(), parentItem.Node().Id());
            }

            auto rules = DMConf.GetRules(*parent, DMConfErrors);
            auto& record = PrepareManagedPeersRecord(rules, parentItem.Node().Id(), *parent, parentItem.Peers);
            // Copy direct unmanageable peers to record, for use in AddUnmanageablePeersToClosure later
            for (auto unpeer : parentItem.UnmanageablePeers) {
                record.UnmanageablePeers.emplace_back(unpeer);
            }
            if (parentItem.UseExcludes) {
                auto& appliedExcludes = RestoreContext.Modules.GetExtraDependencyManagementInfo(parent->GetId()).AppliedExcludes;
                for (const auto* itemStats: record.Closure.GetStableOrderStats()) {
                    if (itemStats->second.Excluded) {
                        appliedExcludes.push_back(itemStats->first);
                    }
                }
            }
            ManagePeersClosure(rules, record, *parent, parentItem);

            // If finished start module, print all DM configuration errors
            if ((parentItem.IsStart) && (!DMConfErrors.empty())) {

                // Generate Misconfiguration error for DM errors on exists peers
                const auto& modules = RestoreContext.Modules;
                for (auto managedPeerClosureNode: modules.GetModuleNodeLists(parent->GetId()).UniqPeers()) {
                    const auto* subModule = GetModule(managedPeerClosureNode);
                    auto peerDir = subModule->GetDir().CutAllTypes();
                    auto errIt = DMConfErrors.find(peerDir);
                    if (!errIt.IsEnd()) {
                        YConfErr(Misconfiguration) << errIt->second; // generate error
                        DMConfErrors.erase(errIt); // and remove error
                    }
                }

                // Generate warnings by other DM errors
                if (!DMConfErrors.empty()) {
                    for (auto& [libWithVer, error] : DMConfErrors) {
                        YConfWarn(GlobalDMViolation) << error;
                    }
                    DMConfErrors.clear(); // clear errors for next start module
                }
            }

            // DEPRECATED, used only for fill DART_CLASSPATH_DEPS
            // Real add unmanageable peers to peers closure moved to AddUnmanageablePeersToClosure
            record.UnmanageablePeersClosure = HandleUnmanageables(parentItem, *parent);

            if (parent->GetAttrs().ConsumeNonManageablePeers) {
                AddUnmanageablePeersToClosure(parentItem, *parent);
            }

            // Used by jbuild. Jbuild receives direct JNI deps and calculates closure inside
            parent->Set(NON_NAMAGEABLE_PEERS, ToPeerListVar(parentItem.UnmanageablePeers.Data(), EPathType::Moddir));
            parent->SetPeersComplete();
        }

        void Collect(TStateItem& parentItem, TConstDepNodeRef peerNode) {
            const TModule* peer = RestoreContext.Modules.Get(peerNode->ElemId);
            Y_ASSERT(peer);
            if (!peer->GetAttrs().RequireDepManagement) {
                parentItem.UnmanageablePeers.Push(peerNode.Id());
                return;
            }

            parentItem.Peers.push_back(peerNode.Id());
        }

        const TModule* GetModule(TNodeId nodeId) const {
            const auto* res = RestoreContext.Modules.Get(RestoreContext.Graph[nodeId]->ElemId);
            return res;
        }

        ui32 GetAppliedExcludesProp() const {
            return RestoreContext.Graph.Names().CommandConf.GetIdNx(FormatProperty(NProps::USED_RESERVED_VAR, "APPLIED_EXCLUDES"sv));
        }

    private:
        THashMap<TStringBuf, TString> DMConfErrors; // DM configure errors as libWithVer -> Error

        void AddUnmanageablePeersToClosure(TStateItem& parentItem, TModule& parent) const {
            auto& modules = RestoreContext.Modules;
            auto& listStore = modules.GetNodeListStore();
            auto& parentLists = modules.GetModuleNodeIds(parent.GetId());
            auto& parentPeersClosureListId = parentLists.UniqPeers;
            const auto& parentPeersClosure = listStore.GetList(parentPeersClosureListId);

            // All unmanageable peers begin from direct unmanageable peers of module
            auto unmanageablePeers = parentItem.UnmanageablePeers;

            // Search unmanageable peers in all closure peers
            for (auto peer : parentPeersClosure) {
                const auto& peerUnmanageablePeers = ManagedPeers.at(peer).UnmanageablePeers;
                for (auto unpeer : peerUnmanageablePeers) {
                    unmanageablePeers.Push(unpeer);
                }
            }

            // Add all unmanageable peers to peeers closure
            for (auto unpeer : unmanageablePeers) {
                listStore.AddToList(parentPeersClosureListId, unpeer);
            }
        }

        // DEPRECATED
        TVector<TNodeId> HandleUnmanageables(TStateItem& parentItem, TModule& parent) const {
            TUniqVector<TNodeId> unmanageablePeersClosure;
            auto& modules = RestoreContext.Modules;
            for (TNodeId peer : modules.GetModuleNodeLists(parent.GetId()).UniqPeers()) {
                for (TNodeId unpeer : ManagedPeers.at(peer).UnmanageablePeersClosure) {
                    unmanageablePeersClosure.Push(unpeer);
                }
            }
            for (TNodeId unpeer: parentItem.UnmanageablePeers.Data()) {
                unmanageablePeersClosure.Push(unpeer);
            }

            // TODO(svidyuk): Ugly hack: TEST_CLASSPATH_DEPS in test DART must support ${ext:so:MANAGED_PEERS_CLOSURE} expansion instead of hardcode here
            if (parent.Get("MODULE_TYPE") == "JTEST" || parent.Get("MODULE_TYPE") == "JTEST_FOR" || parent.Get("MODULE_TYPE") == "JUNIT5"  || parent.Get("MODULE_TYPE") == "JUNIT6") {
                // Used by tests. Tests require all JNI deps in order to work properly
                TString var;
                for (TNodeId item : unmanageablePeersClosure.Data()) {
                    const auto* peer = GetModule(item);
                    Y_ASSERT(peer);
                    TStringBuf ext = peer->GetName().Extension();
                    if (ext != "so" && ext != "dll" && ext != "dylib") {
                        continue;
                    }

                    if (!var.empty()) {
                        var += ' ';
                    }
                    var += peer->GetName().GetTargetStr();
                }
                parent.Set(DART_CLASSPATH_DEPS, var);
            }
            // End of TODO

            if (!parent.GetAttrs().ConsumeNonManageablePeers) {
                // Propagate info
                return unmanageablePeersClosure.Take();
            }

            // Consume info DELETED
            // This logic moved to AddUnmanageablePeersToClosure
            return {};
        }

        TManagedPeers& PrepareManagedPeersRecord(
            const TDependencyManagementRules& rules,
            TNodeId nodeId,
            TModule& parent,
            const TVector<TNodeId>& peers) {
            const auto [pos, _] = ManagedPeers.emplace(
                nodeId,
                TManagedPeers{ManageLocalPeers(parent, peers, rules), {}, {}, {}});
            pos->second.Closure = MergeClosure(pos->second.Direct, rules);
            return pos->second;
        }

        void ManagePeersClosure(
            const TDependencyManagementRules& rules,
            const TManagedPeers& peersRecord,
            TModule& parent,
            TStateItem& parentItem
        ) {
            parent.Set(MANAGED_PEERS, ToPeerListVar(peersRecord.Direct, EPathType::Moddir));
            const auto managedPeers = PreorderSort(
                peersRecord.Direct,
                ResolveConflicts(TConflictResolver{rules, ContribsDict, RestoreContext.Graph.Names().FileConf}, peersRecord.Direct, peersRecord.Closure));
            auto& listsStore = RestoreContext.Modules.GetNodeListStore();
            auto& parentPeerIds = RestoreContext.Modules.GetModuleNodeIds(parent.GetId());
            for (const auto& peer: managedPeers) {
                listsStore.AddToList(parentPeerIds.UniqPeers, peer);
            }
            for (const auto& peer: peersRecord.Direct) {
                listsStore.AddToList(parentPeerIds.ManagedDirectPeers, peer.Id);
            }
            if (parent.GetAttrs().ConsumeNonManageablePeers) {
                for (auto unpeer : parentItem.UnmanageablePeers) {
                    listsStore.AddToList(parentPeerIds.ManagedDirectPeers, unpeer);
                }
            }
            parent.Set(MANAGED_PEERS_CLOSURE, ToPeerListVar(managedPeers, EPathType::Moddir));
            parent.Set(DART_CLASSPATH, ToPeerListVar(managedPeers, EPathType::Artefact));
        }

        TVector<TResolvedPeer> ManageLocalPeers(
            TModule& parent,
            const TVector<TNodeId>& rawPeers,
            const TDependencyManagementRules& rules) {
            TVector<TResolvedPeer> managedPeers;
            auto& listsStore = RestoreContext.Modules.GetNodeListStore();
            auto& parentPeerIds = RestoreContext.Modules.GetModuleNodeIds(parent.GetId());
            auto& fileConf = RestoreContext.Graph.Names().FileConf;
            const auto unittestDir = parent.Get("UNITTEST_DIR");
            for (const TNodeId peerId : rawPeers) {
                const TModule* peer = GetModule(peerId);
                Y_ASSERT(peer);

                if (IsGhost(parent, *peer)) {
                    continue;
                }

                TFileView peerDir = peer->GetDir();
                // TODO(svidyuk) hack for JTEST_FOR. There are 2 better solutions
                //  * Implement tgt modifier ${tgt:UNITTEST_DIR} in a way suitable to use in dart file rendering
                //  * Remove JTEST_FOR module
                if (!unittestDir.empty() && peerDir.CutType() == unittestDir) {
                    parent.Set("UNITTEST_MOD", peer->GetName().CutType());
                }

                if (auto proxyIt = Proxies.find(peer->GetId()); proxyIt != Proxies.end()) {
                    const auto rule = rules.GetRuleForPeer(peerDir, true);
                    if (rule.empty()) {
                        YConfErr(Misconfiguration) << "Dependency with [[alt1]]default[[rst]] version: [[imp]]" << GetModule(proxyIt->second)->GetDir().CutAllTypes() << "[[rst]]" << Endl;
                        managedPeers.push_back({proxyIt->second, EPeerResolution::Default});
                        // Add default versions to the list of managed peers closure to traverse Module -> Proxy -> DefaultVer
                        // dependency chain in DM aware visitors. Default versions are forbidden but may appear in the selective
                        // checkout case when dependency management is stored in some ya.make.inc file from not yes checked out
                        // dir. TODO(svidyuk) need to investigate how to handle this situation in a better way.
                        listsStore.AddToList(parentPeerIds.UniqPeers, peerId);
                        continue;
                    }

                    if (const auto libIt = ContribsDict.find(rule); libIt == ContribsDict.end()) {
                        YConfErr(BadDir)
                            << "[[alt1]]DEPENDENCY_MANAGEMENT[[rst]] replaces peerdir to [[imp]]"
                            << peerDir
                            << "[[rst]] with dependency to missing directory or directory without module [[imp]]"
                            << rule
                            << "[[rst]]"
                            << Endl;
                    } else {
                        managedPeers.push_back({libIt->second, EPeerResolution::Managed});
                    }
                    continue;
                }

                const bool isContrib = DMConf.IsContribWithVer(*peer);
                if (rules.ForbidDirectPeerdirs && isContrib) {
                    YConfErr(Misconfiguration) << "PEERDIR to [[alt1]]direct[[rst]] version: [[imp]]" << peerDir.CutAllTypes() << "[[rst]]" << Endl;
                }
                managedPeers.push_back({peerId, isContrib ? EPeerResolution::Direct : EPeerResolution::Unversioned});

                if (rawPeers.size() == 1 && !Proxies.contains(peer->GetId()) && fileConf.Parent(peerDir) == parent.GetDir() && DMConf.IsContribWithVer(*peer)) {
                    Proxies.emplace(parent.GetId(), peerId);
                }
            }

            if (rules.RequireDM) {
                for (const auto& [lib, resolution]: managedPeers) {
                    if (resolution != EPeerResolution::Managed && resolution != EPeerResolution::Unversioned) {
                        const TModule* peer = GetModule(lib);
                        Y_ASSERT(peer);
                        YConfErr(Misconfiguration)
                            << "Dependency version resolved [[alt1]]without DEPENDENCY_MANAGEMENT[[rst]]: [[imp]]"
                            << peer->GetDir().CutAllTypes()
                            << "[[rst]]"
                            << Endl;
                    }
                }
            }

            return managedPeers;
        }

        NDetail::TPeersClosure MergeClosure(const TVector<TResolvedPeer>& directPeers, const TDependencyManagementRules& rules) const {
            NDetail::TPeersClosure closure;
            for (auto [peerId, _] : directPeers) {
                closure.Merge(peerId, ManagedPeers.at(peerId).Closure.Exclude(
                    [&](TNodeId id) { return rules.IsExcluded(RestoreContext.Graph.GetFileName(RestoreContext.Graph[id]->ElemId)); },
                    [&](TNodeId id) -> const NDetail::TPeersClosure& { return ManagedPeers.at(id).Closure; }));
            }

            return closure;
        }

        TString ToPeerListVar(const TVector<TNodeId>& peerList, EPathType pathType) const {
            TString var;
            for (const TNodeId& item : peerList) {
                const auto* peer = GetModule(item);
                Y_ASSERT(peer);

                if (!var.empty()) {
                    var += ' ';
                }
                var += pathType == EPathType::Moddir ? peer->GetDir().GetTargetStr() : peer->GetName().GetTargetStr();
            }
            return var;
        }

        TString ToPeerListVar(const TVector<TResolvedPeer>& peerList, EPathType pathType) const {
            TString var;
            for (const TResolvedPeer& item : peerList) {
                const auto* peer = GetModule(item.Id);
                Y_ASSERT(peer);

                if (!var.empty()) {
                    var += ' ';
                }
                var += pathType == EPathType::Moddir ? peer->GetDir().GetTargetStr() : peer->GetName().GetTargetStr();
            }
            return var;
        }

        THashMap<TNodeId, TResolutionInfo> ResolveConflicts(
            TConflictResolver resolver,
            const TVector<TResolvedPeer>& managedPeers,
            const NDetail::TPeersClosure& closure) const {
            if (managedPeers.empty()) {
                return {};
            }
            ++BFSRuns;

            NDetail::TPeersClosure explicitResolvesClosure;

            TQueue<TResolvedPeer> searchQueue;
            THashSet<TNodeId> visited;
            auto enqueDeps = [&] (const TVector<TResolvedPeer>& peers) mutable {
                for (auto peer: peers) {
                    if (!visited.insert(peer.Id).second || (!closure.Contains(peer.Id) && !explicitResolvesClosure.Contains(peer.Id))) {
                        continue;
                    }
                    searchQueue.push(peer);
                }
            };

            unsigned depth = 0;
            size_t currDepthEnd = 0;
            size_t nodesVisited = 0;
            TResolvedPeer cur = {TNodeId::Invalid, EPeerResolution::Direct};
            for (
                enqueDeps(managedPeers);
                !searchQueue.empty();
                enqueDeps(cur.Id != TNodeId::Invalid ? ManagedPeers.at(cur.Id).Direct : TVector<TResolvedPeer>{}), searchQueue.pop(), ++nodesVisited
            ) {
                cur = searchQueue.front();
                if (nodesVisited == currDepthEnd) {
                    ++depth;
                    currDepthEnd += searchQueue.size();
                }

                const auto resolved = resolver.Resolve(cur, *GetModule(cur.Id), depth);
                if (resolved == cur.Id) {
                    continue; // continue traverse normally without replacements
                }

                cur.Id = resolved; // replace current node in traversal by resolve result
                if (resolved == TNodeId::Invalid) {
                    continue;
                }
                visited.insert(resolved); // mark replacement as visited

                auto resolveClosure = ManagedPeers.at(resolved).Closure.Exclude(
                    [&](TNodeId id) { return resolver.GetRules().IsExcluded(RestoreContext.Graph.GetFileName(RestoreContext.Graph.Get(id))); },
                    [&](TNodeId id) -> const NDetail::TPeersClosure& { return ManagedPeers.at(id).Closure; }
                );
                explicitResolvesClosure.Merge(resolved, resolveClosure);
            }

            return resolver.Finalize();
        }

        TVector<TNodeId> PreorderSort(TArrayRef<const TResolvedPeer> managedDirectPeers, const THashMap<TNodeId, TResolutionInfo>& conflictsResolution) const {
            TUniqVector<TNodeId> res;
            PreorderSort(managedDirectPeers, conflictsResolution, res);
            return res.Take();
        }

        void PreorderSort(
            TArrayRef<const TResolvedPeer> managedDirectPeers,
            const THashMap<TNodeId, TResolutionInfo>& conflictsResolution,
            TUniqVector<TNodeId>& res) const {
            TVector<TArrayRef<const TResolvedPeer>> stack;
            stack.push_back(managedDirectPeers);

            while (!stack.empty()) {
                if (stack.back().empty()) {
                    stack.pop_back();
                    continue;
                }

                const TResolvedPeer cur = stack.back().front();
                stack.back() = stack.back().subspan(1);

                const auto resolutionIt = conflictsResolution.find(cur.Id);
                if (resolutionIt == conflictsResolution.end()) {
                    continue;
                }

                if (!res.Push(resolutionIt->second.Resolution.Id)) {
                    continue;
                }
                if (const auto& curPeers = ManagedPeers.at(res.Data().back()).Direct; !curPeers.empty()) {
                    stack.push_back(curPeers);
                }
            }
        }

    private:
        TRestoreContext RestoreContext;
        TDependencyManagementConf DMConf;

        THashMap<ui32, TNodeId> Proxies;
        THashMap<TNodeId, TManagedPeers> ManagedPeers;
        THashMap<TStringBuf, TNodeId> ContribsDict;

        // Some debug stats
        unsigned ModulesWithDepMng = 0;
        mutable unsigned BFSRuns = 0;
    };

    class TDependencyManagementExplainer {
    public:
        TDependencyManagementExplainer(const TRestoreContext& restoreContext, const TModule& startModule)
            : RestoreContext{restoreContext}
            , DMConf{restoreContext.Conf.CommandConf}
            , StartModule{startModule}
            , StartModuleManagedDeps{restoreContext.Modules.GetModuleNodeLists(startModule.GetId()).UniqPeers()} {
        }

        enum EResolution {
            RegularDep,
            Default,
            DirectManaged,
            Managed,
            Conflict,
            Excluded
        };

        struct TDependencyResolutionInfo {
            const TModule* Peer = nullptr;
            const TModule* Orig = nullptr;
            TNodeId PeerId = TNodeId::Invalid;
            EResolution Resolution = Excluded;
        };

        TDependencyResolutionInfo GetResolutionInfo(const TConstDepRef& dep) {
            const TModule* parent = RestoreContext.Modules.Get(dep.From()->ElemId);
            Y_ASSERT(parent);
            const bool isDirect = StartModule.GetId() == parent->GetId();
            const TConstDepNodeRef peerNodeRef = GetReplacement(dep.To(), isDirect);
            TDependencyResolutionInfo res;
            res.Orig = FindOrig(*parent, dep.To());
            if (!peerNodeRef.IsValid()) {
                return res; // Dependency is excluded on all paths
            }
            if (!CanFollow(*parent, peerNodeRef)) {
                return res; // Dependency is excluded on this particular path
            }

            const TModule* peer = RestoreContext.Modules.Get(peerNodeRef->ElemId);
            Y_ASSERT(peer);
            res.Peer = peer;
            res.PeerId = peerNodeRef.Id();
            if (IsGhost(StartModule, *peer)) {
                res.Resolution = isDirect ? EResolution::DirectManaged : EResolution::Managed;
            } else if (dep.To().Id() == peerNodeRef.Id()) {
                res.Resolution = EResolution::RegularDep;
            } else if (res.Orig->GetId() != peer->GetId()) {
                res.Resolution = EResolution::Conflict;
            } else if (IsGhost(*parent, *peer)) {
                res.Resolution = DirectManaged;
            } else {
                res.Resolution = EResolution::Default;
            }

            return res;
        }

        const TRestoreContext& GetRestoreContext() const noexcept {
            return RestoreContext;
        }

        const TModule& GetStartModule() const noexcept {
            return StartModule;
        }

    private:
        TConstDepNodeRef GetReplacement(const TConstDepNodeRef& peerNode, bool directPeer) {
            if (StartModuleManagedDeps.has(peerNode.Id())) {
                return peerNode;
            }
            const auto* peer = RestoreContext.Modules.Get(peerNode->ElemId);
            Y_ASSERT(peer);
            if (!DMConf.IsContribWithVer(*peer)) {
                return RestoreContext.Graph.GetInvalidNode();
            }

            const auto& libResolutions = GetLibResolutions(StartModule.GetId());
            auto it = libResolutions.find(peer->GetDir());
            if (it == libResolutions.end() && !directPeer) {
                auto& fileConf = RestoreContext.Graph.Names().FileConf;
                it = libResolutions.find(fileConf.Parent(peer->GetDir()));
            }
            if (it == libResolutions.end()) {
                return RestoreContext.Graph.GetInvalidNode();
            }

            return RestoreContext.Graph[it->second];
        }

        const TModule* FindOrig(const TModule& parent, const TConstDepNodeRef& peerNode) {
            const auto* peer = RestoreContext.Modules.Get(peerNode->ElemId);
            Y_ASSERT(peer);
            const auto& libResolutions = GetLibResolutions(parent.GetId());
            auto it = libResolutions.find(peer->GetDir());
            if (it != libResolutions.end()) {
                return RestoreContext.Modules.Get(RestoreContext.Graph[it->second]->ElemId);
            }

            return peer;
        }

        const THashMap<TFileView, TNodeId>& GetLibResolutions(ui32 moduleId) {
            const auto [it, inserted] = ModuleLibVersionsCache.emplace(moduleId, THashMap<TFileView, TNodeId>{});
            if (!inserted) {
                return it->second;
            }
            for (TNodeId peerId: RestoreContext.Modules.GetModuleNodeLists(moduleId).UniqPeers()) {
                auto* peer = RestoreContext.Modules.Get(RestoreContext.Graph[peerId]->ElemId);
                Y_ASSERT(peer);
                if (DMConf.IsContribWithVer(*peer)) {
                    auto& fileConf = RestoreContext.Graph.Names().FileConf;
                    it->second.emplace(fileConf.Parent(peer->GetDir()), peerId);
                }
            }
            return it->second;
        }

        bool CanFollow(const TModule& parent, const TConstDepNodeRef& peerNode) {
            if (RestoreContext.Modules.GetModuleNodeLists(parent.GetId()).UniqPeers().has(peerNode.Id())) {
                return true;
            }

            const auto *peer = RestoreContext.Modules.Get(peerNode->ElemId);
            Y_ASSERT(peer);
            auto& fileConf = RestoreContext.Graph.Names().FileConf;
            return GetLibResolutions(parent.GetId()).contains(fileConf.Parent(peer->GetDir()));
        }

    private:
        TRestoreContext RestoreContext;
        TDependencyManagementConf DMConf;
        const TModule& StartModule;
        const TUniqVector<TNodeId>& StartModuleManagedDeps;

        THashMap<ui32, THashMap<TFileView, TNodeId>> ModuleLibVersionsCache;
    };

    class TDepTreeVisitor: public TNoReentryVisitorBase<TVisitorStateItemBase, TGraphIteratorStateItemBase<true>> {
    public:
        using TBase = TNoReentryVisitorBase<TVisitorStateItemBase, TGraphIteratorStateItemBase<true>>;
        using TState = TBase::TState;

        TDepTreeVisitor(TRestoreContext restoreContext, const TModule& startModule): Explainer{restoreContext, startModule} {}

        bool Enter(TState& state) {
            const bool fresh = TBase::Enter(state);
            if (!IsModule(state.Top())) {
                return false;
            }

            if (state.Size() == 1) {
                const auto& mod = Explainer.GetStartModule();
                if (mod.IsFromMultimodule())
                    DMExlain(state, SUBMODULE, "path"_a = mod.GetDir().CutAllTypes(), "type"_a = mod.GetUserType());
                else {
                    DMExlain(state, NORMAL, "path"_a = mod.GetDir().CutAllTypes());
                }
                return fresh;
            }

            if (!state.HasIncomingDep()) {
                return fresh;
            }

            const auto* module = Explainer.GetRestoreContext().Modules.Get(state.TopNode()->ElemId);
            Y_ASSERT(module);
            if (!module->GetAttrs().RequireDepManagement) {
                return false;
            }

            const auto info = Explainer.GetResolutionInfo(state.IncomingDep());
            Y_ASSERT(info.Peer || info.Resolution == TDependencyManagementExplainer::Excluded);

            const bool isReplacement = info.Resolution != TDependencyManagementExplainer::Conflict && info.Resolution != TDependencyManagementExplainer::Excluded;
            if (isReplacement && !Reported.emplace(info.Orig->GetId(), info.Peer->GetId()).second) {
                DMExlain(state, DUPLICATE, "path"_a = info.Peer->GetDir().CutAllTypes());
                return false;
            }

            switch(info.Resolution) {
                case TDependencyManagementExplainer::RegularDep:
                    DMExlain(state, NORMAL, "path"_a = info.Peer->GetDir().CutAllTypes());
                    break;
                case TDependencyManagementExplainer::Default:
                    DMExlain(state, DIRECT_DEFAULT, "path"_a = info.Peer->GetDir().CutAllTypes());
                    return !TraverseReplacement(state, info.PeerId) && fresh;
                case TDependencyManagementExplainer::DirectManaged:
                    DMExlain(state, DIRECT_MANAGED, "path"_a = info.Peer->GetDir().CutAllTypes());
                    return !TraverseReplacement(state, info.PeerId) && fresh;
                case TDependencyManagementExplainer::Managed:
                    DMExlain(state, MANAGED, "path"_a = info.Peer->GetDir().CutAllTypes(), "orig"_a = info.Orig->GetDir().Basename());
                    return !TraverseReplacement(state, info.PeerId) && fresh;
                case TDependencyManagementExplainer::Conflict:
                    DMExlain(state, CONFLICT, "path"_a = info.Orig->GetDir().CutAllTypes(), "conflict_resolution"_a = info.Peer->GetDir().Basename());
                    return false;
                case TDependencyManagementExplainer::Excluded:
                    DMExlain(state, EXCLUDED, "path"_a = info.Orig->GetDir().CutAllTypes());
                    return false;
            }

            return fresh;
        }

        bool AcceptDep(TState& state) {
            const auto dep = state.NextDep();
            if (!TBase::AcceptDep(state) || !IsDirectPeerdirDep(dep)) {
                return false;
            }
            const auto* parent = Explainer.GetRestoreContext().Modules.Get(dep.From()->ElemId);
            Y_ASSERT(parent);
            const auto* peer = Explainer.GetRestoreContext().Modules.Get(dep.To()->ElemId);
            Y_ASSERT(peer);
            return !IsGhost(*parent, *peer);
        }

    private:
        template<typename... T>
        void DMExlain(TState& state, const char* fmtStr, T&&... args) {
            if (auto depth = state.Size() - ReplacementsInStack - 1; depth > 0) {
                Cout << "[[unimp]]";
                while (depth-- > 1) {
                    Cout << "|   ";
                }
                Cout << "|-->[[rst]]";
            }
            fmt::memory_buffer buf;
            fmt::format_to(std::back_inserter(buf), fmt::runtime(fmtStr), std::forward<T>(args)...);
            Cout.Write(buf.data(), buf.size());
            Cout << Endl;
        }

        bool TraverseReplacement(TState& state, TNodeId replacement) {
            if (state.TopNode().Id() == replacement) {
                return false;
            }
            ++ReplacementsInStack;
            IterateAll(state.TopNode().Graph()[replacement], state, *this);
            --ReplacementsInStack;
            return true;
        }

    private:
        static constexpr const char* NORMAL = "[[imp]]{path}[[rst]]";
        static constexpr const char* SUBMODULE = "[[imp]]{path}@{type}[[rst]]";
        static constexpr const char* CONFLICT = "[[unimp]]{path} (omitted because of [[c:yellow]]confict with {conflict_resolution}[[unimp]])[[rst]]";
        static constexpr const char* MANAGED = "[[imp]]{path}[[unimp]] (replaced from [[c:blue]]{orig}[[unimp]] because of [[c:blue]]DEPENDENCY_MANAGEMENT[[unimp]])[[rst]]";
        static constexpr const char* DUPLICATE = "[[unimp]]{path} (*)[[rst]]";
        static constexpr const char* EXCLUDED = "[[unimp]]{path} (omitted because of [[c:red]]EXCLUDE[[unimp]])[[rst]]";
        static constexpr const char* DIRECT_MANAGED = "[[imp]]{path}[[unimp]] (replaced from [[c:blue]]unversioned[[unimp]] because of [[c:blue]]DEPENDENCY_MANAGEMENT[[unimp]])[[rst]]";
        static constexpr const char* DIRECT_DEFAULT = "[[imp]]{path}[[unimp]] (replaced from [[c:magenta]]unversioned[[unimp]] to [[c:magenta]]default[[unimp]])[[rst]]";

    private:
        TDependencyManagementExplainer Explainer;
        THashSet<std::pair<ui32, ui32>> Reported; // from module id, to module id
        size_t ReplacementsInStack = 0;
    };

    class TDependencyManagementCollectingVisitor: public TPeerCollectingVisitor<TDependencyManagementCollector> {
    public:
        using TBase = TPeerCollectingVisitor<TDependencyManagementCollector>;

        TDependencyManagementCollectingVisitor(TDependencyManagementCollector& collector)
            : TPeerCollectingVisitor<TDependencyManagementCollector>{collector}
            , ApplyExcludesProp(collector.GetAppliedExcludesProp())
        {}

        bool Enter(TState& state) {
            if (
                ApplyExcludesProp != 0 &&
                (state.TopNode()->NodeType == EMNT_Property && state.TopNode()->ElemId == ApplyExcludesProp) ||
                ((
                    state.TopNode()->NodeType == EMNT_BuildCommand ||
                    state.TopNode()->NodeType == EMNT_BuildVariable
                ) && CmdsWithApplyExcludes.contains(state.TopNode()->ElemId))
            ) {
                const auto curentModule = state.FindRecent([](auto& item) { return IsModule(item); });
                Y_ASSERT(curentModule != state.end());
                curentModule->UseExcludes = true;
            }
            return TBase::Enter(state);
        }

        void Left(TState& state) {
            TBase::Left(state);
            if (
                ApplyExcludesProp == 0 ||
                !(
                    state.Top().CurDep().From()->NodeType == EMNT_BuildCommand ||
                    state.Top().CurDep().From()->NodeType == EMNT_BuildVariable
                ) ||
                !(IsInnerCommandDep(state.Top().CurDep()) || IsPropertyDep(state.Top().CurDep()))
            ) {
                return;
            }

            const ui32 destId = state.Top().CurDep().To()->ElemId;
            if (destId == ApplyExcludesProp || CmdsWithApplyExcludes.contains(destId)) {
                auto cmdId = TVersionedCmdId(state.Top().CurDep().From()->ElemId);
                if (cmdId.IsNewFormat()) {
                    CmdsWithApplyExcludes.insert(cmdId.ElemId());
                } else {
                    if (GetId(TDepGraph::GetCmdName(state.Top().CurDep().From()).GetStr()) == 0) {
                        CmdsWithApplyExcludes.insert(cmdId.ElemId());
                    }
                }
            }
        }

        private:
            ui32 ApplyExcludesProp = 0;
            THashSet<ui32> CmdsWithApplyExcludes;
    };

}

namespace NDetail {
    void TPeersClosure::Merge(TNodeId node, const TPeersClosure& closure, ui32 pathsCount) {
        for (const auto* peerStat : closure.TopSort) {
            auto mergedStats = peerStat->second;
            mergedStats.PathCount *= pathsCount;
            const auto [pos, inserted] = Stats.emplace(peerStat->first, mergedStats);
            if (!inserted) {
                pos->second.PathCount += mergedStats.PathCount;
                pos->second.Excluded = pos->second.Excluded && mergedStats.Excluded;
            } else {
                TopSort.push_back(&(*pos));
            }
        }

        const auto [pos, inserted] = Stats.emplace(node, TClosureStats{pathsCount});
        if (!inserted) {
            pos->second.PathCount += pathsCount;
            pos->second.Excluded = false;
        } else {
            TopSort.push_back(&(*pos));
        }
    }

    TPeersClosure TPeersClosure::Exclude(const std::function<bool(TNodeId)>& predicate, const std::function<const TPeersClosure&(TNodeId)>& nodeClosure) const {
        TPeersClosure res;
        res.TopSort.reserve(TopSort.size());

        THashMap<TNodeId, ui32> excludedPaths;
        for (auto srcIt = TopSort.rbegin(); srcIt != TopSort.rend(); ++srcIt) {
            auto [it, _] = res.Stats.insert(*(*srcIt));
            res.TopSort.push_back(&(*it));

            auto& [id, stat] = *it;
            if (!predicate(id)) {
                continue;
            }

            stat.Excluded = true;
            auto [idExcludes, inserted] = excludedPaths.emplace(id, static_cast<ui32>(stat.PathCount));
            const ui32 newExcludes = inserted ? stat.PathCount : stat.PathCount - idExcludes->second;
            if (newExcludes == 0) {
                continue;
            }
            idExcludes->second = stat.PathCount;

            for (auto [peer, peerStat]: nodeClosure(id).Stats) {
                auto peerExcldes = excludedPaths.emplace(peer, 0).first;
                peerExcldes->second += newExcludes * peerStat.PathCount;
            }
        }

        Reverse(res.TopSort.begin(), res.TopSort.end());
        for (auto [id, excludes]: excludedPaths) {
            auto& stat = res.Stats.at(id);
            Y_ASSERT(stat.PathCount >= excludes);
            if (stat.PathCount == excludes) {
                stat.Excluded = true;
            }
        }
        return res;
    }

    bool TPeersClosure::Contains(TNodeId id) const noexcept {
        const auto it = Stats.find(id);
        return it != Stats.end() && !it->second.Excluded;
    }

    bool TPeersClosure::ContainsWithAnyStatus(TNodeId id) const noexcept {
        const auto it = Stats.find(id);
        return it != Stats.end();
    }
}

TMaybe<EBuildResult> TYMake::ApplyDependencyManagement() {
    if (CanBypassConfigure() && Conf.ReadDepManagementCache && Conf.YmakeDMCache.Exists()) {
        auto status = LoadDependencyManagementCache(Conf.YmakeDMCache);
        if (status == TCacheFileReader::EReadResult::Success) {
            YDebug() << "Use Dependency management cache" << Endl;
            return TMaybe<EBuildResult>();
        } else if (status == TCacheFileReader::EReadResult::Exception) {
            return TMaybe<EBuildResult>(BR_RETRYABLE_ERROR);
        }
    }

    FORCE_TRACE(U, NEvent::TStageStarted("Apply Dependency Management"));
    Modules.ResetTransitiveInfo();
    TDependencyManagementCollector collector{GetRestoreContext()};
    TDependencyManagementCollectingVisitor collectorVisitor{collector};
    TDependencyManagementCollectingVisitor::TState collectorState;
    IterateAll(Graph, StartTargets, collectorState, collectorVisitor, [](const TTarget& t) -> bool { return t.IsModuleTarget; });
    FORCE_TRACE(U, NEvent::TStageFinished("Apply Dependency Management"));

    GetDependsToModulesClosure();

    SaveDependencyManagementCache(Conf.YmakeDMCache, &DMCacheTempFile);

    collector.LogStats();

    return TMaybe<EBuildResult>();
}

void ExplainDM(TRestoreContext restoreContext, const THashSet<TNodeId>& roots) {
    for (TConstDepNodeRef node : GetStartModules(restoreContext.Graph, roots)) {
        const TModule* module = restoreContext.Modules.Get(node->ElemId);
        Y_ASSERT(module);
        TScopedContext diagContext{module->GetName()};
        if (!module->GetAttrs().RequireDepManagement) {
            YConfWarn(UserWarn) << "Do not support dependency management, dependency-tree will not be printed for it." << Endl;;
            continue;
        }

        TDepTreeVisitor visitor{restoreContext, *module};
        IterateAll(node, visitor);
    }
}

void DumpDM(TRestoreContext restoreContext, const THashSet<TNodeId>& roots, EManagedPeersDepth depth) {
    const auto& rootModules = GetStartModules(restoreContext.Graph, roots);
    bool printRoots = rootModules.size() != 1;
    for (TConstDepNodeRef node : rootModules) {
        const TModule* module = restoreContext.Modules.Get(node->ElemId);
        Y_ASSERT(module);
        TScopedContext diagContext{module->GetName()};
        if (!module->GetAttrs().RequireDepManagement) {
            YConfWarn(UserWarn) << "Do not support dependency management, managed dependencies will not be printed for it." << Endl;;
            continue;
        }

        if (printRoots) {
            if (module->IsFromMultimodule()) {
                fmt::print("=== [[imp]]{}@{}[[rst]] ===\n", module->GetDir().CutAllTypes(), module->GetUserType());
            } else {
                fmt::print("=== [[imp]]{}[[rst]] ===\n", module->GetDir().CutAllTypes());
            }
        }
        Cout << module->GetName().CutAllTypes() << Endl;
        auto transitiveInfo = restoreContext.Modules.GetModuleNodeLists(module->GetId());
        const auto& peers = depth == EManagedPeersDepth::Direct ? transitiveInfo.ManagedDirectPeers() : transitiveInfo.UniqPeers();
        for (TNodeId dep: peers) {
            const TModule* depModule = restoreContext.Modules.Get(restoreContext.Graph[dep]->ElemId);
            Y_ASSERT(depModule);
            if (depModule->GetAttrs().RequireDepManagement) {
                Cout << depModule->GetName().CutAllTypes() << Endl;
            }
        }
    }
}

void DumpFDM(const TVars& globalVars, bool asJson) {
    TDependencyManagementConf conf(globalVars);
    if (asJson) {
        NJsonWriter::TBuf json{NJsonWriter::HEM_UNSAFE};
        json.BeginObject();
        for (const auto& [lib, libWithVer]: conf.GetForcedLib2Ver()) {
            json.WriteKey(lib);
            json.WriteString(libWithVer);
        }
        json.EndObject();
        json.FlushTo(&Cout);
    } else {
        for (const auto& [lib, libWithVer]: conf.GetForcedLib2Ver()) {
            Cout << lib << ": " << libWithVer << Endl;
        }
    }
}
