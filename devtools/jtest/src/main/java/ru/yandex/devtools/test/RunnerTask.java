package ru.yandex.devtools.test;

import java.io.Writer;

import ru.yandex.devtools.log.LoggingContext;
import ru.yandex.devtools.test.Shared.Parameters;

public class RunnerTask {
    private final LoggingContext loggingContext;
    private final RunnerTiming timing;
    private final Parameters params;
    private final Writer writer;

    public RunnerTask(LoggingContext loggingContext, RunnerTiming timing, Parameters params, Writer writer) {
        this.loggingContext = loggingContext;
        this.timing = timing;
        this.params = params;
        this.writer = writer;
    }

    public LoggingContext getLoggingContext() {
        return loggingContext;
    }

    public RunnerTiming getTiming() {
        return timing;
    }

    public Parameters getParams() {
        return params;
    }

    public Writer getWriter() {
        return writer;
    }
}
