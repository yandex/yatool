#include "transitive_requirements_check.h"

#include "module_restorer.h"
#include "module_store.h"

#include <devtools/ymake/common/split.h>
#include <devtools/ymake/compact_graph/iter_direct_peerdir.h>
#include <devtools/ymake/compact_graph/query.h>
#include <devtools/ymake/diag/trace.h>
#include <devtools/ymake/macro_processor.h>
#include <devtools/ymake/spdx.h>
#include <devtools/ymake/ymake.h>
#include <library/cpp/json/writer/json.h>
#include <util/generic/array_ref.h>
#include <util/memory/pool.h>

namespace {

    const TVector<TNodeId>& EffectivePeersClosure(const TRestoreContext& restoreContext, const TModule& module) {
        Y_ASSERT(module.GetAttrs().RequireDepManagement || module.IsPeersComplete());
        const auto& nodeIds = restoreContext.Modules.GetModuleNodeIds(module.GetId());
        return restoreContext.Modules.GetNodeListStore().GetList(nodeIds.UniqPeers).Data();
    }

    TMaybe<EConstraintsType> EvalConstraintType(TStringBuf macro, TStringBuf types) {
        TMaybe<EConstraintsType> constraintsType;
        for (TStringBuf type : SplitBySpace(types)) {
            if (type == "DENY") {
                if (constraintsType && *constraintsType != EConstraintsType::Deny) {
                    YConfErr(UserErr) << "Multiple " << macro << " macro with different restriction types applied to the same module" << Endl;
                    return {};
                }
                constraintsType = EConstraintsType::Deny;
            } else if (type == "ALLOW_ONLY") {
                if (constraintsType && *constraintsType != EConstraintsType::AllowOnly) {
                    YConfErr(UserErr) << "Multiple " << macro << " macro with different restriction types applied to the same module" << Endl;
                    return {};
                }
                constraintsType = EConstraintsType::AllowOnly;
            } else {
                YConfErr(UserErr) << macro << " macro requires first argument to be restriction type 'DENY' or 'ALLOW_ONLY'. Got '" << type << "' instead" << Endl;
            }
        }
        return constraintsType;
    }

    template <ERequirementsScope Scope>
    struct TModuleDepsVisitor : TDirectPeerdirsConstVisitor<> {
        using TBase = TDirectPeerdirsConstVisitor<>;

        bool AcceptDep(TState& state) {
            if (Scope == ERequirementsScope::Peers && !IsDirectPeerdirDep(state.Top().CurDep())) {
                return false;
            }
            return TBase::AcceptDep(state) && !IsTooldirDep(state.Top().CurDep());
        }
    };

    template <ERequirementsScope Scope>
    struct TDependencyPathFormatter {
        const TRestoreContext& RestoreContext;
        TNodeId From;
        TNodeId To;
        bool IndirectDepsOnly;
    };

    template <ERequirementsScope Scope>
    IOutputStream& operator<<(IOutputStream& out, const TDependencyPathFormatter<Scope>& path) {
        Y_ASSERT(IsModuleType(path.RestoreContext.Graph[path.From]->NodeType));
        Y_ASSERT(IsModuleType(path.RestoreContext.Graph[path.To]->NodeType));

        TGraphConstIteratorState state;
        TModuleDepsVisitor<Scope> visitor;
        TDepthGraphIterator<TGraphConstIteratorState, TModuleDepsVisitor<Scope>> it(path.RestoreContext.Graph, state, visitor);

        for (bool res = it.Init(path.From); res; res = it.Next()) {
            if ((*it).Node().Id() == path.To) {
                break;
            }
        }
        if (state.Size() < 2) {
            YConfErr(KnownBug)
                << "Print dependnencies path called on independent modues: from='"
                << path.RestoreContext.Modules.Get(path.RestoreContext.Graph[path.From]->ElemId)->GetName()
                << "' to='"
                << path.RestoreContext.Modules.Get(path.RestoreContext.Graph[path.To]->ElemId)->GetName()
                << "'. Transitive dependencies check is probably broken." << Endl;
            return out;
        } else if (path.IndirectDepsOnly && state.Size() == 2) {
            return out;
        }

        for (auto prev = state.Stack().begin(), next = std::next(prev); next != state.Stack().end(); ++next) {
            if (!IsModule(*next)) {
                continue;
            }
            TModule* prevMod = path.RestoreContext.Modules.Get(prev->Node()->ElemId);
            TModule* nextMod = path.RestoreContext.Modules.Get(next->Node()->ElemId);
            Y_ASSERT(prevMod && nextMod);
            out << Endl << "    " << prevMod->GetName() << " -> " << nextMod->GetName();
            prev = next;
        }
        return out;
    }

    TString EvalVariable(TStringBuf name, const TVars& vars, const TBuildConfiguration& conf) {
        return TCommandInfo(conf, nullptr, nullptr).SubstVarDeeply(name, vars);
    }

    class TProvidesChecker {
    private:
        struct TModuleRef {
            TNodeId Node = TNodeId::Invalid;
            const TModule* Module = nullptr;
        };

        const TRestoreContext& RestoreContext;
        TModuleRef StartTarget;
        THashMap<TStringBuf, TModuleRef> Provided;

    public:
        TProvidesChecker(const TRestoreContext& restoreContext, const TModule& startTarget, TNodeId node)
            : RestoreContext{restoreContext}
            , StartTarget{node, &startTarget} {
            CheckProvides(startTarget, node);
        }

