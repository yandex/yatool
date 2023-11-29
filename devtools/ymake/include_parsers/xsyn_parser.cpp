#include "xsyn_parser.h"

#include <library/cpp/xml/document/xml-document.h>
#include <util/string/cast.h>

void TXsynIncludesParser::Parse(IContentHolder& file, TVector<TString>& includes) {
    TStringBuf input = file.GetContent();

    NXml::TDocument xml(ToString(input), NXml::TDocument::String);

    NXml::TNamespacesForXPath nss;
    NXml::TNamespaceForXPath ns = {"parse", "parse"};
    nss.push_back(ns);

    NXml::TConstNode root = xml.Root();
    NXml::TConstNodes nodes = root.XPath("//parse:include", true, nss);

    for (const auto& node : nodes) {
        includes.emplace_back(node.Attr<TString>("path"));
    }
}
