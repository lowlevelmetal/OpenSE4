"""Data classes (OpenSE4 script runtime): the commonly used part of CPython's
dataclasses module.

    @dataclass
    class Order:
        target: int
        priority: int = 0
        tags: list = field(default_factory=list)

gives Order an __init__, __repr__ and __eq__ (and with order=True the comparisons,
with frozen=True read-only fields and a hash). Fields are the class's annotated
names, in order; ClassVar annotations are not fields. Also: field(), fields(),
asdict(), astuple(), replace(), is_dataclass(), MISSING, KW_ONLY and
FrozenInstanceError. Not supported: InitVar, slots=True (accepted, ignored),
match_args.
"""

__all__ = [
    "dataclass", "field", "Field", "fields", "asdict", "astuple", "replace", "is_dataclass",
    "MISSING", "KW_ONLY", "FrozenInstanceError",
]


class _Missing:
    def __repr__(self):
        return "MISSING"


MISSING = _Missing()


class _KwOnly:
    def __repr__(self):
        return "KW_ONLY"


KW_ONLY = _KwOnly()


class FrozenInstanceError(AttributeError):
    pass


class Field:
    def __init__(self, default, default_factory, init, repr, hash, compare, metadata, kw_only):
        self.name = None
        self.type = None
        self.default = default
        self.default_factory = default_factory
        self.init = init
        self.repr = repr
        self.hash = hash
        self.compare = compare
        self.metadata = {} if metadata is None else metadata
        self.kw_only = kw_only

    def __repr__(self):
        return "Field(name={!r},type={!r},default={!r},default_factory={!r},init={!r},repr={!r},hash={!r},compare={!r},kw_only={!r})".format(
            self.name, self.type, self.default, self.default_factory, self.init, self.repr, self.hash,
            self.compare, self.kw_only)


def field(*, default=MISSING, default_factory=MISSING, init=True, repr=True, hash=None, compare=True,
          metadata=None, kw_only=MISSING):
    if default is not MISSING and default_factory is not MISSING:
        raise ValueError("cannot specify both default and default_factory")
    return Field(default, default_factory, init, repr, hash, compare, metadata, kw_only)


def _is_classvar(annotation):
    if not isinstance(annotation, str):
        return False
    return (annotation in ("ClassVar", "typing.ClassVar") or annotation.startswith("ClassVar[")
            or annotation.startswith("typing.ClassVar["))


def _is_kw_only_marker(annotation):
    return isinstance(annotation, str) and annotation in ("KW_ONLY", "dataclasses.KW_ONLY")


def _class_fields(cls, kw_only_default):
    result = []
    # fields of dataclass bases come first, as in CPython
    for base in reversed(getattr(cls, "__bases__", ())):
        base_fields = getattr(base, "__dataclass_fields__", None)
        if base_fields:
            for f in base_fields.values():
                result = [g for g in result if g.name != f.name]
                result.append(f)
    annotations = cls.__dict__.get("__annotations__", {})
    kw_only = kw_only_default
    for name, annotation in annotations.items():
        if _is_classvar(annotation):
            continue
        if _is_kw_only_marker(annotation):
            kw_only = True
            continue
        value = cls.__dict__.get(name, MISSING)
        if isinstance(value, Field):
            f = value
            if f.default is not MISSING:
                setattr(cls, name, f.default)
            else:
                try:
                    delattr(cls, name)
                except AttributeError:
                    pass
        else:
            f = Field(value, MISSING, True, True, None, True, None, MISSING)
        if isinstance(f.default, (list, dict, set)):
            raise ValueError("mutable default {} for field {} is not allowed: use default_factory".format(
                type(f.default).__name__, name))
        f.name = name
        f.type = annotation
        if f.kw_only is MISSING:
            f.kw_only = kw_only
        result = [g for g in result if g.name != name]
        result.append(f)
    return result


def _make_init(fields, has_post_init, frozen):
    positional = [f for f in fields if f.init and not f.kw_only]
    keyword = [f for f in fields if f.init and f.kw_only]
    seen_default = False
    for f in positional:
        if f.default is not MISSING or f.default_factory is not MISSING:
            seen_default = True
        elif seen_default:
            raise TypeError("non-default argument '{}' follows default argument".format(f.name))
    by_name = {f.name: f for f in positional + keyword}

    def __init__(self, *args, **kwargs):
        if len(args) > len(positional):
            raise TypeError("__init__() takes {} positional arguments but {} were given".format(
                len(positional) + 1, len(args) + 1))
        values = {}
        for f, value in zip(positional, args):
            values[f.name] = value
        for name, value in kwargs.items():
            if name not in by_name:
                raise TypeError("__init__() got an unexpected keyword argument '{}'".format(name))
            if name in values:
                raise TypeError("__init__() got multiple values for argument '{}'".format(name))
            values[name] = value
        for f in fields:
            if f.name in values:
                value = values[f.name]
            elif f.default_factory is not MISSING:
                value = f.default_factory()
            elif f.default is not MISSING:
                value = f.default
            elif f.init:
                raise TypeError("__init__() missing required argument: '{}'".format(f.name))
            else:
                continue
            object.__setattr__(self, f.name, value)
        if has_post_init:
            self.__post_init__()

    return __init__


