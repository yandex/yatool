package ru.yandex.example.sublib;

import java.io.PrintStream;

public class SubGreet {
    private SubGreet() { }

    public static void greet(String name, PrintStream out) {
        out.format("SubHello %s!\n", name);
    }
}
