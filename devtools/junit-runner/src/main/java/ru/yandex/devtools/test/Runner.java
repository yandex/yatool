package ru.yandex.devtools.test;

import java.io.Writer;
import java.lang.reflect.Method;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.Set;

import org.junit.Ignore;
import org.junit.internal.runners.ErrorReportingRunner;
import org.junit.runner.Computer;
import org.junit.runner.Description;
import org.junit.runner.JUnitCore;
import org.junit.runner.Request;
import org.junit.runner.manipulation.Filter;
import org.junit.runner.notification.RunListener;
import org.junit.runners.model.RunnerBuilder;

import ru.yandex.devtools.log.Logger;
import ru.yandex.devtools.test.Shared.Parameters;
import ru.yandex.devtools.test.annotations.YaExternal;
import ru.yandex.devtools.test.annotations.YaIgnore;
import ru.yandex.devtools.util.StopWatch;


public class Runner extends AbstractRunner {

    private final static Logger logger = Logger.getLogger(Runner.class);

    // For backward compatibility only
    public static Parameters params;

    public static final String initializationErrorPrefix = "[INITIALIZATION_ERROR] ";

    @Override
    protected void testCompatibility() {
        try {
            Class.forName("org.junit.platform.launcher.Launcher");
            throw new RuntimeException("JUnit 5 classpath detected in JUnit 4 Test Launcher");
        } catch (ClassNotFoundException e) {
            // OK
        }
    }

    @Override
    protected boolean isLegacy() {
        return true;
    }

    @Override
    protected String getName() {
        return "Legacy JUnit 4";
    }

    @Override
    protected void setParams(Parameters params) {
        Runner.params = params;
    }

    @Override
    protected int listTests(RunnerTask task) throws Exception {
        Parameters params = task.getParams();
        Writer writer = task.getWriter();

        StopWatch cfg = task.getTiming().getConfiguration();
        cfg.start();

        Set<Description> testsSeen = new HashSet<>();

        for (Class<?> testClass : listClasses(params.testsJar)) {
            org.junit.runner.Runner runner = Request.aClass(testClass).getRunner();
            logger.info("Runner for %s is %s", testClass, runner.getClass());

            if (runner instanceof ErrorReportingRunner) {
                continue;
            }

            Optional<Map<Description, List<Description>>> junitParamsRunnerSubtestsMap =
                    makeJUnitParamsRunnerSubtestsMap(runner);

            for (Description subtest : ScanUtils.listSubtests(runner)) {
                String subtestName = TestUtils.getSubtestName(subtest);
                logger.info("Found subtest %s", subtestName);

                if (Matcher.matchesAny(subtestName, params.filters) && !testsSeen.contains(subtest)) {
                    testsSeen.add(subtest);
                    logger.info("Subtest %s matches all filters", subtestName);

                    String ignored = getIgnoreReasons(subtest);

                    List<Description> subtests = new ArrayList<>();
                    if (junitParamsRunnerSubtestsMap.isPresent()) {
                        for (Description desc : junitParamsRunnerSubtestsMap.get().get(subtest)) {
                            subtests.addAll(ScanUtils.listSubtests(desc));
                        }
                    } else {
                        subtests.add(subtest);
                    }

                    for (Description description : subtests) {
                        Map<Object, Object> subtestInfo = new HashMap<>();
                        List<String> testTags = new ArrayList<>();
                        if (hasYaExternalTag(description)) {
                            testTags.add("ya:external");
                        }
                        if (ignored != null) {
                            logger.info("Subtest %s is ignored", subtestName);
                            subtestInfo.put("skipped", true);
                        }
                        subtestInfo.put("test", TestUtils.getTestClassName(description));
                        subtestInfo.put("subtest", TestUtils.getTestMethodName(description));
                        subtestInfo.put("tags", testTags);
                        writer.write(Shared.GSON.toJson(subtestInfo));
                        writer.write(System.lineSeparator());
                    }

                }
            }
        }
        cfg.stop();
        return 0;
    }


