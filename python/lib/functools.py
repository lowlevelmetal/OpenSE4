"""Higher-order functions, as in CPython's functools (OpenSE4 script runtime).

reduce, partial, wraps and update_wrapper, lru_cache and cache, cmp_to_key,
total_ordering, cached_property and singledispatch (dispatching on the class of the
first argument).
"""

_initial_missing = object()


def reduce(function, iterable, initial=_initial_missing):
    it = iter(iterable)
    if initial is _initial_missing:
        try:
            value = next(it)
        except StopIteration:
            raise TypeError("reduce() of empty iterable with no initial value")
    else:
        value = initial
    for element in it:
        value = function(value, element)
    return value


class partial:
    def __init__(self, func, *args, **keywords):
        if not callable(func):
            raise TypeError("the first argument must be callable")
        self.func = func
        self.args = args
        self.keywords = keywords

    def __call__(self, *args, **keywords):
        merged = dict(self.keywords)
        merged.update(keywords)
        return self.func(*self.args, *args, **merged)

    def __repr__(self):
        return "functools.partial({!r}, {})".format(self.func, ", ".join(
            [repr(a) for a in self.args] + ["{}={!r}".format(k, v) for k, v in self.keywords.items()]))


WRAPPER_ASSIGNMENTS = ("__module__", "__name__", "__qualname__", "__doc__")
WRAPPER_UPDATES = ("__dict__",)


def update_wrapper(wrapper, wrapped, assigned=WRAPPER_ASSIGNMENTS, updated=WRAPPER_UPDATES):
    # Function objects here take no new attributes: copy what can be copied.
    for attr in assigned:
        try:
            setattr(wrapper, attr, getattr(wrapped, attr))
        except (AttributeError, TypeError):
            pass
    try:
        wrapper.__wrapped__ = wrapped
    except (AttributeError, TypeError):
        pass
    return wrapper


def wraps(wrapped, assigned=WRAPPER_ASSIGNMENTS, updated=WRAPPER_UPDATES):
    return partial(update_wrapper, wrapped=wrapped, assigned=assigned, updated=updated)


def _make_key(args, kwds, typed):
    key = args
    if kwds:
        key += (_kwd_mark,)
        for item in kwds.items():
            key += item
    if typed:
        key += tuple(type(v) for v in args)
        if kwds:
            key += tuple(type(v) for v in kwds.values())
    return key


_kwd_mark = object()


class _CacheInfo:
    def __init__(self, hits, misses, maxsize, currsize):
        self.hits = hits
        self.misses = misses
        self.maxsize = maxsize
        self.currsize = currsize

    def __repr__(self):
        return "CacheInfo(hits={}, misses={}, maxsize={}, currsize={})".format(
            self.hits, self.misses, self.maxsize, self.currsize)


class _LruCacheWrapper:
    def __init__(self, func, maxsize, typed):
        self.__wrapped__ = func
        self._func = func
        self._maxsize = maxsize
        self._typed = typed
        self._cache = {}   # dicts keep insertion order: the first key is the oldest
        self._hits = 0
        self._misses = 0

    def __call__(self, *args, **kwargs):
        key = _make_key(args, kwargs, self._typed)
        cache = self._cache
        if key in cache:
            self._hits += 1
            value = cache.pop(key)
            cache[key] = value   # now the most recent
            return value
        self._misses += 1
        value = self._func(*args, **kwargs)
        if self._maxsize is not None and self._maxsize <= 0:
            return value
        cache[key] = value
        if self._maxsize is not None and len(cache) > self._maxsize:
            del cache[next(iter(cache))]
        return value

    def __get__(self, obj, objtype=None):
        if obj is None:
            return self
        return partial(self, obj)

    def cache_info(self):
        return _CacheInfo(self._hits, self._misses, self._maxsize, len(self._cache))

    def cache_clear(self):
        self._cache = {}
        self._hits = self._misses = 0


