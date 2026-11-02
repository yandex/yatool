#include <devtools/ymake/lang/confreader.h>

#include "parser_lexer.h"

#include "devtools/ymake/lang/properties.h"

#include <devtools/ymake/common/npath.h>

#include <devtools/ymake/builtin_macro_consts.h>
#include <devtools/ymake/cmd_properties.h>
#include <devtools/ymake/conf.h>
#include <devtools/ymake/macro_string.h>
#include <devtools/ymake/out.h>
#include <devtools/ymake/vardefs.h>

#include <library/cpp/digest/md5/md5.h>
#include <library/cpp/iterator/mapped.h>

#include <util/folder/path.h>
#include <util/folder/pathsplit.h>
#include <util/generic/algorithm.h>
#include <util/generic/hash_set.h>
#include <util/generic/map.h>
#include <util/generic/noncopyable.h>
#include <util/generic/scope.h>
#include <util/generic/strbuf.h>
#include <util/generic/string.h>
#include <util/generic/utility.h>
#include <util/generic/set.h>
#include <util/generic/vector.h>
#include <util/stream/file.h>
#include <util/stream/str.h>
#include <util/string/builder.h>
#include <util/string/join.h>
#include <util/string/split.h>
#include <util/string/strip.h>
#include <util/string/type.h>

#include <fmt/format.h>

#include <optional>
#include <string>
#include <string_view>

using namespace NConfReader;
using NYndex::TSourceRange;

namespace {
    class TConfBuilder;

    void ParseConfig(TConfBuilder& builder, TStringBuf fileName, TStringBuf content);

    void ReportConfigError(TStringBuf message, TStringBuf confFileName, size_t line = 0, size_t column = 0)
    {
        // Temporary implementation of ConfReader error report function.
        // It would be nice to add the context to the error message here.
        TStringBuf fileName = confFileName.empty() ? TStringBuf("ConfigError") : confFileName;
        ythrow TConfigError()
            << fileName << "(" << line << ", " << column << "): " << message << Endl;
    }

    void UpdateConfMd5(TStringBuf content, MD5* confMd5, TString* ignoredContent) {
        if (confMd5 == nullptr) {
            // Nothing to do
            return;
        }

        bool ignoreNextLine = false;
        for (const auto& it: StringSplitter(content).Split('\n').SkipEmpty()) {
            const auto line = it.Token();
            if (line.StartsWith(TStringBuf("# IGNORE YMAKE CONF CONTEXT"))) {
                ignoreNextLine = true;
                continue;
            } else if (ignoreNextLine) {
                if (ignoredContent) {
                    ignoredContent->append(line).append('\n');
                }
                ignoreNextLine = false;
                continue;
            }
            confMd5->Update(line.data(), line.size());
        }
    }

    std::string NormalizeRval(std::string_view rval, bool leftTrim)
    {
        // Eliminate 'new line' and 'carriage return' symbols from the string
        std::string result;
        bool backSlashSeen = true;
        for (const auto& it : StringSplitter(rval).Split('\n')) {
            Y_ASSERT(backSlashSeen);
            auto line = it.Token();
            if (!line.empty() && line.back() == '\r') {
                line.remove_suffix(1);
            }
            if (!line.empty() && line.back() == '\\') {
                line.remove_suffix(1);
            } else {
                backSlashSeen = false;
            }

            if (leftTrim) {
                auto pos = line.find_first_not_of(" \t");
                if (pos == std::string_view::npos) {
                    pos = line.size();
                }
                line.remove_prefix(pos);
            }

            result += line;
        }
        return result;
    }

    class TConditionStack
    {
    public:
        TConditionStack() = default;
        ~TConditionStack() = default;
        const TString& ToString() const { return Condition_; }
        void Push(const TString& condition) {
            Stack_.push_back(Condition_.length());
            Condition_ += condition;
        }
        void Pop() {
            Y_ASSERT(!Stack_.empty());
            Condition_.resize(Stack_.back());
            Stack_.pop_back();
        }
        size_t Depth() const { return Stack_.size(); }
        bool Empty() const { return Depth() == 0; }
    private:
        TString Condition_{"? "};
        TVector<size_t> Stack_;
    };

    enum class EArgType {
        Positional,
        Vararg,
        NamedArray,
        NamedScalar,
        NamedBool,
        Specialization
    };

    inline bool IsPositional(EArgType argType) {
        return argType == EArgType::Positional;
    }

    inline bool IsVararg(EArgType argType) {
        return argType == EArgType::Vararg;
    }

    inline bool IsNamedArray(EArgType argType) {
        return argType == EArgType::NamedArray;
    }

    inline bool IsNamedScalar(EArgType argType) {
        return argType == EArgType::NamedScalar;
    }

    inline bool IsNamedBool(EArgType argType) {
        return argType == EArgType::NamedBool;
    }

    inline bool IsSpecialization(EArgType argType) {
        return argType == EArgType::Specialization;
    }

    inline bool IsNamedArgType(EArgType argType) {
        return IsNamedArray(argType) || IsNamedScalar(argType) || IsNamedBool(argType);
    }

    struct TArgDesc {
        EArgType Type;
        TString Name;
        TString DeepReplace;
        TString InitTrue;
        TString InitFalse;
    };

    struct TForeachCallInfo {
        TForeachCallInfo(TBlockData& blockData, const TString& blockName, const TString& loopVar, const TString& macroName, const TString& actualArgs)
            : BlockData(blockData), BlockName(blockName), LoopVar(loopVar), MacroName(macroName), ActualArgs(actualArgs) {}
        TBlockData& BlockData;
        TString BlockName;
        TString LoopVar;
        TString MacroName;
        TString ActualArgs;
    };

    using TArgs = TVector<TArgDesc>;
    using TVarSet = TSet<TString>;
    using TPropertySet = TSet<TString>;
    using TSelectAlts = TVector<TString>;

    // Arguments considered to be compatible for <generic macro>/<partial macro specialization> and
    // <partial macro specialization> if:
    // 1) both arguments are of the same type and have the same name
    // 2) both arguments are of spec type
    // 3) one of the arguments is spec arg and the other is positional
    bool IsCompatibleSpecArg(const TArgDesc& arg1, const TArgDesc& arg2) {
        return arg1.Type == arg2.Type && (arg1.Name == arg2.Name || IsSpecialization(arg1.Type))
            || (IsSpecialization(arg1.Type) && IsPositional(arg2.Type) || IsPositional(arg1.Type) && IsSpecialization(arg2.Type));
    }

    // Check if the signature of partial macro specialization is compatible to the signature
    // of generic macro. The signatures are considered to be compatible if they have the same
    // number of formal arguments and their formal arguments are compatible
    bool IsCompatibleSpecSignature(const TArgs& genArgs, const TArgs& specArgs) {
        if (genArgs.size() != specArgs.size()) {
            return false;
        }

        return Mismatch(specArgs.begin(), specArgs.end(), genArgs.begin(), IsCompatibleSpecArg).first == specArgs.end();
    }

    template<typename P>
    TVector<TString> CollectArgNamesIf(const TArgs& signature, P pred) {
        TVector<TString> args;
        for (const auto& arg : signature) {
            if (pred(arg)) {
                args.push_back(arg.Name);
            }
        }
        return args;
    }

    TVector<TString> CollectArgNames(const TArgs& signature) {
        return CollectArgNamesIf(signature, [] (const auto&) { return true; });
    }

    TVector<TString> CollectPlainArgNames(const TArgs& signature) {
        return CollectArgNamesIf(signature, [] (const auto& arg) { return IsPositional(arg.Type) || IsVararg(arg.Type); });
    }

    TVector<TString> CollectSpecArgNames(const TArgs& signature) {
        return CollectArgNamesIf(signature, [] (const auto& arg) { return IsSpecialization(arg.Type); });
    }

    TSignature::TKeywords CollectKeywords(const TArgs& arguments) {
        TSignature::TKeywords keywords;
        for (const auto& arg : arguments) {
            TStringBuf kwPresent, kwMissing;
            switch (arg.Type) {
                case EArgType::NamedBool:
                    if (!arg.InitTrue.empty()) {
                        kwPresent = arg.InitTrue;
                    }
                    if (!arg.InitFalse.empty()) {
                        kwMissing = arg.InitFalse;
                    }
                    keywords.AddFlagKeyword(arg.Name, kwPresent, kwMissing);
                    break;
                case EArgType::NamedScalar:
                    if (!arg.InitFalse.empty()) {
                        kwMissing = arg.InitFalse;
                    }
                    keywords.AddScalarKeyword(arg.Name, kwMissing, arg.DeepReplace);
                    break;
                case EArgType::NamedArray:
                    keywords.AddArrayKeyword(arg.Name, arg.DeepReplace);
                    break;
                default:
                    break;
            }
        }
        return keywords;
    };

