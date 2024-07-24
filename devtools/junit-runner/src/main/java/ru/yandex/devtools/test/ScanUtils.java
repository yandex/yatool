package ru.yandex.devtools.test;

import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.lang.reflect.Modifier;
import java.net.URL;
import java.net.URLClassLoader;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Comparator;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.jar.JarFile;
import java.util.regex.Pattern;
import java.util.stream.Collectors;
import java.util.zip.ZipEntry;

import junit.framework.TestCase;
import org.junit.Test;
import org.junit.runner.Description;
import org.junit.runner.RunWith;
import org.junit.runners.model.FrameworkMethod;

import ru.yandex.devtools.log.Logger;


public class ScanUtils {

    private static final Logger logger = Logger.getLogger(ScanUtils.class);
    private static final Pattern MULTI_RELEASE_PREFIX = Pattern.compile("^META-INF/versions/\\d+/");

    public static List<Class<?>> scanJar(String jarPath) throws Exception {
        logger.info("Scanning JAR: %s", jarPath);
        try (JarFile jarFile = new JarFile(jarPath);
             URLClassLoader loader = new URLClassLoader(new URL[]{new URL("jar", "", "file:" + jarPath + "!/")})
        ) {
            List<Class<?>> result = Collections.list(jarFile.entries())
                    .stream()
                    .filter(jarEntry -> !jarEntry.isDirectory())
                    .map(ZipEntry::getName)
                    .filter(name -> name.endsWith(".class"))
                    .map(name -> name.substring(0, name.length() - 6))
                    .filter(name -> !name.endsWith("package-info")) // ignore package-info classes
                    .filter(name -> !name.endsWith("module-info")) // ignore module-info classes
                    /*
                    Multi-release JARs could contain multiple versions of the same class in root and in META-INFO/versions/N/ directories.
                    The multi-release prefix is not part of the class name and should be stripped before attempting to load the class,
                    the class loader will load the proper version based on the current JVM version.
                     */
                    .map(name -> MULTI_RELEASE_PREFIX.matcher(name).replaceFirst(""))
                    .distinct()
                    .sorted()
                    .map(name -> {
                        try {
                            return loader.loadClass(name.replace('/', '.'));
                        } catch (ClassNotFoundException e) {
                            throw new ClassNotFoundRuntimeException("failed to load class " + name, e);
                        } catch (NoClassDefFoundError e) {
                            throw new ClassNotFoundRuntimeException("failed to initialize class " + name, e);
                        }
                    })
                    .filter(clazz -> {
                        try {
                            return isTestClass(clazz);
                        } catch (NoClassDefFoundError e) {
                            throw new ClassNotFoundRuntimeException("failed to determine if class " + clazz.getName() + " is a test class", e);
                        }
                    })
                    .collect(Collectors.toList());

            logger.info("Found %s test classes", result.size());
            return result;
        }
    }

    public static boolean isTestClass(Class<?> clazz) {
        if (clazz.isAnnotation()
                        || clazz.isEnum()
                        || clazz.isInterface()
                        || Modifier.isAbstract(clazz.getModifiers())
                        || !Modifier.isPublic(clazz.getModifiers())
                ) {
                return false;
        }

        for (Class<?> klass = clazz; klass != null; klass = klass.getSuperclass()) {
            if (klass.isAnnotationPresent(RunWith.class)) {
                return true;
            }
        }

        for (Method m: clazz.getMethods()) {
            if (m.isAnnotationPresent(Test.class)) {
                return true;
            }
        }

        return TestCase.class.isAssignableFrom(clazz);
    }

    public static void listSubtests(Description description, List<Description> store) {
        if (description.isTest()) {
            store.add(description);

            return;
        }

        for (Description child: description.getChildren()) {
            listSubtests(child, store);
        }
    }

    public static List<Description> listSubtests(Description description) {
        List<Description> store = new ArrayList<>();

        listSubtests(description, store);

        // Stable list of subtest names
        store.sort(Comparator.comparing(TestUtils::getSubtestName));

        return store;
    }

    public static Method getDeclaredMethod(Class<?> clazz, String methodName, Class<?>... args) {
        try {
            return clazz.getDeclaredMethod(methodName, args);
        } catch (NoSuchMethodException e) {
            throw new RuntimeException(e);
        }
    }

    public static Object invokeMethod(Method method, Object object, Object... args) {
        method.setAccessible(true);
        try {
            return method.invoke(object, args);
        } catch (IllegalAccessException | InvocationTargetException e) {
            throw new RuntimeException(e);
        }
    }

    @SuppressWarnings("unchecked")
    public static List<FrameworkMethod> listJUnitParamsRunnerTestMethods(org.junit.runner.Runner runner) {
        return (List<FrameworkMethod>)invokeMethod(getDeclaredMethod(runner.getClass(), "getListOfMethods"), runner);
    }

    public static Description describeJUnitParamsRunnerTestMethodDummy(org.junit.runner.Runner runner, FrameworkMethod method) {
        return (Description)invokeMethod(getDeclaredMethod(runner.getClass().getSuperclass(), "describeChild", FrameworkMethod.class), runner, method);
    }

    public static Description describeJUnitParamsRunnerTestMethodReal(org.junit.runner.Runner runner, FrameworkMethod method) {
        return (Description)invokeMethod(getDeclaredMethod(runner.getClass(), "describeMethod", FrameworkMethod.class), runner, method);
    }

    public static Map<Description, List<Description>> makeJUnitParamsRunnerSubtestsMap(org.junit.runner.Runner runner) {
        Map<Description, List<Description>> result = new HashMap<>();
        for (FrameworkMethod method : listJUnitParamsRunnerTestMethods(runner)) {
            result.computeIfAbsent(describeJUnitParamsRunnerTestMethodDummy(runner, method), k -> new ArrayList<>())
                    .add(describeJUnitParamsRunnerTestMethodReal(runner, method));
        }
        return result;
    }

    public static List<Description> listSubtests(org.junit.runner.Runner runner) {
        if ("junitparams.JUnitParamsRunner".equals(runner.getClass().getCanonicalName())) {
            return listJUnitParamsRunnerTestMethods(runner)
                    .stream()
                    .map(method -> describeJUnitParamsRunnerTestMethodDummy(runner, method))
                    .collect(Collectors.toList());
        }

        return listSubtests(runner.getDescription());
    }
}
