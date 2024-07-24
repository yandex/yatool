package ru.yandex.example;

import ru.yandex.example.lib.Greet;

public class Hello {
    private Hello() { }

    public static void main(String[] args) {
        Greet.greet("World", System.out);
    }
}