    class TBlockDesc
    {
    public:
        enum class EBlockType {
            None = 0,
            Module,
            MultiModule,
            Macro,
            Foreach
        };

        TBlockDesc(TBlockData& blockData, const TString& name, EBlockType type)
            : BlockData_{blockData}
            , Name_{name}
            , Type_{type}
        {
        }

        EBlockType Type() const { return Type_; }
        bool IsMacro() const { return Type_ == EBlockType::Macro; }
        bool IsModule() const { return Type_ == EBlockType::Module; }
        bool IsMultiModule() const { return Type_ == EBlockType::MultiModule; }
        bool IsFromMultimodule() const { return !BlockData_.OwnerName.empty(); }
        bool IsForeach() const { return Type_ == EBlockType::Foreach; }
        TBlockData& BlockData() { return BlockData_; }
        const TBlockData& BlockData() const { return BlockData_; }
        const TString& Name() const { return Name_; }
        const TString& GetArgs() const {
            Y_ASSERT(IsMacro());
            return Args_;
        }
        void SetArgs(const TString& args) {
            Y_ASSERT(IsMacro());
            Y_ASSERT(Args_.empty() && !args.empty());
            Args_ = args;
        }
        const TString& GetLoopVar() const {
            Y_ASSERT(IsForeach());
            return LoopVar_;
        }
        void SetLoopVar(const TString& loopVar) {
            Y_ASSERT(IsForeach());
            Y_ASSERT(!loopVar.empty());
            LoopVar_ = loopVar;
        }
        bool AddArgName(const TString& argName) {
            Y_ASSERT(IsMacro());
            return ArgNames_.insert(argName).second;
        }
        const TVarSet& ArgNames() const {
            Y_ASSERT(IsMacro());
            return ArgNames_;
        }
        bool AddVarName(const TString& varName) {
            Y_ASSERT(IsMacro());
            return VarNames_.insert(varName).second;
        }
        const TVarSet& VarNames() const {
            Y_ASSERT(IsMacro());
            return VarNames_;
        }
        bool HasProperty(const TString& propName) const {
            Y_ASSERT(IsMacro() || IsModule() || IsMultiModule());
            return PropertySet_.contains(propName);
        }
        bool AddProperty(const TString& propName) {
            Y_ASSERT(IsMacro() || IsModule() || IsMultiModule());
            return PropertySet_.insert(propName).second;
        }

    private:
        TBlockData& BlockData_;
        TString Name_;
        EBlockType Type_;
        TString Args_;
        TString LoopVar_;
        TVarSet ArgNames_;
        TVarSet VarNames_;
        TPropertySet PropertySet_;
    };

    class TConfBuilder : public TNonCopyable
    {
    public:
        TConfBuilder(TStringBuf sourceRoot, TStringBuf buildRoot, TStringBuf fileName, TYmakeConfig& conf, MD5* confMd5)
            : SourceRoot(sourceRoot)
            , BuildRoot(buildRoot)
            , Conf(conf)
            , ConfMd5(confMd5)
            , AllowConditionVars{false}
        {
            ImportsStack.push_back(TPathSplit(fileName).Reconstruct());
            ImportsSet.insert(ImportsStack.back().GetPath());
        }
        ~TConfBuilder() = default;

        void YndexerAddDefinition(
            const TString& name,
            const TString& file,
            const TSourceRange& range,
            const TString& docText,
            NYndex::EDefinitionType type)
        {
            if (name.empty() || file.empty()) {
                // nothing to do
                return;
            }

            Conf.CommandDefinitions.AddDefinition(name, file, range, docText, type);
        }

        void YndexerAddReference(
            const TString& name,
            const TString& file,
            const TSourceRange& range)
        {
            if (name.empty() || file.empty() || !range.IsValid()) {
                // nothing to do
                return;
            }

            Conf.CommandReferences.AddReference(name, file, range);
        }

        TString GetConfFileName() const {
            Y_ASSERT(!ImportsStack.empty());
            return ImportsStack.back().GetPath();
        }

        TString GetRootRelativeConfFileName() {
            Y_ASSERT(!ImportsStack.empty());
            const auto& path = ImportsStack.back();
            if (SourceRoot.IsAbsolute() && path.IsAbsolute() && path.IsSubpathOf(SourceRoot)) {
                TFsPath relPath = path.RelativePath(SourceRoot);
                return relPath.GetPath();
            }
            return "";
        }

        void ReportError(const TString& message, size_t line = 0, size_t column = 0) {
            ReportConfigError(message, GetConfFileName(), line, column);
        }

        bool IsTopLevel() const {
            Y_ASSERT(!ImportsStack.empty());
            return ImportsStack.size() == 1;
        }

        TStringBuf GetVarValue(TStringBuf name) {
            return GetCmdValue(Conf.CommandConf.Get1(name));
        }

        bool GetLeftTrim() const {
            return LeftTrim;
        }

        TFsPath& GetOrInitConfRoot(TStringBuf varName) {
            TFsPath* root = nullptr;
            TFsPath* confRoot = nullptr;
            if (varName == TStringBuf("CONF_ROOT")) {
                root = &SourceRoot;
                confRoot = &ConfSourceRoot;
            } else if (varName == TStringBuf("CONF_BUILD_ROOT")) {
                root = &BuildRoot;
                confRoot = &ConfBuildRoot;
            } else {
                ReportError(TStringBuilder() << "Unexpected name of variable [" << varName << "] in call of GetOrInitConfRoot");
            }
            if (!(*confRoot).IsDefined()) {
                if (auto value = GetVarValue(varName); value.empty()) {
                    *confRoot = *root / TStringBuf("build");
                } else {
                    *confRoot = TFsPath(value).Fix();
                    if (!(*confRoot).IsAbsolute()) {
                        ReportError(TStringBuilder() << "Invalid value of " << varName << " [" << value << "]");
                    } else if (!(*confRoot).IsSubpathOf(*root)) {
                        ReportError(TStringBuilder() << varName << " [" << value << "] is not a subpath of [" << *root << "]");
                    }
                }
            }
            return *confRoot;
        }

        void UpdateConfMd5(TStringBuf content) {
            ::UpdateConfMd5(content, ConfMd5, nullptr);
        }

        void AddToImportedFiles(const TString& fileName, const TString& md5, time_t modTime) {
            Conf.ImportedFiles.push_back(TImportedFileDescription{fileName, md5, modTime});
        }

        TString CanonizeImportFileName(TStringBuf fileName) {
            TFsPath path = fileName;
            if (path.IsSubpathOf(SourceRoot)) {
                return ArcPath(path.RelativePath(SourceRoot).GetPath());
            } else if (path.IsSubpathOf(BuildRoot)) {
                return BuildPath(path.RelativePath(BuildRoot).GetPath());
            } else {
                Y_ASSERT(0);
                return "";
            }
        }

        void ProcessImport(TStringBuf fileName) {
            YDIAG(Conf) << "Import: [" << fileName << "]" << Endl;
            bool isFileNameValid = (
                (fileName == "${CONF_ROOT}/ymake.core.conf") ||
                fileName.starts_with("${CONF_ROOT}/conf/") ||
                fileName.starts_with("${CONF_ROOT}/internal/conf/")
            );
            if (!isFileNameValid) {
                ReportError(TStringBuilder() << "Invalid file name in @import statement: [" << fileName << "]");
            }
            TString importFileName = GetOrInitConfRoot("CONF_ROOT") + TString(fileName.substr("${CONF_ROOT}"sv.size()));
            ImportsStack.push_back(importFileName);
            YDIAG(Conf) << "Import [" << fileName << "] is resolved to [" << importFileName << "]" << Endl;
            if (auto [_, ok] = ImportsSet.insert(importFileName); !ok) {
                auto loop = MakeRangeJoiner(
                    TStringBuf(" -> "),
                    MakeMappedRange(ImportsStack, [](auto& item) { return item.GetPath(); })
                );
                ReportError(TStringBuilder() << "@import loop has been detected in build configuration: " << loop);
            }

            TString importContent = TFileInput{importFileName}.ReadAll();
            ValidateUtf8(importContent, importFileName);
            TString md5 = NConfReader::CalculateConfMd5(importContent);
            const auto path = TFsPath(importFileName);
            TFileStat stat;
            path.Stat(stat);
            time_t modTime = stat.MTime;

            ParseConfig(*this, importFileName, importContent);

            ImportsSet.erase(importFileName);
            AddToImportedFiles(CanonizeImportFileName(importFileName), md5, modTime);
            Y_ASSERT(ImportsStack.back().GetPath() == importFileName);
            ImportsStack.pop_back();
        }

