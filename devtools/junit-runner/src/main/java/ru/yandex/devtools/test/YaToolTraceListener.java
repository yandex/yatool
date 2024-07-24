package ru.yandex.devtools.test;

import java.io.PrintWriter;
import java.io.StringWriter;
import java.io.Writer;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;

import org.junit.internal.runners.ErrorReportingRunner;
import org.junit.runner.Description;
import org.junit.runner.Result;
import org.junit.runner.manipulation.Filter;
import org.junit.runner.notification.Failure;
import org.junit.runner.notification.RunListener;

import ru.yandex.devtools.log.Logger;
import ru.yandex.devtools.log.LoggingContext;

public class YaToolTraceListener extends RunListener {

    private final static Logger logger = Logger.getLogger(YaToolTraceListener.class);

    private final Map<Description, Failure> errors = new HashMap<>();
    private final Set<Description> assumptionViolations = new HashSet<>();

    private final TraceListener<Object, Description> listener;

    public boolean noTestsFoundByFilter = false;

    public YaToolTraceListener(LoggingContext loggingContext, Writer writer) {
        this.listener = new TraceListener<>(loggingContext, writer, TestUtils.getNameCache());
    }

    public TraceListener<Object, Description> getListener() {
        return listener;
    }

    @Override
    public void testRunStarted(Description description) {
        listener.reportChunkStarted();
    }

    @Override
    public void testRunFinished(Result result) {
        listener.reportChunkFinished();
    }

    @Override
    public void testStarted(Description description) {
        synchronized (listener) {
            listener.reportPreStarted(description);

            if (Filter.class.equals(description.getTestClass())) {
                noTestsFoundByFilter = true;
                return;
            }
            listener.reportStarted(description);
            logger.info("testStarted %s", TestUtils.getSubtestName(description));
        }
    }

    @Override
    public void testFinished(Description description) {
        synchronized (listener) {
            logger.info("testFinished %s", TestUtils.getSubtestName(description));
            listener.reportPreFinished(description);

            try {
                if (Filter.class.equals(description.getTestClass())) {
                    return;
                }

                if (assumptionViolations.contains(description)) {
                    return;
                }


                String className = TestUtils.getTestClassName(description);

                if (className == null) {
                    String comment = null;
                    if (errors.containsKey(description)) {
                        comment = getComment(description, true);
                    }

                    if (comment == null) {
                        comment = "[[bad]]Internal error - can't find error for suite";
                    } else {
                        System.err.println(getComment(description, false));
                    }

                    listener.reportChunkError(comment);
                    return;
                }

                listener.reportFinished(description, data -> {
                    TestStatus status;

                    if (errors.containsKey(description)) {
                        status = TestStatus.fail;
                        String comment = getComment(description, true);
                        if (comment != null) {
                            data.put("comment", comment);
                            logger.error("Test case failed with:\n%s", getComment(description, false));
                        }
                    } else {
                        status = TestStatus.good;
                    }

                    List<String> testTags = new ArrayList<>();
                    data.put("tags", testTags);

                    if (Runner.hasYaExternalTag(description)) {
                        testTags.add("ya:external");
                    }

                    return status;
                });
            }
            finally {
                listener.reportReconfigureLoggers();
            }
        }
    }

    @Override
    public void testFailure(Failure failure) {
        synchronized (listener) {
            Description description = failure.getDescription();

            logger.info("testFailure %s", description);

            if (!listener.subtestStarts(description) && !Filter.class.equals(description.getTestClass())) {
                failure.getException().printStackTrace();
                errors.put(description, failure);

                String comment = getComment(description, true);
                listener.reportChunkError(comment);
            } else {
                errors.put(description, failure);
            }
        }
    }

    @Override
    public void testAssumptionFailure(Failure failure) {
        synchronized (listener) {
            for (Description subtest : ScanUtils.listSubtests(failure.getDescription())) {
                logger.info("testAssumptionFailure %s", TestUtils.getSubtestName(subtest));
                assumptionViolations.add(subtest);
                subtestIgnored(subtest, failure.getTrace());
            }
        }
    }

    @Override
    public void testIgnored(Description description) {
        synchronized (listener) {
            logger.info("testIgnored %s", TestUtils.getSubtestName(description));
            listener.reportPartiallyFinished(description, TestStatus.skipped);
        }
    }

    void subtestIgnored(Description description, String comment) {
        synchronized (listener) {
            listener.reportPartiallyFinished(description, TestStatus.skipped, comment);
        }
    }

    void subtestNotLaunched(Description description) {
        synchronized (listener) {
            listener.reportPartiallyFinished(description, TestStatus.not_launched);
        }
    }

    private String getComment(Description description, Boolean colorize) {
        Failure err = errors.get(description);
        String comment = err.getMessage();

        if (err.getException() != null) {
            comment += "\n";
            if (colorize) {
                comment += listener.colorizeStackTrace(err.getException());
            } else {
                comment += listener.extractStackTrace(err.getException());
            }
        }

        return comment;
    }


    void initializationError(Class<?> testClass, ErrorReportingRunner runner) throws Exception {
        synchronized (listener) {
            List<Throwable> causes;
            try {
                //noinspection unchecked
                causes = (List<Throwable>) Shared.getDeclaredFieldValue(runner, "causes");
            } catch (Exception e) {
                //noinspection unchecked
                causes = (List<Throwable>) Shared.getDeclaredFieldValue(runner, "fCauses");
            }

            StringWriter errors = new StringWriter();
            PrintWriter errorsPW = new PrintWriter(errors);

            for (Throwable cause : causes) {
                errors.write(testClass.getCanonicalName() + ": " + cause);
                cause.printStackTrace(errorsPW);
            }

            String errorsString = errors.toString();
            synchronized (listener) {
                listener.reportChunkError(errorsString);
            }
        }
    }

}
