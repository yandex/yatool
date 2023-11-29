#pragma once

#include <util/generic/hash.h>
#include <util/generic/string.h>
#include <util/generic/vector.h>
#include <util/generic/strbuf.h>

namespace NYndex {
    struct TSourceRange {
        size_t Line;
        size_t Column;
        size_t EndLine;
        size_t EndColumn;
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

        TString File;
        TSourceRange Range;
    };

    enum class EDefinitionType {
        Macro /* "macro" */,
        Module /* "module" */,
        MultiModule /* "multimodule" */,
        Variable /* "variable" */
    };

    struct TDefinition {
        TDefinition(const TString& name, const TString& docText, const TSourceLocation& link, EDefinitionType type)
            : Name(name)
            , DocText(docText)
            , Link(link)
            , Type(type)
        {
        }

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

    private:
        THashMap<TString, TDefinition> Definitions;
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
        TYndex(const TDefinitions& Definitions);

        bool AddReference(const TString& name, const TString& file, const TSourceRange& range);

        void WriteJSON(IOutputStream& out) const;

    private:
        using TFileYndex = TVector<TYndexRecord>;

        const TDefinitions& Definitions;
        THashMap<TString, TFileYndex> Files;
    };
}
