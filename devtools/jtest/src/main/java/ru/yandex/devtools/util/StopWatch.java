package ru.yandex.devtools.util;

public class StopWatch {
    private long started;
    private long stopped;
    private long total;

    public long start() {
        if (started == 0 && stopped == 0) {
            started = System.currentTimeMillis();
        }
        return started;
    }

    public long stop() {
        long ret = 0;
        if (started > 0 && stopped == 0) {
            stopped = System.currentTimeMillis();
            ret = stopped;
            total += stopped - started;
            started = 0;
            stopped = 0;
        }
        return ret;
    }

    public long total() {
        if (total > 0) {
            return total;
        } else {
            return -1;
        }
    }
}
