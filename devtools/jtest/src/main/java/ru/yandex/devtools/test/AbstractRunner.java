package ru.yandex.devtools.test;

import java.io.BufferedWriter;
import java.io.FileNotFoundException;
import java.io.FileOutputStream;
import java.io.FileReader;
import java.io.OutputStreamWriter;
import java.io.Writer;
import java.nio.charset.StandardCharsets;

import com.beust.jcommander.JCommander;
import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import com.google.gson.JsonParser;

import ru.yandex.devtools.log.Logger;
import ru.yandex.devtools.log.LoggingContext;
import ru.yandex.devtools.test.Shared.Parameters;

import static ru.yandex.devtools.test.Shared.extendSystemProperties;
import static ru.yandex.devtools.test.Shared.loadProperties;

public abstract class AbstractRunner {

    private static final Logger logger = Logger.getLogger(AbstractRunner.class);

    //

    protected abstract void testCompatibility();

    protected abstract void setParams(Parameters params);

    protected abstract boolean isLegacy();

    protected abstract String getName();

    protected abstract int listTests(RunnerTask task) throws Exception;

    protected abstract int executeTests(RunnerTask task) throws Exception;

    //

    protected int run(RunnerTask task) throws Exception {
        try {
            String disabled = System.getenv("DISABLE_JUNIT_COMPATIBILITY_TEST");
            if (("1".equals(disabled) || Boolean.parseBoolean(disabled))) {
                logger.info("JUnit versions compatibility is disabled");
            } else {
                this.testCompatibility();
            }
        } catch (RuntimeException e) {
            logger.error("Test configuration is invalid: %s", e.getMessage());
            throw e;
        }

        Parameters params = task.getParams();

        logger.info("Modulo           : %s", params.modulo);
        logger.info("Modulo index     : %s", params.moduloIndex);
        logger.info("Fork mode        : %s", (params.forkSubtests ? "subtests" : "tests"));
        logger.info("Test partition   : %s", params.testPartition);
        logger.info("Experimental fork: %s", params.experimentalFork);
        logger.info("Filters          : %s", params.filters);

        Shared.initTmpDir();

        if (params.list) {
            logger.info("Test list mode");
            return listTests(task);
        } else {
            logger.info("Test run mode");
            return executeTests(task);
        }
    }


    void run(String[] args) throws Exception {
        if (isLegacy()) {
            logger.info("Using %s Runner", getName());
        }

        RunnerTiming timing = new RunnerTiming();
        long started = timing.getTotal().start();

        Parameters params = new Parameters();
        setParams(params);
        new JCommander(params).parse(args);

        if (params.rawProperties != null) {
            extendSystemProperties(loadProperties(params.rawProperties));
        }

        int exitCode;
        try (LoggingContext loggingContext = LoggingContext.optional(params.runnerLogPath)) {
            loggingContext.configureMainLoggers();

            if (!params.list) {
                String testContextPath = System.getenv("YA_TEST_CONTEXT_FILE");
                JsonElement testContext = new JsonParser().parse(new FileReader(testContextPath)); // WARNING: DO NOT CHANGE
                JsonObject contextRuntime = testContext.getAsJsonObject().get("runtime").getAsJsonObject();
                Paths.sourceRoot = contextRuntime.has("source_root") ? contextRuntime.get("source_root").getAsString() : "";
                Paths.buildRoot = contextRuntime.has("build_root") ? contextRuntime.get("build_root").getAsString() : "";
                Paths.projectPath = contextRuntime.has("project_path") ? contextRuntime.get("project_path").getAsString() : "";
                Paths.workPath = contextRuntime.has("work_path") ? contextRuntime.get("work_path").getAsString() : "";
                Paths.sandboxResourcesRoot = params.sandboxResourcesRoot;
                Paths.testOutputsRoot = contextRuntime.has("output_path") ?
                        contextRuntime.get("output_path").getAsString() : "";
                Paths.ytHddPath = contextRuntime.has("yt_hdd_path") ? contextRuntime.get("yt_hdd_path").getAsString() : "";
            }
            Params.setParams(params.testParams);

            try (Writer outputWriter = getWriter(params)) {
                exitCode = run(new RunnerTask(loggingContext, timing, params, outputWriter));
            }

            long stopped = timing.getTotal().stop();
            logger.info(String.format("%s Tests Complete, total time: %s msec " +
                            "(%s msec configuration, %s msec execution [%s msec loggers]), [started: %s, finished: %s]",
                    getName(),
                    timing.getTotal().total(), timing.getConfiguration().total(), timing.getExecution().total(),
                    loggingContext.getStopWatch().total(), started, stopped));
            logger.info("Done with " + exitCode);
        }
        System.exit(exitCode);
    }

    static Writer getWriter(Parameters params) throws FileNotFoundException {
        if (params.output != null) {
            return new BufferedWriter(new OutputStreamWriter(new FileOutputStream(params.output, true),
                    StandardCharsets.UTF_8));

        } else {
            return new OutputStreamWriter(System.out, StandardCharsets.UTF_8);
        }
    }
}
