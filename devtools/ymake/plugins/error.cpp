#include "error.h"

#include <util/generic/yexception.h>

#include <Python.h>

namespace NYMake::NPlugins {
    void CheckForError() {
        if (PyErr_Occurred()) {
            PyErr_Print();
            ythrow yexception() << "Error in plugin";
        }
    }
}
