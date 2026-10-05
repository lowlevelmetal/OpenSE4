// The script runtime's sandbox and limits (docs/sdk/runtime.md): what doesn't
// exist, the bytecode budget, memory, call depth and the C stack.

#include "script_test_util.hpp"

#include <format>

using namespace opense4::script;
using namespace opense4::script::test;

TEST_CASE("script sandbox: no files, clock, OS, threads, network or randomness") {
    auto interp = makeInterpreter();
    for (const char* module : {"os", "io.open", "time", "socket", "select", "random", "machine", "_thread", "gc",
                               "uctypes", "ffi", "marshal", "weakref", "errno", "platform", "asyncio", "subprocess",
                               "pathlib", "shutil", "ctypes", "signal", "threading", "urandom", "ssl", "hashlib",
                               "builtins.open", "uos", "utime", "vfs"}) {
        std::string code = std::string("import ") + module;
        auto r = interp->exec(code);
        REQUIRE_MESSAGE(!r.has_value(), code);
        CHECK_MESSAGE((r.error().type == "ImportError" || r.error().type == "AttributeError"), code << ": " << r.error().describe());
    }
    for (const char* name : {"open", "input", "execfile", "help", "compile_file"}) {
        auto r = interp->exec(std::string(name) + "('x')");
        REQUIRE_FALSE(r.has_value());
        CHECK_MESSAGE(r.error().type == "NameError", name);
    }
    // nothing reaches them indirectly either
    CHECK(execFails(*interp, "__import__('os')").type == "ImportError");
    CHECK(execFails(*interp, "exec('import os')").type == "ImportError");
    CHECK(execFails(*interp, "eval(\"__import__('time')\")").type == "ImportError");
    CHECK(execFails(*interp, "import io\nio.open('x')").type == "AttributeError");
    CHECK(execFails(*interp, "import builtins\nbuiltins.open('x')").type == "AttributeError");
    CHECK(execFails(*interp, "import sys\nsys.path").type == "AttributeError");
    CHECK(execFails(*interp, "import sys\nsys.argv").type == "AttributeError");
    CHECK(execFails(*interp, "import sys\nsys.stdout").type == "AttributeError");
    CHECK(execFails(*interp, "import sys\nsys.getsizeof(1)").type == "AttributeError");
    CHECK(execFails(*interp, "(lambda: 0).__code__").type == "AttributeError");
    // no raw pointers in struct or array
    CHECK(execFails(*interp, "import struct\nstruct.pack('P', 0)").type == "ValueError");
    CHECK(execFails(*interp, "import struct\nstruct.unpack('O', b'12345678')").type == "ValueError");
    CHECK(execFails(*interp, "import array\narray.array('O')").type == "ValueError");
    // what does exist is harmless and the same everywhere
    execOk(*interp, "import sys, io, json, re, struct, array, math, collections, heapq");
    CHECK(evalOk(*interp, "sys.platform") == Value("opense4"));
    CHECK(evalOk(*interp, "sys.maxsize") == Value(int64_t{9223372036854775807}));
    CHECK(evalOk(*interp, "sys.byteorder") == Value("little"));
    CHECK(evalOk(*interp, "sys.implementation.name") == Value("micropython"));
    CHECK(evalOk(*interp, "struct.calcsize('l')") == Value(8));
    CHECK(evalOk(*interp, "io.StringIO('abc').read()") == Value("abc"));
}

TEST_CASE("script sandbox: the budget stops an endless loop at the same bytecode every time") {
    int64_t reached[2] = {0, 0};
    for (int run = 0; run < 2; ++run) {
        auto interp = makeInterpreter();
        execOk(*interp, "n = 0");
        CallOptions call;
        call.budget = 1'000'000;
        Error e = execFails(*interp, "while True:\n    n += 1\n", call);
        CHECK(e.kind == ErrorKind::Budget);
        CHECK(e.type == "BudgetExceeded");
        CHECK(e.message == "the call used up its budget of 1000000 bytecodes");
        CHECK(e.traceback.find("line 2") != std::string::npos);
        CHECK(interp->lastCallBudget() == 1'000'000);
        // the interpreter goes on with what it has left
        reached[run] = evalOk(*interp, "n").asInt();
    }
    CHECK(reached[0] == reached[1]);
    CHECK(reached[0] == 200000);   // five bytecodes per iteration
}

TEST_CASE("script sandbox: each exhausted budget has its own traceback") {
    auto interp = makeInterpreter();
    REQUIRE(interp->addFile("loops.py", "def first():\n    while True:\n        pass\n\n\ndef second():\n    while True:\n        pass\n").has_value());
    CallOptions call;
    call.budget = 10'000;
    auto a = interp->call("loops", "first", {}, call);
    auto b = interp->call("loops", "second", {}, call);
    REQUIRE_FALSE(a.has_value());
    REQUIRE_FALSE(b.has_value());
    CHECK(a.error().traceback.find("in first") != std::string::npos);
    CHECK(b.error().traceback.find("in second") != std::string::npos);
    CHECK(b.error().traceback.find("in first") == std::string::npos);
}

