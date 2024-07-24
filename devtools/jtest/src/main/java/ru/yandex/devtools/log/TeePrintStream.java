package ru.yandex.devtools.log;

import java.io.OutputStream;
import java.io.PrintStream;

public class TeePrintStream extends PrintStream {
    private final PrintStream delegate;

    public TeePrintStream(OutputStream out, PrintStream delegate) {
        super(out);
        this.delegate = delegate;
    }

    PrintStream getDelegate() {
        return delegate;
    }

    @Override
    public void write(int b) {
        super.write(b);
        delegate.write(b);
    }

    @Override
    public void write(byte[] b) {
        write(b, 0, b.length);
    }

    @Override
    public void write(byte[] buf, int off, int len) {
        super.write(buf, off, len);
        delegate.write(buf, off, len);
    }

}
