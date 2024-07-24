package ru.yandex.example.lib;

import java.io.PrintStream;

public class Greet {
    private Greet() { }

    public static void greet(String name, PrintStream out) {
        out.format("Hello %s!\n", name);
    }
}
