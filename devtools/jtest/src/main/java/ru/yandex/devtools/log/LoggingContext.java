package ru.yandex.devtools.log;

import java.io.BufferedOutputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.OutputStream;
import java.io.PrintStream;

import ru.yandex.devtools.util.StopWatch;

public class LoggingContext implements AutoCloseable {
    public static final String YA_JTEST_DONT_SPLIT_LOGS = "YA_JTEST_DONT_SPLIT_LOGS";

    private final DelegateOutputStream delegateOutputStream;

    private final OutputStream mainOutputStream;
    private final StopWatch stopWatch = new StopWatch();
    private boolean configured;
    private OutputStream lastOutputStream;

    private LoggingContext(String mainLogFile) {
        this.mainOutputStream = getOutputStream(mainLogFile);
        this.delegateOutputStream = new DelegateOutputStream();
    }

    public StopWatch getStopWatch() {
        return stopWatch;
    }

    public void configureMainLoggers() {
        if (mainOutputStream != null) {
            stopWatch.start();
            try {
                closeOutputStream(this.lastOutputStream);
                switchTo(mainOutputStream);
            } finally {
                stopWatch.stop();
            }
        }
    }

    public void configureLoggers(String filename) {
        stopWatch.start();
        try {
            closeOutputStream(this.lastOutputStream);

            OutputStream outputStream = getOutputStream(filename);
            this.lastOutputStream = outputStream;
            switchTo(outputStream);
        } finally {
            stopWatch.stop();
        }
    }

    @Override
    public void close() {
        closeOutputStream(this.mainOutputStream);
        closeOutputStream(this.lastOutputStream);
    }

    private void switchTo(OutputStream stream) {
        if (!configured) {
            configured = true;
            configureImpl();
        }
        delegateOutputStream.setImpl(stream);
    }

    private void configureImpl() {
        PrintStream systemOut = buildLoggingStream(System.out);
        PrintStream systemErr = buildLoggingStream(System.err);

        System.setOut(systemOut);
        System.setErr(systemErr);

        Logger.out = systemOut;
        Logger.err = systemErr;
    }

    private TeePrintStream buildLoggingStream(PrintStream stream) {
        while (stream instanceof TeePrintStream) {
            stream = ((TeePrintStream) stream).getDelegate();
        }
        return new TeePrintStream(delegateOutputStream, stream);
    }


    private static OutputStream getOutputStream(String filename) {
        if (filename == null) {
            return null;
        }
        try {
            return new BufferedOutputStream(new FileOutputStream(filename));
        } catch (Exception e) {
            System.err.println("Failed to configure output stream for " + filename);
            e.printStackTrace();
            return null; // ---
        }
    }

    private static void closeOutputStream(OutputStream outputStream) {
        if (outputStream != null) {
            try {
                outputStream.close();
            } catch (IOException e) {
                System.err.println("Failed to close output stream");
                e.printStackTrace();
            }
        }
    }

    public static LoggingContext optional(String fileName) {
        boolean splitLogs = System.getProperty(YA_JTEST_DONT_SPLIT_LOGS) == null;
        if (splitLogs) {
            return new LoggingContext(fileName);
        } else {
            return bypass();
        }
    }

    public static LoggingContext bypass() {
        return new LoggingContext(null) {
            @Override
            public void configureLoggers(String filename) {
                // do nothing
            }

            @Override
            public void configureMainLoggers() {
                // do nothing
            }
        };

    }


}
