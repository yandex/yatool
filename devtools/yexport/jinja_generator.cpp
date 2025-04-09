#include "jinja_generator.h"
#include "read_sem_graph.h"
#include "graph_visitor.h"
#include "internal_attributes.h"

#include <devtools/ymake/compact_graph/query.h>
#include <devtools/ymake/common/uniq_vector.h>

#include <library/cpp/json/json_reader.h>
#include <library/cpp/resource/resource.h>

#include <util/generic/algorithm.h>
#include <util/generic/set.h>
#include <util/generic/yexception.h>
#include <util/generic/map.h>
#include <util/string/type.h>
#include <util/system/fs.h>

#include <contrib/libs/jinja2cpp/include/jinja2cpp/template.h>
#include <contrib/libs/jinja2cpp/include/jinja2cpp/template_env.h>

#include <vector>
#include <regex>

namespace NYexport {

TJinjaProject::TJinjaProject() {
    SetFactoryTypes<TProjectSubdir, TProjectTarget>();
}

TJinjaProject::TBuilder::TBuilder(TJinjaGenerator* generator, TAttrsPtr rootAttrs)
    : TProject::TBuilder(generator)
    , RootAttrs(rootAttrs)
    , Generator(generator)
{
    Project_ = MakeSimpleShared<TJinjaProject>();
}

static bool AddValueToJinjaList(jinja2::ValuesList& list, const jinja2::Value& value) {
    if (value.isList()) {
        // Never create list of lists, if value also list, concat all lists to one big list
        for (const auto& v: value.asList()) {
            list.emplace_back(v);
        }
    } else {
        list.emplace_back(value);
    }
    return true;
}

bool TJinjaProject::TBuilder::AddToTargetInducedAttr(const std::string& attrName, const jinja2::Value& value, const std::string& nodePath) {
    if (!CurTarget_) {
        spdlog::warn("attempt to add target attribute '{}' while there is no active target at node {}", attrName, nodePath);
        return false;
    }
    auto& attrs = CurTarget_->Attrs->GetWritableMap();
    auto [listAttrIt, _] = attrs.emplace(attrName, jinja2::ValuesList{});
    auto& list = listAttrIt->second.asList();
    if (ValueInList(list, value)) {
        return true; // skip adding fully duplicate induced attributes
    }
    return AddValueToJinjaList(list, value);
}

void TJinjaProject::TBuilder::OnAttribute(const std::string& attrName, const std::span<const std::string>& attrValue) {
    Generator->OnAttribute(attrName, attrValue);
}

const TNodeSemantics& TJinjaProject::TBuilder::ApplyReplacement(TPathView path, const TNodeSemantics& inputSem) const {
    return Generator->ApplyReplacement(path, inputSem);
}

bool TJinjaProject::TBuilder::ValueInList(const jinja2::ValuesList& list, const jinja2::Value& value) {
    if (list.empty()) {
        return false;
    }
    TStringBuilder valueStr;
    TStringBuilder listValueStr;
    Dump(valueStr.Out, value);
    for (const auto& listValue : list) {
        listValueStr.clear();
        Dump(listValueStr.Out, listValue);
        if (valueStr == listValueStr) {
            return true;
        }
    }
    return false;
}

class TJinjaGeneratorVisitor: public TGraphVisitor {
public:
    TJinjaGeneratorVisitor(TJinjaGenerator* generator, TAttrsPtr rootAttrs, const TGeneratorSpec& generatorSpec)
        : TGraphVisitor(generator)
    {
        JinjaProjectBuilder_ = MakeSimpleShared<TJinjaProject::TBuilder>(generator, rootAttrs);
        ProjectBuilder_ = JinjaProjectBuilder_;
        if (const auto* tests = generatorSpec.Merge.FindPtr("test")) {
            for (const auto& item : *tests) {
                TestSubdirs_.push_back(item.c_str());
            }
        }
    }

    std::optional<bool> OnEnter(TState& state) override {
        if (state.TopNode()->NodeType != EMNT_File) {
            return {};
        }
        return AddCopyIfNeed(state.TopNode().Value().Path);
    }