        void CheckProvides(const TModule& module, TNodeId node) {
            Y_ASSERT(StartTarget.Module);
            for (const auto& feature : module.Provides) {
                const auto [collision, inserted] = Provided.try_emplace(feature, TModuleRef{node, &module});
                if (inserted) {
                    continue;
                }
                if (collision->second.Node != StartTarget.Node) {
                    YConfErr(BadDep) << "depends on two modules which [[alt1]]PROVIDES[[rst]] same feature '" << feature << "':" << Endl
                                     << "[[imp]]" << module.GetName() << "[[rst]]"
                                     << TDependencyPathFormatter<ERequirementsScope::Peers>{RestoreContext, StartTarget.Node, node, false} << Endl
                                     << "[[imp]]" << collision->second.Module->GetName() << "[[rst]]"
                                     << TDependencyPathFormatter<ERequirementsScope::Peers>{RestoreContext, StartTarget.Node, collision->second.Node, false} << Endl;
                } else {
                    YConfErr(BadDep) << "[[alt1]]PROVIDES[[rst]] feature '" << feature << "' and depends on module [[imp]]" << module.GetName() << "[[rst]] providing same feature:"
                                     << TDependencyPathFormatter<ERequirementsScope::Peers>{RestoreContext, StartTarget.Node, node, false} << Endl;
                }
            }
        }
    };

    const auto checkPoliciesRequest = TMatchPeerRequest::CheckOnly({EPeerSearchStatus::DeprecatedByRules});

    class TConstraintsChecker {
    public:
        explicit TConstraintsChecker(TRestoreContext restoreContext)
            : RestoreContext{restoreContext} {
            RequirementLoaders.reserve(std::size(TRANSITIVE_CHECK_REGISTRY));
            for (const auto& item : TRANSITIVE_CHECK_REGISTRY) {
                RequirementLoaders.push_back(item.RequirementLoaderFactory(RestoreContext.Conf.CommandConf));
            }
        }

        void Check(const TModule& module, TNodeId node) {
            // Check if there are unchecked constraints
            if (!CheckedModules.insert(node).second) {
                return;
            }

            TScopedContext context(module.GetName());

            const auto requirements = LoadTransitiveRequirements(module);
            if (!module.GetAttrs().CheckProvides && !module.GetAttrs().RequireDepManagement && requirements.empty()) {
                return;
            }

            for (const auto& requirement : requirements) {
                if (requirement.Scopes & ERequirementsScope::Peers) {
                    requirement.Check(RestoreContext, module, node, {EffectivePeersClosure(RestoreContext, module), ERequirementsScope::Peers});
                }
                if (requirement.Scopes & ERequirementsScope::Tools) {
                    TModuleRestorer restorer{RestoreContext, RestoreContext.Graph[node]};
                    restorer.RestoreModule();
                    requirement.Check(RestoreContext, module, node, {restorer.GetTools().Data(), ERequirementsScope::Tools});
                }
            }

            // Check if constraints are satisfied
            TProvidesChecker providesChecker{RestoreContext, module, node};
            for (TNodeId peer : EffectivePeersClosure(RestoreContext, module)) {
                TModule* peerModule = RestoreContext.Modules.Get(RestoreContext.Graph[peer]->ElemId);
                Y_ASSERT(peerModule);
                if (module.GetAttrs().RequireDepManagement && module.MatchPeer(*peerModule, checkPoliciesRequest) == EPeerSearchStatus::DeprecatedByRules) {
                    YConfErr(BadDir) << "Transitive [[alt1]]PEERDIR[[rst]] from [[imp]]"
                                     << module.GetDir()
                                     << "[[rst]] to [[imp]]"
                                     << peerModule->GetDir() << "[[rst]] is prohibited by peerdir policy. Use EXCLUDE or DEPENDENCY_MANAGEMENT to avoid this dependency."
                                     << Endl;
                }
                providesChecker.CheckProvides(*peerModule, peer);
            }
        }

    private:
        TVector<TTransitiveRequirement> LoadTransitiveRequirements(const TModule& module) {
            TVector<TTransitiveRequirement> res;

            for (const auto& load : RequirementLoaders) {
                if (auto requirement = load(RestoreContext.Graph, RestoreContext.Conf, module); requirement.Check != nullptr) {
                    res.push_back(std::move(requirement));
                }
            }

            return res;
        }

    private:
        TRestoreContext RestoreContext;
        THashSet<TNodeId> CheckedModules;
        TVector<TTransitiveCheckRegistryItem::TRequirementsLoader> RequirementLoaders;
    };

    // Check implementations
    class TRestrictLicensesLoader {
    private:
        class TRestrictions {
        public:
            TRestrictions(EConstraintsType type) noexcept
                : Type{type} {};

            void Set(size_t prop) {
                Restrictions.set(prop);
            }

            bool Check(NSPDX::EPeerType peerType, TArrayRef<const NSPDX::TLicenseProps> licenses) const noexcept {
                switch (Type) {
                    case EConstraintsType::Deny:
                        return AnyOf(licenses, [&](const NSPDX::TLicenseProps& license) { return (Restrictions & license.GetProps(peerType)).none(); });
                    case EConstraintsType::AllowOnly:
                        return AnyOf(licenses, [&](const NSPDX::TLicenseProps& license) { return (~Restrictions & license.GetProps(peerType)).none(); });
                }
                Y_UNREACHABLE();
            }

        private:
            EConstraintsType Type;
            NSPDX::TPropSet Restrictions;
        };

        struct TExceptions : public TNonCopyable {
            TString Str;
            THashMap<TStringBuf, bool> Map;
        };

