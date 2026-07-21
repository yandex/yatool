package ru.yandex.devtools.test;

import java.util.ArrayList;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.function.Function;
import java.util.stream.Collectors;

import org.junit.platform.engine.DiscoverySelector;
import org.junit.platform.engine.UniqueId;
import org.junit.platform.engine.discovery.DiscoverySelectors;

import ru.yandex.devtools.fnmatch.FileNameMatcher;
import ru.yandex.devtools.test.containers.ClassContainer;
import ru.yandex.devtools.test.containers.ParametrizedTestContainer;

public class YaFilter {

    private final Set<ClassContainer> classContainers;

    public YaFilter(Set<ClassContainer> classContainers) {
        this.classContainers = classContainers;
    }

    public List<DiscoverySelector> filtering(YaTestNameBase baseName, List<String> filters) {
        List<DiscoverySelector> selectors = new ArrayList<>();
        LinkedHashMap<String, String> searchableMap = getTestSearchableMap(classContainers);

        Set<String> notAppliedFilters = new HashSet<>();
        for (String filter : filters) {
            String uniqueId = searchableMap.get(filter);
            if (uniqueId != null) {
                var parsedUniqueId = UniqueId.parse(uniqueId);
                if (Boolean.parseBoolean(System.getProperty("old.reruning.strategy", "false"))
                        && parsedUniqueId.getLastSegment().getType().equals("test-template-invocation")) {
                    parsedUniqueId = parsedUniqueId.removeLastSegment();
                }
                selectors.add(DiscoverySelectors.selectUniqueId(parsedUniqueId));
                String[] testsPart = filter.split("::");
                if (testsPart.length >= 2) {
                    // for correct logic reporting if we can restart tests with duplicate Display name
                    baseName.putMethodName(parsedUniqueId, testsPart[1]);
                }
            } else  {
                notAppliedFilters.add(filter);
            }
        }
        final Map<String, FileNameMatcher> fileNameMatchers = notAppliedFilters.stream()
                .collect(Collectors.toMap(Function.identity(), pattern -> new FileNameMatcher(pattern, null)));
        for (Map.Entry<String, String> entry : searchableMap.entrySet()) {
            String testName = entry.getKey();
            for (String notAppliedFilter : notAppliedFilters) {
                FileNameMatcher fnMatcher = fileNameMatchers.get(notAppliedFilter);
                fnMatcher.append(testName);
                if (notAppliedFilter.startsWith(testName) || fnMatcher.isMatch()) {
                    selectors.add(DiscoverySelectors.selectUniqueId(entry.getValue()));
                    fnMatcher.reset();
                    break;
                }
                fnMatcher.reset();
            }
        }
        return selectors;
    }

    private LinkedHashMap<String, String> getTestSearchableMap(Set<ClassContainer> classContainers) {
        LinkedHashMap<String, String> searchableMap = new LinkedHashMap<>();
        DuplicateTestWatcher duplicateWatcher = new DuplicateTestWatcher();
        classContainers.forEach(classContainer -> {
            // this order is required:
            // simple test, parametrized test with parameters, parametrized test
            classContainer.getTests().forEach(testContainer ->
                    searchableMap.put(classContainer.getReportingView() +
                                    duplicateWatcher.getUniqueTestName(testContainer.getDisplayName()),
                            testContainer.getUniqueId()));
            classContainer.getParametrizedTests().forEach(parametrizedContainer -> {
                putParametrizedTest(searchableMap, parametrizedContainer, classContainer, duplicateWatcher);
                searchableMap.put(classContainer.getReportingView() +
                                duplicateWatcher.getUniqueTestName(parametrizedContainer.getDisplayName()),
                        parametrizedContainer.getUniqueId());
            });
        });
        return searchableMap;
    }

    private void putParametrizedTest(LinkedHashMap<String, String> searchableMap,
                                     ParametrizedTestContainer parametrizedTestContainer,
                                     ClassContainer classContainer,
                                     DuplicateTestWatcher duplicateWatcher) {
        var parametrizedTest = parametrizedTestContainer.getTests();
        for (int i = 0; i < parametrizedTest.size(); i++) {
            var testContainer = parametrizedTest.get(i);
            searchableMap.put(classContainer.getReportingView() +
                            duplicateWatcher.getUniqueTestName(testContainer.extractMethodDisplayName()),
                    testContainer.getUniqueId());
            String testParameterByIndex = testContainer.getParent().getDisplayName() + String.format(":[%d]", i + 1);
            searchableMap.put(classContainer.getReportingView() + duplicateWatcher.getUniqueTestName(testParameterByIndex),
                    testContainer.getUniqueId());

        }
    }
}
