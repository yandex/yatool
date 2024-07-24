import devtools.examples.tutorials.python.library.decorate as deco


def say_hello(capitalize, color):
    hw = 'Hello, world!'
    if capitalize:
        hw = deco.capitalize(hw)
    if color:
        hw = deco.colorize(hw, color)
    print(hw)
