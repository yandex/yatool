package ru.yandex.devtools.test;


import java.util.Objects;

import ru.yandex.devtools.log.Logger;

public class Metrics {
    private final static Logger logger = Logger.getLogger(Metrics.class);

    private static volatile MetricsListener listener;

    static void setListener(MetricsListener listener) {
        Metrics.listener = Objects.requireNonNull(listener);
    }

    public static void set(String name, int value) {
        if (listener == null) {
            logger.error("Metrics Listener is missing. Metric name: %s, value: %d.", name, value);
        } else {
            listener.subtestMetricAdded(name, value);
        }
    }

    public interface MetricsListener {
        void subtestMetricAdded(String name, int value);
    }
}