        void EnterMacro(const TString& name, const TArgs& arguments, const TSourceRange& range, const TString& docText) {
            Y_ASSERT(!name.empty());
            static const TString delim{"____"};

            auto macroIter = Macros.find(name);
            // Check if this macro definition is partial macro specialization. Specialization
            // parameters are needed to create a mangled name for this specialized macro and
            // to create a corresponding BlockData
            TVector<TString> specArgs = CollectSpecArgNames(arguments);
            if (specArgs.empty() && macroIter != Macros.end()) {
                ReportError(
                    TString::Join("Macro [", name, "] has already been defined"));
            } else if (!specArgs.empty() && macroIter == Macros.end()) {
                ReportError(
                    TString::Join("No generic macro definition found for macro [", name, "]"));
            }

            TString specName{name};
            if (!specArgs.empty()) {
                // Mangle name for partial macro specialization of macro definition
                // macro specialization _SRC("cpp", FILE, OPTIONS...) for generic
                // macro definition _SRC(EXT, FILE, OPTIONS...) gets the following
                // signature _SRC____cpp(EXT, FILE, OPTIONS...)
                specName += delim;
                specName += JoinStrings(specArgs, delim);
            }

            YndexerAddDefinition(
                specName,
                GetRootRelativeConfFileName(),
                range,
                docText,
                NYndex::EDefinitionType::Macro
            );

            auto& blockData = Conf.BlockData[specName];
            BlockStack.emplace_back(blockData, specName, TBlockDesc::EBlockType::Macro);
            auto& block = BlockStack.back();

            // Now we are ready to process named arguments since the BlockData is already created
            for (const auto& arg : arguments) {
                const bool isNamedArrayOrScalar = IsNamedArray(arg.Type) || IsNamedScalar(arg.Type);
                if (!arg.DeepReplace.empty() && !isNamedArrayOrScalar) {
                    ReportError(
                        "DeepReplace modifiers are allowed for named arrays or scalar arguments only");
                }
                // Add argument name to the set of formal argument names (This set
                // will be used in ExitMacro to determine which formal arguments
                // were used in conditions). Report an error in case of duplication.
                if (!IsSpecialization(arg.Type) && !block.AddArgName(arg.Name)) {
                    ReportError(fmt::format("Parameter [{}] has been already declared for macro [{}]", arg.Name, block.Name()));
                }
            }

            blockData.CmdProps.Reset(new TCmdProperty{
                CollectPlainArgNames(specArgs.empty() ? arguments : macroIter->second),
                CollectKeywords(arguments)
            });
            if (!blockData.CmdProps->Signature().ArgNames().empty()) {
                block.SetArgs(blockData.CmdProps->ConvertCmdArgs());
            }

            // Add "guard" for conditions. All conditions defined inside the macro are prepended
            // with the guard condition $<macro service var> == "yes" && ... , where macro service
            // variable is spelled as follows: "env" + <macro name>. Macro service variable will
            // be set to "yes" just before macro invocation.
            PushConditionVars();
            EnterCondition();
            TString envVar("env");
            envVar += name;
            AddConditionVar(envVar);
            ExitCondition();
            AppendCondition(TString::Join("$", envVar, "==\"yes\""));

            if (specArgs.empty()) {
                Macros[name] = arguments;
                return;
            }

            // OK. We are currently processing partial macro specialization definition.
            // Let's check the signature of this partial macro specialization and corresponding
            // generic macro definition
            if (!IsCompatibleSpecSignature(arguments, macroIter->second)) {
                TStringStream message;
                const auto specSignature = JoinStrings(CollectArgNames(arguments), ", ");
                const auto genSignature = JoinStrings(CollectArgNames(macroIter->second), ", ");
                message << "Incompatible partial macro specialization definition ["
                    << name << '(' << specSignature << ')'
                    << "] for generic macro ["
                    << name << '(' << genSignature << ')'
                    << "]";
                ReportError(message.Str());
            }

            // TODO(snermolaev): Partial specialization of macro definition (Not fully implemented yet)
            //                   We currently support only the first argument specialization
            if (specArgs.size() > 1) {
                ReportError("Macro specialization is currently supported for the first argument only");
            }
            auto& genericBlockData = Conf.BlockData[name];
            auto& sectionPatterns = GetOrInit(genericBlockData.SectionPatterns);

            const auto& pattern = specArgs[0];
            if (Generics.insert(name).second) {
                genericBlockData.IsGenericMacro = true;

                const auto& value = GetCmdValue(Conf.CommandConf.Get1(name));
                TStringBuf cmdValue = (value.empty()) ? TStringBuf("") : MacroDefBody(value);
                // If generic definition of macro has its own command then put this command
                // into "*" pattern section
                if (!cmdValue.empty()) {
                    TString starName = TString::Join(name, "*");
                    Y_ASSERT(!Conf.CommandConf.Contains(starName));
                    Conf.CommandConf.SetValue(starName, cmdValue);
                    Conf.CommandConf.RemoveFromScope(name);
                    sectionPatterns["*"] = starName;
                }
                // We set an empty command for a generic macro deliberately here. This
                // will cause a configuration error when no appropriate specialization
                // is defined in cases when this generic macro is called out of context
                // of (special) SRCS macro.
                const TString emptyCommand = "";
                SetCommandInternal(genericBlockData, name, MakeFullCommand(block.GetArgs(), emptyCommand), true);
            }
            Y_ASSERT(genericBlockData.IsGenericMacro);

            sectionPatterns[pattern] = specName;
        }

        void ExitMacro() {
            Y_ASSERT(!BlockStack.empty() && BlockStack.back().IsMacro());
            PopConditionVars();
            RemoveLastCondition();

            // Generate tautological calls to macro SET for those arguments
            // which were used in conditions. This trick enforces recalcaulation
            // of conditions when macro call is processed:
            //     SET(Arg $Arg)
            const auto& block = BlockStack.back();
            auto& argNames = block.ArgNames();
            auto& varNames = block.VarNames();
            if (!argNames.empty() && !varNames.empty()) {
                TVector<TString> usedArgs;
                SetIntersection(argNames.begin(), argNames.end(),
                                varNames.begin(), varNames.end(),
                                std::back_inserter(usedArgs));
                for (const auto& argName : usedArgs) {
                    AddMacroCall(NMacro::SET, TString::Join(argName, " $", argName), {});
                }
            }
            BlockStack.pop_back();
        }

        void EnterMultiModule(const TString& name, const TSourceRange& range, const TString& docText) {
            Y_ASSERT(!name.empty());

            YndexerAddDefinition(
                name,
                GetRootRelativeConfFileName(),
                range,
                docText,
                NYndex::EDefinitionType::MultiModule
            );

            auto& blockData = Conf.BlockData[name];
            blockData.IsMultiModule = true;
            BlockStack.emplace_back(blockData, name, TBlockDesc::EBlockType::MultiModule);
        }

        void ExitMultiModule() {
            Y_ASSERT(!BlockStack.empty() && BlockStack.back().IsMultiModule());
            BlockStack.pop_back();
        }

        static inline TString MangleSubModuleName(TString subName, TString ownerName) {
            Y_ASSERT(!subName.empty() && !ownerName.empty());
            return subName + MODULE_MANGLING_DELIM + ownerName;
        }