    void OnTargetNodeSemantic(TState& state, const std::string& semName, const std::span<const std::string>& semArgs) override {
        bool isTestTarget = false;
        std::string modDir = semArgs[0].c_str();
        for (const auto& testSubdir: TestSubdirs_) {
            if (modDir != testSubdir && modDir.ends_with(testSubdir)) {
                modDir.resize(modDir.size() - testSubdir.size());
                isTestTarget = true;
                break;
            }
        }
        auto macroArgs = semArgs.subspan(2);
        state.Top().CurTargetHolder = ProjectBuilder_->CreateTarget(modDir);
        Y_ASSERT(ProjectBuilder_->CurrentSubdir()->Attrs); // Attributes for all directories must be created at TProject::TBuilder::CreateDirectories (in CreateTarget)
        auto* curTarget = ProjectBuilder_->CurrentTarget();
        if (isTestTarget) {
            curTarget->TestModDir = modDir;
        }
        curTarget->Macro = semName;
        curTarget->Name = semArgs[1];
        curTarget->Attrs = Generator_->MakeAttrs(EAttrGroup::Target, "target " + curTarget->Macro + " " + curTarget->Name, Generator_->GetToolGetter(), true);
        auto& attrs = curTarget->Attrs->GetWritableMap();
        NInternalAttrs::EmplaceAttr(attrs, NInternalAttrs::Macro, curTarget->Macro);
        NInternalAttrs::EmplaceAttr(attrs, NInternalAttrs::Name, curTarget->Name);
        if (!macroArgs.empty()) {
            curTarget->MacroArgs = {macroArgs.begin(), macroArgs.end()};
            NInternalAttrs::EmplaceAttr(attrs, NInternalAttrs::MacroArgs, jinja2::ValuesList(curTarget->MacroArgs.begin(), curTarget->MacroArgs.end()));
        }
        NInternalAttrs::EmplaceAttr(attrs, NInternalAttrs::IsTest, isTestTarget);
        const auto* jinjaTarget = dynamic_cast<const TProjectTarget*>(JinjaProjectBuilder_->CurrentTarget());
        Mod2Target_.emplace(state.TopNode().Id(), jinjaTarget);
    }

    void OnNodeSemanticPostOrder(TState& state, const std::string& semName, ESemNameType semNameType, const std::span<const std::string>& semArgs, bool isIgnored) override {
        const TSemNodeData& data = state.TopNode().Value();
        switch (semNameType) {
        case ESNT_RootAttr:
            JinjaProjectBuilder_->SetRootAttr(semName, semArgs, data.Path);
            break;
        case ESNT_PlatformAttr:
            JinjaProjectBuilder_->SetPlatformAttr(semName, semArgs, data.Path);
            break;
        case ESNT_DirectoryAttr:
            JinjaProjectBuilder_->SetDirectoryAttr(semName, semArgs, data.Path);
            break;
        case ESNT_TargetAttr:
            if (!isIgnored) {
                JinjaProjectBuilder_->SetTargetAttr(semName, semArgs, data.Path);
            }
            break;
        case ESNT_InducedAttr:
            StoreInducedAttrValues(state.TopNode().Id(), semName, semArgs, data.Path);
            break;
        case ESNT_Unknown:
            spdlog::warn("Skip unknown semantic '{}' for file '{}'", semName + (semArgs.empty() ? "" : " " + NYexport::join(semArgs)), data.Path);
            break;
        case ESNT_Ignored:
        case ESNT_Target:
            // skip in post-order flow
            break;
        }
    }

