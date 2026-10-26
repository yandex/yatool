#pragma once

#include <Python.h>

#include <library/cpp/ucompress/reader.h>
#include <util/generic/ptr.h>
#include <util/generic/string.h>
#include <util/stream/file.h>

namespace NSJson {
    // Reads directly into the SAX parser's input buffer. UC decoded blocks
    // remain in TDecodedInput, without Python bytes or an intermediate pipe.
    class TNativeFileInput: public IInputStream {
    public:
        explicit TNativeFileInput(const TString& path);

    private:
        size_t DoRead(void* data, size_t size) override;

        TFileInput File_;
        THolder<NUCompress::TDecodedInput> Decoded_;
    };
} // namespace NSJson