        void EnterModule(const TString& name, const TString ancestor, const TSourceRange& range, const TString& docText) {
            Y_ASSERT(!name.empty());
            TString fullName = name;
            TString ownerName;
            bool inMultiModule = !BlockStack.empty() && BlockStack.back().IsMultiModule();
            Y_ASSERT(BlockStack.empty() || inMultiModule);
            if (inMultiModule) {
                ownerName = BlockStack.back().Name();
                fullName = MangleSubModuleName(fullName, ownerName);
            }
            if (!ancestor.empty()) {
                YDIAG(Conf) << "Inherit [" << fullName << "] from module [" << ancestor << "]" << Endl;
            }

            if (!inMultiModule) {
                YndexerAddDefinition(
                        name,
                        GetRootRelativeConfFileName(),
                        range,
                        docText,
                        NYndex::EDefinitionType::Module);
            }

            auto& blockData = Conf.BlockData[fullName];
            GetOrInit(blockData.CmdProps).SetHasConditions(true);
            blockData.ParentName = ancestor;
            if (inMultiModule) {
                blockData.OwnerName = ownerName;
                GetOrInit(blockData.ModuleConf).Tag = name;
            }
            BlockStack.emplace_back(blockData, fullName, TBlockDesc::EBlockType::Module);

            // Add "guard" for conditions. All conditions defined inside the module are prepended
            // with the gaurd condition $<module service var> == "yes" && ... , where module service
            // variable is spelled as follows: "env" + <module name>. Module service varaibale will
            // be set to "yes" when module "instantiation" begins.
            PushConditionVars();
            EnterCondition();
            TString envVar = TString::Join("env", fullName);
            AddConditionVar(envVar);
            ExitCondition();
            AppendCondition(TString::Join("$", envVar, "==\"yes\""));
            if (inMultiModule) {
                // Set default value of MODULE_TAG in submodule to name of the submodule
                AddMacroCall(NMacro::SET, TString::Join(NVariableDefs::VAR_MODULE_TAG, " ", name), {});
            }
        }

        void ExitModule() {
            Y_ASSERT(!BlockStack.empty() && BlockStack.back().IsModule());
            PopConditionVars();
            RemoveLastCondition();
            BlockStack.pop_back();
        }

        void EnterCondition() {
            AllowConditionVars = true;
        }

        void ExitCondition() {
            AllowConditionVars = false;
        }

        void PushConditionVars() {
            ConditionVarsStack.emplace_back(TVector<TString>());
        }

        void PopConditionVars() {
            Y_ASSERT(!ConditionVarsStack.empty());
            for (auto& var : ConditionVarsStack.back()) {
                ConditionVars.erase(var);
            }
            ConditionVarsStack.pop_back();
        }

        void AddConditionVar(const TString& name) {
            Y_ASSERT(AllowConditionVars);
            Y_ASSERT(!name.empty());
            Y_ASSERT(!ConditionVarsStack.empty());
            if (ConditionVars.insert(name).second) {
                ConditionVarsStack.back().push_back(name);
            }

            if (!BlockStack.empty() && BlockStack[0].IsMacro()) {
                BlockStack[0].AddVarName(name);
            }
        }

        void AppendCondition(const TString& condition, bool revert = false) {
            Y_ASSERT(!condition.empty());
            TStringStream str;
            if (!ConditionStack.Empty()) str << "&&";
            if (revert) str << "!";
            str << '(' << condition << ')';
            ConditionStack.Push(str.Str());
        }

        void RemoveLastCondition(int depth = 1) {
            Y_ASSERT(depth <= static_cast<int>(ConditionStack.Depth()));
            for (; depth > 0; --depth) {
                ConditionStack.Pop();
            }
        }

        void AddMacroCall(TStringBuf macroName, const TString& args, const TSourceRange& range) {
            Y_ASSERT(!BlockStack.empty());
            YndexerAddReference(TString{macroName}, GetRootRelativeConfFileName(), range);
            auto& block = BlockStack.back();
            if (block.IsForeach()) {
                Y_ASSERT(BlockStack.size() > 1);
                auto& parentBlock = BlockStack[BlockStack.size() - 2];
                Y_ASSERT(parentBlock.IsMacro());
                ForeachCallInfo.emplace_back(parentBlock.BlockData(), parentBlock.Name(), block.GetLoopVar(), TString{macroName}, args);
            } else {
                YDIAG(Conf) << "Add macro call for [" << block.Name() << "] -> "
                      << macroName << "(" << args << ")" << Endl;
                TCmdProperty& prop = GetOrInit(block.BlockData().CmdProps);
                prop.AddMacroCall(macroName, args);
            }
        }

        void AddAssignment(const TString& varName, const TString& assignOp, const TString& rvalue) {
            if (!BlockStack.empty() && BlockStack.front().IsMacro() && ConditionStack.Depth() == 1) {
                Y_ASSERT(assignOp == "=");
                // FIXME: Check the old code if assignment operator does really matter!!!
                SetOption(varName, rvalue);
                return;
            }
            if (!ConditionStack.Empty()) {
                if (!BlockStack.empty()) {
                    auto& block = BlockStack.back();
                    if (block.IsMacro() || block.IsModule()) {
                        auto& blockData = block.BlockData();
                        GetOrInit(blockData.CmdProps).SetHasConditions(true);
                    }
                }
                YDIAG(Conf) << "Condition action: [" << varName << "] -> [" << rvalue << "]" << Endl;
                int conditionNumber = Conf.Conditions.GetRawConditions().size();
                Conf.Conditions.AddRawCondition(ConditionStack.ToString());
                TConditionAction action(varName, GetOperator(assignOp), rvalue);
                for (auto& var : ConditionVars) {
                    TString dollarVar = TString::Join("$", var);
                    YDIAG(Conf) << "Condition [" << ConditionStack.ToString() << "] -> "
                          << conditionNumber << " for var [" << dollarVar << "]" << Endl;
                    Conf.Conditions.AddActionForVariable(dollarVar, conditionNumber, action);
                }
            } else {
                YDIAG(Conf) << "Simple pair: [" << varName << "] -> [" << rvalue << "]" << Endl;
                auto op = GetOperator(assignOp);
                if (op == EMAO_Set) {
                    Conf.CommandConf.SetValue(varName, rvalue);
                } else {
                    if (!Conf.CommandConf.Has(varName)) {
                        if (op == EMAO_Append || op == EMAO_Define) {
                            Conf.CommandConf.SetValue(varName, rvalue);
                        } else if (op == EMAO_Exclude) {
                            YDIAG(Conf) << "Can't make exclude for empty variable ["
                                  << varName << "]. Just leave it empty."
                                  << Endl;
                            Conf.CommandConf.SetValue(varName, "");
                        }
                    } else {
                        if (op == EMAO_Append) {
                            Conf.CommandConf.Add1Sp(varName, rvalue);
                        } else if (op == EMAO_Exclude) {
                            Conf.CommandConf.Del1Sp(varName, rvalue);
                        }
                    }
                }
            }
        }

        void SetOption(const TString& name, const TString& value) {
            Y_ASSERT(!BlockStack.empty());
            auto& block = BlockStack.back();
            auto& blockData = block.BlockData();
            YDIAG(Conf) << "Property for [" << block.Name() << "]: [" << name << "] -> [" << value << "]" << Endl;
            if (!blockData.SetOption(block.Name(), name, value, Conf.CommandConf, Conf.RenderSemantics)) {
                YDIAG(Conf) << "Local macro variable: [" << name << "] -> [" << value << "]" << Endl;
                GetOrInit(blockData.CmdProps).SetSpecVar(name, value);
            }
        }

        void SetPropertyForMacro(const TString& name, const TString& value) {
            Y_ASSERT(!BlockStack.empty());
            const auto& block = BlockStack.back();
            Y_ASSERT(block.IsMacro());

            if (name == NProperties::CMD) {
                if (!Conf.RenderSemantics || !block.BlockData().HasSemantics) {
                    SetCommand(value);
                }
                return;
            } else if (name == NProperties::SEM) {
                if (Conf.RenderSemantics) {
                    BlockStack.back().BlockData().HasSemantics = true;
                    SetCommand(value);
                }
                return;
            } else if (name == NProperties::PROXY) {
                SetProxyProp();
                return;
            } else if (name == NProperties::FILE_GROUP) {
                if (value == "yes") {
                    BlockStack.back().BlockData().IsFileGroupMacro = true;
                } else if (value == "no") {
                    BlockStack.back().BlockData().IsFileGroupMacro = false;
                } else {
                    ReportError(TString::Join("Only yes or no are expected for macro property [", block.Name(), ".", name, "]"));
                }
            } else {
                if (name == NProperties::ADDINCL ||
                    name == NProperties::PEERDIR ||
                    name == NProperties::ALLOWED_IN_LINTERS_MAKE)
                {
                    // Do nothing
                } else if (name == NProperties::GEN_FROM_FILE ||
                           name == NProperties::NO_EXPAND) {
                    if (value == "yes") {
                        // Do nothing
                    } else if (value != "no") {
                        ReportError(TString::Join("Unexpected value [", value, "] for macro property [", block.Name(), ".", name, "]"));
                    }
                } else {
                    ReportError(TString::Join("Unknown macro property name [",  name, "] in macro [", block.Name(), "]"));
                }

                SetOption(name, value);
            }
        }

