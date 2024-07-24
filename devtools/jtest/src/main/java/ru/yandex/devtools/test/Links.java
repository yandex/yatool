package ru.yandex.devtools.test;


import java.util.Objects;

import ru.yandex.devtools.log.Logger;

public class Links {
    private final static Logger logger = Logger.getLogger(Links.class);

    private static volatile LinksListener listener;

    static void setListener(LinksListener listener) {
        Links.listener = Objects.requireNonNull(listener);
    }

    public static void set(String name, String value) {
        if (listener == null) {
            logger.error("Links Listener is missing. Link name: %s, value: %s.", name, value);
        } else {
            listener.subtestLinksAdded(name, value);
        }
    }

    public interface LinksListener {
        void subtestLinksAdded(String name, String value);
    }
}
