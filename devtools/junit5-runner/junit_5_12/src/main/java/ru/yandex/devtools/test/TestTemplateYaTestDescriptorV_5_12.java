package ru.yandex.devtools.test;

import java.lang.reflect.Method;
import java.util.List;
import java.util.function.Supplier;

import org.junit.jupiter.engine.config.JupiterConfiguration;
import org.junit.jupiter.engine.descriptor.TestTemplateTestDescriptor;
import org.junit.platform.engine.UniqueId;
import org.junit.platform.engine.discovery.UniqueIdSelector;
import org.junit.platform.launcher.LauncherDiscoveryRequest;

public class TestTemplateYaTestDescriptorV_5_12 extends TestTemplateTestDescriptor {

    public TestTemplateYaTestDescriptorV_5_12(UniqueId uniqueId, Class<?> testClass, Method templateMethod,
                                              Supplier<List<Class<?>>> enclosingInstanceTypes,
                                              JupiterConfiguration configuration, LauncherDiscoveryRequest request) {
        super(uniqueId, testClass, templateMethod, enclosingInstanceTypes, configuration);
        request.getSelectorsByType(UniqueIdSelector.class).stream()
                .map(UniqueIdSelector::getUniqueId).forEach(getDynamicDescendantFilter()::allowUniqueIdPrefix);
    }
}
