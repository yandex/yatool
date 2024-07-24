package ru.yandex.example.libtest;

import org.junit.Test;

import static org.junit.Assert.assertEquals;

public class Libtest {

    @Test
    public void testMultiply() {
        assertEquals(2 * 2, 4);
    }
}
