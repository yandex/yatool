package ru.yandex.devtools.test;

import java.io.Serializable;
import java.lang.invoke.MethodHandle;
import java.util.function.Function;

import org.junit.runner.Description;

public class YaTestName extends CachedTestNames<Object, Description> {

    private static final Function<Description, Object> KEY_FUNC;

    static {
        Function<Description, Object> keyFunc = String::valueOf;
        try {
            MethodHandle fUniqueId = Shared.getFieldAccessor(Description.class, "fUniqueId");
            keyFunc = key -> {
                try {
                    return (Serializable) fUniqueId.invoke(key);
                } catch (RuntimeException e) {
                    throw e;
                } catch (Throwable e) {
                    throw new RuntimeException(e);
                }
            };
        } catch (Throwable t) {
            //
        }

        KEY_FUNC = keyFunc;
    }

    @Override
    protected Object getKeyImpl(Description key) {
        return KEY_FUNC.apply(key);
    }

    @Override
    protected String getClassNameImpl(Description description) {
        if (description.getTestClass() != null) {
            return description.getTestClass().getCanonicalName();
        } else {
            return description.getClassName();
        }
    }

    @Override
    protected String getMethodNameImpl(Description description) {
        return description.getMethodName();
    }
}
