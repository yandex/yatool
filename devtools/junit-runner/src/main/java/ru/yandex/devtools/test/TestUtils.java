package ru.yandex.devtools.test;

import org.junit.runner.Description;

public class TestUtils {
    private static final YaTestName NAME_CACHE = new YaTestName();

    public static String getSubtestName(Description description) {
        return NAME_CACHE.getFullName(description);
    }

    public static String getTestClassName(Description description) {
        return NAME_CACHE.getClassName(description);
    }

    public static String getTestMethodName(Description description) {
        return NAME_CACHE.getMethodName(description);
    }

    public static YaTestName getNameCache() {
        return NAME_CACHE;
    }
}
