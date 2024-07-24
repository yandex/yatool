package ru.yandex.example.lib.test;


import java.io.ByteArrayOutputStream;
import java.io.PrintStream;
import java.io.UnsupportedEncodingException;
import java.nio.charset.StandardCharsets;

import org.junit.jupiter.api.Test;

import ru.yandex.example.lib.Greet;

import static org.junit.jupiter.api.Assertions.assertEquals;

class GreetTest {
    private String greetToString(String name) throws UnsupportedEncodingException {
        final ByteArrayOutputStream baos = new ByteArrayOutputStream();
        final String utf8 = StandardCharsets.UTF_8.name();
        try (PrintStream ps = new PrintStream(baos, true, utf8)) {
            Greet.greet(name, ps);
        }
        return baos.toString(utf8);
    }

    @Test
    void messageFormat() throws UnsupportedEncodingException {
        assertEquals("Hello World!\n", greetToString("World"));
    }

    @Test
    void properName() throws UnsupportedEncodingException {
        assertEquals("Hello Vasja!\n", greetToString("Vasja"));
    }

}
