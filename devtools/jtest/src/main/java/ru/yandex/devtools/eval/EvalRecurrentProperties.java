package ru.yandex.devtools.eval;

import java.util.HashSet;
import java.util.Optional;
import java.util.Properties;
import java.util.Set;

/**
 * @author Stepan Koltsov
 * @author Sergey Vavinov
 */
public class EvalRecurrentProperties {
    private static final String PP_START = "${";
    private static final String PP_END = "}";
    public static final String PP_DEFAULT = ":-";

    private static Set<String> plus1(Set<String> set, String key) {
        Set<String> result = new HashSet<>();
        result.addAll(set);
        result.add(key);
        return result;
    }

    private static void validateNotEmpty(String string) {
        if (string == null || string.length() == 0) {
            throw new IllegalArgumentException("The validated string is empty");
        }
    }

    private static void validateNotNull(String string) {
        if (string == null) {
            throw new IllegalArgumentException("must be not null: string");
        }
    }

    private static String getProperty(Properties properties, String key, Set<String> visitedPlaceholders) {
        int separator = key.indexOf(PP_DEFAULT);
        Optional<String> defaultValue = Optional.empty();
        if (separator >= 0) {
            defaultValue = Optional.of(key.substring(separator + PP_DEFAULT.length()));
            key = key.substring(0, separator);
        }

        String value = properties.getProperty(key);
        if (value == null) {
            if (defaultValue.isPresent()) {
                value = defaultValue.get();
            }

            throw new IllegalArgumentException("cannot resolve property '" + key + "'");
        }

        return eval(value, properties, plus1(visitedPlaceholders, key));
    }

    private static String eval(String string, Properties properties, Set<String> visitedPlaceholders) {
        if (!string.contains(PP_START)) return string;

        int i1 = string.lastIndexOf(PP_START);
        if (i1 < 0) return string;

        int i2 = string.indexOf(PP_END, i1);
        if (i2 < 0) throw new IllegalArgumentException("unterminated '}' while resolving '" + string + "'");

        String name = string.substring(i1 + PP_START.length(), i2);
        if (visitedPlaceholders.contains(name)) {
            throw new IllegalArgumentException("cyclic dependency on property '" + name + "'");
        }

        String value = getProperty(properties, name, visitedPlaceholders);

        String expanded = string.substring(0, i1) + value + string.substring(i2 + PP_END.length());
        return eval(expanded, properties, visitedPlaceholders);
    }

    public static String eval(final String string, final Properties properties) {
        validateNotNull(string);
        return eval(string, properties, new HashSet<String>());
    }

    public static String getProperty(Properties properties, String key, String defaultValue) {
        validateNotEmpty(key);

        if (properties instanceof RecurrentProperties)
            return properties.getProperty(key, defaultValue);

        String string = properties.getProperty(key, defaultValue);
        return eval(string, properties);
    }

    public static String getProperty(Properties properties, String key) {
        validateNotEmpty(key);

        if (properties instanceof RecurrentProperties)
            return properties.getProperty(key);

        String string = properties.getProperty(key);
        if (string == null) throw new IllegalArgumentException("property " + key + " not found");
        return eval(string, properties);
    }
}

