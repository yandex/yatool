package ru.yandex.example.lib;

import java.io.PrintStream;

import ru.yandex.example.sublib.SubGreet;

public class Greet {
    private Greet() { }

    public static void greet(String name, PrintStream out) {
        SubGreet.greet(name, out);
        out.format("Hello %s!\n", name);
    }
}
