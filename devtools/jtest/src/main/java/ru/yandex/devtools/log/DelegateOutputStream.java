package ru.yandex.devtools.log;

import java.io.IOException;
import java.io.OutputStream;

class DelegateOutputStream extends OutputStream {

    private volatile OutputStream impl;

    void setImpl(OutputStream impl) {
        this.impl = impl;
    }

    @Override
    public void write(byte[] b, int off, int len) throws IOException {
        OutputStream impl = this.impl;
        if (impl != null) {
            impl.write(b, off, len);
        }
    }

    @Override
    public void write(byte[] b) throws IOException {
        OutputStream impl = this.impl;
        if (impl != null) {
            impl.write(b);
        }
    }

    @Override
    public void write(int b) throws IOException {
        OutputStream impl = this.impl;
        if (impl != null) {
            impl.write(b);
        }
    }

    @Override
    public void flush() {
        //
    }

    @Override
    public void close() {
        //
    }
}
