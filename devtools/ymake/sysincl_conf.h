#pragma once

#include "sysincl_resolver.h"

#include <util/folder/path.h>
#include <util/generic/strbuf.h>
#include <util/generic/string.h>
#include <util/generic/vector.h>

class MD5;

/// @brief loads all sysincl configuration for current build
TSysinclRules LoadSystemIncludes(const TVector<TFsPath>& configs, MD5& confData);

/// @brief loads config from provided content
TSysinclRules LoadSystemIncludes(const TString& content);
