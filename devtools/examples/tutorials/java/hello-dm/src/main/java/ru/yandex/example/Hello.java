package ru.yandex.example;

import org.yaml.snakeyaml.Yaml;

public class Hello {
    private Hello() { }

    public static void main(String[] args) {
        Yaml yaml = new Yaml();
        String output = yaml.dump(args);
        System.out.println(output);
    }
}
