#include "yndex.h"

#include <util/string/strip.h>
#include <library/cpp/json/json_writer.h>

using namespace NYndex;

bool TDefinitions::AddDefinition(const TString& name, const TString& file, const TSourceRange& range, const TString& docText, EDefinitionType type) {
    return AddDefinition(TDefinition(
        name,
        docText,
        TSourceLocation(file, range),
        type
    ));
}

bool TDefinitions::AddDefinition(const TDefinition& def) {
    if (!AreEnabled()) {
        return false;
    }

    auto it = Definitions.find(def.Name);
    if (it != Definitions.end()) {
        bool replaceDefinition = it->second.DocText.empty();
        if (!replaceDefinition) {
            return false;
        }
        Definitions.erase(it);
    }

    if (!def.Link.IsValid()) {
        return false;
    }

    Definitions.try_emplace(def.Name, def);

    return true;
}

const NYndex::TDefinition* TDefinitions::GetDefinition(const TString& name) const {
    auto it = Definitions.find(name);

    if (it == Definitions.end()) {
        return nullptr;
    }

    return &it->second;
}

const THashMap<TString, TDefinition>& TDefinitions::GetDefinitions() const {
    return Definitions;
}

const std::pair<TString, TString> TYndexRecord::ExtractUsage(TStringBuf docText) {
    TStringBuf prefix, usage;
    if (!docText.TrySplit(TStringBuf("@usage"), prefix, usage)) {
        return std::make_pair(TString(), TString{docText});
    } else {
        TStringBuf suffix;
        usage.SkipPrefix(":");
        usage = StripStringLeft(usage);
        if (!usage.TrySplit('\n', usage, suffix)) {
            return std::make_pair(TString{StripStringRight(usage)}, TString{StripStringRight(prefix)});
        }
        return std::make_pair(TString{StripStringRight(usage)},
                              TString::Join(StripStringRight(prefix), suffix));
    }
}

void TReferences::AddReference(const TString& name, const TString& file, const TSourceRange& range) {
    if (!AreEnabled()) {
        return;
    }
    References.emplace_back(name, TSourceLocation{file, range});
}

TYndexRecord::TYndexRecord(ERecordType type, const NYndex::TSourceRange& range, const TDefinition& def)
    : Type(type)
    , Range(range)
    , Link(def.Link)
{
    Properties.emplace("name", def.Name);
    Properties.emplace("type", ToString(def.Type));
    if (!def.DocText.empty()) {
        auto usageAndComment = ExtractUsage(def.DocText);
        if (!usageAndComment.first.empty()) {
            Properties.emplace("usage", usageAndComment.first);
        }
        if (!usageAndComment.second.empty()) {
            Properties.emplace("comment", usageAndComment.second);
        }
    }
}

TYndex::TYndex(const TDefinitions& definitions, const TReferences& references)
    : Definitions(definitions)
{
    for (auto it : definitions.GetDefinitions()) {
        const TDefinition& def = it.second;
        TFileYndex& fileYndex = Files[def.Link.File];
        fileYndex.emplace_back(ERecordType::Node, def.Link.Range, def);
    }
    for (const auto& ref : references.GetReferences()) {
        TFileYndex& fileYndex = Files[ref.Link.File];
        if (const auto* def = definitions.GetDefinition(ref.Name)) {
            fileYndex.emplace_back(ERecordType::Use, ref.Link.Range, *def);
        }
    }
}

bool TYndex::AddReference(const TString& name, const TString& file, const TSourceRange& range) {
    if (!Definitions.AreEnabled()) {
        return false;
    }

    const TDefinition* def = Definitions.GetDefinition(name);
    if (!def) {
        return false;
    }

    TFileYndex& fileYndex = Files[file];
    fileYndex.emplace_back(ERecordType::Use, range, *def);
    return true;
}

void TYndex::WriteJSON(IOutputStream& out) const {
    Y_ASSERT(Definitions.AreEnabled());
    NJson::TJsonWriter jsonWriter(&out, true, true);
    jsonWriter.OpenMap();
    for (auto file : Files) {
        jsonWriter.OpenArray(file.first);
        for (auto record : file.second) {
            jsonWriter.OpenMap();

            switch (record.Type) {
                case ERecordType::Node:
                    jsonWriter.Write("kind", "node");
                    break;
                case ERecordType::Use:
                    jsonWriter.Write("kind", "use");
                    break;
            }

            jsonWriter.OpenMap("properties");
            for (auto prop : record.Properties) {
                jsonWriter.Write(prop.first, prop.second);
            }
            jsonWriter.CloseMap();

            jsonWriter.OpenMap("range");
            jsonWriter.Write("line", record.Range.Line);
            jsonWriter.Write("column", record.Range.Column);
            jsonWriter.Write("endLine", record.Range.EndLine);
            jsonWriter.Write("endColumn", record.Range.EndColumn);
            jsonWriter.CloseMap();

            if (record.Type == ERecordType::Use && record.Link.IsValid()) {
                jsonWriter.OpenMap("link");
                jsonWriter.Write("file", record.Link.File);
                jsonWriter.OpenMap("range");
                jsonWriter.Write("line", record.Link.Range.Line);
                jsonWriter.Write("column", record.Link.Range.Column);
                jsonWriter.Write("endLine", record.Link.Range.EndLine);
                jsonWriter.Write("endColumn", record.Link.Range.EndColumn);
                jsonWriter.CloseMap();
                jsonWriter.CloseMap();
            }

            jsonWriter.CloseMap();
        }
        jsonWriter.CloseArray();
    }
    jsonWriter.CloseMap();
}
