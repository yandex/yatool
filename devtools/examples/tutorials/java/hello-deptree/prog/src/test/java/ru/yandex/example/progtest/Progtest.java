package ru.yandex.example.progtest;

import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;

class Progtest {

    @Test
    void multiplication() {
        assertEquals(2 * 2, 4);
    }

}