TEST_CASE("script sandbox: the interpreter's budget runs out for good") {
    Limits limits;
    limits.budget = 50'000;
    auto interp = makeInterpreter(limits);
    Error e = execFails(*interp, "while True:\n    pass\n");
    CHECK(e.kind == ErrorKind::Budget);
    CHECK(e.message == "the interpreter used up its budget of 50000 bytecodes");
    CHECK(interp->budgetLeft() == 0);
    auto after = interp->eval("1");
    REQUIRE_FALSE(after.has_value());
    CHECK(after.error().kind == ErrorKind::Budget);
}

TEST_CASE("script sandbox: running out of budget can't be caught") {
    auto interp = makeInterpreter();
    CallOptions call;
    call.budget = 200'000;
    Error e = execFails(*interp,
                        "count = 0\n"
                        "while True:\n"
                        "    try:\n"
                        "        while True:\n"
                        "            pass\n"
                        "    except BaseException:\n"
                        "        count += 1\n"
                        "    finally:\n"
                        "        count += 1\n",
                        call);
    CHECK(e.kind == ErrorKind::Budget);
    CHECK(evalOk(*interp, "count") == Value(0));
}

TEST_CASE("script sandbox: native work counts against the budget") {
    auto interp = makeInterpreter();
    CallOptions call;
    call.budget = 2'000'000;
    // loops in C over iterators, with no bytecode of their own
    CHECK(execFails(*interp, "sum(range(10**9))", call).kind == ErrorKind::Budget);
    CHECK(execFails(*interp, "max(range(2**31 - 1))", call).kind == ErrorKind::Budget);
    CHECK(execFails(*interp, "any(x > 10**15 for x in range(2**31 - 1))", call).kind == ErrorKind::Budget);
    // huge integer arithmetic
    CHECK(execFails(*interp, "x = 7 ** (10**7)", call).kind == ErrorKind::Budget);
    CHECK(execFails(*interp, "x = str(10 ** 200000)", call).kind == ErrorKind::Budget);
    // regular expression backtracking (exponential here)
    CHECK(execFails(*interp, "import re\nre.match('(a|aa)*c', 'a' * 60)", call).kind == ErrorKind::Budget);
    // substring search
    CHECK(execFails(*interp, "h = 'a' * 400000\nn = 'a' * 200000 + 'b'\nh.find(n)", call).kind == ErrorKind::Budget);
    // sorting
    CHECK(execFails(*interp, "sorted(range(10**6, 0, -1))", call).kind == ErrorKind::Budget);
    // and the interpreter still works
    CHECK(evalOk(*interp, "sum(range(10))") == Value(45));
    // a constant expression in source is folded only within a fixed allowance
    REQUIRE(interp->addFile("constant.py", "X = 3 ** 1000000\n").has_value());
    CallOptions small;
    small.budget = 100'000;
    auto imported = interp->importModule("constant", small);
    REQUIRE_FALSE(imported.has_value());
    CHECK(imported.error().kind == ErrorKind::Budget);
}

TEST_CASE("script sandbox: running out of memory is an error the engine survives") {
    Limits limits;
    limits.heapBytes = size_t{4} << 20;
    auto interp = makeInterpreter(limits);
    Error e = execFails(*interp, "x = []\nwhile True:\n    x.append([0] * 1000)\n");
    CHECK(e.kind == ErrorKind::Memory);
    CHECK(e.type == "MemoryError");
    CHECK(e.traceback.find("line 3") != std::string::npos);
    CHECK(execFails(*interp, "y = 'z' * (64 << 20)").kind == ErrorKind::Memory);
    CHECK(execFails(*interp, "y = bytearray(1 << 30)").kind == ErrorKind::Memory);
    // the garbage is collected and the interpreter goes on
    execOk(*interp, "x = None");
    CHECK(evalOk(*interp, "len([0] * 100000)") == Value(100000));
    // a script may catch MemoryError itself
    execOk(*interp, "try:\n    big = [0] * (1 << 28)\nexcept MemoryError:\n    big = 'too big'\n");
    CHECK(evalOk(*interp, "big") == Value("too big"));
}