        // Module level vars
        constexpr static TStringBuf MODULE_LICENSES_RESTRICTIONS = "MODULE_LICENSES_RESTRICTIONS";
        constexpr static TStringBuf MODULE_LICENSES_RESTRICTION_TYPES = "MODULE_LICENSES_RESTRICTION_TYPES";
        constexpr static TStringBuf MODULE_LICENSES_RESTRICTION_EXCEPTIONS = "MODULE_LICENSES_RESTRICTION_EXCEPTIONS";
        constexpr static TStringBuf LICENSE_EXPRESSION = "LICENSE_EXPRESSION";
        constexpr static TStringBuf EXPLICIT_LICENSE_PREFIXES = "EXPLICIT_LICENSE_PREFIXES";
        constexpr static TStringBuf EXPLICIT_LICENSE_EXCEPTIONS = "EXPLICIT_LICENSE_EXCEPTIONS";
        constexpr static TStringBuf MODULEWISE_LICENSES_RESTRICTIONS = "MODULEWISE_LICENSES_RESTRICTIONS";
        constexpr static TStringBuf MODULEWISE_LICENSES_RESTRICTION_TYPES = "MODULEWISE_LICENSES_RESTRICTION_TYPES";

    public:
        constexpr static const TStringBuf CONF_VARS[] = {
            MODULE_LICENSES_RESTRICTIONS,
            MODULE_LICENSES_RESTRICTION_TYPES,
            MODULE_LICENSES_RESTRICTION_EXCEPTIONS,
            LICENSE_EXPRESSION,
            EXPLICIT_LICENSE_PREFIXES,
            MODULEWISE_LICENSES_RESTRICTIONS,
            MODULEWISE_LICENSES_RESTRICTION_TYPES,
        };

        TRestrictLicensesLoader(const TVars&) {
        }

        static TTransitiveCheckRegistryItem::TRequirementsLoader Create(const TVars& globals) {
            return [self = TRestrictLicensesLoader(globals)](TDepGraph& graph, const TBuildConfiguration& conf, const TModule& module) mutable {
                return self.Load(graph, conf, module);
            };
        }

        TTransitiveRequirement Load(TDepGraph&, const TBuildConfiguration& conf, const TModule& module) {
            LoadLicenses(conf);

            auto holderExceptions = GetExceptions(conf, module.Vars);
            // Validates licenses on module if they were not yet vaidated and populates cache.
            GetModuleLicenses(module, conf, *holderExceptions);

            TTransitiveRequirement result{};
            const TStringBuf macroName = "LICENSE_RESTRICTION";
            const auto constraintType = EvalConstraintType(macroName, EvalVariable(MODULE_LICENSES_RESTRICTION_TYPES, module.Vars, conf));
            if (!constraintType) {
                return result;
            }
            result.Scopes |= ERequirementsScope::Peers;
            const auto restrictions = GetRestrictions(conf, module.Vars, constraintType, macroName, MODULE_LICENSES_RESTRICTIONS);
            result.Check = [this, restrictions = std::move(restrictions), holderExceptions = holderExceptions.Release()]
                (TRestoreContext restoreContext, const TModule&, TNodeId modId, TScopeClosureRef closure) {
                CheckModuleLicenses(restoreContext, modId, restrictions, *holderExceptions, closure);
            };
            return result;
        }

        TVector<TStringBuf> GetPropNames(NSPDX::TPropSet props) const noexcept {
            TVector<TStringBuf> res;
            for (size_t pos = 0; pos < std::min(props.size(), PropNames.size()); ++pos) {
                if (props[pos]) {
                    res.push_back(PropNames[pos]);
                }
            }
            return res;
        }

        const THashMap<TString, NSPDX::TLicenseProps>& GetLicenses() const noexcept {
            return Licenses;
        }

        void LoadLicenses(const TBuildConfiguration& conf) {
            if (IsLicenseConfLoaded) {
                return;
            }
            auto& allLicenses = conf.Licenses;

            constexpr size_t maxProperties = NSPDX::TPropSet{}.size() - 1; // 1 bit is reserved for INVALID property for unknown licenses
            if (allLicenses.size() >= maxProperties) {
                YConfErr(KnownBug) << "To many license properties." << Endl;
            }

            size_t pos = 0;
            for (const auto& [propName, licenseGroup] : allLicenses) {
                const auto propBit = pos++;
                PropNames.push_back(propName);
                NSPDX::ForEachLicense(licenseGroup.Dynamic, [&](TStringBuf license) { Licenses[license].Dynamic.set(propBit); });
                NSPDX::ForEachLicense(licenseGroup.Static, [&](TStringBuf license) { Licenses[license].Static.set(propBit); });

                if (pos == maxProperties) {
                    break;
                }
            }

            PropNames.push_back(TStringBuf("INVALID"));
            DefaultLicenseName = GetValueOrEmpty(conf.CommandConf, NVariableDefs::VAR_DEFAULT_MODULE_LICENSE);
            if (DefaultLicenseName.empty()) {
                DefaultLicense.push_back(GetInvalidLicenseProps());
                return;
            }

            try {
                DefaultLicense = NSPDX::ParseLicenseExpression(Licenses, DefaultLicenseName);
            } catch (const NSPDX::TExpressionError& err) {
                YConfErr(Misconfiguration) << "Failed to evaluate default license: " << err.what() << Endl;;
                DefaultLicense.push_back(GetInvalidLicenseProps());
            }
            IsLicenseConfLoaded = true;
        }

