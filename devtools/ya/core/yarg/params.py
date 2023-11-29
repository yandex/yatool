class Params(object):
    def __init__(self, **entries):
        self.__dict__.update(entries)

    def __str__(self):
        return str(self.__dict__)

    def __repr__(self):
        return repr(self.__dict__)

    def as_dict(self):
        return self.__dict__.copy()


def merge_params(*params):
    res = {}
    if len(params) == 1:
        # For ParamAsArgs
        return params[0]

    for x in params:
        assert not isinstance(x, list), "Maybe use of ParamAsArgs. {}".format(params)

        res.update(x.__dict__)
    return Params(**res)