TEST_CASE("script sandbox: call depth is limited the same way everywhere") {
    Limits limits;
    limits.maxDepth = 150;
    limits.cStackBytes = size_t{4} << 20;   // ample, so the depth limit decides
    auto interp = makeInterpreter(limits);
    execOk(*interp,
           "depth = 0\n"
           "def down(n):\n"
           "    global depth\n"
           "    depth = n\n"
           "    down(n + 1)\n");
    Error e = execFails(*interp, "down(1)");
    CHECK(e.kind == ErrorKind::Recursion);
    CHECK(e.type == "RecursionError");
    // the module level is one call, down(1) the second: 149 calls of down fit
    CHECK(evalOk(*interp, "depth") == Value(149));
    // scripts can catch it, as RecursionError or RuntimeError
    execOk(*interp, "try:\n    down(1)\nexcept RecursionError:\n    caught = depth\n");
    CHECK(evalOk(*interp, "caught") == Value(149));
    execOk(*interp, "try:\n    down(1)\nexcept RuntimeError:\n    caught2 = True\n");
    // generators count too
    execOk(*interp,
           "def nest(n):\n"
           "    yield n\n"
           "    yield from nest(n + 1)\n");
    CHECK(execFails(*interp, "list(nest(0))").kind == ErrorKind::Recursion);
    // after unwinding, the full depth is available again
    CHECK(evalOk(*interp, "(lambda: 5)()") == Value(5));
}

TEST_CASE("script sandbox: the C stack limit stops recursion that has no Python calls") {
    Limits limits;
    limits.cStackBytes = size_t{64} << 10;
    limits.maxDepth = 100000;
    auto interp = makeInterpreter(limits);
    execOk(*interp, "deep = []\nfor i in range(100000):\n    deep = [deep]\n");
    Error e = execFails(*interp, "s = repr(deep)");
    CHECK(e.kind == ErrorKind::Recursion);
    Error e2 = execFails(*interp, "def f(n):\n    return f(n + 1)\nf(0)");
    CHECK(e2.kind == ErrorKind::Recursion);
    CHECK(evalOk(*interp, "len(deep)") == Value(1));
}

TEST_CASE("script sandbox: machine-word ints are 32-bit on every build") {
    auto interp = makeInterpreter();
    // what a 32-bit build can't take, no build takes
    CHECK(execFails(*interp, "range(2**31)").type == "OverflowError");
    CHECK(execFails(*interp, "[1, 2][:2**40]").type == "OverflowError");
    CHECK(execFails(*interp, "'abc'.find('c', 0, 2**32)").type == "OverflowError");
    CHECK(execFails(*interp, "len(range(-2**31, 2**31 - 1))").type == "OverflowError");
    CHECK(evalOk(*interp, "list(range(2**31 - 10, 2**31 - 1, 7))") == Value(ValueList{Value(2147483638), Value(2147483645)}));
    CHECK(evalOk(*interp, "list(range(-2**31 + 5, -2**31 + 1, -3))") == Value(ValueList{Value(-2147483643), Value(-2147483646)}));
    CHECK(execFails(*interp, "range(-2**31)").type == "OverflowError");   // as a 32-bit build converts it
    CHECK(evalOk(*interp, "len(range(0, 2**31 - 1, 2**30))") == Value(2));
    // numbers themselves are unlimited, and pack whole
    execOk(*interp, "import struct, array");
    CHECK(evalOk(*interp, "struct.unpack('<q', struct.pack('<q', -2**40))[0]") == Value(-(int64_t{1} << 40)));
    CHECK(evalOk(*interp, "[struct.unpack('<q', struct.pack('<q', v))[0] == v for v in (2**62, 2**63 - 1, -2**63)]") ==
          Value(ValueList{true, true, true}));
    CHECK(evalOk(*interp, "list(array.array('q', [2**40, -3]))") == Value(ValueList{Value(int64_t{1} << 40), Value(-3)}));
    CHECK(evalOk(*interp, "(2**40).to_bytes(6, 'little') == bytes([0, 0, 0, 0, 0, 1])") == Value(true));
}