    private:
        void CheckModuleLicenses(TRestoreContext restoreContext, TNodeId modId, const TRestrictions& restrictions, TExceptions& exceptions, TScopeClosureRef closure) {
            for (TNodeId peer : closure.Closure) {
                const auto* module = restoreContext.Modules.Get(restoreContext.Graph[peer]->ElemId);
                Y_ASSERT(module);
                if (const auto used = exceptions.Map.FindPtr(module->GetDir().CutType())) {
                    *used = true;
                    continue;
                }
                const auto& moduleLicenses = GetModuleLicenses(*module, restoreContext.Conf, exceptions);
                const auto peerType = module->GetAttrs().DynamicLink ? NSPDX::EPeerType::Dynamic : NSPDX::EPeerType::Static;
                if (!restrictions.Check(peerType, moduleLicenses)) {
                    YConfErr(BadDep) << "Forbids direct or indirect PEERDIR dependency on code with any license used by module [[imp]]"
                                     << module->GetName()
                                     << "[[rst]] ([[alt2]]"
                                     << ListModuleLicenses(*module)
                                     << "[[rst]]) via [[alt1]]LICENSE_RESTRICTION[[rst]] restrictions"
                                     << TDependencyPathFormatter<ERequirementsScope::Peers>{restoreContext, modId, peer, true}
                                     << Endl;
                }
            }
            for (const auto [exception, used] : exceptions.Map) {
                if (!used) {
                    YConfWarn(PedanticLicenses) << "Unused [[alt1]]LICENSE_RESTRICTION_EXCEPTIONS[[rst]] [[imp]]" << exception << "[[rst]]" << Endl;
                }
            }
        }

        TRestrictions GetRestrictions(const TBuildConfiguration& conf, const TVars& moduleVars, const TMaybe<EConstraintsType> constraintType, const TStringBuf macroName, const TStringBuf varName) const {
            TRestrictions restrictions{*constraintType};
            const TString restrictionsStr = EvalVariable(varName, moduleVars, conf);
            for (TStringBuf arg : SplitBySpace(restrictionsStr)) {
                const auto prop = FindIndex(PropNames, arg);
                if (prop < PropNames.size()) {
                    restrictions.Set(prop);
                } else {
                    YConfErr(Misconfiguration) << "adds restriction to unknown license property [[alt2]]" << arg << "[[rst]] via [[alt1]]" << macroName << "[[rst]]" << Endl;
                }
            }
            return restrictions;
        }

        static THolder<TExceptions> GetExceptions(const TBuildConfiguration& conf, const TVars& moduleVars) {
            auto exceptions = MakeHolder<TExceptions>();
            exceptions->Str = EvalVariable(MODULE_LICENSES_RESTRICTION_EXCEPTIONS, moduleVars, conf);
            for (auto exception: SplitBySpace(exceptions->Str)) {
                exceptions->Map.emplace(exception, false);
            }
            return exceptions;
        }

        TVector<NSPDX::TLicenseProps> LoadModuleLicenses(const TModule& module) const {
            TScopedContext context(module.GetName());
            const auto license = module.Get(LICENSE_EXPRESSION);
            if (license.empty()) {
                TStringBuf mandatoryLicensePrefixes = module.Get(EXPLICIT_LICENSE_PREFIXES);
                TStringBuf mandatoryLicenseExceptions = module.Get(EXPLICIT_LICENSE_EXCEPTIONS);
                TStringBuf moduleDir = module.GetDir().CutType();
                for (TStringBuf prefix : SplitBySpace(mandatoryLicensePrefixes)) {
                    if (NPath::IsPrefixOf(prefix, moduleDir)) {
                        for (TStringBuf exceptionPrefix: SplitBySpace(mandatoryLicenseExceptions)) {
                            if (NPath::IsPrefixOf(exceptionPrefix, moduleDir)) {
                                return DefaultLicense;
                            }
                        }
                        YConfErr(Misconfiguration) << "Explicit license must be specified for modules inside " << prefix << " directory" << Endl;
                    }
                }
                return DefaultLicense;
            }

            try {
                return NSPDX::ParseLicenseExpression(Licenses, license);
            } catch (const NSPDX::TExpressionError& err) {
                YConfErr(Misconfiguration) << "Module LICENSE must be valid SPDX expression: " << err.what() << Endl;;
            }
            return DefaultLicense;
        }

        TStringBuf ListModuleLicenses(const TModule& module) const {
            const auto allLicenses = module.Get(LICENSE_EXPRESSION);
            return allLicenses.empty() ? DefaultLicenseName : allLicenses;
        }

        const TVector<NSPDX::TLicenseProps>& GetModuleLicenses(const TModule& module, const TBuildConfiguration& conf, TExceptions& exceptions) {
            auto [it, inserted] = ModuleLicenseCache.emplace(module.GetId(), TVector<NSPDX::TLicenseProps>{});
            if (inserted) {
                it->second = LoadModuleLicenses(module);
                CheckModulewiseRestrictions(module, it->second, conf, exceptions);
            }
            return it->second;
        }

        void CheckModulewiseRestrictions(const TModule& module, const TVector<NSPDX::TLicenseProps>& moduleLicences, const TBuildConfiguration& conf, TExceptions& exceptions) {
            const TStringBuf macroName = "MODULEWISE_LICENSE_RESTRICTION";
            const auto constraintType = EvalConstraintType(macroName, EvalVariable(MODULEWISE_LICENSES_RESTRICTION_TYPES, module.Vars, conf));
            if (!constraintType) {
                return;
            }
            const auto restrictions = GetRestrictions(conf, module.Vars, constraintType, macroName, MODULEWISE_LICENSES_RESTRICTIONS);
            if (const auto used = exceptions.Map.FindPtr(module.GetDir().CutType())) {
                *used = true;
                return;
            }
            const auto peerType = module.GetAttrs().DynamicLink ? NSPDX::EPeerType::Dynamic : NSPDX::EPeerType::Static;
            if (!restrictions.Check(peerType, moduleLicences)) {
                YConfErr(BadDep) << "Modules licensed with "
                    << "[[alt2]]"
                    << ListModuleLicenses(module)
                    << "[[rst]] are forbidden."
                    << Endl;
            }
        }

