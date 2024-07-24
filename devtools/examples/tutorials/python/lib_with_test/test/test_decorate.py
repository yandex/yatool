import devtools.examples.tutorials.python.lib_with_test.decorate as deco


def test_capitalize():
    assert deco.capitalize('Hello, world!') == 'HELLO, WORLD!'
    assert deco.capitalize('Привет, мир!') == 'ПРИВЕТ, МИР!'


def test_colorize():
    assert deco.colorize('Hello, world!', 'red') != 'Hello, world!'
    assert deco.colorize('Hello, world!', 'red') != deco.colorize('Hello, world!', 'green')
