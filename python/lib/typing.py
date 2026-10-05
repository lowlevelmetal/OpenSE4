"""Type hints (OpenSE4 script runtime): the names of CPython's typing module, so that
annotated code imports and runs unchanged.

The runtime never evaluates function annotations, and records class annotations as
text, so these objects only have to exist and accept subscripts: List[int],
Optional[Fleet], Callable[[int], str], Dict[str, List[int]] all work, and mean
nothing at run time. Generic[T] and Protocol can be base classes. cast() returns its
value; NamedTuple and TypedDict build a namedtuple and a dict.
"""

from collections import namedtuple as _namedtuple

TYPE_CHECKING = False


class _Hint:
    """A subscriptable placeholder for a typing name."""

    def __init__(self, name):
        self._name = name

    def __getitem__(self, params):
        return _Hint(self._name + "[...]")

    def __call__(self, *args, **kwargs):
        raise TypeError("typing." + self._name + " cannot be called")

    def __or__(self, other):
        return _Hint("Union[...]")

    def __ror__(self, other):
        return _Hint("Union[...]")

    def __repr__(self):
        return "typing." + self._name


Any = _Hint("Any")
Union = _Hint("Union")
Optional = _Hint("Optional")
Callable = _Hint("Callable")
Tuple = _Hint("Tuple")
List = _Hint("List")
Dict = _Hint("Dict")
Set = _Hint("Set")
FrozenSet = _Hint("FrozenSet")
DefaultDict = _Hint("DefaultDict")
OrderedDict = _Hint("OrderedDict")
Counter = _Hint("Counter")
ChainMap = _Hint("ChainMap")
Deque = _Hint("Deque")
Type = _Hint("Type")
Iterable = _Hint("Iterable")
Iterator = _Hint("Iterator")
Generator = _Hint("Generator")
Reversible = _Hint("Reversible")
Sequence = _Hint("Sequence")
MutableSequence = _Hint("MutableSequence")
Mapping = _Hint("Mapping")
MutableMapping = _Hint("MutableMapping")
AbstractSet = _Hint("AbstractSet")
MutableSet = _Hint("MutableSet")
Collection = _Hint("Collection")
Container = _Hint("Container")
Hashable = _Hint("Hashable")
Sized = _Hint("Sized")
ItemsView = _Hint("ItemsView")
KeysView = _Hint("KeysView")
ValuesView = _Hint("ValuesView")
Awaitable = _Hint("Awaitable")
Coroutine = _Hint("Coroutine")
AsyncIterable = _Hint("AsyncIterable")
AsyncIterator = _Hint("AsyncIterator")
AsyncGenerator = _Hint("AsyncGenerator")
ContextManager = _Hint("ContextManager")
SupportsInt = _Hint("SupportsInt")
SupportsFloat = _Hint("SupportsFloat")
SupportsAbs = _Hint("SupportsAbs")
SupportsIndex = _Hint("SupportsIndex")
Pattern = _Hint("Pattern")
Match = _Hint("Match")
Text = str
AnyStr = _Hint("AnyStr")
ClassVar = _Hint("ClassVar")
Final = _Hint("Final")
Literal = _Hint("Literal")
Annotated = _Hint("Annotated")
NoReturn = _Hint("NoReturn")
Never = _Hint("Never")
Self = _Hint("Self")
LiteralString = _Hint("LiteralString")
TypeAlias = _Hint("TypeAlias")
TypeGuard = _Hint("TypeGuard")
Concatenate = _Hint("Concatenate")
Unpack = _Hint("Unpack")
Required = _Hint("Required")
NotRequired = _Hint("NotRequired")


class TypeVar:
    def __init__(self, name, *constraints, bound=None, covariant=False, contravariant=False):
        self.__name__ = name
        self.__constraints__ = constraints
        self.__bound__ = bound

    def __repr__(self):
        return "~" + self.__name__


class ParamSpec(TypeVar):
    pass


class TypeVarTuple(TypeVar):
    pass


class _GenericBase:
    pass


class _GenericAlias:
    """Generic: Generic[T] is a base class (class Box(Generic[T]): ...)."""

    def __getitem__(self, params):
        return _GenericBase

    def __repr__(self):
        return "typing.Generic"


Generic = _GenericAlias()


class Protocol:
    """A base class for structural types; nothing is checked at run time."""


class NewType:
    def __init__(self, name, tp):
        self.__name__ = name
        self.__supertype__ = tp

    def __call__(self, value):
        return value


def cast(typ, value):
    return value


def overload(func):
    return func


def final(f):
    return f


def no_type_check(f):
    return f


def runtime_checkable(cls):
    return cls


def dataclass_transform(**kwargs):
    return lambda f: f


def assert_never(value):
    raise AssertionError("expected code to be unreachable")


def reveal_type(obj):
    return obj


def get_type_hints(obj, globalns=None, localns=None):
    return dict(getattr(obj, "__annotations__", {}))


def NamedTuple(typename, fields=None, **kwargs):
    """NamedTuple("Point", [("x", int), ("y", int)]): a namedtuple."""
    if fields is None:
        names = list(kwargs)
    else:
        names = [f[0] if isinstance(f, tuple) else f for f in fields]
    return _namedtuple(typename, names)


def TypedDict(typename, fields=None, total=True, **kwargs):
    """TypedDict("Order", {...}): plain dicts are what it makes."""
    return dict
