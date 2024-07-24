package ru.yandex.devtools.test;

import java.util.List;

import ru.yandex.devtools.fnmatch.FileNameMatcher;


public class Matcher {

    public static boolean matchesAll(String name, List<String> patterns) {
        for (String pattern: patterns) {
            FileNameMatcher matcher = new FileNameMatcher(pattern, null);

            matcher.append(name);

            if (!matcher.isMatch()) {
                return false;
            }
        }

        return true;
    }

    public static boolean matchesAny(String name, List<String> patterns) {
        if(patterns.isEmpty()) {
            return true;
        }

        for (String pattern: patterns) {
            FileNameMatcher matcher = new FileNameMatcher(pattern, null);

            matcher.append(name);

            if (matcher.isMatch()) {
                return true;
            }
        }

        return false;
    }
}