        NSPDX::TLicenseProps GetInvalidLicenseProps() const noexcept {
            return NSPDX::TLicenseProps{
                NSPDX::TPropSet{1} << (PropNames.size() - 1),
                NSPDX::TPropSet{1} << (PropNames.size() - 1)};
        }

        static TStringBuf GetValueOrEmpty(const TVars& vars, TStringBuf var) {
            const auto propStr = vars.Get1(var);
            return propStr.empty() ? TStringBuf{} : GetPropertyValue(propStr);
        }

    private:
        THashMap<ui32, TVector<NSPDX::TLicenseProps>> ModuleLicenseCache;
        THashMap<TString, NSPDX::TLicenseProps> Licenses;
        TVector<TStringBuf> PropNames;
        TVector<NSPDX::TLicenseProps> DefaultLicense;
        TStringBuf DefaultLicenseName;
        bool IsLicenseConfLoaded = false;
    };

    class TCheckDependentDirsLoader {
    public:
        constexpr static const TStringBuf CONF_VARS[] = {
            TStringBuf("CHECK_DEPENDENT_DIRS_TYPES"),
            TStringBuf("CHECK_DEPENDENT_DIRS_RESTRICTIONS")};

        static TTransitiveCheckRegistryItem::TRequirementsLoader Create(const TVars&) {
            return Load;
        }

        static TTransitiveRequirement Load(TDepGraph& graph, const TBuildConfiguration& conf, const TModule& module) {
            TTransitiveRequirement result{};

            const auto constraintsType = EvalConstraintType("CHECK_DEPENDENT_DIRS", EvalVariable("CHECK_DEPENDENT_DIRS_TYPES"sv, module.Vars, conf));
            ;
            if (!constraintsType) {
                return result;
            }
            TDependencyConstraints constraints{*constraintsType};
            TDependencyConstraints exceptions{EConstraintsType::AllowOnly};

            auto currentScope = REQUIREMENT_SCOPE_ALL;
            bool glob = false;
            bool exception = false;
            const TString restrictionsStr = EvalVariable("CHECK_DEPENDENT_DIRS_RESTRICTIONS"sv, module.Vars, conf);
            for (TStringBuf arg : SplitBySpace(restrictionsStr)) {
                if (arg == "PEERDIRS") {
                    currentScope = ERequirementsScope::Peers;
                    continue;
                }
                if (arg == "ALL") {
                    currentScope = REQUIREMENT_SCOPE_ALL;
                    continue;
                }
                if (arg == "GLOB") {
                    glob = true;
                    continue;
                }
                if (arg == "EXCEPT") {
                    exception = true;
                    continue;
                }

                const bool treatAsExcept = std::exchange(exception, false);
                if (treatAsExcept && constraintsType == EConstraintsType::AllowOnly) {
                    YConfErr(Misconfiguration) << "[[alt1]]EXCEPT[[rst]] items are not allowed for [[alt1]]ALLOW_ONLY[[rst]] restrictions via [[alt1]]CHECK_DEPENDENT_DIRS[[rst]] macro" << Endl;
                    continue;
                }
                TDependencyConstraints& dest = treatAsExcept ? exceptions : constraints;
                const auto scope = treatAsExcept ? REQUIREMENT_SCOPE_ALL : currentScope;

                if (std::exchange(glob, false)) {
                    dest.Add(TGlobConstraint{arg, scope});
                } else {
                    dest.Add(TPrefixConstraint{graph, NPath::ConstructPath(arg, NPath::Source), scope});
                }
            }
            if (constraints.HasRestrictions(ERequirementsScope::Peers)) {
                result.Scopes |= ERequirementsScope::Peers;
            }
            if (constraints.HasRestrictions(ERequirementsScope::Tools)) {
                result.Scopes |= ERequirementsScope::Tools;
            }

            // Check if constraints are correct
            if (Diag()->ChkDepDirExists) {
                constraints.ValidatePaths(graph);
                exceptions.ValidatePaths(graph);
            }

            result.Check = [constraints = std::move(constraints), exceptions = std::move(exceptions)](
                TRestoreContext restoreContext,
                const TModule& parent,
                TNodeId node,
                TScopeClosureRef closure
            ) {
                if (closure.Scope == ERequirementsScope::Peers) {
                    for (TNodeId peer : closure.Closure) {
                        TModule* peerModule = restoreContext.Modules.Get(restoreContext.Graph[peer]->ElemId);
                        Y_ASSERT(peerModule);
                        if (parent.GetDirId() == peerModule->GetDirId() && Find(parent.SelfPeers, peerModule->GetId()) != parent.SelfPeers.end()) {
                            continue;
                        }
                        if (!constraints.IsAllowed(peerModule->GetDir(), ERequirementsScope::Peers)
                            && (exceptions.Empty() || !exceptions.IsAllowed(peerModule->GetDir(), ERequirementsScope::Peers))
                        ) {
                            YConfErr(BadDep) << "forbids direct or indirect PEERDIR dependency to module [[imp]]" << peerModule->GetName() << "[[rst]] via [[alt1]]CHECK_DEPENDENT_DIRS[[rst]]"
                                             << TDependencyPathFormatter<ERequirementsScope::Peers>{restoreContext, node, peer, true} << Endl;
                        }
                    }
                }

                if (closure.Scope == ERequirementsScope::Tools) {
                    THashSet<TNodeId> checkedToolPeers;
                    for (TNodeId tool : closure.Closure) {
                        TModule* toolModule = restoreContext.Modules.Get(restoreContext.Graph[tool]->ElemId);
                        Y_ASSERT(toolModule);
                        if (!constraints.IsAllowed(toolModule->GetDir(), ERequirementsScope::Tools)
                            && (exceptions.Empty() || !exceptions.IsAllowed(toolModule->GetDir(), ERequirementsScope::Tools))) {
                            YConfErr(BadDep) << "forbids direct or indirect dependency to tool [[imp]]" << toolModule->GetName() << "[[rst]] via [[alt1]]CHECK_DEPENDENT_DIRS[[rst]]"
                                             << TDependencyPathFormatter<ERequirementsScope::Tools>{restoreContext, node, tool, true} << Endl;
                        }

                        TModuleRestorer toolRstorer{restoreContext, restoreContext.Graph[tool]};
                        toolRstorer.RestoreModule();
                        for (TNodeId peer : toolRstorer.GetPeers()) {
                            if (!checkedToolPeers.insert(peer).second) {
                                continue;
                            }
                            TModule* peerModule = restoreContext.Modules.Get(restoreContext.Graph[peer]->ElemId);
                            Y_ASSERT(peerModule);
                            if (!constraints.IsAllowed(peerModule->GetDir(), ERequirementsScope::Tools)
                                && (exceptions.Empty() || !exceptions.IsAllowed(peerModule->GetDir(), ERequirementsScope::Tools))) {
                                YConfErr(BadDep) << "forbids direct or indirect dependency to module [[imp]]" << peerModule->GetName() << "[[rst]] via [[alt1]]CHECK_DEPENDENT_DIRS[[rst]]"
                                                 << TDependencyPathFormatter<ERequirementsScope::Tools>{restoreContext, node, tool, false}
                                                 << TDependencyPathFormatter<ERequirementsScope::Peers>{restoreContext, tool, peer, false} << Endl;
                            }
                        }
                    }
                }
            };
            return result;
        }
    };

