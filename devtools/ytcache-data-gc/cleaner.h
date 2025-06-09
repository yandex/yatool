#pragma once

#include <util/generic/string.h>

void YtJobEntry(int argc, const char* argv[]);
void DoCleanData(const TString& ytProxy, const TString& ytToken, const TString& ytDir, const TString& metadataTable, const TString& dataTable, const i64 jobMemoryLimit, const bool dryRun);
