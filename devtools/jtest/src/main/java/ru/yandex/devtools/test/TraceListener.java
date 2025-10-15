package ru.yandex.devtools.test;

import java.io.File;
import java.io.IOException;
import java.io.PrintWriter;
import java.io.StringWriter;
import java.io.Writer;
import java.util.Arrays;
import java.util.Collections;
import java.util.HashMap;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.Map;
import java.util.Objects;
import java.util.Optional;
import java.util.Set;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.function.Function;
import java.util.regex.Pattern;

import ru.yandex.devtools.log.LoggingContext;
import ru.yandex.devtools.test.Canonizer.CanonizingListener;
import ru.yandex.devtools.test.Links.LinksListener;
import ru.yandex.devtools.test.Metrics.MetricsListener;
import ru.yandex.devtools.util.PerformanceMetrics;

import static ru.yandex.devtools.test.Shared.GSON;

public class TraceListener<K, V> {

    private static class LazyFilters {
        private static final Object[] FILTERS = {
                // method name
                Pattern.compile("at ([^(]+)"), "at [[alt1]]$1[[rst]]",
                // filename and lineno
                Pattern.compile("\\((.*?\\.java):(\\d+)\\)"), "([[unimp]]$1[[rst]]:[[alt2]]$2[[rst]])"};
    }

    private static final String LOG_SUFFIX = ".log";

    // Гарантируем одинаковые названия файлов для одного и того же теста,
    // даже если название файла превышает 140 символов
    private final Map<K, String> logFiles = new HashMap<>(128);

    // Кэши для суффиксов имен файлов (если длина превышает 140 символов) - позволяет не проверять существование
    // файлов на диске
    private final Map<String, AtomicInteger> logFileIndexes = new HashMap<>();
    private final ThreadLocal<ThreadLocalState<K>> threadLocalState =
            ThreadLocal.withInitial(ThreadLocalState::new);

    //
    private final LoggingContext loggingContext;
    private final Writer writer;
    private final CachedTestNames<K, V> cache;

    private PerformanceMetrics chunkPerformanceMetrics;

    public TraceListener(LoggingContext loggingContext, Writer writer, CachedTestNames<K, V> cache) {
        this.loggingContext = Objects.requireNonNull(loggingContext);
        this.writer = Objects.requireNonNull(writer);
        this.cache = Objects.requireNonNull(cache);
        this.chunkPerformanceMetrics = new PerformanceMetrics();
    }

    public boolean subtestStarts(V test) {
        ThreadLocalState<K> state = threadLocalState.get();
        return state.subtestStarts.containsKey(getKeyImpl(test));
    }

    public String getTestLogFile(V test) {
        return logFiles.computeIfAbsent(getKeyImpl(test), key -> {
            String name = Shared.safeFileName(cache.getClassName(test) + "." + cache.getMethodName(test));
            int maxFileNameLen = 140;
            String path;
            if (name.length() > (maxFileNameLen - LOG_SUFFIX.length())) {
                name = name.substring(0, maxFileNameLen - LOG_SUFFIX.length() * 2);

                String pathPrefix = Paths.getTestOutputsRoot() + File.separator + name;
                path = pathPrefix + "." + logFileIndexes.computeIfAbsent(pathPrefix, n -> new AtomicInteger())
                        .getAndIncrement() + LOG_SUFFIX;
            } else {
                path = Paths.getTestOutputsRoot() + File.separator + name + LOG_SUFFIX;
            }


            return Shared.ensureUTF8(path);
        });
    }

    public String extractStackTrace(Throwable e) {
        StringWriter sw = new StringWriter();
        e.printStackTrace(new PrintWriter(sw));
        return sw.toString();
    }

    public String colorizeStackTrace(Throwable e) {
        String trace = extractStackTrace(e);
        Object[] filters = LazyFilters.FILTERS;
        for (int i = 0; i < filters.length - 1; i += 2) {
            trace = ((Pattern) filters[i]).matcher(trace).replaceAll((String) filters[i + 1]);
        }
        return trace;
    }

    public double epoch() {
        return System.currentTimeMillis() / 1000.0;
    }

    public void reportPreStarted(V test) {
        loggingContext.configureLoggers(getTestLogFile(test));

        ThreadLocalState<K> state = threadLocalState.get();
        state.subtestsOnFly.add(getTestName(test));
        state.subtestStarts.put(getKeyImpl(test), epoch());

        if (state.subtestsOnFly.size() == 1) {
            PerformanceMetrics currentPerformanceMetrics = new PerformanceMetrics();
            currentPerformanceMetrics.fillThreadMetrics();
            state.threadCPUTime = currentPerformanceMetrics.getThreadCPUTime();

            PerformanceMetrics.resetPeakMemoryUsage();
            PerformanceMetrics.resetPeakThreadCount();
        }
    }

