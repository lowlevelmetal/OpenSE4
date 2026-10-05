"""Shallow and deep copies, as in CPython's copy module (OpenSE4 script runtime).

copy() and deepcopy() handle the built-in containers, class instances (their
attributes are copied; __copy__ and __deepcopy__ are used when a class defines
them) and anything immutable (returned as it is).
"""


class Error(Exception):
    pass


_atomic = (type(None), bool, int, float, str, bytes, type, range, type(len), type(lambda: 0))


def copy(x):
    cls = type(x)
    if isinstance(x, _atomic) or cls is tuple or cls is frozenset:
        return x
    if hasattr(x, "__copy__"):
        return x.__copy__()
    if cls is list:
        return list(x)
    if cls is dict:
        return dict(x)
    if cls is set:
        return set(x)
    if cls is bytearray:
        return bytearray(x)
    if isinstance(x, (list, dict, set)) and hasattr(x, "copy"):
        return x.copy()
    return _copy_instance(x, None)


def _copy_instance(x, memo):
    cls = type(x)
    y = object.__new__(cls)
    if memo is not None:
        memo[id(x)] = y
    for name, value in x.__dict__.items():
        object.__setattr__(y, name, value if memo is None else deepcopy(value, memo))
    return y


def deepcopy(x, memo=None):
    if memo is None:
        memo = {}
    key = id(x)
    if key in memo:
        return memo[key]
    cls = type(x)
    if isinstance(x, _atomic):
        return x
    if hasattr(x, "__deepcopy__"):
        y = x.__deepcopy__(memo)
    elif cls is list:
        y = []
        memo[key] = y
        for item in x:
            y.append(deepcopy(item, memo))
    elif cls is dict:
        y = {}
        memo[key] = y
        for k, v in x.items():
            y[deepcopy(k, memo)] = deepcopy(v, memo)
    elif cls is tuple:
        y = tuple(deepcopy(item, memo) for item in x)
    elif cls is set:
        y = set(deepcopy(item, memo) for item in x)
    elif cls is frozenset:
        y = frozenset(deepcopy(item, memo) for item in x)
    elif cls is bytearray:
        y = bytearray(x)
    elif isinstance(x, tuple) and hasattr(x, "_fields"):
        y = cls(*[deepcopy(item, memo) for item in x])
    elif isinstance(x, (dict, list)):
        y = copy(x)
        memo[key] = y
        if isinstance(x, dict):
            for k in list(y.keys()):
                y[k] = deepcopy(y[k], memo)
        else:
            for i in range(len(y)):
                y[i] = deepcopy(y[i], memo)
    else:
        y = _copy_instance(x, memo)
    memo[key] = y
    return y
