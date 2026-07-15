package ru.yandex.devtools.test.containers;

import org.junit.platform.launcher.TestIdentifier;

public abstract class JunitEntity {
    private final TestIdentifier testIdentifier;

    protected JunitEntity(TestIdentifier testIdentifier) {
        this.testIdentifier = testIdentifier;
    }

    public String getDisplayName() {
        return testIdentifier.getDisplayName();
    }

    public String getUniqueId() {
        return testIdentifier.getUniqueId();
    }

    public TestIdentifier getTestIdentifier() {
        return testIdentifier;
    }
}
