#pragma once

#include <util/generic/string.h>
#include <util/generic/map.h>

#include <Python.h>

class TBuildConfiguration;

void AddParser(TBuildConfiguration* conf, const TString& ext, PyObject* callable, std::map<TString, TString> inducedDeps, bool passInducedIncludes);
