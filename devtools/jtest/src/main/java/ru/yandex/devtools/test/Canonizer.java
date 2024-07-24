package ru.yandex.devtools.test;


import java.util.Objects;

public class Canonizer {

    private static volatile CanonizingListener listener;

    static void setListener(CanonizingListener listener) {
        Canonizer.listener = Objects.requireNonNull(listener);
    }

    public static void canonize(Object object) {
        if (listener == null) {
            throw new RuntimeException("Listener is missing.");
        } else {
            listener.subtestCanonized(object);
        }
    }


    public interface CanonizingListener {
        void subtestCanonized(Object object);
    }
}
