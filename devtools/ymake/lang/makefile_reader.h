#pragma once

#include <devtools/ymake/symbols/content_provider.h>

#include <devtools/ymake/diag/dbg.h>   // For TMakeError
#include <devtools/ymake/options/roots_options.h>
#include <devtools/ymake/yndex/yndex.h>

#include <util/generic/vector.h>
#include <util/generic/string.h>

class TEvalContext;

class TLangError: public TMakeError {
};

void ReadMakeFile(const TString& path, const TRootsOptions& bconf, IContentProvider* provider, TEvalContext* result, NYndex::TYndex& yndex);