    class TRequiresLoader {
    public:
        constexpr static const TStringBuf CONF_VARS[] = {
            TStringBuf("REQUIRED_TRANSITIVE_PEERS")};

        static TTransitiveCheckRegistryItem::TRequirementsLoader Create(const TVars&) {
            return Load;
        }

        static TTransitiveRequirement Load(TDepGraph&, const TBuildConfiguration& conf, const TModule& module) {
            TTransitiveRequirement result;
            auto requirements = EvalVariable("REQUIRED_TRANSITIVE_PEERS"sv, module.Vars, conf);
            if (requirements.empty()) {
                return result;
            }

            result.Scopes |= ERequirementsScope::Peers;
            result.Check = [requirements](TRestoreContext restoreContext, const TModule&, TNodeId, TScopeClosureRef closure) {
                THashSet<ui32> unmetRequirements;
                for (TStringBuf dir : SplitBySpace(requirements)) {
                    const auto dirPath = NPath::ConstructPath(dir, NPath::ERoot::Source);
                    if (!restoreContext.Graph.Names().FileConf.CheckExistentDirectory(dirPath)) {
                        YConfErr(BadDir) << "[[alt1]]REQUIRES[[rst]] to non-directory [[imp]]" << dirPath << "[[rst]]" << Endl;
                    } else {
                        unmetRequirements.insert(restoreContext.Graph.Names().IdByName(EMNT_File, dirPath));
                    }
                }

                for (TNodeId peerId : closure.Closure) {
                    TModule* peer = restoreContext.Modules.Get(restoreContext.Graph[peerId]->ElemId);
                    Y_ASSERT(peer);
                    unmetRequirements.erase(peer->GetDirId());
                }
                for (ui32 rq : unmetRequirements) {
                    YConfErr(BadDep)
                        << "missing indirect PEERDIR dependency to directory [[imp]]"
                        << restoreContext.Graph.Names().FileNameById(rq)
                        << "[[rst]] required by [[alt1]]REQUIRES[[rst]]"
                        << Endl;
                }
            };
            return result;
        }
    };

    class TFeatureVersionsLoader {
    public:
        static constexpr TStringBuf CHECK_FEATURE_VERSION_CONFLICTS = "CHECK_FEATURE_VERSION_CONFLICTS";
        static constexpr TStringBuf FEATURE_VERSIONS = "FEATURE_VERSIONS";

        constexpr static const TStringBuf CONF_VARS[] = {
            TStringBuf(CHECK_FEATURE_VERSION_CONFLICTS),
            TStringBuf(FEATURE_VERSIONS),
        };

        static TTransitiveCheckRegistryItem::TRequirementsLoader Create(const TVars&) {
            return Load;
        }

