#include <devtools/ymake/lang/confreader.h>

#include <contrib/libs/antlr4_cpp_runtime/src/antlr4-runtime.h>

#include "devtools/ymake/lang/properties.h"
#include <devtools/ymake/lang/TConfLexer.h>
#include <devtools/ymake/lang/TConfParser.h>
#include <devtools/ymake/lang/TConfBaseVisitor.h>

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

    TCmdProperty::TKeywords CollectKeywords(const TArgs& arguments) {
        TCmdProperty::TKeywords keywords;
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
                    keywords.AddKeyword(arg.Name, 0, 0, TString(), kwPresent, kwMissing);
                    break;
                case EArgType::NamedScalar:
                    if (!arg.InitFalse.empty()) {
                        kwMissing = arg.InitFalse;
                    }
                    keywords.AddKeyword(arg.Name, 1, 1, arg.DeepReplace, kwPresent, kwMissing);
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

    class TArgsBuilder {
    public:
        using TArgsList = std::span<NConfReader::TConfParser::FormalArgContext* const>;

        TArgsBuilder(TArgsList args, TStringBuf fileName, TStringBuf macroName) noexcept
            : RemainingArgs_{args}
            , FileName_{fileName}
            , MacroName_{macroName}
        {}

        TArgsBuilder& ConsumeSpecArgs() {
            if (RemainingArgs_.empty())
                return *this;

            while(auto specArg = RemainingArgs_.front()->specArg()) {
                Arguments_.emplace_back(TArgDesc{
                    .Type = EArgType::Specialization,
                    .Name = TString(specArg->string()->stringContent()->getText()),
                    .DeepReplace = {},
                    .InitTrue = {},
                    .InitFalse = {},
                });
                if (Arguments_.back().Name == "*") {
                    ReportConfigError(fmt::format(
                        "This value {} is prohibited for use as specialization parameter in the definition of macro [{}]", RemainingArgs_.front()->getText(), MacroName_
                    ), FileName_);
                }
                RemainingArgs_ = RemainingArgs_.subspan(1);
            }
            return *this;
        }

        TArgsBuilder& ConsumeRegularArgs() {
            const bool specArgSeen = std::ranges::any_of(Arguments_, [](const auto& arg) {return arg.Type == EArgType::Specialization;});
            while (!RemainingArgs_.empty()) {
                const auto arg = RemainingArgs_.front();
                RemainingArgs_ = RemainingArgs_.subspan(1);

                Arguments_.emplace_back(TArgDesc());
                auto& argDesc = Arguments_.back();
                argDesc.Name = TString(arg->argName()->getText());
                if (auto deepReplace = arg->deepReplace()) {
                    argDesc.DeepReplace = TString(deepReplace->modifiers()->getText());
                    Y_ASSERT(!argDesc.DeepReplace.empty());
                    argDesc.DeepReplace += ":";
                }
                if (arg->arraySpec()) {
                    argDesc.Type = EArgType::NamedArray;
                } else if (arg->boolSpec()) {
                    argDesc.Type = EArgType::NamedBool;
                    if (auto boolInit = arg->boolInit()) {
                        auto initTrue = boolInit->initTrue;
                        Y_ASSERT(initTrue != nullptr);
                        argDesc.InitTrue = TString(initTrue->stringContent()->getText());
                        auto initFalse = boolInit->initFalse;
                        Y_ASSERT(initFalse != nullptr);
                        argDesc.InitFalse = TString(initFalse->stringContent()->getText());
                    }
                } else if (auto defaultInit = arg->defaultInit()) {
                    argDesc.Type = EArgType::NamedScalar;
                    argDesc.InitFalse = TString(defaultInit->string()->stringContent()->getText());
                } else if (auto specArg = arg->specArg()) {
                    ReportConfigError(fmt::format(
                        "Specialization parameter [{}] must precede all positional and named arguments in the definition of macro [{}]", arg->getText(), MacroName_
                    ), FileName_);
                } else {
                    argDesc.Type = EArgType::Positional;
                }
                if (specArgSeen && IsNamedArgType(argDesc.Type)) {
                    ReportConfigError(fmt::format(
                        "Named parameter [{}] is prohibited in the definition of partial macro specialization [{}]", argDesc.Name, MacroName_
                    ), FileName_);
                }
            }
            return *this;
        }

        TArgsBuilder& ConsumeVarargs(NConfReader::TConfParser::VarargContext* vararg) {
            if (vararg) {
                Arguments_.emplace_back(TArgDesc{
                    .Type = EArgType::Vararg,
                    .Name = TString{vararg->getText()},
                    .DeepReplace = {},
                    .InitTrue = {},
                    .InitFalse = {},
                });
            }
            return *this;
        }

        TArgs Build() noexcept {
            return std::move(Arguments_);
        }

    private:
        TArgsList RemainingArgs_;
        TStringBuf FileName_;
        TStringBuf MacroName_;
        TArgs Arguments_;
    };

    TArgs FillArguments(NConfReader::TConfParser::FormalArgsContext* formalArgs, TStringBuf fileName, TStringBuf macroName) {
        if (!formalArgs) {
            return {};
        }

        return TArgsBuilder{formalArgs->formalArg(), fileName, macroName}
            .ConsumeSpecArgs()
            .ConsumeRegularArgs()
            .ConsumeVarargs(formalArgs->vararg())
            .Build();
    }

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
            if (!blockData.CmdProps->ArgNames().empty()) {
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
            } else if (name == NProperties::STRUCT_CMD) {
                if (Conf.RenderSemantics)
                    return;
                if (value == "yes") {
                    BlockStack.back().BlockData().StructCmd = true;
                } else if (value == "no") {
                    BlockStack.back().BlockData().StructCmd = false;
                } else {
                    ReportError(TString::Join("Unexpected value [", value, "] for macro property [", block.Name(), ".", name, "]"));
                }
                return;
            } else if (name == NProperties::STRUCT_SEM) {
                if (!Conf.RenderSemantics)
                    return;
                if (value == "yes") {
                    BlockStack.back().BlockData().StructCmd = true;
                } else if (value == "no") {
                    BlockStack.back().BlockData().StructCmd = false;
                } else {
                    ReportError(TString::Join("Unexpected value [", value, "] for macro property [", block.Name(), ".", name, "]"));
                }
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
                       name == NProperties::STRUCT_CMD ||
                       name == NProperties::STRUCT_SEM ||
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

    class TConfReaderVisitor : public TConfBaseVisitor
    {
    public:
        TConfReaderVisitor(TConfBuilder& builder, TStringBuf fileName)
            : Builder{builder}
            , FileName{fileName}
        {
        }

        template<typename T, typename Ctx>
        T visitAs(Ctx* ctx) {
            return std::any_cast<T>(visit(ctx));
        }

        antlrcpp::Any visitDocComment(TConfParser::DocCommentContext *ctx) override {
            YDIAG(Conf) << "DocComment: " << ctx->getText() << Endl;
            return nullptr;
        }

        antlrcpp::Any visitImportStmt(TConfParser::ImportStmtContext *ctx) override {
            Builder.ProcessImport(ctx->string()->stringContent()->getText());
            return nullptr;
        }

        antlrcpp::Any visitStmt(TConfParser::StmtContext *ctx) override {
            if (auto docComment = ctx->docComment()) {
                DocStream << StripStringRight(StripStringLeft(TString(docComment->getText()), EqualsStripAdapter('#'))) << "\n";
            } else {
                DocStream.Clear();
            }
            visitChildren(ctx);
            return nullptr;
        }

        antlrcpp::Any visitAssignStmt(TConfParser::AssignStmtContext *ctx) override {
            Builder.AddAssignment(TString(ctx->ident()->getText()),
                                  TString(ctx->assignOp()->getText()),
                                  TString(NormalizeRval(ctx->rvalue()->getText(), Builder.GetLeftTrim())));
            return nullptr;
        }

        antlrcpp::Any visitCallStmt(TConfParser::CallStmtContext *ctx) override {
            TString macroName(ctx->macroName()->getText());
            TString args;
            if (auto actualArgs = ctx->actualArgs()) {
                args = actualArgs->getText();
            }

            antlr4::Token* token = ctx->macroName()->getStart();
            TSourceRange range {
                token->getLine(),
                token->getCharPositionInLine() + 1,
                token->getLine(),
                token->getCharPositionInLine() + macroName.length()
            };

            Builder.AddMacroCall(macroName, args, range);

            return nullptr;
        }

        antlrcpp::Any visitWhenStmt(TConfParser::WhenStmtContext *ctx) override {
            auto whenClause = ctx->whenClause();
            Y_ASSERT(whenClause != nullptr);

            Builder.PushConditionVars();

            Builder.EnterCondition();
            TString condition = std::any_cast<std::string>(visit(whenClause->logicExpr()));
            Builder.ExitCondition();
            Builder.AppendCondition(condition);
            visitChildren(whenClause->simpleBlockStmt());
            const auto& elseWhenClause = ctx->elseWhenClause();
            for (auto clause : elseWhenClause) {
                Builder.RemoveLastCondition();
                Builder.AppendCondition(condition, /* revert */ true);
                Builder.EnterCondition();
                condition = visitAs<std::string>(clause->logicExpr());
                Builder.ExitCondition();
                Builder.AppendCondition(condition);
                visitChildren(clause->simpleBlockStmt());
            }
            if (auto otherwiseClause = ctx->otherwiseClause()) {
                Builder.RemoveLastCondition();
                Builder.AppendCondition(condition, /* revert */ true);
                visitChildren(otherwiseClause->simpleBlockStmt());
            }
            Builder.RemoveLastCondition(1 + elseWhenClause.size());

            Builder.PopConditionVars();

            return nullptr;
        }

        antlrcpp::Any visitSelectStmt(TConfParser::SelectStmtContext *ctx) override {
            TString selectVar(ctx->varRef()->varName()->getText());
            Builder.PushConditionVars();
            Builder.PushSelect();
            Builder.EnterCondition();
            Builder.AddConditionVar(selectVar);
            Builder.ExitCondition();
            for (auto alt : ctx->alternativeClause()) {
                alt->selectVar = &selectVar;
                visit(alt);
            }
            if (auto defaultAlt = ctx->defaultClause()) {
                auto defaultCondition = Builder.ComputeDefaultAlt();
                defaultAlt->defaultCondition = &defaultCondition;
                visit(defaultAlt);
            }
            Builder.PopSelect();
            Builder.PopConditionVars();
            return nullptr;
        }

        antlrcpp::Any visitAlternativeClause(TConfParser::AlternativeClauseContext *ctx) override {
            Y_ASSERT(ctx->selectVar != nullptr);

            TStringStream condition("(");
            bool firstTime = true;
            for (auto value : ctx->string()) {
                if (firstTime) {
                    firstTime = false;
                } else {
                    condition << "||";
                }
                condition << "$" << *(ctx->selectVar) << "==" << value->getText();
            }
            condition << ")";

            auto conditionStr = condition.Str();
            Builder.AddSelectAlt(conditionStr);

            Builder.AppendCondition(conditionStr);
            visitChildren(ctx->simpleBlockStmt());
            Builder.RemoveLastCondition();

            return nullptr;
        }

        antlrcpp::Any visitDefaultClause(TConfParser::DefaultClauseContext *ctx) override {
            Y_ASSERT(ctx->defaultCondition != nullptr);

            Builder.AppendCondition(*(ctx->defaultCondition));
            visitChildren(ctx->simpleBlockStmt());
            Builder.RemoveLastCondition();
            return nullptr;
        }

        antlrcpp::Any visitForeachStmt(TConfParser::ForeachStmtContext *ctx) override {
            Builder.EnterForeach(TString(ctx->ident()->getText()), TString(ctx->varRef()->getText()));
            visitChildren(ctx->blockStmt());
            Builder.ExitForeach();
            return nullptr;
        }

        antlrcpp::Any visitExtStmt(TConfParser::ExtStmtContext *ctx) override {
            TVector<TString> exts;
            TString command(ctx->rvalue()->getText());
            for (const auto& ext : ctx->ext()) {
                exts.emplace_back(TString(ext->extName()->getText()));
            }
            Builder.SetExtCommand(exts, command);
            return nullptr;
        }

        antlrcpp::Any visitMacroDefSignature(TConfParser::MacroDefSignatureContext *ctx) override {
            return nullptr;
        }

        antlrcpp::Any visitMacroDef(TConfParser::MacroDefContext *ctx) override {
            Y_ASSERT(ScopeKind == EScopeKind::None);
            ScopeKind = EScopeKind::Macro;

            TString macroName(ctx->macroDefSignature()->macroName()->getText());

            antlr4::Token* token = ctx->macroDefSignature()->macroName()->getStart();
            TSourceRange range {
                token->getLine(),
                token->getCharPositionInLine() + 1,
                token->getLine(),
                token->getCharPositionInLine() + macroName.length()
            };

            if (DocStream.Str().empty()) {
                DocStream << "@usage: " << ctx->macroDefSignature()->getText();
            }

            Builder.EnterMacro(
                macroName,
                FillArguments(ctx->macroDefSignature()->formalArgs(), FileName, macroName),
                range,
                DocStream.Str()
            );
            visitChildren(ctx->block());
            Builder.ExitMacro();

            ScopeKind = EScopeKind::None;
            return nullptr;
        }

        antlrcpp::Any visitModuleDefSignature(TConfParser::ModuleDefSignatureContext *ctx) override {
            return nullptr;
        }

        antlrcpp::Any visitModuleDef(TConfParser::ModuleDefContext *ctx) override {
            Y_ASSERT(ScopeKind == EScopeKind::None || ScopeKind == EScopeKind::MultiModule);
            auto savedScopeKind = ScopeKind;
            ScopeKind = EScopeKind::Module;

            TString moduleName(ctx->moduleDefSignature()->moduleName()->getText());
            TString ancestorName;
            if (auto ancestor = ctx->moduleDefSignature()->ancestor()) {
                ancestorName = ancestor->moduleName()->getText();
            }

            antlr4::Token* token = ctx->moduleDefSignature()->moduleName()->getStart();
            TSourceRange range {
                token->getLine(),
                token->getCharPositionInLine() + 1,
                token->getLine(),
                token->getCharPositionInLine() + moduleName.length()
            };

            if (DocStream.Str().empty()) {
                DocStream << "@usage: " << ctx->moduleDefSignature()->getText();
            }

            Builder.EnterModule(moduleName, ancestorName, range, DocStream.Str());
            visitChildren(ctx->block());
            Builder.ExitModule();

            ScopeKind = savedScopeKind;
            return nullptr;
        }

        antlrcpp::Any visitMultiModuleDefSignature(TConfParser::MultiModuleDefSignatureContext *ctx) override {
            return nullptr;
        }

        antlrcpp::Any visitMultiModuleDef(TConfParser::MultiModuleDefContext *ctx) override {
            Y_ASSERT(ScopeKind == EScopeKind::None);
            ScopeKind = EScopeKind::MultiModule;

            TString name(ctx->multiModuleDefSignature()->moduleName()->getText());

            antlr4::Token* token = ctx->multiModuleDefSignature()->moduleName()->getStart();
            TSourceRange range {
                token->getLine(),
                token->getCharPositionInLine() + 1,
                token->getLine(),
                token->getCharPositionInLine() + name.length()
            };

            if (DocStream.Str().empty()) {
                DocStream << "@usage: " << ctx->multiModuleDefSignature()->getText();
            }

            Builder.EnterMultiModule(name, range, DocStream.Str());
            visitChildren(ctx->block());
            Builder.ExitMultiModule();

            ScopeKind = EScopeKind::None;
            return nullptr;
        }

        antlrcpp::Any visitBlock(TConfParser::BlockContext *ctx) override {
            Y_ASSERT(BlockLevel >= 0);
            ++BlockLevel;
            visitChildren(ctx);
            Y_ASSERT(BlockLevel > 0);
            --BlockLevel;
            return nullptr;
        }

        antlrcpp::Any visitPropStmt(TConfParser::PropStmtContext *ctx) override {
            TString propName(ctx->propName()->getText());
            TString propValue(ctx->propValue()->getText());
            antlr4::Token* token = ctx->propName()->getStart();
            TSourceRange range {
                token->getLine(),
                token->getCharPositionInLine() + 1,
                token->getLine(),
                token->getCharPositionInLine() + propName.length()
            };
            Builder.SetProperty(propName, propValue, range);
            return nullptr;
        }

        antlrcpp::Any visitLogicVarRef(TConfParser::LogicVarRefContext *ctx) override {
            auto varRef = ctx->varRef();
            Y_ASSERT(varRef != nullptr);
            auto varName = varRef->varName();
            Y_ASSERT(varName != nullptr);
            Builder.AddConditionVar(TString(varName->getText()));
            return varRef->getText();
        }

        antlrcpp::Any visitLogicTerm(TConfParser::LogicTermContext *ctx) override {
            if (auto logicVarRef = ctx->logicVarRef()) {
                return visit(logicVarRef);
            } else if (ctx->string() != nullptr) {
                return ctx->getText();
            } else if (auto logicExpr = ctx->logicExpr()) {
                std::string result("(");
                result += visitAs<std::string>(logicExpr);
                result += ')';
                return result;
            } else {
                Y_ASSERT(false && "Unreachable code");
            }
            return nullptr;
        }

        antlrcpp::Any visitLogicNot(TConfParser::LogicNotContext *ctx) override {
            std::string result;
            if (auto negationOp = ctx->negationOp()) {
                result += negationOp->getText();
            }
            result += visitAs<std::string>(ctx->logicTerm());
            return result;
        }

        antlrcpp::Any visitLogicRel(TConfParser::LogicRelContext *ctx) override {
            const auto& opnds = ctx->logicNot();
            Y_ASSERT(opnds.size() == 1 || opnds.size() == 2);
            std::string result = visitAs<std::string>(opnds[0]);
            if (opnds.size() > 1) {
                auto relationOp = ctx->relationOp();
                Y_ASSERT(relationOp != nullptr);
                result += relationOp->getText();
                result += visitAs<std::string>(opnds[1]);
            }
            return result;
        }

        antlrcpp::Any visitLogicAnd(TConfParser::LogicAndContext *ctx) override {
            const auto& opnds = ctx->logicRel();
            Y_ASSERT(!opnds.empty());
            std::string result = visitAs<std::string>(opnds[0]);
            for (auto i = opnds.begin() + 1; i != opnds.end(); ++i) {
                result += "&&";
                result += visitAs<std::string>(*i);
            }
            return result;
        }

        antlrcpp::Any visitLogicOr(TConfParser::LogicOrContext *ctx) override {
            const auto& opnds = ctx->logicAnd();
            Y_ASSERT(!opnds.empty());
            std::string result = visitAs<std::string>(opnds[0]);
            for (auto i = opnds.begin() + 1; i != opnds.end(); ++i) {
                result += "||";
                result += visitAs<std::string>(*i);
            }
            return result;
        }

        antlrcpp::Any visitLogicIn(TConfParser::LogicInContext *ctx) override {
            auto logicVarRef = ctx->logicVarRef();
            Y_ASSERT(logicVarRef != nullptr);
            auto stringArray = ctx->stringArray();
            Y_ASSERT(stringArray != nullptr);
            const auto& values = stringArray->string();
            Y_ASSERT(!values.empty());
            std::string result("(");
            std::string var = visitAs<std::string>(logicVarRef);
            bool firstTime = true;
            for (auto& value : values) {
                if (firstTime) {
                    firstTime = false;
                } else {
                    result += "||";
                }
                result += var;
                result += "==";
                result += value->getText();
            }
            result += ')';
            return result;
        }

        antlrcpp::Any visitLogicExpr(TConfParser::LogicExprContext *ctx) override {
            return visitChildren(ctx);
        }

    private:
        int BlockLevel{0};
        enum class EScopeKind {
            None = 0,
            Macro,
            Module,
            MultiModule
        } ScopeKind{EScopeKind::None};
        TConfBuilder& Builder;
        TStringBuf FileName;
        TStringStream DocStream;
    };

    void ParseConfig(TConfBuilder& builder, TStringBuf fileName, TStringBuf content) {
        try {
            antlr4::ANTLRInputStream input(content);
            TConfLexer lexer(&input);
            antlr4::CommonTokenStream tokens(&lexer);
            tokens.fill();
            TConfParser parser(&tokens);

            // This dummy error handler just stops parsing the tree when
            // the first syntax error is encountered
            struct TConfReaderErrorListener: public antlr4::BaseErrorListener {
                explicit TConfReaderErrorListener(TStringBuf fileName) : FileName{fileName} {}
                void syntaxError(antlr4::Recognizer* /* recognizer */,
                                 antlr4::Token* /* token */,
                                 size_t line,
                                 size_t column,
                                 const std::string& message,
                                 std::exception_ptr /* e */) override
                {
                    ReportConfigError(message, FileName, line, column);
                }
                TStringBuf FileName;
            } errorListener{fileName};

            parser.addErrorListener(&errorListener);
            TConfReaderVisitor visitor(builder, fileName);
            visitor.visit(parser.entries());

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
