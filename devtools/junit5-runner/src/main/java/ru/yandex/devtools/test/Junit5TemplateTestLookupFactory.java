package ru.yandex.devtools.test;

import java.lang.reflect.Method;
import java.nio.file.Path;
import java.util.function.Supplier;

import org.junit.platform.launcher.LauncherDiscoveryRequest;
import org.junit.platform.launcher.TestIdentifier;

public class Junit5TemplateTestLookupFactory {

    private static final Method NEW_INSTANCE_METHOD;

    static {
        boolean supportV5_5;
        try {
            Class.forName("org.junit.platform.engine.reporting.OutputDirectoryProvider");
            supportV5_5 = false;
        } catch (ClassNotFoundException e) {
            supportV5_5 = true;
        }

        var className = supportV5_5
                ? "ru.yandex.devtools.test.Junit5TemplateTestLookupV_5_5"
                : "ru.yandex.devtools.test.Junit5TemplateTestLookupV_5_12";

        try {
            var type = Class.forName(className);
            NEW_INSTANCE_METHOD = type.getDeclaredMethod("lazyLookup", CachedTestNames.class, LauncherDiscoveryRequest.class, Path.class);
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
            return (Supplier<Junit5TemplateTestLookup>) NEW_INSTANCE_METHOD.invoke(null, testNames, request, outputRoot);
        } catch (Exception e) {
            throw new RuntimeException("Unable to get instance from " + NEW_INSTANCE_METHOD.getDeclaringClass(), e);
        }

    }

}
