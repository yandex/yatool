package ru.yandex.devtools.test;

import java.util.function.Consumer;

import org.junit.platform.engine.support.descriptor.MethodSource;
import org.junit.platform.launcher.TestIdentifier;

/**
 * Класс выполняет дополнительный анализ тестовых методов, собирая список параметризованных вызовов.
 */
public interface Junit5TemplateTestLookup {

    void discoverTemplateInvocation(
            TestIdentifier test,
            MethodSource methodSource,
            Consumer<TestIdentifier> newIdentifierListener
    );
}

