package ru.yandex.devtools.test;

import ru.yandex.devtools.util.StopWatch;

public class RunnerTiming {
    private final StopWatch total = new StopWatch();
    private final StopWatch configuration = new StopWatch();
    private final StopWatch execution = new StopWatch();

    public StopWatch getTotal() {
        return total;
    }

    public StopWatch getConfiguration() {
        return configuration;
    }

    public StopWatch getExecution() {
        return execution;
    }
}
