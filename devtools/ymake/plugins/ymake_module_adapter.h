#pragma once

#include <util/generic/string.h>
#include <util/generic/map.h>

#include <Python.h>

void AddParser(const TString& ext, PyObject* callable, std::map<TString, TString> inducedDeps, bool passInducedIncludes);
