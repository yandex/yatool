package ru.yandex.devtools.test;

import java.lang.reflect.Method;
import java.nio.file.Path;
import java.util.function.Supplier;

import org.junit.platform.engine.UniqueId;
import org.junit.platform.launcher.LauncherDiscoveryRequest;
import org.junit.platform.launcher.TestIdentifier;

public class Junit5TemplateTestLookupFactory {

    private static final Method NEW_INSTANCE_METHOD;

    static {

        JunitVersion version;
        try {
            // OutputDirectoryProvider is present since junit-platform 1.12
            Class.forName("org.junit.platform.engine.reporting.OutputDirectoryProvider");
            try {
                Class.forName("org.junit.jupiter.engine.descriptor.LauncherStoreFacade");
                version = JunitVersion.V13;
            } catch (ClassNotFoundException e) {
                version = JunitVersion.V12;
            }
        } catch (ClassNotFoundException e) {
            // DynamicDescendantFilter.allow was renamed to allowUniqueIdPrefix in jupiter 5.9.0,
            // before the platform 1.12 marker class above appeared, so probe the method itself
            version = JunitVersion.V5;
            try {
                Class.forName("org.junit.jupiter.engine.descriptor.DynamicDescendantFilter")
                        .getMethod("allowUniqueIdPrefix", UniqueId.class);
                version = JunitVersion.V9;
            } catch (ClassNotFoundException | NoSuchMethodException ignored) {
            }
        }

        var className = version.className;
        try {
            var type = Class.forName(className);
            NEW_INSTANCE_METHOD = type.getDeclaredMethod("lazyLookup", CachedTestNames.class,
                    LauncherDiscoveryRequest.class, Path.class);
        } catch (Exception e) {
            throw new RuntimeException("Unable to configure required implementation class " + className, e);
        }
    }

    @SuppressWarnings("unchecked")
    static Supplier<Junit5TemplateTestLookup> lazyLookup(
            CachedTestNames<String, TestIdentifier> testNames,
            LauncherDiscoveryRequest request,
            Path outputRoot
    ) {
        try {
            return (Supplier<Junit5TemplateTestLookup>) NEW_INSTANCE_METHOD.invoke(null, testNames, request,
                    outputRoot);
        } catch (Exception e) {
            throw new RuntimeException("Unable to get instance from " + NEW_INSTANCE_METHOD.getDeclaringClass(), e);
        }

    }

    private enum JunitVersion {
        V5("ru.yandex.devtools.test.Junit5TemplateTestLookupV_5_5"),
        V9("ru.yandex.devtools.test.Junit5TemplateTestLookupV_5_9"),
        V12("ru.yandex.devtools.test.Junit5TemplateTestLookupV_5_12"),
        V13("ru.yandex.devtools.test.Junit5TemplateTestLookupV_5_13");

        private final String className;

        JunitVersion(String className) {
            this.className = className;
        }
    }
}
