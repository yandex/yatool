#pragma once

#include "std_helpers.h"
#include "attributes.h"

#include <contrib/libs/jinja2cpp/include/jinja2cpp/value.h>

#include <util/generic/hash_table.h>
#include <util/generic/vector.h>
#include <util/generic/ptr.h>

#include <string>
#include <filesystem>
#include <span>

namespace NYexport {

    class TSpecBasedGenerator;
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

    class TSemsDumpWithAttrs {
    public:
        std::string SemsDump;///< Semantic dumps, collected during dispatch graph
        size_t SemsDumpDepth{0};///< Current depth of semantics for SemsDump
        size_t SemsDumpEmptyDepth{0};///< How many empty depth (cut off on leave)
        TAttrsPtr Attrs;
    };

    class TProjectTarget : public TSemsDumpWithAttrs {
    public:
        virtual ~TProjectTarget() = default;

        std::string Macro;
        std::string Name;
        TVector<std::string> MacroArgs;

        std::string TestModDir; ///< If target is test, here directory of module with this test inside

        bool IsTest() const {
            return !TestModDir.empty();
        }
    };

    class TProjectSubdir : public TSemsDumpWithAttrs {
    public:
        virtual ~TProjectSubdir() = default;
        bool IsTopLevel() const;

        TVector<TProjectTargetPtr> Targets;///< All targets for directory
        TVector<TProjectSubdirPtr> Subdirs;///< Direct subdirectories
        fs::path Path;
        std::string MainTargetMacro;///< Macro attribute of main target
    };

    class TProject  : public TSemsDumpWithAttrs {
    public:
        class TBuilder;
        virtual ~TProject() = default;

        const TVector<TProjectSubdirPtr>& GetSubdirs() const;
        TVector<TProjectSubdirPtr>& GetSubdirs();
        TProjectSubdirPtr GetSubdir(std::string_view path) const;

    protected:
        template <CSubdirLike TSubdirLike, CTargetLike TTargetLike>
        void SetFactoryTypes() {
            Reset();
            Factory_ = MakeHolder<TCustomFactory<TSubdirLike, TTargetLike>>();
        }
        void Reset();

        THashMap<fs::path, TProjectSubdirPtr> PathToSubdir_;
        TVector<TProjectSubdirPtr> Subdirs_;

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
        void OnAttribute(const std::string& attrName, const std::span<const std::string>& attrValue);

        const TProjectTarget* CurrentTarget() const noexcept;
        const TProjectSubdir* CurrentSubdir() const noexcept;
        const TProject* CurrentProject() const noexcept { return Project_.Get(); }
        TProjectTarget* CurrentTarget() noexcept;
        TProjectSubdir* CurrentSubdir() noexcept;
        TProject* CurrentProject() noexcept { return Project_.Get(); }

    protected:
        virtual void CustomFinalize() {};
        virtual void CustomOnAttribute(const std::string& /*attrName*/, const std::span<const std::string>& /*attrValue*/) {}

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
