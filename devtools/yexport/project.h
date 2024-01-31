#pragma once

#include "std_helpers.h"
#include "spec_based_generator.h"

#include <contrib/libs/jinja2cpp/include/jinja2cpp/value.h>

#include <util/generic/hash_table.h>
#include <util/generic/vector.h>
#include <util/generic/ptr.h>

#include <string>
#include <filesystem>

namespace NYexport {

    class TProjectTarget;
    class TProjectSubdir;
    class TProject;

    using TProjectTargetPtr = TSimpleSharedPtr<TProjectTarget>;
    using TProjectSubdirPtr = TSimpleSharedPtr<TProjectSubdir>;
    using TProjectPtr = TSimpleSharedPtr<TProject>;

    template <typename T>
    concept CTargetLike = std::derived_from<T, TProjectTarget> && std::is_default_constructible_v<T>;
    template <typename T>
    concept CSubdirLike = std::derived_from<T, TProjectSubdir> && std::is_default_constructible_v<T>;

    class TProjectTarget {
    public:
        virtual ~TProjectTarget() = default;

        std::string Macro;
        std::string Name;
        TVector<std::string> MacroArgs;
        jinja2::ValuesMap Attrs; // TODO: use attr storage from jinja_helpers
    };

    class TProjectSubdir {
    public:
        virtual ~TProjectSubdir() = default;
        bool IsTopLevel() const;

        TVector<TProjectTargetPtr> Targets;
        TVector<TProjectSubdirPtr> Subdirectories;
        jinja2::ValuesMap Attrs; // TODO: use attr storage from jinja_helpers
        fs::path Path;
    };

    class TProject {
    public:
        class TBuilder;
        virtual ~TProject() = default;

        const TVector<TProjectSubdirPtr>& GetSubdirs() const;

    protected:
        template <CSubdirLike TSubdirLike, CTargetLike TTargetLike>
        void SetFactoryTypes() {
            Reset();
            Factory_ = MakeHolder<TCustomFactory<TSubdirLike, TTargetLike>>();
        }
        void Reset();

        THashMap<fs::path, TProjectSubdirPtr> SubdirsByPath_;
        TVector<TProjectSubdirPtr> SubdirsOrder_;

    private:
        class TFactory {
        public:
            virtual ~TFactory() = default;
            virtual TProjectSubdirPtr CreateSubdir() const = 0;
            virtual TProjectTargetPtr CreateTarget() const = 0;
        };

        template <CSubdirLike TSubdir, CTargetLike TTarget>
        class TCustomFactory: public TFactory {
        public:
            virtual TProjectSubdirPtr CreateSubdir() const {
                return MakeSimpleShared<TSubdir>();
            }
            virtual TProjectTargetPtr CreateTarget() const {
                return MakeSimpleShared<TTarget>();
            }
        };

        THolder<TFactory> Factory_;
    };

    class TProject::TBuilder {
    public:
        class TTargetHolder;

        TBuilder(TSpecBasedGenerator* generator);
        virtual ~TBuilder() = default;

        TProjectPtr Finalize();
        TTargetHolder CreateTarget(const fs::path& targetDir);
        void OnAttribute(const std::string& attribute);

        const TProjectTarget* CurrentTarget() const noexcept;
        const TProjectSubdir* CurrentSubdir() const noexcept;
        TProjectTarget* CurrentTarget() noexcept;
        TProjectSubdir* CurrentSubdir() noexcept;

    protected:
        virtual void CustomFinalize() {};
        virtual void CustomOnAttribute(const std::string&) {}

        TProjectSubdirPtr CreateDirectories(const fs::path& dir);

        TSpecBasedGenerator* Generator_ = nullptr;
        TProjectPtr Project_ = nullptr;
        TProjectTargetPtr CurTarget_ = nullptr;
        TProjectSubdirPtr CurSubdir_ = nullptr;
    };
    using TProjectBuilderPtr = TSimpleSharedPtr<TProject::TBuilder>;

    class TProject::TBuilder::TTargetHolder {
    public:
        TTargetHolder() noexcept = default;

        TTargetHolder(const TTargetHolder&) = delete;
        TTargetHolder& operator=(const TTargetHolder&) = delete;

        TTargetHolder(TTargetHolder&& other) noexcept;
        TTargetHolder& operator=(TTargetHolder&& other) noexcept;

        TTargetHolder(TBuilder& builder, TProjectSubdirPtr subdir, TProjectTargetPtr target) noexcept;

        ~TTargetHolder() noexcept;

    private:
        TBuilder* Builder_ = nullptr;
        TProjectSubdirPtr PrevSubdir_ = nullptr;
        TProjectTargetPtr PrevTarget_ = nullptr;
    };

}