    void OnLeft(TState& state) override {
        const auto& dep = state.Top().CurDep();
        if (IsDirectPeerdirDep(dep)) {
            //Note: This part checks dependence of the test on the library in the same dir, because for java we should not distribute attributes
            bool isSameDir = false;
            const auto* toTarget = Mod2Target_[dep.To().Id()];
            if (auto* curSubdir = ProjectBuilder_->CurrentSubdir(); curSubdir) {
                for (const auto& target: curSubdir->Targets) {
                    if (target.Get() == toTarget) {
                        isSameDir = true;
                        break;
                    }
                }
            }
            const auto libIt = InducedAttrs_.find(dep.To().Id());
            if (!isSameDir && libIt != InducedAttrs_.end()) {
                const TSemNodeData& data = dep.To().Value();
/*
    Generating induced attributes and excludes< for example, project is

    prog --> lib1 with excluding ex1
         \-> lib2 with excluding ex2

    And toml is:

    [attrs.induced]
    consumer-classpath="str"
    consumer-jar="str"

    For prog must be attributes map:
    ...
    consumer: [
        {
            classpath: "lib1",
            jar: "lib1.jar",
            excludes: {
                consumer: [
                    {
                        classpath: "ex1",
                        jar: "ex1.jar",
                    }
                ],
            },
        },
        {
            classpath: "lib2",
            jar: "lib2.jar",
            excludes: {
                consumer: [
                    {
                        classpath: "ex2",
                        jar: "ex2.jar",
                    }
                ],
            },
        },
    ]
    ...
*/
                // Make excludes map for each induced library
                jinja2::ValuesMap excludes;
                // Extract excludes node ids from current dependence
                if (const TSemDepData* depData = reinterpret_cast<const TSemGraph&>(dep.Graph()).GetDepData(dep); depData) {
                    if (const auto excludesIt = depData->find(DEPATTR_EXCLUDES); excludesIt != depData->end()) {
                        TVector<TNodeId> excludeNodeIds;
                        TVector<TNodeId> excludeNodeIdsWOInduced;
                        Y_ASSERT(excludesIt->second.isList());
                        // Collect all excluded node ids and collect ids without induced attributes
                        for (const auto& excludeNodeVal : excludesIt->second.asList()) {
                            const auto excludeNodeId = static_cast<TNodeId>(excludeNodeVal.get<int64_t>());
                            excludeNodeIds.emplace_back(excludeNodeId);
                            if (!InducedAttrs_.contains(excludeNodeId)) {
                                excludeNodeIdsWOInduced.emplace_back(excludeNodeId);
                            }
                        }
                        if (!excludeNodeIdsWOInduced.empty()) { // visit all not visited exclude nodes for make induced attributes
                            IterateAll(reinterpret_cast<const TSemGraph&>(dep.Graph()), excludeNodeIdsWOInduced, *this);
                        }
                        // for each excluded node id get all it induced attrs
                        for (auto excludeNodeId : excludeNodeIds) {
                            const auto excludeIt = InducedAttrs_.find(excludeNodeId);
                            if (excludeIt != InducedAttrs_.end()) {
                                // Put all induced attrs of excluded library to lists by induced attribute name
                                for (const auto& [attrName, value] : excludeIt->second->GetMap()) {
                                    auto [listIt, _] = excludes.emplace(attrName, jinja2::ValuesList{});
                                    AddValueToJinjaList(listIt->second.asList(), value);
                                }
                            } else {
                                spdlog::warn("Not found induced for excluded node id {} at {}", ToUnderlying(excludeNodeId), data.Path);
                            }
                        }
                    }
                }
                const auto fromTarget = Mod2Target_[dep.From().Id()];
                bool isTestDep = fromTarget && toTarget && toTarget->IsTest();
                for (const auto& [attrName, value] : libIt->second->GetMap()) {
                    if (value.isMap() && (!excludes.empty() || isTestDep)) {
                        // For each induced attribute in map format add submap with excludes
                        jinja2::ValuesMap valueWithDepAttrs = value.asMap();
                        if (!excludes.empty()) {
                            NInternalAttrs::EmplaceAttr(valueWithDepAttrs, NInternalAttrs::Excludes, excludes);
                        }
                        if (isTestDep) {
                            NInternalAttrs::EmplaceAttr(valueWithDepAttrs, NInternalAttrs::Testdep, toTarget->TestModDir);
                        }
                        JinjaProjectBuilder_->AddToTargetInducedAttr(attrName, valueWithDepAttrs, data.Path);
                    } else {
                        JinjaProjectBuilder_->AddToTargetInducedAttr(attrName, value, data.Path);
                    }
                }
            }
        }
    }

private:
    template<IterableValues Values>
    void StoreInducedAttrValues(TNodeId nodeId, const std::string& attrName, const Values& values, const std::string& nodePath) {
        auto nodeIt = InducedAttrs_.find(nodeId);
        if (nodeIt == InducedAttrs_.end()) {
            auto [emplaceNodeIt, _] = InducedAttrs_.emplace(
                nodeId,
                Generator_->MakeAttrs(EAttrGroup::Induced, "induced by " + std::to_string(ToUnderlying(nodeId)))
            );
            nodeIt = emplaceNodeIt;
        }
        nodeIt->second->SetAttrValue(attrName, values, nodePath);
    }

