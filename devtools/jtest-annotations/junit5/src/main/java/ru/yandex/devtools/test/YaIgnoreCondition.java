package ru.yandex.devtools.test;

import org.junit.jupiter.api.extension.ConditionEvaluationResult;
import org.junit.jupiter.api.extension.ExecutionCondition;
import org.junit.jupiter.api.extension.ExtensionContext;

import ru.yandex.devtools.test.annotations.YaIgnore;

public class YaIgnoreCondition implements ExecutionCondition {

    /**
     * Containers/tests are disabled if {@code @YaIgnore} is present on the test
     * class or method.
     */
    @Override
    public ConditionEvaluationResult evaluateExecutionCondition(ExtensionContext context) {
        String reason = "Disabled by @YaIgnore" + context.getElement()
                .map(element -> element.getAnnotation(YaIgnore.class))
                .map(YaIgnore::value)
                .filter(s -> !s.isEmpty())
                .map(value -> ": " + value)
                .orElse("");
        return ConditionEvaluationResult.disabled(reason);
    }
}