TEST_CASE("script sandbox: hashes and identities never show addresses") {
    auto interp = makeInterpreter();
    execOk(*interp,
           "class Ship:\n    pass\n"
           "a, b, c = Ship(), Ship(), Ship()\n"
           "ids = [id(a), id(b), id(c)]\n"
           "hashes = [hash(a), hash(b), hash(c)]\n");
    Value ids = evalOk(*interp, "ids");
    Value hashes = evalOk(*interp, "hashes");
    // numbered in the order the objects were made
    CHECK(ids.asList()[1].asInt() - ids.asList()[0].asInt() == 4);
    CHECK(ids.asList()[2].asInt() - ids.asList()[1].asInt() == 4);
    CHECK(hashes.asList()[1].asInt() - hashes.asList()[0].asInt() == 1);
    // and the same numbers in a new interpreter that does the same things
    interp.reset();
    auto again = makeInterpreter();
    execOk(*again,
           "class Ship:\n    pass\n"
           "a, b, c = Ship(), Ship(), Ship()\n"
           "ids = [id(a), id(b), id(c)]\n"
           "hashes = [hash(a), hash(b), hash(c)]\n");
    CHECK(evalOk(*again, "ids") == ids);
    CHECK(evalOk(*again, "hashes") == hashes);
    CHECK(evalOk(*again, "repr(a)") == Value(std::format("<Ship object at {}>", hashes.asList()[0].asInt())));
    // value hashes: the same on 32-bit and 64-bit builds
    CHECK(evalOk(*again, "hash(5)") == Value(5));
    CHECK(evalOk(*again, "hash(-1)") == Value(1073741788));
    CHECK(evalOk(*again, "hash(2**40) == hash(2.0**40) == hash(2**40 + 0.0)") == Value(true));
    CHECK(evalOk(*again, "hash(2**100) == hash(2.0**100)") == Value(true));
    CHECK(evalOk(*again, "hash(2**40)") == Value(int64_t{35840}));
    CHECK(evalOk(*again, "hash((1, 2)) != hash((2, 1))") == Value(true));
    CHECK(evalOk(*again, "hash(True) == hash(1) and hash(1.0) == 1") == Value(true));
    CHECK(evalOk(*again, "0 <= hash('text') < 65536") == Value(true));
    CHECK(evalOk(*again, "{2**40: 'x'}[2.0**40]") == Value("x"));
}

TEST_CASE("script sandbox: sort is stable, calls its key once per item, and survives misuse") {
    auto interp = makeInterpreter();
    execOk(*interp,
           "calls = 0\n"
           "def key(item):\n"
           "    global calls\n"
           "    calls += 1\n"
           "    return item[0]\n"
           "data = [(i % 3, i) for i in range(30)]\n"
           "s = sorted(data, key=key)\n"
           "r = sorted(data, key=key, reverse=True)\n");
    CHECK(evalOk(*interp, "calls") == Value(60));
    CHECK(evalOk(*interp, "s[:4]") == Value(ValueList{ValueList{0, 0}, ValueList{0, 3}, ValueList{0, 6}, ValueList{0, 9}}));
    CHECK(evalOk(*interp, "r[:3]") == Value(ValueList{ValueList{2, 2}, ValueList{2, 5}, ValueList{2, 8}}));
    CHECK(evalOk(*interp, "sorted(range(1000)) == list(range(1000))") == Value(true));
    CHECK(evalOk(*interp, "sorted(range(1000), reverse=True)[:3]") == Value(ValueList{999, 998, 997}));
    // an exception while comparing leaves every item in the list
    execOk(*interp, "mixed = [3, 'a', 1, 2]\ntry:\n    mixed.sort()\nexcept TypeError:\n    pass\n");
    CHECK(evalOk(*interp, "sorted(mixed, key=str)") == Value(ValueList{1, 2, 3, Value("a")}));
    // changing the list from a key is an error, and the list survives
    execOk(*interp,
           "victim = [3, 1, 2]\n"
           "try:\n"
           "    victim.sort(key=lambda x: victim.append(0) or x)\n"
           "    changed = False\n"
           "except ValueError:\n"
           "    changed = True\n");
    CHECK(evalOk(*interp, "changed") == Value(true));
    CHECK(evalOk(*interp, "sorted(victim)") == Value(ValueList{1, 2, 3}));
}

TEST_CASE("script sandbox: dicts keep insertion order, as in CPython") {
    auto interp = makeInterpreter();
    execOk(*interp,
           "d = {}\n"
           "for k in ['zeta', 'alpha', 7, (1, 2), None, 'mid']:\n"
           "    d[k] = str(k)\n"
           "del d[7]\n"
           "d[7] = 'back'\n"
           "keys = [repr(k) for k in d]\n"
           "class Ship:\n"
           "    def __init__(self):\n"
           "        self.z = 1\n"
           "        self.a = 2\n"
           "attrs = list(Ship().__dict__)\n"
           "big = {i * 7919 % 1000: i for i in range(1000)}\n"
           "for i in range(0, 1000, 2):\n"
           "    del big[i * 7919 % 1000]\n"
           "order_ok = list(big.values()) == list(range(1, 1000, 2))\n"
           "last = big.popitem()\n");
    CHECK(evalOk(*interp, "keys") ==
          Value(ValueList{Value("'zeta'"), Value("'alpha'"), Value("(1, 2)"), Value("None"), Value("'mid'"), Value("7")}));
    CHECK(evalOk(*interp, "attrs") == Value(ValueList{Value("z"), Value("a")}));
    CHECK(evalOk(*interp, "order_ok") == Value(true));
    CHECK(evalOk(*interp, "last") == Value(ValueList{Value(999 * 7919 % 1000), Value(999)}));
    CHECK(evalOk(*interp, "dict({'b': 1}, a=2, b=3)") == evalOk(*interp, "dict(b=3, a=2)"));
}