    public void reportStarted(V test) {
        String className = getClassName(test);
        if (className != null) {

            // If null then looks like runtime error.
            // Don't generate fake test entries - it will be processed in testFinished() properly

            String methodName = getMethodName(test);

            Map<String, Object> data = new HashMap<>(4);
            data.put("class", className);
            data.put("subtest", methodName == null ? "unknown" : methodName);

            trace("subtest-started", data);
        }
    }

    public void reportPreFinished(V test) {
        ThreadLocalState<K> state = threadLocalState.get();

        if (state.subtestsOnFly.size() == 1) {
            PerformanceMetrics currentPerformanceMetrics = new PerformanceMetrics();
            currentPerformanceMetrics.fillThreadMetrics();
            currentPerformanceMetrics.fillMemoryUsageMetrics();
            currentPerformanceMetrics.fillThreadMetrics();

            long cpuTime = currentPerformanceMetrics.getThreadCPUTime() - state.threadCPUTime;

            subtestMetricAdded("java_peak_memory_usage", currentPerformanceMetrics.getPeakMemoryUsage());
            subtestMetricAdded("java_peak_thread_count", currentPerformanceMetrics.getPeakThreadCount());
            subtestMetricAdded("java_thread_cpu_time", cpuTime / 1e9); // From nanos to seconds

            chunkPerformanceMetrics.updatePeakMemoryUsage(currentPerformanceMetrics.getPeakMemoryUsage());
            chunkPerformanceMetrics.updatePeakThreadCount(currentPerformanceMetrics.getPeakThreadCount());
        }

        state.subtestsOnFly.remove(getTestName(test));
    }

    public void reportFinished(V test, Function<Map<String, Object>, TestStatus> stateSupplier) {
        String testName = getTestName(test);

        Map<String, Object> data = new HashMap<>();
        TestStatus status = stateSupplier.apply(data);

        ThreadLocalState<K> state = threadLocalState.get();

        Optional.ofNullable(state.canonData.get(testName))
                .ifPresent(value -> data.put("result", value));

        Optional.ofNullable(state.metrics.get(testName))
                .ifPresent(value -> data.put("metrics", value));

        Object userLinks = state.links.get(testName);
        if (userLinks != null) {
            data.put("logs", userLinks);
        } else {
            // default logs
            Map<String, Object> logs = new HashMap<>();
            logs.put("logsdir", Paths.getTestOutputsRoot());
            logs.put("log", getTestLogFile(test));
            data.put("logs", logs);
        }

        Optional.ofNullable(state.subtestStarts.get(getKeyImpl(test)))
                .ifPresent(value -> data.put("time", epoch() - value));

        reportFinishedImpl(test, status, data);
    }

    public void reportReconfigureLoggers() {
        loggingContext.configureMainLoggers();
    }

    public void reportPartiallyFinished(V test, TestStatus status) {
        reportPartiallyFinished(test, status, null);
    }

    public void reportPartiallyFinished(V test, TestStatus status, String comment) {
        Map<String, Object> data = new HashMap<>(4);
        if (comment != null) {
            data.put("comment", comment);
        }
        reportFinishedImpl(test, status, data);
    }

    private void reportFinishedImpl(V test, TestStatus status, Map<String, Object> data) {
        String className = getClassName(test);
        if (className != null) {
            String methodName = getMethodName(test);

            data.put("class", className);
            data.put("subtest", methodName == null ? "unknown" : methodName);
            data.put("status", status.name());

            trace("subtest-finished", data);
        }
    }

    public void reportChunkStarted() {
        chunkPerformanceMetrics.fillGarbageCollectionMetrics();
        chunkPerformanceMetrics.fillThreadMetrics();
        chunkPerformanceMetrics.fillTotalCpuUsageMetrics();
    }

