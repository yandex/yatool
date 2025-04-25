#include "project.h"
#include "spec_based_generator.h"

#include <devtools/yexport/diag/exception.h>

namespace NYexport {

    bool TProjectSubdir::IsTopLevel() const {
        return !Path.has_parent_path();
    }

    const TVector<TProjectSubdirPtr>& TProject::GetSubdirs() const {
        return Subdirs_;
    }

    TVector<TProjectSubdirPtr>& TProject::GetSubdirs() {
        return Subdirs_;
    }

    TProjectSubdirPtr TProject::GetSubdir(std::string_view path) const {
        const auto it = PathToSubdir_.find(path);
        if (it != PathToSubdir_.end()) {
            return it->second;
        }
        return nullptr;
    }

    void TProject::Reset() {
        Factory_ = nullptr;
        PathToSubdir_.clear();
        Subdirs_.clear();
    }

    TProject::TBuilder::TBuilder(TSpecBasedGenerator* generator) : Generator_(generator) {
        YEXPORT_VERIFY(Generator_, "Invalid generator in project builder");
    }

    TProjectPtr TProject::TBuilder::Finalize() {
        YEXPORT_VERIFY(Project_, "No active project to finish");
        CustomFinalize();
        auto project = Project_;
        Project_ = nullptr;
        CurSubdir_ = nullptr;
        CurTarget_ = nullptr;
        return project;
    }

    TProject::TBuilder::TTargetHolder TProject::TBuilder::CreateTarget(const fs::path& targetDir) {
        YEXPORT_VERIFY(Project_->Factory_, "Creating project target without factory");
        auto dir = CreateDirectories(targetDir);
        auto target = Project_->Factory_->CreateTarget();
        // We can't create target attributes here, because for attributes hint require Macro and Name, which absent at this moment
        dir->Targets.push_back(target);
        return {*this, dir, target};
    }

    void TProject::TBuilder::OnAttribute(const std::string& attrName, const std::span<const std::string>& attrValue) {
        Generator_->OnAttribute(attrName, attrValue);
        CustomOnAttribute(attrName, attrValue);
    }

    TProjectSubdirPtr TProject::TBuilder::CreateDirectories(const fs::path& dir) {
        YEXPORT_VERIFY(Project_->Factory_, "Creating project directory without factory");
        auto& subdirs = Project_->PathToSubdir_;
        auto& subdirsOrder = Project_->Subdirs_;
        auto currDir = dir;

        TVector<TProjectSubdirPtr> createDirectories;
        while (!subdirs.contains(currDir)) {
            auto subdir = Project_->Factory_->CreateSubdir();
            subdir->Path = currDir;
            subdir->Attrs = Generator_->MakeAttrs(EAttrGroup::Directory, "dir " + currDir.string());
            subdirs[currDir] = subdir;
            subdirsOrder.push_back(subdir);
            createDirectories.push_back(subdir);
            if (!currDir.has_parent_path()) {
                break;
            }
            currDir = currDir.parent_path();
        }
        auto currDirPtr = subdirs.at(currDir);
        if (!createDirectories.empty() && currDirPtr != createDirectories.back()) {
            createDirectories.push_back(currDirPtr);
        }
        for (size_t i = 0; i + 1 < createDirectories.size(); ++i) {
            createDirectories[i + 1]->Subdirs.push_back(createDirectories[i]);
        }

        return subdirs.at(dir);
    }

    const TProjectTarget* TProject::TBuilder::CurrentTarget() const noexcept {
        return CurTarget_.Get();
    }

    const TProjectSubdir* TProject::TBuilder::CurrentSubdir() const noexcept {
        return CurSubdir_.Get();
    }

    TProjectTarget* TProject::TBuilder::CurrentTarget() noexcept {
        return CurTarget_.Get();
    }

    TProjectSubdir* TProject::TBuilder::CurrentSubdir() noexcept {
        return CurSubdir_.Get();
    }

    TProject::TBuilder::TTargetHolder::TTargetHolder(TTargetHolder&& other) noexcept
        : Builder_{std::exchange(other.Builder_, nullptr)}
        , PrevSubdir_{other.PrevSubdir_}
        , PrevTarget_(other.PrevTarget_)
    {
    }

    TProject::TBuilder::TTargetHolder& TProject::TBuilder::TTargetHolder::operator=(TTargetHolder&& other) noexcept {
        if (Builder_) {
            Builder_->CurTarget_ = PrevTarget_;
            Builder_->CurSubdir_ = PrevSubdir_;
        }
        Builder_ = std::exchange(other.Builder_, nullptr);
        PrevSubdir_ = other.PrevSubdir_;
        PrevTarget_ = other.PrevTarget_;
        return *this;
    }

    TProject::TBuilder::TTargetHolder::TTargetHolder(TBuilder& builder, TProjectSubdirPtr subdir, TProjectTargetPtr target) noexcept
        : Builder_(&builder)
    {
        PrevSubdir_ = std::exchange(builder.CurSubdir_, subdir);
        PrevTarget_ = std::exchange(builder.CurTarget_, target);
    }

    TProject::TBuilder::TTargetHolder::~TTargetHolder() noexcept {
        if (Builder_) {
            Builder_->CurTarget_ = PrevTarget_;
            Builder_->CurSubdir_ = PrevSubdir_;
        }
    }

}
