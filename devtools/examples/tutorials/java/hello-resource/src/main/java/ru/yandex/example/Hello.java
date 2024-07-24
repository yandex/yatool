package ru.yandex.example;

import java.io.IOException;
import java.util.Properties;

public class Hello {
    private Hello() { }

    public static void main(String[] args) throws IOException {
        Properties properties = new Properties();
        properties.load(Thread.currentThread().getContextClassLoader().getResourceAsStream("myapp.properties"));

        System.out.format("Hello %s!\n", properties.getProperty("name"));
    }
}
