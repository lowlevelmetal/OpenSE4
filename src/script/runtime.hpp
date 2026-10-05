#pragma once

// The script runtime (docs/sdk/runtime.md): MicroPython inside the engine, as a
// sandbox. Scripts see only the modules the runtime compiles in and the files and
// native functions the engine gives them; no files, network, clock, threads or
// other programs. Every interpreter has a fixed memory heap, a budget counted in
// bytecodes (the same on every computer), a limit on call depth and on C stack.
//
// Interpreters live for one engine call (docs/MODDING_SDK.md, section 14.3):
//
//     auto interp = script::Interpreter::create(limits);   // at most one per process
//     interp->addFile("ai/__init__.py", text);              // a mod's files, as text
//     interp->addNativeFunction("engine", "rng", fn);       // the engine's side
//     auto result = interp->call("ai", "economy", std::vector<Value>{view});  // Result<Value>
//
// Compiled bytecode is cached per process, keyed by file and text, so creating an
// interpreter and importing a module again is cheap.
//
// Values crossing into Python: null -> None, bool, int, string -> str, list ->
// list, map -> dict. Coming back: None, bool, int (within 64 bits), str, list and
// tuple -> list, dict with str keys -> map. Dicts keep their insertion order in this
// runtime (as in CPython 3.7+), and a map keeps it too, both ways: the order is the
// same on every platform and never depends on hashing. Floats, bigger ints, other
// key types and other objects are conversion errors that name the place
// ("the result['ships'][3]").
//
// Threads: an interpreter may be used from any thread, one call at a time. Its C
// stack limit applies to the thread of each call, which must have that much stack
// left.

#include "script/value.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

namespace opense4::script {

struct Limits {
    // The GC heap: all memory the scripts' objects can use.
    size_t heapBytes = size_t{32} << 20;
    // Bytecodes (plus native work in the same units, docs/sdk/runtime.md) over the
    // interpreter's whole life. Running out stops the script at the same bytecode on
    // every computer.
    int64_t budget = 200'000'000;
    // Python calls nested inside one another (functions and generators).
    size_t maxDepth = 200;
    // C stack the interpreter may use below the engine's call into it; a backstop
    // behind maxDepth, for the C functions that recurse without Python calls.
    size_t cStackBytes = size_t{256} << 10;
    // print() output kept per interpreter; more is dropped.
    size_t outputBytes = size_t{64} << 10;
};

struct CallOptions {
    // At most this budget for this call; negative: whatever the interpreter has left.
    int64_t budget = -1;
};

enum class ErrorKind : uint8_t {
    Syntax,      // a file doesn't compile
    Exception,   // the script raised an exception (or an engine function raised one in it)
    Budget,      // the call or the interpreter used up its budget
    Memory,      // the heap ran out
    Recursion,   // calls nested deeper than maxDepth, or the C stack limit
    Conversion,  // a value could not cross between the engine and Python
    NotFound,    // no such module or function
    Usage,       // the engine used the runtime wrongly (a second interpreter, a bad path...)
};
std::string_view errorKindName(ErrorKind k);

struct Error {
    ErrorKind kind = ErrorKind::Exception;
    std::string type;       // the Python exception's type, e.g. "ValueError"; empty when none
    std::string message;    // str() of the exception, or the runtime's own explanation
    std::string traceback;  // "Traceback (most recent call last):", file and line of each call
    // One line for logs: "Exception: ValueError: bad target".
    std::string describe() const;
};

template <class T>
using Result = std::expected<T, Error>;

// Thrown by a native function to raise a Python exception in the script, e.g.
// NativeError("ValueError", "no such fleet"). Recognised types: ArithmeticError,
// AssertionError, AttributeError, Exception, IndexError, KeyError, LookupError,
// MemoryError, NameError, NotImplementedError, OverflowError, RuntimeError,
// TypeError, ValueError, ZeroDivisionError; any other name raises RuntimeError. Any other C++
// exception from a native function raises RuntimeError with its what().
class NativeError : public std::runtime_error {
public:
    NativeError(std::string type, const std::string& message) : std::runtime_error(message), type_(std::move(type)) {}
    const std::string& type() const { return type_; }

private:
    std::string type_;
};

// An engine function callable from scripts: positional arguments only.
using NativeFunction = std::function<Value(std::span<const Value> args)>;

class Interpreter {
public:
    // A new interpreter, or a Usage error while another one lives in this process
    // (MicroPython keeps its state in globals).
    static Result<std::unique_ptr<Interpreter>> create(const Limits& limits = {});
    ~Interpreter();
    Interpreter(const Interpreter&) = delete;
    Interpreter& operator=(const Interpreter&) = delete;

    // Whether an interpreter lives in this process now.
    static bool active();

    // A module file, as text: "ai/__init__.py", "ai/strategy.py", "helpers.py".
    // Paths are relative and '/'-separated; each part is a Python identifier. The
    // runtime's own module names (sys, math, json, typing, itertools...) are taken.
    // Add files before the modules are imported.
    Result<void> addFile(std::string_view path, std::string text);
    // An engine function `name` in the native module `module` ("engine"; one name,
    // no dots), importable by scripts.
    Result<void> addNativeFunction(std::string_view module, std::string_view name, NativeFunction fn);

    // Imports `module` ("ai.strategy"), running its top level the first time.
    Result<void> importModule(std::string_view module, const CallOptions& options = {});
    // Calls `function` in `module` ("plan", or "Admiral.economy" for an attribute of
    // an attribute) with positional arguments.
    Result<Value> call(std::string_view module, std::string_view function, std::span<const Value> args = {},
                       const CallOptions& options = {});
    // Runs statements in the interpreter's __main__ module (tests, tools).
    Result<void> exec(std::string_view source, const CallOptions& options = {});
    // Evaluates an expression in __main__ and converts its value.
    Result<Value> eval(std::string_view expression, const CallOptions& options = {});

    const Limits& limits() const;
    // Budget used and left over the interpreter's life.
    int64_t budgetUsed() const;
    int64_t budgetLeft() const;
    // Budget the last call used.
    int64_t lastCallBudget() const;
    // What scripts printed (up to Limits::outputBytes).
    const std::string& output() const;
    // Bytes in use on the heap now.
    size_t heapUsed();

    struct Impl;

private:
    explicit Interpreter(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

// The per-process cache of compiled bytecode (keyed by file path and text).
struct BytecodeCacheStats {
    size_t entries = 0;
    size_t bytes = 0;    // bytecode and the texts it was compiled from
    uint64_t hits = 0;
    uint64_t misses = 0;
};
BytecodeCacheStats bytecodeCacheStats();
// Drops every entry no interpreter is using.
void clearBytecodeCache();

// The runtime's own Python modules (python/lib: typing, dataclasses, itertools,
// functools, bisect...), built into the program.
struct LibraryFile {
    std::string_view path;   // "typing.py"
    std::string_view text;
};
std::span<const LibraryFile> libraryFiles();

} // namespace opense4::script