        void SetPropertyForModule(const TString& name, const TString& value) {
            Y_ASSERT(!BlockStack.empty());
            const auto& block = BlockStack.back();
            Y_ASSERT(block.IsModule() || block.IsMultiModule());

            if (name == NProperties::ALIASES ||
                name == NProperties::ALLOWED ||
                name == NProperties::CMD ||
                name == NProperties::DEFAULT_NAME_GENERATOR ||
                name == NProperties::ARGS_PARSER ||
                name == NProperties::SEM ||
                name == NProperties::EXTS ||
                name == NProperties::GLOBAL ||
                name == NProperties::IGNORED ||
                name == NProperties::NODE_TYPE ||
                name == NProperties::PEERDIR_POLICY ||
                name == NProperties::SYMLINK_POLICY ||
                name == NProperties::RESTRICTED ||
                name == NProperties::GLOBAL_CMD ||
                name == NProperties::GLOBAL_SEM ||
                name == NProperties::GLOBAL_EXTS ||
                name == NProperties::EPILOGUE ||
                name == NProperties::TRANSITION)
            {
                // Do nothing
            } else if (name == NProperties::PEERDIRSELF) {
                if (!block.IsFromMultimodule()) {
                    ReportError(TStringBuilder() << "Property [.PEERDIRSELF] is set for module ["
                        << block.Name() << "] which is not a sub-module of a multi-module.");
                }
            } else if (name == NProperties::FINAL_TARGET ||
                       name == NProperties::INCLUDE_TAG ||
                       name == NProperties::PROXY ||
                       name == NProperties::VERSION_PROXY ||
                       name == NProperties::USE_PEERS_LATE_OUTS)
            {
                if (value == "yes") {
                    // Do nothing
                } else if (value != "no") {
                    ReportError(TString::Join("Unexpected value [", value, "] for module property [", block.Name(), ".", name, "]"));
                }
            } else {
                ReportError(TString::Join("Unknown property [", name, "] in module [", block.Name(), "]"));
            }
            if (block.IsMultiModule() && name != NProperties::ALIASES) {
                ReportError(TString::Join( "Unexpected property [", name, "] in multimodule [", block.Name(), "]"));
            }

            SetOption(name, value);
        }

        void SetProperty(const TString& name, const TString& value, const TSourceRange& range) {
            Y_ASSERT(!BlockStack.empty());
            YndexerAddReference(name, GetRootRelativeConfFileName(), range);
            auto& block = BlockStack.back();
            switch (block.Type()) {
                case TBlockDesc::EBlockType::Macro:
                    SetPropertyForMacro(name, value);
                    break;
                case TBlockDesc::EBlockType::Module:
                case TBlockDesc::EBlockType::MultiModule:
                    SetPropertyForModule(name, value);
                    break;
                default:
                    Y_ASSERT(false && "Unreachable code");
            }
            if (!block.AddProperty(name)) {
                ReportError(TString::Join("Property [", name, "] for [", block.Name(), "] has been already set"));
            }
        }

        void SetCommand(const TString& command) {
            Y_ASSERT(!BlockStack.empty());
            const auto& block = BlockStack.back();
            SetCommandCommon(block.Name(), block.GetArgs(), command, /* isUserMacro */ true);
        }

        void SetProxyProp() {
            Y_ASSERT(!BlockStack.empty() && BlockStack.back().IsMacro());
            // FIXME: BlockStack.back().BlockData().IsProxyMacro = true;
            Y_ASSERT(false && "Not implemented yet");
        }

        void SetExtCommand(const TVector<TString>& exts, const TString& command) {
            Y_ASSERT(!exts.empty());
            Y_ASSERT(!command.empty());
            Y_ASSERT(!BlockStack.empty());
            const auto& block = BlockStack.back();
            Y_ASSERT(block.IsForeach() && BlockStack.size() > 1);
            auto& parentBlock = BlockStack[BlockStack.size() - 2];
            Y_ASSERT(parentBlock.IsMacro());
            TString name = TString::Join(parentBlock.Name(), JoinStrings(exts, " "));
            const TString& loopVar = block.GetLoopVar();
            SetCommandCommon(name, loopVar, command, false);
            auto& sectionPatterns = GetOrInit(parentBlock.BlockData().SectionPatterns);
            for (auto& ext : exts) {
                Y_ASSERT(!ext.empty());
                sectionPatterns[ext] = name;
            }
        }

        void EnterForeach(const TString& loopVar, const TString& loopSet) {
            YDIAG(Conf) << "Foreach: (" << loopVar << " : " << loopSet << ")" << Endl;
            AddAssignment(loopVar, "=", loopSet);
            auto& parentBlock = BlockStack.back();
            TString name = TString::Join(parentBlock.Name(), ".foreach");
            auto& blockData = parentBlock.BlockData();
            YDIAG(Conf) << "Local var [" << loopVar << "] set to [" << loopSet << "] in [" << parentBlock.Name() << "]" << Endl;
            auto& prop = GetOrInit(blockData.CmdProps);
            prop.SetSpecVar(loopVar, loopSet);
            BlockStack.emplace_back(blockData, name, TBlockDesc::EBlockType::Foreach);
            BlockStack.back().SetLoopVar(loopVar);
        }

        void ExitForeach() {
            Y_ASSERT(!BlockStack.empty() && BlockStack.back().IsForeach());
            BlockStack.pop_back();
        }

        void PushSelect() {
            SelectStack.emplace_back(TSelectAlts{});
        }

        void PopSelect() {
            Y_ASSERT(!SelectStack.empty());
            SelectStack.pop_back();
        }

        void AddSelectAlt(const TString& selectAlt) {
            if (!selectAlt.empty()) {
                SelectStack.back().push_back(selectAlt);
            }
        }

        TString ComputeDefaultAlt() {
            Y_ASSERT(!SelectStack.empty());
            return TString::Join("!(", JoinStrings(SelectStack.back(), "||"), ")");
        }

        void Postprocess() {
            PostprocessForeachStatements();
        }

        static void ValidateUtf8(const TStringBuf content, const TStringBuf fileName) {
            if (!IsUtf(content)) {
                ReportConfigError("Non-UTF8 symbols in file", fileName);
            }
        }

    private:
        static TString MakeFullCommand(const TString args, const TString& command) {
            return TString::Join("(", args, ")", command);
        }

        void SetCommandInternal(TBlockData& blockData, const TString& name, const TString& fullCommand, bool isUserMacro) {
            Y_ASSERT(!name.empty());
            Y_ASSERT(!fullCommand.empty());
            YDIAG(Conf) << "Command for [" << name << "] = [" << fullCommand << "]" << Endl;
            Conf.CommandConf.SetValue(name, fullCommand);
            blockData.IsUserMacro = isUserMacro;
        }

        void SetCommandCommon(const TString& name, const TString& args, const TString& command, bool isUserMacro) {
            Y_ASSERT(!BlockStack.empty());
            auto& block = BlockStack.back();
            auto& blockData = (name == block.Name()) ? block.BlockData() : Conf.BlockData[name];
            const TString& fullCommand = MakeFullCommand(args, command);
            SetCommandInternal(blockData, name, fullCommand, isUserMacro);
        }

        void PostprocessForeachStatements() {
            YDIAG(Conf) << __FUNCTION__ << " started ..." << Endl;

            // Go through all calls which were encountered inside foreach block and
            // perform generation of pattern sections as follows:
            // 1) if the call is a call to a generic macro then "copy" pattern sections
            //    defined for corresponding generic macro
            // 2) otherwise, generate a "*" pattern section which will "redirect" the call
            //    to the specified macro
            for (const auto& info : ForeachCallInfo) {
                auto& sectionPatterns = GetOrInit(info.BlockData.SectionPatterns);
                if (!sectionPatterns.empty()) {
                    ReportError(TString::Join("Redefinition of patterns section in 'foreach' construct for [", info.BlockName, "]"));
                }
                TSectionPatterns nongenericPatterns;
                TSectionPatterns* patterns = nullptr;
                if (!Generics.contains(info.MacroName)) {
                    nongenericPatterns.try_emplace("*", info.MacroName);
                    patterns = &nongenericPatterns;
                } else {
                    const auto& blockData = Conf.BlockData[info.MacroName];
                    patterns = blockData.SectionPatterns.Get();
                }
                Y_ASSERT(patterns != nullptr);
                for (const auto& pattern : *patterns) {
                    const TString name = TString::Join(info.BlockName, pattern.first);
                    TString command = TString::Join("$", pattern.second, "(", info.ActualArgs, ")");
                    SetCommandInternal(info.BlockData, name, MakeFullCommand(info.LoopVar, command), true);
                    sectionPatterns[pattern.first] = name;
                }
            }

            YDIAG(Conf) << __FUNCTION__ << " finished ..." << Endl;
        }

