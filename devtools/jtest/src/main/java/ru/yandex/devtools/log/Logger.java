package ru.yandex.devtools.log;

import java.io.PrintStream;
import java.time.Instant;
import java.time.format.DateTimeFormatter;

public class Logger {

    private static final DateTimeFormatter FORMATTER = DateTimeFormatter.ISO_INSTANT;

    static volatile PrintStream out = System.out;
    static volatile PrintStream err = System.err;

    private final String prefix;

    public Logger(String prefix) {
        this.prefix = prefix;
    }

    public void info(String message) {
        out.println("INFO: " + FORMATTER.format(Instant.now()) + ": " + prefix + ": " + message);
    }

    public void info(String message, Object... args) {
        out.println("INFO: " + FORMATTER.format(Instant.now()) + ": " + prefix + ": " + String.format(message, args));
    }

    public void error(String message) {
        err.println("ERROR: " + FORMATTER.format(Instant.now()) + ": " + prefix + ": " + message);
    }

    public void error(String message, Object... args) {
        err.println("ERROR: " + FORMATTER.format(Instant.now()) + ": " + prefix + ": " + String.format(message, args));
    }


    public static Logger getLogger(Class<?> type) {
        return new Logger(type.getSimpleName());
    }

}