    public void reportChunkFinished() {
        PerformanceMetrics currentPerformanceMetrics = new PerformanceMetrics();
        currentPerformanceMetrics.fillTotalCpuUsageMetrics();
        currentPerformanceMetrics.fillGarbageCollectionMetrics();

        long garbageCollections = currentPerformanceMetrics.getGarbageCollections() -
                chunkPerformanceMetrics.getGarbageCollections();
        long garbageCollectionTime = currentPerformanceMetrics.getGarbageCollectionTime() -
                chunkPerformanceMetrics.getGarbageCollectionTime();
        long cpuTime = currentPerformanceMetrics.getTotalCPUTime() -
                chunkPerformanceMetrics.getTotalCPUTime();

        Map<String, Object> metrics = new HashMap<>();
        metrics.put("java_garbage_collections", garbageCollections);
        metrics.put("java_garbage_collection_time", garbageCollectionTime / 1000.0);
        metrics.put("java_peak_memory_usage", chunkPerformanceMetrics.getPeakMemoryUsage());
        metrics.put("java_peak_thread_count", chunkPerformanceMetrics.getPeakThreadCount());
        metrics.put("java_total_cpu_time", cpuTime / 1e9); // From nanos to seconds

        addChunkMetrics(metrics);
    }

    private void addChunkMetrics(Map<String, Object> metrics) {
        Map<String, Object> data = new HashMap<>();
        data.put("metrics", metrics);
        trace("chunk-event", data);
    }

    public void reportChunkError(String comment) {
        Map<String, Object> data = new HashMap<>();
        data.put("errors", Collections.singletonList(Arrays.asList("fail", comment)));
        trace("chunk-event", data);
    }

    public void trace(String key, Map<?, ?> value) {
        Map<Object, Object> result = new LinkedHashMap<>(4);
        result.put("timestamp", epoch());
        result.put("name", key);
        result.put("value", value);
        try {
            StringBuilder buffer = new StringBuilder();
            GSON.toJson(result, buffer);
            buffer.append(System.lineSeparator());

            writer.write(buffer.toString());
            writer.flush();
        } catch (IOException e) {
            throw new RuntimeException(e);
        }
    }

    private K getKeyImpl(V test) {
        return cache.getKeyImpl(test);
    }

    public String getClassName(V test) {
        return cache.getClassName(test);
    }

    private String getMethodName(V test) {
        return cache.getMethodName(test);
    }

    private String getTestName(V test) {
        return cache.getFullName(test);
    }

    //


    public void subtestCanonized(Object object) {
        ThreadLocalState<K> state = threadLocalState.get();
        ensureSubtestsOnFly(state);
        for (String subtestName : state.subtestsOnFly) {
            state.canonData.put(subtestName, object);
        }
    }

    public void subtestMetricAdded(String name, long value) {
        ThreadLocalState<K> state = threadLocalState.get();
        ensureSubtestsOnFly(state);
        for (String subtestName : state.subtestsOnFly) {
            state.metrics.computeIfAbsent(subtestName, n -> new HashMap<>()).put(name, value);
        }
    }

    public void subtestMetricAdded(String name, double value) {
        ThreadLocalState<K> state = threadLocalState.get();
        ensureSubtestsOnFly(state);
        for (String subtestName : state.subtestsOnFly) {
            state.metrics.computeIfAbsent(subtestName, n -> new HashMap<>()).put(name, value);
        }
    }

    public void subtestLinksAdded(String name, String value) {
        ThreadLocalState<K> state = threadLocalState.get();
        ensureSubtestsOnFly(state);
        for (String subtestName : state.subtestsOnFly) {
            state.links.computeIfAbsent(subtestName, n -> new HashMap<>()).put(name, value);
        }
    }

    public LinksListener getLinksListener() {
        return (name, value) -> {
            synchronized (TraceListener.this) {
                subtestLinksAdded(name, value);
            }
        };
    }

    public MetricsListener getMetricsListener() {
        return (name, value) -> {
            synchronized (TraceListener.this) {
                subtestMetricAdded(name, value);
            }
        };
    }

    public CanonizingListener getCanonizingListener() {
        return object -> {
            synchronized (TraceListener.this) {
                subtestCanonized(object);
            }
        };
    }

    private void ensureSubtestsOnFly(ThreadLocalState<K> state) {
        if (state.subtestsOnFly.size() != 1) {
            throw new RuntimeException("Expected 1 running subtest, got " + state.subtestsOnFly.size());
        }
    }

    private static class ThreadLocalState<K> {

        private final Set<String> subtestsOnFly = new HashSet<>();

        private final Map<String, Object> canonData = new HashMap<>();
        private final Map<String, Map<String, Object>> metrics = new HashMap<>();
        private final Map<String, Map<String, String>> links = new HashMap<>();

        private final Map<K, Double> subtestStarts = new HashMap<>();

        private long threadCPUTime;
    }

}