    @Override
    protected int executeTests(RunnerTask task) throws Exception {
        Parameters params = task.getParams();
        Writer writer = task.getWriter();

        YaToolTraceListener listener = new YaToolTraceListener(task.getLoggingContext(), writer);
        TraceListener<Object, Description> traceListener = listener.getListener();
        Canonizer.setListener(traceListener.getCanonizingListener());
        Metrics.setListener(traceListener.getMetricsListener());
        Links.setListener(traceListener.getLinksListener());
        Request request = genRunRequest(params, listener);
        writer.flush();

        JUnitCore runner = new JUnitCore();

        runner.addListener(listener);
        if (params.allure) {
            tryAddAllureListener(runner);
        }
        runner.run(request);

        return 0;
    }

    static void reportInitializationError(Class<?> testClass, ErrorReportingRunner runner) throws Exception {
        List<Throwable> causes;
        try {
            //noinspection unchecked
            causes = (List<Throwable>) Shared.getDeclaredFieldValue(runner, "causes");
        } catch (Exception e) {
            //noinspection unchecked
            causes = (List<Throwable>) Shared.getDeclaredFieldValue(runner, "fCauses");
        }

        for (Throwable cause : causes) {
            System.err.println(initializationErrorPrefix + testClass.getCanonicalName() + ": " + cause);
            cause.printStackTrace();
        }
    }

    static String getIgnoreReasons(Description description) {
        String reasons = null;

        if (description.getAnnotation(Ignore.class) != null) {
            reasons = String.format("by class [[imp]]@Ignore[[rst]](%s);",
                    description.getAnnotation(Ignore.class).value());
        }
        if (description.getAnnotation(YaIgnore.class) != null) {
            reasons = "by class [[imp]]@YaIgnore[[rst]];";
        }

        Class<?> testClass = description.getTestClass();

        if (testClass != null) {
            if (testClass.isAnnotationPresent(Ignore.class)) {
                reasons = String.format("by class [[imp]]@Ignore[[rst]](%s);",
                        testClass.getAnnotation(Ignore.class).value());
            }
            if (testClass.isAnnotationPresent(YaIgnore.class)) {
                reasons = "by class [[imp]]@YaIgnore[[rst]];";
            }
            try {
                Method method = testClass.getMethod(description.getMethodName());
                if (method.isAnnotationPresent(Ignore.class)) {
                    reasons = String.format("by method [[imp]]@Ignore[[rst]](%s);",
                            method.getAnnotation(Ignore.class).value());
                }
                if (method.isAnnotationPresent(YaIgnore.class)) {
                    reasons = "by method [[imp]]@YaIgnore[[rst]];";
                }
            } catch (Exception e) {
                //
            }
        }
        return reasons;
    }

    static boolean hasYaExternalTag(Description description) {
        boolean isYaExternal = false;
        Class<?> testClass = description.getTestClass();

        if (testClass != null) {
            try {
                Method method = testClass.getMethod(description.getMethodName());
                if (method.isAnnotationPresent(YaExternal.class)) {
                    isYaExternal = true;
                }
            } catch (Exception e) {
                //
            }
            if (testClass.isAnnotationPresent(YaExternal.class)) {
                isYaExternal = true;
            }
        }
        return isYaExternal;
    }

    static Optional<Map<Description, List<Description>>> makeJUnitParamsRunnerSubtestsMap(
            org.junit.runner.Runner runner) {
        Map<Description, List<Description>> result = null;

        if ("junitparams.JUnitParamsRunner".equals(runner.getClass().getCanonicalName())) {
            result = ScanUtils.makeJUnitParamsRunnerSubtestsMap(runner);
        }

        return Optional.ofNullable(result);
    }


