package ru.yandex.devtools.test;

import org.junit.jupiter.api.extension.ConditionEvaluationResult;
import org.junit.jupiter.api.extension.ExecutionCondition;
import org.junit.jupiter.api.extension.ExtensionContext;

public class YaIgnoreCondition implements ExecutionCondition {

    /**
     * Containers/tests are disabled if {@code @YaIgnore} is present on the test
     * class or method.
     */
    @Override
    public ConditionEvaluationResult evaluateExecutionCondition(ExtensionContext context) {
        return ConditionEvaluationResult.disabled("Disabled by @YaIgnore");
    }

}