    private:
        TFsPath SourceRoot;
        TFsPath BuildRoot;
        TFsPath ConfSourceRoot;
        TFsPath ConfBuildRoot;
        TMaybe<bool> YndexerIgnoreBuildRoot;
        TYmakeConfig& Conf;
        MD5* ConfMd5;
        TVector<TBlockDesc> BlockStack;
        TSet<TString> ConditionVars;
        TConditionStack ConditionStack;
        TVector<TVector<TString>> ConditionVarsStack;
        bool AllowConditionVars;
        THashMap<TString, TArgs> Macros;
        TSet<TString> Generics;
        TVector<TSelectAlts> SelectStack;
        TVector<TForeachCallInfo> ForeachCallInfo;
        TVector<TFsPath> ImportsStack;
        THashSet<TString> ImportsSet;
        bool LeftTrim = true;
    };

    class TConfigParser {
        using EToken = NYMake::NParsing::EToken;
        using TToken = NYMake::NParsing::TToken;
        enum class EScope {
            None,
            Macro,
            Module,
            MultiModule,
            Foreach
        };

    public:
        TConfigParser(TConfBuilder& builder, TStringBuf fileName, TStringBuf content)
            : Builder(builder)
            , FileName(fileName)
            , Lexer(content, NYMake::NParsing::EMode::Config)
        {
        }

        void Parse() {
            for (;;) {
                Space();
                if (Peek().Kind == EToken::End) {
                    return;
                }
                ParseEntry(true, false);
                Space();
                if (Peek().Kind == EToken::End) {
                    return;
                }
                Expect(EToken::Eol);
            }
        }

    private:
        const TToken& Peek() {
            if (!Token) {
                Token = Lexer.Next();
            }
            return *Token;
        }

        TToken Take() {
            auto token = Peek();
            Token.reset();
            return token;
        }

        void Space() {
            if (Peek().Kind == EToken::Space) {
                Take();
            }
        }

        bool At(TStringBuf text) {
            Space();
            return Peek().Text == text;
        }

        bool Accept(TStringBuf text) {
            if (!At(text)) {
                return false;
            }
            Take();
            return true;
        }

        bool AcceptAdjacent(TStringBuf text) {
            if (Peek().Text != text) {
                return false;
            }
            Take();
            return true;
        }

        void ExpectAdjacent(TStringBuf text) {
            if (!AcceptAdjacent(text)) {
                Error(TStringBuilder() << "expected '" << text << "' without intervening space");
            }
        }

        void Expect(TStringBuf text) {
            if (!Accept(text)) {
                Error(TStringBuilder() << "expected '" << text << "'");
            }
        }

        void Expect(EToken kind) {
            Space();
            if (Peek().Kind != kind) {
                Error("unexpected token");
            }
            Take();
        }
        size_t Offset() const {
            return Token ? Token->Offset : Lexer.Position();
        }

        [[noreturn]] void Error(TStringBuf message) {
            auto token = Peek();
            ReportConfigError(message, FileName, token.Line, token.Column);
            Y_UNREACHABLE();
        }

        static TString Clean(TStringBuf text) {
            if (text.find('\r') == TStringBuf::npos) {
                return TString(text);
            }
            TString result;
            result.reserve(text.size());
            for (char c : text) {
                if (c != '\r') {
                    result += c;
                }
            }
            return result;
        }

        static bool ReservedName(TStringBuf name) {
            for (TStringBuf keyword : {"when", "elsewhen", "otherwise", "select", "default", "foreach", "multimodule", "env"}) {
                if (name == keyword) {
                    return true;
                }
            }
            return false;
        }

        TToken Name(bool skipSpace = true) {
            if (skipSpace) {
                Space();
            }
            auto token = Peek();
            if (token.Kind != EToken::Identifier) {
                Error("expected an identifier");
            }
            if (ReservedName(token.Text)) {
                Error("reserved word used as an identifier");
            }
            Take();
            return token;
        }

        TString String(bool unquote = true) {
            Space();
            if (Peek().Kind != EToken::String) {
                Error("expected a string");
            }
            auto text = Take().Text;
            return Clean(unquote ? text.SubStr(1, text.size() - 2) : text);
        }

        TSourceRange Range(const TToken& token) {
            return {token.Line, token.Column + 1, token.Line, token.Column + token.Text.size()};
        }

        bool EntryAhead(TStringBuf source, bool simple) const {
            NYMake::NParsing::TLexer lexer(source, NYMake::NParsing::EMode::Config);
            auto next = [&lexer] {
                auto token = lexer.Next();
                if (token.Kind == EToken::Space) {
                    token = lexer.Next();
                }
                return token;
            };
            auto token = next();
            if (token.Kind == EToken::End || token.Kind == EToken::Eol ||
                token.Kind == EToken::Comment || token.Kind == EToken::DocComment) {
                return true;
            }
            if (token.Text == "}") {
                return Depth > 0;
            }
            if (token.Text == "@" && Depth == 0) {
                return lexer.Next().Text == "import";
            }
            bool property = false;
            if (token.Text == "." && !simple &&
                ((Depth == 1 && (Scope == EScope::Macro || Scope == EScope::Module || Scope == EScope::MultiModule)) ||
                 (Depth == 2 && InMultiModule && Scope == EScope::Module))) {
                token = next();
                property = true;
            } else if (token.Text == "[" && Scope == EScope::Foreach) {
                return next().Text == ".";
            }
            if (token.Kind != EToken::Identifier) {
                return false;
            }
            auto following = next();
            if (token.Text == "when" || token.Text == "select") {
                return following.Text == "(" && Scope != EScope::MultiModule && Scope != EScope::Foreach;
            }
            if (token.Text == "foreach") {
                return following.Text == "(" && !simple && Scope == EScope::Macro;
            }
            if (token.Text == "multimodule") {
                return Depth == 0 && following.Kind == EToken::Identifier;
            }
            if (ReservedName(token.Text)) {
                return false;
            }
            if (following.Text == "=" || following.Text == ":=" || following.Text == "+=" || following.Text == "-=") {
                return property || (Scope != EScope::MultiModule && Scope != EScope::Foreach);
            }
            if (following.Text == "(") {
                // The legacy grammar recognizes calls before checking their scope.
                return !simple;
            }
            return following.Kind == EToken::Identifier &&
                   ((Depth == 0 && (token.Text == "macro" || token.Text == "module" || token.Text == "multimodule")) ||
                    (Scope == EScope::MultiModule && token.Text == "module"));
        }

        TStringBuf RawValue(bool simple = false) {
            Y_ASSERT(!Token);
            const auto source = Lexer.Source();
            const size_t start = Lexer.Position();
            size_t end = start;
            size_t lineStart = start;
            while (end < source.size()) {
                if (source[end] == '\n') {
                    size_t last = end;
                    while (last > start && source[last - 1] == '\r') {
                        --last;
                    }
                    if (last == start || source[last - 1] != '\\') {
                        break;
                    }
                    // ANTLR prefers a new entry over consuming a continuation.
                    // A backslash inside a comment is never a continuation token.
                    if (source.SubStr(lineStart, end - lineStart).find('#') != TStringBuf::npos || EntryAhead(source.SubStr(end + 1), simple)) {
                        break;
                    }
                    lineStart = end + 1;
                }
                ++end;
            }
            Lexer.Advance(end - start);
            const auto value = source.SubStr(start, end - start);
            // Borrow unchanged values; stripping carriage returns needs owned storage.
            if (value.find('\r') == TStringBuf::npos) {
                return value;
            }
            RawValueStorage = Clean(value);
            return RawValueStorage;
        }