        static TTransitiveRequirement Load(TDepGraph&, const TBuildConfiguration& conf, const TModule& module) {
            TTransitiveRequirement result;
            const auto checkFeatureVersions = EvalVariable(CHECK_FEATURE_VERSION_CONFLICTS, module.Vars, conf);
            if (checkFeatureVersions.empty()) {
                return result;
            }
            const auto& moduleFeature2Versions = GetModuleFeature2Versions(module, conf);
            if (moduleFeature2Versions.empty()) { // no feature versions in peer, nothing to check, skip it
                YConfErr(Misconfiguration)
                    << "Usage " << CHECK_FEATURE_VERSION_CONFLICTS << " without FEATURE_VERSION" << Endl;
                return result;
            }

            result.Scopes |= ERequirementsScope::Peers;
            result.Check = [moduleFeature2Versions, &conf](TRestoreContext restoreContext, const TModule&, TNodeId, TScopeClosureRef closure) {
                for (TNodeId peerId : closure.Closure) {
                    TModule* peer = restoreContext.Modules.Get(restoreContext.Graph[peerId]->ElemId);
                    Y_ASSERT(peer);

                    // Check feature versions exists in peer
                    const auto& peerFeature2Versions = GetModuleFeature2Versions(*peer, conf);
                    if (peerFeature2Versions.empty()) {
                        continue; // no feature versions in peer, skip it
                    }

                    for (auto [feature, moduleVersion] :moduleFeature2Versions) {
                        const auto featureIt = peerFeature2Versions.find(feature);
                        if (featureIt.IsEnd()) {
                            continue; // feature absent in peer, nothing to check
                        }
                        const auto peerVersion = featureIt->second;
                        if (moduleVersion != peerVersion) {
                            YConfErr(BadDep)
                                << "Invalid feature " << feature << " version " << peerVersion
                                << " != " << moduleVersion << " in peer " << peer->GetDir().CutAllTypes() << Endl;
                        }
                    }
                }
            };
            return result;
        }

    private:

        using TFeature2Version = THashMap<TStringBuf, TStringBuf>;
        using TModuleId = ui32;

        class TCache {
        public:
            TCache()
                : Module2Features(new TModule2Features())
                , StringsSet(new TStringBufSet())
                , StringsPool(new TMemoryPool(8 * 1024))
            {};

            ~TCache() = default;

            inline const TFeature2Version* Set(TModuleId moduleId, TFeature2Version&& moduleFeature2Version) {
                auto [emplaceIt, ok] = Module2Features->emplace(moduleId, std::move(moduleFeature2Version));
                Y_ASSERT(ok);
                return &emplaceIt->second;
            }

            const TFeature2Version* Get(TModuleId moduleId) {
                auto moduleIt = Module2Features->find(moduleId);
                if (moduleIt.IsEnd()) {
                    return nullptr;
                }
                return &moduleIt->second;
            }

            // Convert TStringBuf to same content local TStringBuf with livetime same as TCache
            TStringBuf LocalBuf(TStringBuf buf) {
                auto localBufIt = StringsSet->find(buf);
                if (!localBufIt.IsEnd()) {
                    return *localBufIt;
                }
                auto localData = StringsPool->Append(buf.data(), buf.size()); // add new buf to end of local strings copy
                // Get new local buffer with same content from local copy
                TStringBuf localBuf(localData, buf.size());
                auto [emplaceIt, ok] = StringsSet->emplace(localBuf); // remember it
                Y_ASSERT(ok);
                return *emplaceIt;
            }

            inline const TFeature2Version* EmptyFeature2Version() {
                return &ConstEmptyFeature2Version;
            }

        private:
            using TModule2Features = THashMap<TModuleId, TFeature2Version>;
            using TStringBufSet = THashSet<TStringBuf>;
            THolder<TModule2Features> Module2Features;
            THolder<TStringBufSet> StringsSet;
            THolder<TMemoryPool> StringsPool;
            const TFeature2Version ConstEmptyFeature2Version;
        };

        // Holder of cache of feature2version by modules
        static THolder<TCache>& CacheHolder() {
            return *Singleton<THolder<TCache>>();
        }

        // Get cache of feature2version by modules from holder
        static TCache& Cache() {
            auto& cacheHolder = CacheHolder();
            auto* cache = cacheHolder.Get();
            Y_ASSERT(cache != nullptr); // must exists at this place
            return *cache;
        }

        static const TFeature2Version& GetModuleFeature2Versions(const TModule& module, const TBuildConfiguration& conf) {
            auto& cache = Cache();
            const TFeature2Version* inCache = cache.Get(module.GetId());
            if (inCache) {
                return *inCache;
            }
            const TString featureVersions = EvalVariable(FEATURE_VERSIONS, module.Vars, conf);
            if (featureVersions.empty()) {
                return *cache.EmptyFeature2Version();
            }
            TFeature2Version feature2version;
            for (TStringBuf featureVersion : SplitBySpace(featureVersions)) {
                TStringBuf feature;
                TStringBuf version;
                StringSplitter(featureVersion).Split('=').CollectInto(&feature, &version);
                if (feature.empty()) {
                    YConfErr(Misconfiguration)
                        << "Invalid " << FEATURE_VERSIONS << " empty feature name declaration '" << featureVersion << "' "
                        << "in module " << module.GetDir().CutAllTypes() << Endl;
                }
                if (version.empty()) {
                    YConfErr(Misconfiguration)
                        << "Invalid " << FEATURE_VERSIONS << " empty feature version declaration '" << featureVersion << "' "
                        << "in module " << module.GetDir().CutAllTypes() << Endl;
                }
                auto featureIt = feature2version.find(feature);
                if (!featureIt.IsEnd()) {
                    YConfWarn(Misconfiguration)
                        << "In " << FEATURE_VERSIONS << " " << feature
                        << " version " << featureIt->second << " overwrited by version " << version
                        << " in module " << module.GetDir().CutAllTypes() << Endl;
                }

                // For cache use only local TStringBuf with livetime same as Cache
                feature2version[cache.LocalBuf(feature)] = cache.LocalBuf(version);
            }
            return *cache.Set(module.GetId(), std::move(feature2version));
        }

    public:

        class TCacheOnStack {
        public:
            TCacheOnStack() {
                auto& cacheHolder = TFeatureVersionsLoader::CacheHolder();
                cacheHolder.Reset(MakeHolder<TCache>());
            }