    static Request genRunRequest(Parameters params, YaToolTraceListener listener) throws Exception {
        int testCount = 0;
        int subtestCount = 0;
        List<Class<?>> testClasses = new ArrayList<>();
        Set<Description> testsSeen = new HashSet<>();
        Map<Class<?>, org.junit.runner.Runner> runnerForClass = new HashMap<>();

        boolean testsWereListed = Shared.loadFilters(params);

        for (Class<?> testClass : listClasses(params.testsJar)) {
            org.junit.runner.Runner runner = Request.aClass(testClass).getRunner();
            runnerForClass.put(testClass, runner);
            logger.info("Runner for %s is %s", testClass, runner.getClass());

            if (runner instanceof ErrorReportingRunner) {
                reportInitializationError(testClass, (ErrorReportingRunner) runner);
                listener.initializationError(testClass, (ErrorReportingRunner) runner);
                continue;
            }

            Optional<Map<Description, List<Description>>> junitParamsRunnerSubtestsMap =
                    makeJUnitParamsRunnerSubtestsMap(runner);

            testCount += 1;
            if (!testsWereListed && !params.forkSubtests && testCount % params.modulo != params.moduloIndex) {
                logger.info("Skipping test %s (%s != %s)", testClass.getCanonicalName(), testCount % params.modulo, params.moduloIndex);
                continue;
            }

            testClasses.add(testClass);

            for (Description description : ScanUtils.listSubtests(runner)) {
                String subtestName = TestUtils.getSubtestName(description);
                logger.info("Found subtest %s", subtestName);

                if (Matcher.matchesAny(subtestName, params.filters) && !testsSeen.contains(description)) {
                    logger.info("Subtest %s matches all filters", subtestName);

                    subtestCount += 1;
                    if (!testsWereListed && params.forkSubtests && subtestCount % params.modulo != params.moduloIndex) {
                        logger.info("Skipping subtest %s (%s != %s)",
                                subtestName, subtestCount % params.modulo, params.moduloIndex);
                        continue;
                    }

                    String ignored = getIgnoreReasons(description);
                    if (ignored != null) {
                        logger.info("Subtest %s is ignored", subtestName);
                        listener.subtestIgnored(description, ignored);
                    } else if (junitParamsRunnerSubtestsMap.isPresent()) {
                        testsSeen.add(description);
                        for (Description desc : junitParamsRunnerSubtestsMap.get().get(description)) {
                            for (Description realSubtest : ScanUtils.listSubtests(desc)) {
                                testsSeen.add(realSubtest);
                                listener.subtestNotLaunched(realSubtest);
                            }
                        }
                    } else {
                        listener.subtestNotLaunched(description);
                        testsSeen.add(description);
                    }
                }
            }
        }

        return Request.classes(
                new Computer() {
                    @Override
                    protected org.junit.runner.Runner getRunner(RunnerBuilder builder, Class<?> testClass) {
                        return runnerForClass.get(testClass);
                    }
                },
                testClasses.toArray(new Class<?>[0])).filterWith(
                new Filter() {
                    @Override
                    public boolean shouldRun(Description description) {
                        boolean result = false;

                        if (testsSeen.contains(description)) {
                            result = true;
                        }

                        for (Description child : description.getChildren()) {
                            if (shouldRun(child)) {
                                result = true;
                            }
                        }

                        return result;
                    }

                    @Override
                    public String describe() {
                        return params.filters.toString();
                    }
                }
        );
    }

    static void tryAddAllureListener(JUnitCore runner) throws Exception {
        Class<?> AllureRunListenerClass = null;
        try {
            AllureRunListenerClass = Class.forName("ru.yandex.qatools.allure.junit.AllureRunListener");
        } catch (ClassNotFoundException e) {
            try {
                AllureRunListenerClass = Class.forName("io.qameta.allure.junit4.AllureJunit4");
            } catch (ClassNotFoundException ee) {
                //
            }
        }

        if (AllureRunListenerClass != null) {
            System.setProperty(
                    "allure.results.directory",
                    System.getProperty("allure.results.directory") + "/../allure"
            );
            Object allureRunListener = AllureRunListenerClass.getConstructor().newInstance();
            runner.addListener((RunListener) allureRunListener);
        } else {
            logger.info("No allure junit listener found in classpath");
        }
    }

    static List<Class<?>> listClasses(String testJar) throws Exception {
        return testJar.startsWith("class:") ?
                Collections.singletonList(Thread.currentThread().getContextClassLoader()
                        .loadClass(testJar.substring("class:".length()))) :
                ScanUtils.scanJar(testJar);
    }


    public static void main(String[] args) throws Exception {
        new Runner().run(args);
    }
}