def lru_cache(maxsize=128, typed=False):
    if callable(maxsize) and not isinstance(maxsize, int):
        return _LruCacheWrapper(maxsize, 128, False)

    def decorating_function(user_function):
        return _LruCacheWrapper(user_function, maxsize, typed)

    return decorating_function


def cache(user_function):
    return _LruCacheWrapper(user_function, None, False)


def cmp_to_key(mycmp):
    class K:
        __slots__ = ["obj"]

        def __init__(self, obj):
            self.obj = obj

        def __lt__(self, other):
            return mycmp(self.obj, other.obj) < 0

        def __gt__(self, other):
            return mycmp(self.obj, other.obj) > 0

        def __eq__(self, other):
            return mycmp(self.obj, other.obj) == 0

        def __le__(self, other):
            return mycmp(self.obj, other.obj) <= 0

        def __ge__(self, other):
            return mycmp(self.obj, other.obj) >= 0

        __hash__ = None

    return K


def total_ordering(cls):
    """Fills in the comparisons a class lacks from __eq__ and one of them."""
    d = cls.__dict__
    if "__lt__" in d:
        if "__gt__" not in d:
            cls.__gt__ = lambda self, other: not (self < other) and self != other
        if "__le__" not in d:
            cls.__le__ = lambda self, other: self < other or self == other
        if "__ge__" not in d:
            cls.__ge__ = lambda self, other: not (self < other)
    elif "__le__" in d:
        if "__ge__" not in d:
            cls.__ge__ = lambda self, other: not (self <= other) or self == other
        if "__lt__" not in d:
            cls.__lt__ = lambda self, other: self <= other and self != other
        if "__gt__" not in d:
            cls.__gt__ = lambda self, other: not (self <= other)
    elif "__gt__" in d:
        if "__lt__" not in d:
            cls.__lt__ = lambda self, other: not (self > other) and self != other
        if "__ge__" not in d:
            cls.__ge__ = lambda self, other: self > other or self == other
        if "__le__" not in d:
            cls.__le__ = lambda self, other: not (self > other)
    elif "__ge__" in d:
        if "__le__" not in d:
            cls.__le__ = lambda self, other: not (self >= other) or self == other
        if "__gt__" not in d:
            cls.__gt__ = lambda self, other: self >= other and self != other
        if "__lt__" not in d:
            cls.__lt__ = lambda self, other: not (self >= other)
    else:
        raise ValueError("must define at least one ordering operation: < > <= >=")
    return cls


class cached_property:
    def __init__(self, func):
        self.func = func
        self.attrname = None

    def __set_name__(self, owner, name):
        self.attrname = name

    def __get__(self, instance, owner=None):
        if instance is None:
            return self
        name = self.attrname
        cache = instance.__dict__
        if name in cache:
            return cache[name]
        value = self.func(instance)
        # an instance attribute of the same name now hides this descriptor
        object.__setattr__(instance, name, value)
        return value


def singledispatch(func):
    registry = {object: func}

    def dispatch(cls):
        for klass in _mro(cls):
            if klass in registry:
                return registry[klass]
        return func

    def register(cls, f=None):
        if f is None:
            if isinstance(cls, type):
                return lambda g: register(cls, g)
            raise TypeError("register() needs a class")
        registry[cls] = f
        return f

    def wrapper(*args, **kwargs):
        if not args:
            raise TypeError("singledispatch function requires at least 1 positional argument")
        return dispatch(type(args[0]))(*args, **kwargs)

    return _Dispatcher(wrapper, register, dispatch, registry)


class _Dispatcher:
    def __init__(self, wrapper, register, dispatch, registry):
        self._wrapper = wrapper
        self.register = register
        self.dispatch = dispatch
        self.registry = registry

    def __call__(self, *args, **kwargs):
        return self._wrapper(*args, **kwargs)


def _mro(cls):
    order = []
    pending = [cls]
    while pending:
        c = pending.pop(0)
        if c not in order:
            order.append(c)
            pending.extend(getattr(c, "__bases__", ()))
    if object not in order:
        order.append(object)
    return order
