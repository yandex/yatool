package ru.yandex.devtools.test;

import java.lang.reflect.Method;
import java.nio.file.Path;
import java.util.List;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.Future;
import java.util.function.Consumer;
import java.util.function.Supplier;

import org.junit.jupiter.engine.config.DefaultJupiterConfiguration;
import org.junit.jupiter.engine.config.JupiterConfiguration;
import org.junit.jupiter.engine.descriptor.JupiterEngineDescriptor;
import org.junit.jupiter.engine.descriptor.TestTemplateTestDescriptor;
import org.junit.jupiter.engine.execution.JupiterEngineExecutionContext;
import org.junit.platform.engine.EngineExecutionListener;
import org.junit.platform.engine.TestDescriptor;
import org.junit.platform.engine.TestExecutionResult;
import org.junit.platform.engine.UniqueId;
import org.junit.platform.engine.reporting.OutputDirectoryProvider;
import org.junit.platform.engine.reporting.ReportEntry;
import org.junit.platform.engine.support.descriptor.MethodSource;
import org.junit.platform.engine.support.hierarchical.Node;
import org.junit.platform.launcher.LauncherDiscoveryRequest;
import org.junit.platform.launcher.TestIdentifier;

import ru.yandex.devtools.log.Logger;

/**
 * Класс выполняет дополнительный анализ тестовых методов, собирая список параметризованных вызовов.
 */
public class Junit5TemplateTestLookupV_5_12 implements Junit5TemplateTestLookup {

    private static final Logger logger = Logger.getLogger(Junit5TemplateTestLookup.class);

    private final CachedTestNames<String, TestIdentifier> testName;
    private final JupiterConfiguration configuration;
    private final JupiterEngineExecutionContext executionContext;

    private Junit5TemplateTestLookupV_5_12(CachedTestNames<String, TestIdentifier> testName, LauncherDiscoveryRequest request, Path outputRoot) {
        this.testName = testName;
        this.configuration = new DefaultJupiterConfiguration(request.getConfigurationParameters(),
                new OutputDirectoryProvider() {
                    @Override
                    public Path getRootDirectory() {
                        return outputRoot;
                    }

                    @Override
                    public Path createOutputDirectory(TestDescriptor testDescriptor) {
                        return outputRoot;
                    }
                });

        JupiterEngineExecutionContext context = new JupiterEngineExecutionContext(
                new EmptyEngineExecutionListener(), configuration);

        // Не более чем инициализация контекста
        JupiterEngineDescriptor engineDescriptor = new JupiterEngineDescriptor(
                UniqueId.root("test", "root"), configuration);
        this.executionContext = engineDescriptor.prepare(context);
    }

    @Override
    public void discoverTemplateInvocation(TestIdentifier test, MethodSource methodSource,
            Consumer<TestIdentifier> newIdentifierListener) {
        try {
            // Ищем подходящий тестовый метод по его описанию
            Class<?> clazz = testName.forName(methodSource.getClassName());
            Method method = null;
            for (Method checkMethod : clazz.getDeclaredMethods()) {
                if (checkMethod.getName().equals(methodSource.getMethodName())) {
                    if (MethodSource.from(checkMethod).equals(methodSource)) {
                        method = checkMethod;
                        break;
                    }
                }
            }
            if (method != null) {
                TestTemplateTestDescriptor descriptor = new TestTemplateTestDescriptor(
                        UniqueId.parse(test.getUniqueId()), clazz, method, List::of, configuration);
                JupiterEngineExecutionContext context = descriptor.prepare(executionContext);
                descriptor.execute(context, new Node.DynamicTestExecutor() {
                    @Override
                    public void execute(TestDescriptor testDescriptor) {
                        newIdentifierListener.accept(TestIdentifier.from(testDescriptor));
                    }

                    @Override
                    public Future<?> execute(TestDescriptor testDescriptor, EngineExecutionListener executionListener) {
                        // in fact, does not support async execution
                        execute(testDescriptor);
                        return CompletableFuture.completedFuture(null);
                    }

                    @Override
                    public void awaitFinished() {
                        //
                    }
                });
            }
        } catch (Throwable t) {
            t.printStackTrace();
            logger.error("Unable to discover configuration parameters for %s: %s", methodSource, t.getMessage());
        }

    }

    // Возвращает лениво инициализируемый lookup - не инициализируется, если не встречаются
    // parameterized или repeated тесты
    static Supplier<Junit5TemplateTestLookup> lazyLookup(CachedTestNames<String, TestIdentifier> testName, LauncherDiscoveryRequest request,
            Path outputRoot) {
        return new Supplier<Junit5TemplateTestLookup>() {
            private Junit5TemplateTestLookup lookup;
            @Override
            public Junit5TemplateTestLookup get() {
                if (lookup == null) {
                    lookup = new Junit5TemplateTestLookupV_5_12(testName, request, outputRoot);
                }
                return lookup;
            }
        };
    }

    private static class EmptyEngineExecutionListener implements EngineExecutionListener {
        @Override
        public void dynamicTestRegistered(TestDescriptor testDescriptor) {
        }

        @Override
        public void executionSkipped(TestDescriptor testDescriptor, String reason) {
        }

        @Override
        public void executionStarted(TestDescriptor testDescriptor) {
        }

        @Override
        public void executionFinished(TestDescriptor testDescriptor, TestExecutionResult testExecutionResult) {
        }

        @Override
        public void reportingEntryPublished(TestDescriptor testDescriptor, ReportEntry entry) {
        }
    }
}