        bool DefinitionAhead() {
            auto lexer = Lexer;
            auto token = Token;
            Take();
            Space();
            const bool result = Peek().Kind == EToken::Identifier;
            Lexer = std::move(lexer);
            Token = token;
            return result;
        }

        void ParseEntry(bool top, bool simple) {
            Space();
            const auto kind = Peek().Kind;
            if (kind == EToken::DocComment) {
                if (simple) {
                    Error("documentation comment in a conditional block");
                }
                Doc += StripStringRight(StripStringLeft(Clean(Take().Text), EqualsStripAdapter('#')));
                Doc += '\n';
                return;
            }
            if (top && (At("macro") || At("module") || At("multimodule")) && DefinitionAhead()) {
                ParseDefinition();
                return;
            }
            if (top && Accept("@")) {
                if (!AcceptAdjacent("import")) {
                    Error("expected import");
                }
                if (Peek().Kind != EToken::Space) {
                    Error("expected space after @import");
                }
                Builder.ProcessImport(String());
                return;
            }
            Doc.clear();
            if (kind == EToken::Eol || kind == EToken::End) {
                return;
            }
            if (kind == EToken::Comment) {
                Take();
                return;
            }
            if (!simple && At("module") && Scope == EScope::MultiModule) {
                ParseDefinition();
                return;
            }
            if (!simple && Accept(".")) {
                if (!((Depth == 1 && (Scope == EScope::Macro || Scope == EScope::Module || Scope == EScope::MultiModule)) ||
                      (Depth == 2 && InMultiModule && Scope == EScope::Module))) {
                    Error("property is not allowed in this block");
                }
                auto name = Name(false);
                AssignmentOperator();
                auto value = RawValue();
                Builder.SetProperty(TString(name.Text), TString(NormalizeRval(value, Builder.GetLeftTrim())), Range(name));
                return;
            }
            if (At("when") || At("select")) {
                if (!simple && (Scope == EScope::MultiModule || Scope == EScope::Foreach)) {
                    Error("conditional is not allowed in this block");
                }
                if (At("when")) {
                    ParseWhen();
                } else {
                    ParseSelect();
                }
                return;
            }
            if (!simple && At("foreach")) {
                if (Scope != EScope::Macro) {
                    Error("foreach is only allowed in a macro");
                }
                Take();
                Expect("(");
                auto name = Name();
                Expect(":");
                Expect("$");
                auto variable = Name(false);
                Expect(")");
                Builder.EnterForeach(TString(name.Text), TString::Join("$", variable.Text));
                ParseBlock(EScope::Foreach, false);
                Builder.ExitForeach();
                return;
            }
            if (!simple && At("[")) {
                if (Scope != EScope::Foreach) {
                    Error("extension section outside foreach");
                }
                Take();
                TVector<TString> extensions;
                do {
                    Expect(".");
                    extensions.push_back(AcceptAdjacent("*") ? TString("*") : TString(Name(false).Text));
                } while (Accept(","));
                Expect("]");
                Expect("=");
                Builder.SetExtCommand(extensions, TString(RawValue()));
                return;
            }
            auto name = Name();
            if (Accept("(")) {
                if (simple || (Scope != EScope::Macro && Scope != EScope::Module && Scope != EScope::Foreach)) {
                    Error("macro call is not allowed in this block");
                }
                // Do not tokenize opaque arguments: quotes and comments belong to their value.
                const auto source = Lexer.Source();
                size_t start = Lexer.Position();
                while (start < source.size() && (source[start] == ' ' || source[start] == '\t' || source[start] == '\r')) {
                    ++start;
                }
                Lexer.Advance(start - Lexer.Position());
                size_t end = source.find('\n', start);
                if (end == TStringBuf::npos) {
                    end = source.size();
                }
                size_t close = end;
                while (close > start && (source[close - 1] == ' ' || source[close - 1] == '\t' || source[close - 1] == '\r')) {
                    --close;
                }
                if (close == start || source[close - 1] != ')') {
                    Error("expected ')' at the end of a macro call");
                }
                --close;
                Lexer.Advance(close + 1 - Lexer.Position());
                Builder.AddMacroCall(name.Text, Clean(source.SubStr(start, close - start)), Range(name));
                return;
            }
            if (!simple && (Scope == EScope::MultiModule || Scope == EScope::Foreach)) {
                Error("assignment is not allowed in this block");
            }
            auto op = AssignmentOperator();
            auto value = RawValue(simple);
            Builder.AddAssignment(TString(name.Text), op, TString(NormalizeRval(value, Builder.GetLeftTrim())));
        }

        TString AssignmentOperator() {
            Space();
            auto text = Peek().Text;
            if (text != "=" && text != ":=" && text != "+=" && text != "-=") {
                Error("expected an assignment operator");
            }
            Take();
            return TString(text);
        }

        void ParseBlock(EScope scope, bool simple) {
            Expect("{");
            const auto saved = Scope;
            Scope = scope;
            if (++Depth > 256) {
                Error("configuration block nesting limit exceeded");
            }
            Expect(EToken::Eol);
            for (;;) {
                Space();
                if (Accept("}")) {
                    break;
                }
                if (Peek().Kind == EToken::End) {
                    Error("expected '}'");
                }
                ParseEntry(false, simple);
                Expect(EToken::Eol);
            }
            --Depth;
            Scope = saved;
            Space();
        }

        TArgs FormalArguments(TStringBuf macroName) {
            TArgs result;
            bool specialization = false, regular = false;
            if (At(")")) {
                return result;
            }
            for (;;) {
                TArgDesc arg{};
                if (Peek().Kind == EToken::String) {
                    arg.Type = EArgType::Specialization;
                    arg.Name = String();
                    if (regular) {
                        Error("specialization parameters must precede regular arguments");
                    }
                    if (arg.Name == "*") {
                        Error("'*' is prohibited as a specialization parameter");
                    }
                    specialization = true;
                } else {
                    regular = true;
                    arg.Name = Name().Text;
                    arg.Type = EArgType::Positional;
                    if (AcceptAdjacent("...")) {
                        arg.Type = EArgType::Vararg;
                        arg.Name += "...";
                        result.push_back(std::move(arg));
                        if (!At(")")) {
                            Error("vararg must be the last parameter");
                        }
                        return result;
                    }
                    if (AcceptAdjacent("{")) {
                        do {
                            auto modifier = Peek().Text;
                            if (modifier != "input" && modifier != "output" && modifier != "tool" && modifier != "env") {
                                Error("unknown deep replacement modifier");
                            }
                            Take();
                            if (!arg.DeepReplace.empty()) {
                                arg.DeepReplace += ';';
                            }
                            arg.DeepReplace += modifier;
                        } while (AcceptAdjacent(";"));
                        ExpectAdjacent("}");
                        arg.DeepReplace += ':';
                    }
                    if (AcceptAdjacent("[]")) {
                        arg.Type = EArgType::NamedArray;
                    } else if (AcceptAdjacent("?")) {
                        arg.Type = EArgType::NamedBool;
                        Space();
                        if (Peek().Kind == EToken::String) {
                            arg.InitTrue = String();
                            Expect(":");
                            arg.InitFalse = String();
                        }
                    } else if (Accept("=")) {
                        arg.Type = EArgType::NamedScalar;
                        arg.InitFalse = String();
                    }
                    if (specialization && IsNamedArgType(arg.Type)) {
                        ReportConfigError(fmt::format("Named parameter [{}] is prohibited in the definition of partial macro specialization [{}]", arg.Name, macroName), FileName);
                    }
                }
                result.push_back(std::move(arg));
                if (!Accept(",")) {
                    return result;
                }
                Space();
            }
        }

        void ParseDefinition() {
            auto kind = Take().Text;
            const auto signatureStart = Offset();
            auto name = Name();
            if (kind == "macro") {
                Expect("(");
                auto arguments = FormalArguments(name.Text);
                Expect(")");
                if (Doc.empty()) {
                    Doc = TString::Join("@usage: ", Clean(Lexer.Source().SubStr(signatureStart, Offset() - signatureStart)));
                }
                Builder.EnterMacro(TString(name.Text), arguments, Range(name), Doc);
                ParseBlock(EScope::Macro, false);
                Builder.ExitMacro();
            } else if (kind == "module") {
                TString ancestor;
                if (Accept(":")) {
                    ancestor = Name().Text;
                }
                Space();
                if (Doc.empty()) {
                    Doc = TString::Join("@usage: ", Clean(Lexer.Source().SubStr(signatureStart, Offset() - signatureStart)));
                }
                Builder.EnterModule(TString(name.Text), ancestor, Range(name), Doc);
                ParseBlock(EScope::Module, false);
                Builder.ExitModule();
            } else {
                if (Doc.empty()) {
                    Doc = TString::Join("@usage: ", Clean(Lexer.Source().SubStr(signatureStart, Offset() - signatureStart)));
                }
                Builder.EnterMultiModule(TString(name.Text), Range(name), Doc);
                const bool saved = InMultiModule;
                InMultiModule = true;
                ParseBlock(EScope::MultiModule, false);
                InMultiModule = saved;
                Builder.ExitMultiModule();
            }
        }

