package ru.yandex.devtools.test;


public class ClassNotFoundRuntimeException extends RuntimeException {

    public ClassNotFoundRuntimeException(String message, Throwable cause) {
        super(message, cause);
    }
}