def _values(obj, fields):
    return tuple(getattr(obj, f.name) for f in fields)


def _process(cls, init, repr, eq, order, unsafe_hash, frozen, kw_only):
    fields = _class_fields(cls, kw_only)
    cls.__dataclass_fields__ = {f.name: f for f in fields}
    cls.__dataclass_params__ = {"init": init, "repr": repr, "eq": eq, "order": order,
                                "unsafe_hash": unsafe_hash, "frozen": frozen}
    if init and "__init__" not in cls.__dict__:
        cls.__init__ = _make_init(fields, hasattr(cls, "__post_init__"), frozen)
    if repr and "__repr__" not in cls.__dict__:
        shown = [f for f in fields if f.repr]

        def __repr__(self):
            return "{}({})".format(type(self).__name__,
                                   ", ".join("{}={!r}".format(f.name, getattr(self, f.name)) for f in shown))

        cls.__repr__ = __repr__
    compared = [f for f in fields if f.compare]
    if eq and "__eq__" not in cls.__dict__:
        def __eq__(self, other):
            if type(other) is not type(self):
                return False
            return _values(self, compared) == _values(other, compared)

        cls.__eq__ = __eq__
    if order:
        def check(other, self):
            if type(other) is not type(self):
                raise TypeError("'<' not supported between instances of '{}' and '{}'".format(
                    type(self).__name__, type(other).__name__))

        def __lt__(self, other):
            check(other, self)
            return _values(self, compared) < _values(other, compared)

        def __le__(self, other):
            check(other, self)
            return _values(self, compared) <= _values(other, compared)

        def __gt__(self, other):
            check(other, self)
            return _values(self, compared) > _values(other, compared)

        def __ge__(self, other):
            check(other, self)
            return _values(self, compared) >= _values(other, compared)

        cls.__lt__ = __lt__
        cls.__le__ = __le__
        cls.__gt__ = __gt__
        cls.__ge__ = __ge__
    if frozen:
        def __setattr__(self, name, value):
            raise FrozenInstanceError("cannot assign to field '{}'".format(name))

        def __delattr__(self, name):
            raise FrozenInstanceError("cannot delete field '{}'".format(name))

        cls.__setattr__ = __setattr__
        cls.__delattr__ = __delattr__
    if unsafe_hash or (eq and frozen):
        hashed = [f for f in fields if (f.compare if f.hash is None else f.hash)]

        def __hash__(self):
            return hash(_values(self, hashed))

        cls.__hash__ = __hash__
    return cls


def dataclass(cls=None, *, init=True, repr=True, eq=True, order=False, unsafe_hash=False, frozen=False,
              match_args=True, kw_only=False, slots=False):
    def wrap(c):
        return _process(c, init, repr, eq, order, unsafe_hash, frozen, kw_only)

    if cls is None:
        return wrap
    return wrap(cls)


def is_dataclass(obj):
    cls = obj if isinstance(obj, type) else type(obj)
    return hasattr(cls, "__dataclass_fields__")


def fields(class_or_instance):
    try:
        return tuple(class_or_instance.__dataclass_fields__.values())
    except AttributeError:
        raise TypeError("must be called with a dataclass type or instance")


def _copy_value(value, dict_factory, as_tuple):
    if is_dataclass(value) and not isinstance(value, type):
        if as_tuple:
            return tuple(_copy_value(getattr(value, f.name), dict_factory, True) for f in fields(value))
        return dict_factory([(f.name, _copy_value(getattr(value, f.name), dict_factory, False)) for f in fields(value)])
    if isinstance(value, tuple) and hasattr(value, "_fields"):
        return type(value)(*[_copy_value(v, dict_factory, as_tuple) for v in value])
    if isinstance(value, (list, tuple)):
        return type(value)(_copy_value(v, dict_factory, as_tuple) for v in value)
    if isinstance(value, dict):
        return type(value)((_copy_value(k, dict_factory, as_tuple), _copy_value(v, dict_factory, as_tuple))
                           for k, v in value.items())
    return value


def asdict(obj, *, dict_factory=dict):
    if not is_dataclass(obj) or isinstance(obj, type):
        raise TypeError("asdict() should be called on dataclass instances")
    return _copy_value(obj, dict_factory, False)


def astuple(obj, *, tuple_factory=tuple):
    if not is_dataclass(obj) or isinstance(obj, type):
        raise TypeError("astuple() should be called on dataclass instances")
    return tuple_factory(_copy_value(obj, dict, True))


def replace(obj, **changes):
    values = {}
    for f in fields(obj):
        if not f.init:
            if f.name in changes:
                raise ValueError("field {} cannot be changed by replace(): it is not in __init__".format(f.name))
            continue
        values[f.name] = changes.pop(f.name) if f.name in changes else getattr(obj, f.name)
    if changes:
        raise TypeError("replace() got unexpected field(s): {}".format(", ".join(changes)))
    return type(obj)(**values)