        bool BranchAhead() {
            auto lexer = Lexer;
            auto token = Token;
            Space();
            if (Peek().Kind == EToken::Eol) {
                do {
                    Take();
                    Space();
                    if (Peek().Kind == EToken::Comment) {
                        Take();
                    }
                } while (Peek().Kind == EToken::Eol);
                if (At("elsewhen") || At("otherwise")) {
                    return true;
                }
            }
            Lexer = std::move(lexer);
            Token = token;
            return false;
        }

        TString Condition() {
            Builder.EnterCondition();
            Expect("(");
            auto result = LogicExpr();
            Expect(")");
            Builder.ExitCondition();
            return result;
        }

        void ParseWhen() {
            Take();
            Builder.PushConditionVars();
            auto condition = Condition();
            Builder.AppendCondition(condition);
            ParseBlock(Scope, true);
            size_t clauses = 1;
            while (BranchAhead()) {
                const bool otherwise = Take().Text == "otherwise";
                Builder.RemoveLastCondition();
                Builder.AppendCondition(condition, true);
                if (!otherwise) {
                    condition = Condition();
                    Builder.AppendCondition(condition);
                    ++clauses;
                }
                ParseBlock(Scope, true);
                if (otherwise) {
                    break;
                }
            }
            Builder.RemoveLastCondition(clauses);
            Builder.PopConditionVars();
        }

        void ParseSelect() {
            Take();
            Expect("(");
            Expect("$");
            auto variable = Name(false);
            Expect(")");
            Expect("{");
            Builder.PushConditionVars();
            Builder.PushSelect();
            Builder.EnterCondition();
            Builder.AddConditionVar(TString(variable.Text));
            Builder.ExitCondition();
            Expect(EToken::Eol);
            bool any = false, sawDefault = false;
            for (;;) {
                Space();
                if (Accept("}")) {
                    break;
                }
                if (Peek().Kind == EToken::Eol || Peek().Kind == EToken::Comment) {
                    if (Peek().Kind == EToken::Comment) {
                        Take();
                    }
                    Expect(EToken::Eol);
                    continue;
                }
                if (sawDefault) {
                    Error("alternative after default");
                }
                TString condition;
                if (Accept("default")) {
                    if (!any) {
                        Error("select needs an alternative before default");
                    }
                    condition = Builder.ComputeDefaultAlt();
                    sawDefault = true;
                } else {
                    condition = "(";
                    do {
                        if (condition.size() > 1) {
                            condition += "||";
                        }
                        condition += TString::Join("$", variable.Text, "==", String(false));
                    } while (Accept("|"));
                    condition += ')';
                    Builder.AddSelectAlt(condition);
                    any = true;
                }
                Expect("?");
                Builder.AppendCondition(condition);
                ParseBlock(Scope, true);
                Builder.RemoveLastCondition();
                Expect(EToken::Eol);
            }
            if (!any) {
                Error("select needs at least one alternative");
            }
            Builder.PopSelect();
            Builder.PopConditionVars();
            Space();
        }

        TString LogicExpr() {
            Space();
            auto lexer = Lexer;
            auto token = Token;
            bool membership = false;
            if (Peek().Text == "$" || Peek().Kind == EToken::String) {
                if (Accept("$")) {
                    Name(false);
                } else {
                    Take();
                }
                if (Peek().Kind == EToken::Space) {
                    Take();
                    membership = Peek().Text == "in";
                }
            }
            Lexer = std::move(lexer);
            Token = token;
            if (!membership) {
                return LogicOr();
            }

            Expect("$");
            const auto name = Name(false);
            Builder.AddConditionVar(TString(name.Text));
            Expect("in");
            if (Peek().Kind != EToken::Space) {
                Error("expected space after 'in'");
            }
            Expect("[");
            if (At("]")) {
                Error("empty array in 'in' expression");
            }
            TString result("(");
            do {
                if (result.size() > 1) {
                    result += "||";
                }
                result += TString::Join("$", name.Text, "==", String(false));
            } while (Accept(","));
            Expect("]");
            result += ')';
            return result;
        }

        TString LogicOr() {
            auto result = LogicAnd();
            while (Accept("||")) {
                result += TString::Join("||", LogicAnd());
            }
            return result;
        }

        TString LogicAnd() {
            auto result = LogicRel();
            while (Accept("&&")) {
                result += TString::Join("&&", LogicRel());
            }
            return result;
        }

        TString LogicRel() {
            auto result = LogicNot();
            if (At("==") || At("!=")) {
                auto op = Take().Text;
                result += TString::Join(op, LogicNot());
            }
            return result;
        }

        TString LogicNot() {
            if (Accept("!")) {
                return TString::Join("!", LogicTerm());
            }
            return LogicTerm();
        }

        TString LogicTerm() {
            if (++ExpressionDepth > 256) {
                Error("condition nesting limit exceeded");
            }
            Y_DEFER {
                --ExpressionDepth;
            };
            TString result;
            if (Accept("$")) {
                auto name = Name(false);
                Builder.AddConditionVar(TString(name.Text));
                result = TString::Join("$", name.Text);
            } else if (At("(")) {
                Take();
                result = TString::Join("(", LogicExpr());
                Expect(")");
                result += ')';
            } else {
                result = String(false);
            }
            return result;
        }
        TConfBuilder& Builder;
        TStringBuf FileName;
        NYMake::NParsing::TLexer Lexer;
        std::optional<TToken> Token;
        TString Doc;
        TString RawValueStorage;
        EScope Scope = EScope::None;
        size_t Depth = 0;
        size_t ExpressionDepth = 0;
        bool InMultiModule = false;
    };

    void ParseConfig(TConfBuilder& builder, TStringBuf fileName, TStringBuf content) {
        try {
            TConfigParser(builder, fileName, content).Parse();

            builder.UpdateConfMd5(content);

            // Postprocessing of resulting configuration should work only for the most-upper
            // conf file (that is all inlcude statements have been already processed)
            if (builder.IsTopLevel()) {
                builder.AddToImportedFiles("-", CalculateConfMd5(content), 0);
                builder.Postprocess();
            }
        } catch (TConfigError&) {
            throw;
        } catch (std::exception& ex) {
            ythrow TConfigError() << "std::exception caught during parsing of YMake configuration: " << ex.what() << Endl;
        } catch (...) {
            ythrow TConfigError() << "Unknown exception caught during parsing of YMake configuration" << Endl;
        }
    }

    void LoadConfig(TStringBuf fileName, TStringBuf content, TStringBuf sourceRoot, TStringBuf buildRoot, TYmakeConfig& conf, MD5* confMd5) {
        TConfBuilder builder(sourceRoot, buildRoot, fileName, conf, confMd5);
        ParseConfig(builder, fileName, content);
    }
}

void TYmakeConfig::LoadConfig(TStringBuf path, TStringBuf sourceRoot, TStringBuf buildRoot, MD5& confData) {
    TIFStream is{TString(path)};
    TString content = is.ReadAll();
    TConfBuilder::ValidateUtf8(content, path);

    ::LoadConfig(path, content, sourceRoot, buildRoot, *this, &confData);
}

void TYmakeConfig::LoadConfigFromContext(TStringBuf context) {
    TConfBuilder::ValidateUtf8(context, "<context>"sv);
    ::LoadConfig(""sv, context, ""sv, ""sv, *this, nullptr);
}

namespace NConfReader {
    void UpdateConfMd5(TStringBuf content, MD5& confData) {
        ::UpdateConfMd5(content, &confData, nullptr);
    }

    TString CalculateConfMd5(TStringBuf content, TString* ignoredHashContenet) {
        MD5 md5;
        char buffer[33];
        ::UpdateConfMd5(content, &md5, ignoredHashContenet);
        md5.End(buffer);
        return buffer;
    }
}
