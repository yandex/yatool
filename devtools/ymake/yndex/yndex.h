#pragma once

#include <util/generic/hash.h>
#include <util/generic/string.h>
#include <util/generic/vector.h>
#include <util/generic/strbuf.h>
#include <util/ysaveload.h>

namespace NYndex {
    struct TSourceRange {
        size_t Line;
        size_t Column;
        size_t EndLine;
        size_t EndColumn;

        Y_SAVELOAD_DEFINE(
            Line,
            Column,
            EndLine,
            EndColumn
        );

        bool IsValid() const {
            return Line <= EndLine && Column <= EndColumn;
        }
    };

    struct TSourceLocation {
        TSourceLocation()
            : Range({0, 0, 0, 0})
        {
        }

        TSourceLocation(const TString& file, const TSourceRange& range)
            : File(file)
            , Range(range)
        {
        }

        bool IsValid() const {
            return !File.empty();
        }

        Y_SAVELOAD_DEFINE(
            File,
            Range
        );

        TString File;
        TSourceRange Range;
    };

    enum class EDefinitionType {
        Macro /* "macro" */,
        Module /* "module" */,
        MultiModule /* "multimodule" */,
        Variable /* "variable" */,
        Property /* "property" */,
    };

    struct TDefinition {
        TDefinition() = default;
        TDefinition(const TString& name, const TString& docText, const TSourceLocation& link, EDefinitionType type)
            : Name(name)
            , DocText(docText)
            , Link(link)
            , Type(type)
        {
        }

        Y_SAVELOAD_DEFINE(
            Name,
            DocText,
            Link,
            Type
        );

        TString Name;
        TString DocText;
        TSourceLocation Link;
        EDefinitionType Type;
    };

    class TDefinitions {
    public:
        bool AddDefinition(const TString& name, const TString& file, const TSourceRange& range, const TString& docText, EDefinitionType type);
        bool AddDefinition(const TDefinition& def);

        const TDefinition* GetDefinition(const TString& name) const;

        const THashMap<TString, TDefinition>& GetDefinitions() const;

        void Disable() {
            Enabled = false;
        }

        bool AreEnabled() const {
            return Enabled;
        }

        void Clear() {
            Definitions.clear();
            Enabled = true;
        }

        Y_SAVELOAD_DEFINE(Definitions, Enabled);

    private:
        THashMap<TString, TDefinition> Definitions;
        bool Enabled = true;
    };

    struct TReference {
        TReference() = default;
        TReference(const TString& name, const TSourceLocation& link)
            : Name(name)
            , Link(link)
        {
        }

        Y_SAVELOAD_DEFINE(
            Name,
            Link
        );

        TString Name;
        TSourceLocation Link;
    };

    class TReferences {
    public:
        void AddReference(const TString& name, const TString& file, const TSourceRange& range);

        const TVector<TReference>& GetReferences() const {
            return References;
        };

        void Disable() {
            Enabled = false;
        }

        bool AreEnabled() const {
            return Enabled;
        }

        void Clear() {
            References.clear();
            Enabled = true;
        }

        Y_SAVELOAD_DEFINE(References, Enabled);

    private:
        TVector<TReference> References;
        bool Enabled = true;
    };

    enum class ERecordType {
        Node,
        Use
    };

    struct TYndexRecord {
        TYndexRecord(ERecordType type, const TSourceRange& range, const TDefinition& def);

        ERecordType Type;
        TSourceRange Range;
        TSourceLocation Link;
        THashMap<TString, TString> Properties;
    private:
        static const std::pair<TString, TString> ExtractUsage(TStringBuf docText);
    };

    class TYndex {
    public:
        TYndex(const TDefinitions& Definitions, const TReferences& References);

        bool AddReference(const TString& name, const TString& file, const TSourceRange& range);

        void WriteJSON(IOutputStream& out) const;

    private:
        using TFileYndex = TVector<TYndexRecord>;

        const TDefinitions& Definitions;
        THashMap<TString, TFileYndex> Files;
    };
}
