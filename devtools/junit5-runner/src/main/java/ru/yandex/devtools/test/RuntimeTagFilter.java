package ru.yandex.devtools.test;

import java.util.List;

import org.junit.platform.engine.FilterResult;
import org.junit.platform.engine.TestDescriptor;
import org.junit.platform.engine.TestTag;
import org.junit.platform.launcher.PostDiscoveryFilter;


public class RuntimeTagFilter implements PostDiscoveryFilter {

    private final YaTestNameBase testName;
    private final List<String> tags;

    public RuntimeTagFilter(YaTestNameBase testName, List<String> tags) {
        this.testName = testName;
        this.tags = tags;
    }

    public FilterResult apply(TestDescriptor test) {
        if (tags.isEmpty()) {
            return FilterResult.included("");
        }
        for (String tag : tags) {
            if (testName.isTest(test)) {
                for (TestTag test_tag: test.getTags()){
                    if (test_tag.getName().equals(tag)){
                        return FilterResult.included("Found tag " + tag + " in test");
                    }
                }
            }
        }
        return FilterResult.excluded("No suitable tags found");
    }
}

