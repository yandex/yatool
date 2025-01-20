import functools
import exts.timer


def timeit(func):
    @functools.wraps(func)
    def wrapper(*args, **kwargs):
        timer = exts.timer.Timer(__name__)
        res = func(*args, **kwargs)
        timer.show_step(func.__name__)
        return res

    return wrapper
