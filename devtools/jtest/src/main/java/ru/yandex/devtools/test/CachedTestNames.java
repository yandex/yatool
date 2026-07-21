package ru.yandex.devtools.test;

import java.util.HashMap;
import java.util.Map;

public abstract class CachedTestNames<K, V> {

    private final Map<String, Class<?>> classCache;
    private final Map<String, Boolean> classExistsCache;

    private final Map<K, String> classNamesDesc = new HashMap<>();
    private final Map<K, String> methodNamesDesc = new HashMap<>();
    private final Map<K, String> fullName = new HashMap<>();

    protected CachedTestNames() {
        this.classCache = new HashMap<>();
        this.classExistsCache = new HashMap<>();
    }

    protected CachedTestNames(CachedTestNames<?, ?> cache) {
        this.classCache = cache.classCache;
        this.classExistsCache = cache.classExistsCache;
    }

    public String getFullName(V test) {
        return fullName.computeIfAbsent(getKeyImpl(test),
                key -> getClassName(test) + "::" + getMethodName(test));
    }

    public String getClassName(V test) {
        return classNamesDesc.computeIfAbsent(getKeyImpl(test),
                key -> Shared.ensureUTF8(getClassNameImpl(test)));
    }

    public String getMethodName(V test) {
        return methodNamesDesc.computeIfAbsent(getKeyImpl(test),
                key -> Shared.ensureUTF8(getMethodNameImpl(test)));
    }

    public Class<?> forName(String name) throws ClassNotFoundException {
        Class<?> clazz = classCache.get(name);
        if (clazz == null) {
            clazz = Class.forName(name, false, Thread.currentThread().getContextClassLoader());
            classCache.put(name, clazz);
        }
        return clazz;
    }

    public boolean isClassName(String name) {
        return classExistsCache.computeIfAbsent(name, n -> {
            try {
                forName(n);
                return true;
            } catch (ClassNotFoundException e) {
                return false;
            } catch (Error e) {
                return true;
            }
        });
    }

    //

    protected abstract K getKeyImpl(V key);

    protected abstract String getClassNameImpl(V key);

    protected abstract String getMethodNameImpl(V key);

}
