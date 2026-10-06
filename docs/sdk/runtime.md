# The script runtime

In-game scripts (data generators, computer players, rules hooks, interface
extensions) run on MicroPython inside OpenSE4 (docs/MODDING_SDK.md, section 14.1).
This page describes what a script can use, what it cannot, the limits it runs
under and how the engine drives it. External bots run on ordinary CPython and are
not covered here.

- Code: `src/script/runtime.hpp` (the engine's interface), `src/script/port/` (the
  MicroPython port), `python/lib/` (the runtime's own Python modules),
  `third_party/micropython/` (MicroPython itself, with our patches).
- Tests: `tests/sdk/` (`-tc="script*"`).

## Interpreters

An interpreter lives for one engine call (section 14.3): the engine creates it,
gives it the mod's files and its own functions, calls into it and drops it.
Nothing a script keeps in module globals survives the call; what it must remember
goes into the saved game through the SDK (AI memory, mod data).

MicroPython keeps its state in globals, so a process runs one interpreter at a
time. `Interpreter::create()` refuses with a usage error while another one lives.
An interpreter may be used from any thread, one call at a time; a call from inside
one of its own engine functions is refused.

Creating an interpreter costs about 25 µs. Compiled bytecode is cached per process,
keyed by file path and text, so a module is compiled once and later interpreters
only load it. For a module of about 2400 lines (160 classes, 100 functions, a table
of 400 entries), creating an interpreter and importing it took 9.4 ms the first time
and 0.55 ms afterwards, in an optimized build on a desktop x86_64 computer. Simple
code runs at about 2.3 ns per bytecode there, so a million bytecodes of budget is a
few milliseconds of work. The runtime adds about 0.65 MB to an executable.

## The language

Python 3 as MicroPython 1.29 implements it: classes with multiple inheritance,
properties and descriptors, generators and `yield from`, closures and `nonlocal`,
comprehensions, f-strings (with `=` and nested format specifications), the walrus
operator, `async`/`await` syntax, exceptions with tracebacks, arbitrary-precision
integers, floats (IEEE double), sets and frozensets, `bytes`, `bytearray` and
`memoryview`, `eval`, `exec` and `compile`.

OpenSE4 changes three things so that code written for CPython behaves the same:

- **Dicts keep insertion order**, as in CPython 3.7 and later; also instance
  attributes and module globals. (MicroPython's dicts iterate in hash order.)
  `popitem()` takes the last item.
- **Sorting is stable** and calls the `key` function once per item, as in CPython;
  `reverse=True` keeps equal items in their order. (MicroPython uses a quicksort,
  which is neither stable nor fast on sorted input.) Changing a list while it is
  being sorted is a `ValueError`.
- **Class annotations are recorded**: `x: int` in a class body puts `'x'` in the
  class's `__annotations__` (in order, with the annotation as short text, as with
  `from __future__ import annotations`). This lets `dataclasses` find fields.

Differences from CPython that remain:

- No `match` statement; no `**` unpacking inside dict displays (`{**a, 'k': 1}`; use
  `dict(a, k=1)`); no `__init_subclass__`.
- `bool` is not a subclass of `int` for `isinstance(True, int)`.
- `str.upper()` and `str.lower()` change ASCII letters only. Some methods are missing:
  `str.format_map`, `casefold`, `title`, `swapcase`, `zfill`, `removeprefix`,
  `str.maketrans`, and `int.bit_length`. `int()` takes its base positionally only.
- Function annotations are never evaluated, and instance `__dict__` is read-only (use
  `setattr`).
- The regular expression engine is small: `. [] [^] * + ? *? +? | ( ) (?: ) ^ $ \d
  \w \s` work; counted repetition does not (`a{2}` matches the text `a{2}`), nor do
  named groups, lookarounds or flags.
- Integers used as sizes, indices, `range()` bounds and counts must fit in 32 bits
  (−2³¹+1 to 2³¹−1) on every build, as on 32-bit ARM: `range(2**40)` or
  `items[:10**10]` is an `OverflowError`. Integers themselves are unlimited.
- `repr()` and `str()` of floats may differ from CPython's in the last digit, and
  rarely print more digits than needed: formatting uses double arithmetic only, so
  that it is the same on every platform.
- Hash values and `id()` numbers differ from CPython's (see "The same on every
  computer"). Don't compare numbers with `is`.

## Modules

| Module | What it is |
|---|---|
| `builtins` | as listed above; `RecursionError` added |
| `sys` | `version`, `version_info`, `implementation`, `platform` (`"opense4"`), `byteorder`, `maxsize` (2⁶³−1 on every build), `modules`, `exit`, `exc_info`, `print_exception` (to the script's output, or into a stream such as an `io.StringIO`) |
| `math` | MicroPython's, plus (in Python) `gcd`, `lcm`, `isqrt`, `comb`, `perm`, `prod`, `dist`, `hypot` with any number of coordinates, `fsum`, `cbrt`, `exp2` |
| `json` | `dumps`, `loads` (`separators=` only) |
| `re` | MicroPython's, plus (in Python) `findall`, `finditer`, `fullmatch`, `escape`, `subn` and compiled patterns with them |
| `struct`, `array` | without the codes that read raw memory (`P`, `O`, `S`); native `l`/`L` is 8 bytes everywhere |
| `collections` | `OrderedDict`; `namedtuple` with keywords, defaults, `_make`, `_replace`, `_asdict`; `deque` (unbounded or with `maxlen`), `defaultdict`, `Counter` (in Python) |
| `heapq` | `heappush`, `heappop`, `heapify`; plus `heapreplace`, `heappushpop`, `nlargest`, `nsmallest`, `merge` |
| `io` | `StringIO`, `BytesIO` only |
| `typing` | every common name, subscriptable (`List[int]`, `Optional[Fleet]`, `Callable[[int], str]`); `Generic[T]` and `Protocol` as base classes; `TypeVar`, `cast`, `NamedTuple`, `TypedDict`, `overload`, `TYPE_CHECKING` |
| `dataclasses` | `@dataclass` with `init`, `repr`, `eq`, `order`, `frozen`, `unsafe_hash`, `kw_only`; `field`, `fields`, `asdict`, `astuple`, `replace`, `is_dataclass`, `MISSING`, `KW_ONLY`, `FrozenInstanceError`; `__post_init__` |
| `itertools` | `count`, `cycle`, `repeat`, `accumulate`, `batched`, `chain`, `compress`, `dropwhile`, `filterfalse`, `groupby`, `islice`, `pairwise`, `starmap`, `takewhile`, `tee`, `zip_longest`, `product`, `permutations`, `combinations`, `combinations_with_replacement` |
| `functools` | `reduce`, `partial`, `wraps`, `update_wrapper`, `lru_cache`, `cache`, `cmp_to_key`, `total_ordering`, `cached_property`, `singledispatch` |
| `bisect` | `bisect_left`, `bisect_right`, `insort_left`, `insort_right`, with `lo`, `hi` and `key` |
| `operator`, `copy`, `abc`, `string` | the commonly used parts |
| `traceback` | `format_exception`, `format_exception_only`, `format_exc`, `print_exception`, `print_exc` (in Python): MicroPython's tracebacks, file and line of each call, without source lines |
| `__future__`, `micropython` | so that code written for CPython or MicroPython imports |

The modules marked "in Python" live in `python/lib`, built into the program. They
run as script code: they cost budget like the script's own.

Not available, by design: files and `open()`, `input()`, the file system (`os`,
`pathlib`), the clock (`time`), threads, sockets, processes, `random` (rules use the
engine's generator), `gc` and `micropython`'s memory functions, `weakref`, native
modules, and any other module. Scripts import only the files the engine gives the
interpreter (a mod's `ai/` or `scripts/`, as text), the modules above, and the
engine's own native modules. `eval`, `exec` and `__import__` reach nothing more.
`__code__` is not available, and nothing reads or writes memory directly.

## Limits

Each interpreter has fixed limits (`script::Limits`), and running past any of them
stops the script with an error the engine reports; the engine itself carries on.

| Limit | Default | When it runs out |
|---|---|---|
| Heap | 32 MiB per interpreter | `MemoryError` (error kind Memory). A script may catch it. |
| Budget | 200 million units over the interpreter's life; a call may get less (`CallOptions::budget`) | `BudgetExceeded` (kind Budget). It cannot be caught: every handler and `finally` block that tries to run raises it again. |
| Call depth | 200 nested Python calls (functions and generators) | `RecursionError` (kind Recursion), which scripts may catch |
| C stack | 256 KiB below the engine's call | `RecursionError` as well. A backstop for recursion inside C code (a deeply nested list's `repr`, a pathological regular expression) |
| Output | 64 KiB of `print()` output kept | Later output is dropped |

**The budget** counts one unit per bytecode executed, plus native work that runs
without bytecodes, in comparable units: each step of an iteration done in C
(`sum(range(n))`, `list(...)`, `max(...)`), each comparison while sorting, each
backtracking step of a regular expression, big-integer multiplication, division and
conversion to text in proportion to their size, long substring searches, and hash
collisions in dicts and sets. Compiling a module costs nothing, whether or not its
bytecode was cached; constant expressions in source (`3 ** 1000000`) are only
computed at compile time within a fixed allowance, otherwise when the code runs.
The count depends only on the script and its inputs, never on the computer, so a
script runs out at the same bytecode everywhere (the tests check this).

**Memory** is checked for real: the heap is fixed and running out is always caught,
but the exact point at which it runs out differs between 32-bit and 64-bit builds
(objects have different sizes). Set heaps well above what scripts need; do not use
running out of memory as part of a game's rules. The same holds for the C stack
limit: the call-depth limit is the same everywhere, the C stack limit is not.

## Errors

`Result<T>` carries a `script::Error`: its kind, the Python exception type, the
message (`str()` of the exception, or the runtime's own explanation) and the
traceback with file and line of each call.

| Kind | When |
|---|---|
| Syntax | A file doesn't compile (`SyntaxError`, `IndentationError`) |
| Exception | The script raised an exception, or an engine function raised one in it |
| Budget | The call or the interpreter used up its budget |
| Memory | The heap ran out |
| Recursion | Calls nested deeper than the limit, or the C stack limit |
| Conversion | A value could not cross between the engine and Python; the message names the place (`the result['ships'][3] is a float`) |
| NotFound | No such module or function |
| Usage | The engine used the runtime wrongly (a second interpreter, a bad file path) |

An exception's `__str__` runs with a small budget of its own: if it fails or never
ends, the message says so. After any error the interpreter can still be called (its
state is whatever the script left).

## Values

Everything crossing between the engine and scripts is a `script::Value`
(section 14.2).

| Engine | Python | Back |
|---|---|---|
| null | `None` | null |
| bool | `bool` | bool |
| int (64 bits) | `int` | int; beyond 64 bits is an error |
| string (UTF-8) | `str` | string |
| list | `list` | list; also from `tuple` (and namedtuples) |
| map | `dict` | map; also from `OrderedDict`, `defaultdict`, `Counter` |

Maps keep their order both ways (dicts are insertion-ordered). Map keys must be
strings. Floats, sets, bytes and other objects cannot cross: the error names the
place. Values nest at most 100 levels (which also catches a list that contains
itself). Text from the engine must be valid UTF-8.

Engine functions (`Interpreter::addNativeFunction`) take positional arguments as
Values and return a Value. To raise a Python exception, they throw
`script::NativeError("KeyError", "no fleet 8")`; any other C++ exception becomes a
`RuntimeError`.

`src/script/json.hpp` reads and writes the same Values as strict JSON for external
bots: UTF-8, whole numbers within 64 bits only (a fraction or exponent is an error
with its line and column), no repeated keys, at most 100 levels, keys in order.

## The same on every computer

Rules scripts and in-game AIs must resolve identically on every machine
(section 14.4). The runtime makes the following the same on Linux, Windows, macOS,
64-bit and 32-bit ARM:

- **Budget counts**, as above.
- **Hashes**: integers hash as their value modulo the prime 2³⁰−35, whatever their
  size or the word size (a whole-valued float hashes like the equal integer); strings
  by a 16-bit hash of their bytes; tuples and frozensets from their items; every hash
  is below 2³⁰. So set order is the same everywhere.
- **Identity**: objects without a value hash (class instances, functions, classes,
  `None`) hash and `id()` by a number given in the order the interpreter first needs
  one (instances: in the order they are created), never by their address. `repr()` of
  such objects shows that number (`<Ship object at 12>`).
- **Dict order** is insertion order; it never depends on hashing.
- **Floating point**: IEEE double arithmetic, with no fused multiply-add on any
  platform. The transcendental math functions (`sin`, `exp`, `log`, `pow`...) are the
  same code everywhere (musl's, bundled), not the platform's C library, whose results
  differ in the last bit between glibc, msvcrt and Apple's. Exact operations (`sqrt`,
  `floor`, `fmod`...) come from the platform: IEEE 754 fixes their results. Float
  formatting and parsing are MicroPython's own code.
- **Big integers** use 16-bit digits on every platform, and their cost in budget
  starts beyond 64 bits, where every build uses big integers alike.
- **Machine words**: sizes, indices and `range()` bounds are limited to 32 bits on
  64-bit builds too, so no build accepts what another refuses.
- **Float formatting and parsing** use double arithmetic only (MicroPython's "exact"
  variant would use `long double`, which is 64, 80 or 128 bits depending on the
  platform).
- `sys.maxsize`, `struct`'s native sizes and `sys.byteorder` are the same everywhere.

`tests/sdk/test_script_determinism.cpp` runs a workload that touches all of these and
compares a checksum of its output, and the budget it used, with a golden value; the
same value holds for the Linux x86_64 build, the Windows build (under Wine) and the
32-bit ARM build (under QEMU).

Not the same everywhere: where memory or the C stack runs out (see "Limits"),
`id()` of integers beyond 2³⁰, `is` between equal integers beyond that, and the
exception type (not the outcome) of some operations on absurd sizes, such as
indexing a list with 2⁴⁰ (`IndexError` on 64-bit builds, `OverflowError` on 32-bit
ones).

## For engine developers

```cpp
auto interp = script::Interpreter::create(limits);   // or a Usage error
interp->addFile("ai/__init__.py", text);              // a mod's files, as text
interp->addNativeFunction("engine", "fleet", fn);     // import engine; engine.fleet(7)
auto r = interp->call("ai", "economy", std::vector<Value>{view});   // Result<Value>
if (!r) log(r.error().describe(), r.error().traceback);
```

- Interpreters are not thread-safe objects; use one from one thread at a time.
  Each call needs `Limits::cStackBytes` of free stack on its thread.
- `bytecodeCacheStats()` and `clearBytecodeCache()` report and drop the cache.
- MicroPython is built from `third_party/micropython`, generated by
  `tools/update_micropython.sh` from the pinned release, our patches
  (`third_party/micropython/patches`) and `src/script/port/mpconfigport.h`
  (docs/BUILDING.md, "MicroPython"). The patch file's header lists what each change
  does.