            ~TCacheOnStack() {
                auto& cacheHolder = TFeatureVersionsLoader::CacheHolder();
                cacheHolder.Destroy();
            }
        };
    };

    const TTransitiveCheckRegistryItem TRANSITIVE_CHECK_REGISTRY_ARRAY[] = {
        {TRestrictLicensesLoader::CONF_VARS, TRestrictLicensesLoader::Create},
        {TCheckDependentDirsLoader::CONF_VARS, TCheckDependentDirsLoader::Create},
        {TRequiresLoader::CONF_VARS, TRequiresLoader::Create},
        {TFeatureVersionsLoader::CONF_VARS, TFeatureVersionsLoader::Create},
    };
}

void CheckGoTestIncorrectDep(TModule* module,const TRestoreContext& restoreContext) {
    if (!module->NeedGoDepsCheck()) {
        return;
    }
    TScopedContext context(module->GetName());

    auto testedNode = restoreContext.Graph.GetFileNode(module->Get("GO_TEST_FOR_DIR"));
    if (!testedNode.IsValid())
        return;
    auto testNode = restoreContext.Graph.GetFileNodeById(module->GetId());

    auto selfPeers = restoreContext.Modules.GetModuleNodeIds(testNode->ElemId).LocalPeers;
    TNodeId target = TNodeId::Invalid;

    for (const auto& edge: testedNode.Edges()) {
        if (IsModuleType(edge.To()->NodeType) && restoreContext.Modules.Get(edge.To()->ElemId)->IsGoModule()) {
            Y_ASSERT(target == TNodeId::Invalid);
            target = edge.To().Id();
        }
    }
    Y_ASSERT(target != TNodeId::Invalid);

    for (TNodeId selfPeer: selfPeers) {
        if(selfPeer == target) {
            continue;
        }
        for (TNodeId peer: EffectivePeersClosure(restoreContext, *restoreContext.Modules.Get(
                restoreContext.Graph[selfPeer]->ElemId))) {
            if (peer == target) {
                YConfErr(BadDep)
                << "go test issue: transitive dependencies on modules under test are prohibited in Arcadia due to scalability issues"
                << Endl
                << "See https://github.com/golang/go/issues/29258 and st/YMAKE-102 for more details"
                << Endl
                << TDependencyPathFormatter<ERequirementsScope::Peers>{restoreContext, selfPeer, peer, false}
                << Endl;
            }
        }
    }
}

void CheckTransitiveRequirements(const TRestoreContext& restoreContext, const TVector<TTarget>& startTargets) {
    FORCE_TRACE(U, NEvent::TStageStarted("Check Transitive Requirements"));
    TConstraintsChecker checker{restoreContext};
    auto cacheOnStack = MakeHolder<TFeatureVersionsLoader::TCacheOnStack>(); // Cache for feature versions in stack
    for (TTarget target : startTargets) {
        if (!target.IsModuleTarget) {
            continue;
        }

        auto startModuleNode = restoreContext.Graph.Get(target.Id);
        TModuleRestorer restorer{restoreContext, startModuleNode};
        TModule* startModule = restorer.RestoreModule();
        Y_ASSERT(startModule);
        const auto peers = restorer.GetPeers().Data();
        checker.Check(*startModule, startModuleNode.Id());

        CheckGoTestIncorrectDep(startModule,restoreContext);

        for (TNodeId peer : peers) {
            const auto node = restoreContext.Graph.Get(peer);
            TModule* module = restoreContext.Modules.Get(node->ElemId);
            Y_ASSERT(module);
            if (!module->IsPeersComplete() && !module->GetAttrs().RequireDepManagement) {
                TModuleRestorer peerRestorer{restoreContext, node};
                peerRestorer.RestoreModule();
                peerRestorer.GetPeers();
            }
            checker.Check(*module, peer);
        }
    }
    FORCE_TRACE(U, NEvent::TStageFinished("Check Transitive Requirements"));
}

const TArrayRef<const TTransitiveCheckRegistryItem> TRANSITIVE_CHECK_REGISTRY{TRANSITIVE_CHECK_REGISTRY_ARRAY};

void DoDumpLicenseInfo(const TBuildConfiguration& conf, const TVars& globals, NSPDX::EPeerType peerType, bool humanReadable, TArrayRef<TString> tagVars) {
    TRestrictLicensesLoader loader{globals};
    loader.LoadLicenses(conf);
    TMap<TStringBuf, TSet<TString>> orderdedPropsContent;
    for (const auto& [lic, props]: loader.GetLicenses()) {
        for (TStringBuf propName: loader.GetPropNames(props.GetProps(peerType))) {
            orderdedPropsContent[propName].insert(lic);
        }
    }

    for (TStringBuf tag: tagVars) {
        NSPDX::ForEachLicense(GetPropertyValue(globals.Get1(tag)), [&](TStringBuf lic) {
            orderdedPropsContent[tag].insert(TString{lic});
        });
    }

    if (humanReadable) {
        for (const auto& [prop, licenses]: orderdedPropsContent) {
            Cout << prop << Endl;
            for (TStringBuf lic: licenses) {
                Cout << '\t' << lic << Endl;
            }
        }
    } else {
        NJsonWriter::TBuf writer{NJsonWriter::HEM_DONT_ESCAPE_HTML, &Cout};
        writer.BeginObject();
        for (const auto& [prop, licenses]: orderdedPropsContent) {
            writer.WriteKey(prop);
            writer.BeginList();
            for (TStringBuf lic: licenses) {
                writer.WriteString(lic);
            }
            writer.EndList();
        }
        writer.EndObject();
    }
}