    TSimpleSharedPtr<TJinjaProject::TBuilder> JinjaProjectBuilder_;

    THashMap<TNodeId, const TProjectTarget*> Mod2Target_;
    THashMap<TNodeId, TAttrsPtr> InducedAttrs_;
    TVector<std::string> TestSubdirs_;

    std::optional<bool> AddCopyIfNeed(const TStringBuf path) {
        static constexpr std::string_view ARCADIA_SCRIPTS_RELPATH = "build/scripts";
        static constexpr std::string_view EXPORT_SCRIPTS_RELPATH = "build/scripts";
        const auto relPath = NPath::CutAllTypes(path);
        if (NPath::IsPrefixOf(ARCADIA_SCRIPTS_RELPATH, relPath)) {
            Generator_->Copy(relPath.data(), fs::path(EXPORT_SCRIPTS_RELPATH) / NPath::Basename(relPath).data());
            return true;
        }
        return {};
    }
};

THolder<TJinjaGenerator> TJinjaGenerator::Load(const TOpts& opts) {
    const auto generatorDir = opts.ArcadiaRoot / GENERATORS_ROOT / opts.Generator;
    const auto generatorFile = generatorDir / GENERATOR_FILE;
    if (!fs::exists(generatorFile)) {
        YEXPORT_THROW(fmt::format("Failed to load generator {}, file {} not found", opts.Generator, generatorFile.c_str()));
    }
    THolder<TJinjaGenerator> result = MakeHolder<TJinjaGenerator>();
    result->DumpOpts_ = opts.DumpOpts;
    result->DebugOpts_ = opts.DebugOpts;
    result->GeneratorDir = generatorDir;
    result->ArcadiaRoot = opts.ArcadiaRoot;
    result->SetupJinjaEnv();
    result->SetSpec(ReadGeneratorSpec(generatorFile), generatorFile.string());
    result->YexportSpec = result->ReadYexportSpec(opts.ConfigDir);
    result->InitReplacer();
    result->InitToolGetter();
    return result;
}

void TJinjaGenerator::LoadSemGraph(const std::string& platformName, const fs::path& semGraph) {
    if (!platformName.empty()) {
        YEXPORT_VERIFY(GeneratorSpec.Platforms.find(platformName) != GeneratorSpec.Platforms.end(), fmt::format("No specification for platform \"{}\"", platformName));
    } else {
        YEXPORT_VERIFY(IgnorePlatforms(), "Empty platform name for generator without ignore platforms");
    }
    auto [graph, startDirs] = ReadSemGraph(semGraph, GeneratorSpec.UseManagedPeersClosure);
    auto& platform = *Platforms.emplace_back(MakeSimpleShared<TPlatform>(platformName));
    graph.Swap(platform.Graph);
    platform.StartDirs = std::move(startDirs);
    OnPlatform(platformName);
    if (!RootAttrs) { // first call, make root attributes storage
        RootAttrs = MakeAttrs(EAttrGroup::Root, "root");
    }
    platform.Project = AnalizeSemGraph(platform);
}

TProjectPtr TJinjaGenerator::AnalizeSemGraph(const TPlatform& platform) {
    TJinjaGeneratorVisitor visitor(this, RootAttrs, GeneratorSpec);
    visitor.FillPlatformName(platform.Name);
    ProjectBuilder_ = visitor.GetProjectBuilder();
    IterateAll(*platform.Graph, platform.StartDirs, visitor);
    return visitor.TakeFinalizedProject();
}

/// Get dump of attributes tree with values for testing or debug
void TJinjaGenerator::DumpSems(IOutputStream& out) const {
    const auto ignorePlatforms = IgnorePlatforms();
    for (const auto& platform: Platforms) {
        if (!ignorePlatforms) {
            out << "--- PLATFORM " << platform->Name << "\n";
        }
        if (DumpOpts_.DumpPathPrefixes.empty()) {
            out << "--- ROOT\n" << platform->Project->SemsDump;
        }
        for (const auto& subdir: platform->Project->GetSubdirs()) {
            if (subdir->Targets.empty()) {
                continue;
            }
            const std::string path = subdir->Path;
            if (!DumpOpts_.DumpPathPrefixes.empty()) {
                bool dumpIt = false;
                for (const auto& prefix : DumpOpts_.DumpPathPrefixes) {
                    if (path.substr(0, prefix.size()) == prefix) {
                        dumpIt = true;
                        break;
                    }
                }
                if (!dumpIt) {
                    continue;
                }
            }
            out << "--- DIR " << path << "\n";
            for (const auto& target: subdir->Targets) {
                out << "--- TARGET " << target->Name << "\n" << target->SemsDump;
            }
        }
    }
}

/// Get dump of attributes tree with values for testing or debug
void TJinjaGenerator::DumpAttrs(IOutputStream& out) {
    ::NYexport::Dump(out, FinalizeAttrsForDump());
}

void TJinjaGenerator::Render(ECleanIgnored cleanIgnored) {
    for (auto& platform : Platforms) {
        RenderPlatform(platform, cleanIgnored);
    }
    CopyFilesAndResources();
    if (!IgnorePlatforms()) {
        MergePlatforms();
    }
    RenderRoot();
    if (cleanIgnored == ECleanIgnored::Enabled) {
        Cleaner.Clean(*ExportFileManager_);
    }
}

void TJinjaGenerator::RenderPlatform(TPlatformPtr platform, ECleanIgnored cleanIgnored) {
    if (cleanIgnored == ECleanIgnored::Enabled) {
        Cleaner.CollectDirs(*platform->Graph, platform->StartDirs);
    }

    FinalizeSubdirsAttrs(platform);
    auto rootSubdirs = jinja2::ValuesList{};
    for (auto subdir: platform->Project->GetSubdirs()) {
        Y_ASSERT(subdir);
        if (subdir->IsTopLevel()) {
            rootSubdirs.emplace_back(subdir->Path.filename());
        }
        RenderSubdir(platform, subdir);
    }

    if (TargetTemplates.contains(EMPTY_TARGET)) {// root of export always without targets
        TAttrsPtr rootdirAttrs = MakeAttrs(EAttrGroup::Directory, "rootdir");
        const auto [_, inserted] = NInternalAttrs::EmplaceAttr(rootdirAttrs->GetWritableMap(), NInternalAttrs::Subdirs, rootSubdirs);
        Y_ASSERT(inserted);
        if (!IgnorePlatforms()) {
            InsertPlatformNames(rootdirAttrs, Platforms);
            InsertPlatformConditions(rootdirAttrs);
        }
        CommonFinalizeAttrs(rootdirAttrs, YexportSpec.AddAttrsDir);
        SetCurrentDirectory(ArcadiaRoot);
        RenderJinjaTemplates(rootdirAttrs, TargetTemplates[EMPTY_TARGET], "", platform->Name);
    }
}

void TJinjaGenerator::RenderRoot() {
    ApplyRules(RootAttrs);
    FinalizeRootAttrs();
    SetCurrentDirectory(ArcadiaRoot);
    RenderJinjaTemplates(RootAttrs, RootTemplates);
}

void TJinjaGenerator::RenderSubdir(TPlatformPtr platform, TProjectSubdirPtr subdir) {
    const auto& macroForTemplate = GetMacroForTemplate(*subdir);
    SetCurrentDirectory(ArcadiaRoot / subdir->Path);
    if (TargetTemplates.contains(macroForTemplate)) {
        RenderJinjaTemplates(subdir->Attrs, TargetTemplates[macroForTemplate], subdir->Path, platform->Name);
    } else if (macroForTemplate != EMPTY_TARGET and macroForTemplate != EXTRA_ONLY_TARGET) {
        spdlog::error("Skip render directory {}, has no templates for main target {}", subdir->Path.string(), macroForTemplate);
    }
}

const jinja2::ValuesMap& TJinjaGenerator::FinalizeRootAttrs() {
    YEXPORT_VERIFY(!Platforms.empty(), "Cannot finalize root attrs because project was not yet loaded");
    auto& attrs = RootAttrs->GetWritableMap();
    if (attrs.contains(NInternalAttrs::ProjectName)) { // already finalized
        return attrs;
    }
    NInternalAttrs::EmplaceAttr(attrs, NInternalAttrs::ProjectName, ProjectName);
    std::unordered_set<std::string> existsSubdirs;
    auto [subdirsIt, subdirsInserted] = NInternalAttrs::EmplaceAttr(attrs, NInternalAttrs::Subdirs, jinja2::ValuesList{});
    auto& subdirs = subdirsIt->second.asList();
    for (const auto& platform : Platforms) {
        for (const auto& subdir: platform->Project->GetSubdirs()) {
            if (subdir->Targets.empty()) {
                continue;
            }
            auto subdirPath = subdir->Path.string();
            if (!existsSubdirs.contains(subdirPath)) {
                subdirs.emplace_back(subdirPath);
                existsSubdirs.insert(subdirPath);
            }
        }
    }
    if (!IgnorePlatforms()) {
        InsertPlatformNames(RootAttrs, Platforms);
        InsertPlatformConditions(RootAttrs);
        InsertPlatformAttrs(RootAttrs);
    }
    CommonFinalizeAttrs(RootAttrs, YexportSpec.AddAttrsRoot);
    if (DebugOpts_.DebugSems) {
        auto& map = RootAttrs->GetWritableMap();
        auto [debugSemsIt, _] = NInternalAttrs::EmplaceAttr(map, NInternalAttrs::DumpSems, "");
        auto& debugSems = debugSemsIt->second.asString();
        for (const auto& platform : Platforms) {
            debugSems += platform->Project->SemsDump;
        }
    }
    return RootAttrs->GetMap();
}

jinja2::ValuesMap TJinjaGenerator::FinalizeSubdirsAttrs(TPlatformPtr platform, const std::vector<std::string>& pathPrefixes) {
    YEXPORT_VERIFY(!Platforms.empty(), "Cannot finalize subdirs attrs because project was not yet loaded");
    jinja2::ValuesMap subdirsAttrs;
    auto platformSuf = [&]() {
        return platform->Name.empty() ? std::string{} : " [ platform " + platform->Name + " ]";
    };
    for (auto& dir: platform->Project->GetSubdirs()) {
        if (!pathPrefixes.empty()) {
            const std::string path = dir->Path;
            bool finalizeIt = false;
            for (const auto& prefix : pathPrefixes) {
                if (path.substr(0, prefix.size()) == prefix) {
                    finalizeIt = true;
                    break;
                }
            }
            if (!finalizeIt) {
                continue;
            }
        }
        Y_ASSERT(dir->Attrs); // Attributes for all directories must be created at TProject::TBuilder::CreateDirectories
        auto& dirMap = dir->Attrs->GetWritableMap();
        NInternalAttrs::EmplaceAttr(dirMap, NInternalAttrs::Curdir, dir->Path.string());
        if (!dir->Subdirs.empty()) {
            auto [subdirsIt, _] = NInternalAttrs::EmplaceAttr(dirMap, NInternalAttrs::Subdirs, jinja2::ValuesList{});
            auto& subdirs = subdirsIt->second.asList();
            for (const auto& subdir : dir->Subdirs) {
                subdirs.emplace_back(subdir->Path.filename());
            }
        }
        TStringBuilder semsDump;
        if (DebugOpts_.DebugSems) {
            semsDump << dir->SemsDump;
        }
        for (auto& target: dir->Targets) {
            Y_ASSERT(target->Attrs);
            CommonFinalizeAttrs(target->Attrs, YexportSpec.AddAttrsTarget, false);
            if (DebugOpts_.DebugSems) {
                semsDump << "\n\n--- TARGET " << target->Name << "\n" << target->SemsDump;
            }
            const auto& targetMacro = target->Macro;
            Y_ASSERT(GeneratorSpec.Targets.contains(targetMacro));
            Y_ASSERT(TargetTemplates.contains(targetMacro));
            const auto& targetSpec = GeneratorSpec.Targets[targetMacro];
            auto isTest = targetSpec.IsTest || target->IsTest();
            if (isTest) {
                auto [hasTestIt, _] = NInternalAttrs::EmplaceAttr(dirMap, NInternalAttrs::HasTest, true);
                hasTestIt->second = true; // emulate insert or_assign
            }
            auto isExtraTarget = targetSpec.IsExtraTarget || isTest;// test always is extra target
            const auto& targetAttrs = target->Attrs->GetMap();
            if (isExtraTarget) {
                auto& extra_targets = NInternalAttrs::EmplaceAttr(dirMap, NInternalAttrs::ExtraTargets, jinja2::ValuesList{}).first->second.asList();
                extra_targets.emplace_back(targetAttrs);
            } else {
                if (dirMap.contains(NInternalAttrs::Target)) {// main target already exists
                    const auto curTargetName = dirMap[NInternalAttrs::Target].asMap().at(NInternalAttrs::Name).asString();
                    const auto newTargetName = targetAttrs.at(NInternalAttrs::Name).asString();
                    if (curTargetName <= newTargetName) {
                        spdlog::error("Skip main target {}, already exists main target {} at {}", newTargetName, curTargetName, dir->Path.string() + platformSuf());
                    } else {
                        spdlog::error("Overwrote main target {} by main target {} at {}", curTargetName, newTargetName, dir->Path.string() + platformSuf());
                        dirMap.insert_or_assign(NInternalAttrs::Target, targetAttrs);
                        dir->MainTargetMacro = targetMacro;
                    }
                } else {
                    dirMap.emplace(NInternalAttrs::Target, targetAttrs);
                    dir->MainTargetMacro = targetMacro;
                }
            }
        }
        CommonFinalizeAttrs(dir->Attrs, YexportSpec.AddAttrsDir);
        if (DebugOpts_.DebugSems) {
            NInternalAttrs::EmplaceAttr(dirMap, NInternalAttrs::DumpSems, std::string{semsDump});
        }
        if (dir->MainTargetMacro.empty() && !dir->Targets.empty()) {
            spdlog::warn("Only {} extra targets without main target in directory {}", dir->Targets.size(), dir->Path.string() + platformSuf());
        }
        subdirsAttrs.emplace(dir->Path.c_str(), dirMap);
    }
    return subdirsAttrs;
}

jinja2::ValuesMap TJinjaGenerator::FinalizeAttrsForDump() {
    // disable creating debug template attributes
    DebugOpts_.DebugSems = false;
    DebugOpts_.DebugAttrs = false;
    jinja2::ValuesMap allAttrs;
    NInternalAttrs::EmplaceAttr(allAttrs, NInternalAttrs::RootAttrs, FinalizeRootAttrs());
    if (IgnorePlatforms()) {
        NInternalAttrs::EmplaceAttr(allAttrs, NInternalAttrs::Subdirs, FinalizeSubdirsAttrs(Platforms[0], DumpOpts_.DumpPathPrefixes));
    } else {
        for (auto& platform : Platforms) {
            auto platformSubdirs = FinalizeSubdirsAttrs(platform, DumpOpts_.DumpPathPrefixes);
            if (!platformSubdirs.empty()) {
                auto [allSubdirsIt, _] = NInternalAttrs::EmplaceAttr(allAttrs, NInternalAttrs::Subdirs, jinja2::ValuesMap{});
                allSubdirsIt->second.asMap().emplace(platform->Name, std::move(platformSubdirs));
            }
        }
    }
    return allAttrs;
}

THashMap<fs::path, TVector<TProjectTarget>> TJinjaGenerator::GetSubdirsTargets() const {
    YEXPORT_VERIFY(!Platforms.empty(), "Cannot get subdirs table because project was not yet loaded");
    THashMap<fs::path, TVector<TProjectTarget>> res;
    for (const auto& platform : Platforms) {
        for (const auto& subdir: platform->Project->GetSubdirs()) {
            TVector<TProjectTarget> targets;
            Transform(subdir->Targets.begin(), subdir->Targets.end(), std::back_inserter(targets), [&](TProjectTargetPtr target) {
                return *target.As<TProjectTarget>();
            });
            auto subdirIt = res.find(subdir->Path);
            if (subdirIt == res.end()) {
                res.emplace(subdir->Path, std::move(targets));
            } else {
                subdirIt->second.insert(subdirIt->second.end(), targets.begin(), targets.end());
            }
        }
    }
    return res;
}

const std::string& TJinjaGenerator::ToolGetter(const std::string& s) const {
    ToolGetterBuffer_.clear();
    do {
        Y_ASSERT(ProjectBuilder_);
        auto* currentSubdir = ProjectBuilder_->CurrentSubdir();
        if (!currentSubdir) {
            break;// no subdir (and tools in it too)
        }
        if (s.find('/') == std::string::npos) {
            break;// can't be tool, must be path
        }
        const auto& subdirAttrs = currentSubdir->Attrs->GetMap();
        const auto subdirToolsIt = subdirAttrs.find(NInternalAttrs::Tools);
        if (subdirToolsIt == subdirAttrs.end()) {
            break;// no tools attribute in subdir, s can't be tool
        }
        const auto& subdirTools = subdirToolsIt->second.asList();
        if (subdirTools.empty()) {
            break;// empty tools in subdir, s can't be tool
        }
        std::string maybeTool = s;
        static const std::regex SOURCE_ROOT_RE("^\\$S/");
        maybeTool = std::regex_replace(maybeTool, SOURCE_ROOT_RE, "");
        static const std::regex BINARY_ROOT_RE("^\\$B/");
        maybeTool = std::regex_replace(maybeTool, BINARY_ROOT_RE, "");
        static const std::regex ESCAPING_RE("(\\$|\\{|\\})");
        static const std::string ESCAPING_REPLACE("\\$1");
        if (!GeneratorSpec.SourceRootReplacer.empty()) {
            maybeTool = std::regex_replace(maybeTool, std::regex(std::string{"^"} + std::regex_replace(GeneratorSpec.SourceRootReplacer, ESCAPING_RE, ESCAPING_REPLACE) + "/"), "");
        }
        if (!GeneratorSpec.BinaryRootReplacer.empty()) {
            maybeTool = std::regex_replace(maybeTool, std::regex(std::string{"^"} + std::regex_replace(GeneratorSpec.BinaryRootReplacer, ESCAPING_RE, ESCAPING_REPLACE) + "/"), "");
        }
        if (std::find(subdirTools.begin(), subdirTools.end(), jinja2::Value{std::string{maybeTool}}) == subdirTools.end()) {
            break;// not tool
        }
        ToolGetterBuffer_ = maybeTool;
    } while (0);
    return ToolGetterBuffer_;
}

void TJinjaGenerator::InitToolGetter() {
    static TAttrs::TReplacer TOOL_GETTER([this](const std::string& s) -> const std::string& {
        return ToolGetter(s);
    });
    ToolGetter_ = &TOOL_GETTER;
}

}
