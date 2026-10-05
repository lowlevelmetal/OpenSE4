// The script runtime (docs/sdk/runtime.md): interpreters, module files and
// packages, calls, native functions, errors with tracebacks, output.

#include "script_test_util.hpp"

#include <thread>
#include <vector>

using namespace opense4::script;
using namespace opense4::script::test;

TEST_CASE("script runtime: evaluate, execute, print") {
    auto interp = makeInterpreter();
    CHECK(evalOk(*interp, "1 + 2") == Value(3));
    CHECK(evalOk(*interp, "'a' * 3") == Value("aaa"));
    execOk(*interp, "x = [i * i for i in range(5)]\nprint('squares', x)");
    CHECK(evalOk(*interp, "x") == Value(ValueList{0, 1, 4, 9, 16}));
    CHECK(interp->output() == "squares [0, 1, 4, 9, 16]\n");
    CHECK(interp->budgetUsed() > 0);
    CHECK(interp->budgetLeft() == interp->limits().budget - interp->budgetUsed());
}

TEST_CASE("script runtime: one interpreter at a time per process") {
    CHECK_FALSE(Interpreter::active());
    {
        auto first = makeInterpreter();
        CHECK(Interpreter::active());
        auto second = Interpreter::create();
        REQUIRE_FALSE(second.has_value());
        CHECK(second.error().kind == ErrorKind::Usage);
        CHECK(second.error().message.find("one at a time") != std::string::npos);
    }
    CHECK_FALSE(Interpreter::active());
    // A new one after the first is gone: nothing of the old one remains.
    auto again = makeInterpreter();
    execOk(*again, "y = 1");
    auto missing = again->eval("x");
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().type == "NameError");
}

TEST_CASE("script runtime: limits are checked") {
    Limits tiny;
    tiny.heapBytes = 1024;
    auto r = Interpreter::create(tiny);
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().kind == ErrorKind::Usage);
    CHECK_FALSE(Interpreter::active());
}

TEST_CASE("script runtime: used from another thread, one call at a time") {
    auto interp = makeInterpreter();
    execOk(*interp, "def twice(n):\n    return 2 * n\n");
    Value fromThread;
    std::thread worker([&] {
        auto r = interp->eval("twice(21)");
        if (r) fromThread = *r;
    });
    worker.join();
    CHECK(fromThread == Value(42));
    CHECK(evalOk(*interp, "twice(5)") == Value(10));
}

TEST_CASE("script runtime: module files, packages and relative imports") {
    auto interp = makeInterpreter();
    REQUIRE(interp->addFile("ai/__init__.py", "from .strategy import plan\nNAME = 'admiral'\n").has_value());
    REQUIRE(interp->addFile("ai/strategy.py",
                            "from . import util\nfrom .util import double\n"
                            "def plan(n):\n    return [double(n), util.triple(n)]\n")
                .has_value());
    REQUIRE(interp->addFile("ai/util.py", "def double(n):\n    return 2 * n\ndef triple(n):\n    return 3 * n\n").has_value());
    REQUIRE(interp->addFile("ai/deep/__init__.py", "").has_value());
    REQUIRE(interp->addFile("ai/deep/inner.py", "from ..util import triple\nVALUE = triple(7)\n")
                .has_value());
    REQUIRE(interp->addFile("helpers.py", "import ai.deep.inner\nVALUE = ai.deep.inner.VALUE\n").has_value());

    auto r = interp->call("ai", "plan", std::vector<Value>{Value(5)});
    REQUIRE_MESSAGE(r.has_value(), explain(r.error()));
    CHECK(*r == Value(ValueList{10, 15}));
    CHECK(evalOk(*interp, "__import__('ai').NAME") == Value("admiral"));
    auto v = interp->call("ai.strategy", "plan", std::vector<Value>{Value(1)});
    REQUIRE(v.has_value());
    CHECK(*v == Value(ValueList{2, 3}));
    REQUIRE(interp->importModule("helpers").has_value());
    CHECK(evalOk(*interp, "__import__('helpers').VALUE") == Value(21));
    // A namespace package: a folder without __init__.py.
    REQUIRE(interp->addFile("mods/extra.py", "X = 5\n").has_value());
    CHECK(evalOk(*interp, "__import__('mods.extra').extra.X") == Value(5));
    // __file__ is the file's path.
    CHECK(evalOk(*interp, "__import__('ai.util').util.__file__") == Value("ai/util.py"));
}

TEST_CASE("script runtime: file paths are checked") {
    auto interp = makeInterpreter();
    CHECK(interp->addFile("ai/strategy.txt", "").error().kind == ErrorKind::Usage);
    CHECK(interp->addFile("/abs.py", "").error().kind == ErrorKind::Usage);
    CHECK(interp->addFile("ai/../x.py", "").error().kind == ErrorKind::Usage);
    CHECK(interp->addFile("ai\\x.py", "").error().kind == ErrorKind::Usage);
    CHECK(interp->addFile("2fast.py", "").error().kind == ErrorKind::Usage);
    CHECK(interp->addFile("json.py", "").error().kind == ErrorKind::Usage);       // the runtime's
    CHECK(interp->addFile("typing.py", "").error().kind == ErrorKind::Usage);     // the runtime's library
    CHECK(interp->addFile("itertools/x.py", "").error().kind == ErrorKind::Usage);
    CHECK(interp->addFile("bad.py", std::string("x = '\xff'\n")).error().kind == ErrorKind::Usage);
    CHECK(interp->addFile("good_name.py", "").has_value());
}

TEST_CASE("script runtime: calls, attributes and missing names") {
    auto interp = makeInterpreter();
    REQUIRE(interp->addFile("game.py",
                            "class Admiral:\n"
                            "    @staticmethod\n"
                            "    def rank():\n        return 'admiral'\n"
                            "def none():\n    pass\n"
                            "counter = 0\n"
                            "def bump():\n    global counter\n    counter += 1\n    return counter\n")
                .has_value());
    auto r = interp->call("game", "Admiral.rank");
    REQUIRE(r.has_value());
    CHECK(*r == Value("admiral"));
    CHECK(*interp->call("game", "none") == Value());
    // module globals live as long as the interpreter
    CHECK(*interp->call("game", "bump") == Value(1));
    CHECK(*interp->call("game", "bump") == Value(2));

    auto noModule = interp->call("nothere", "f");
    REQUIRE_FALSE(noModule.has_value());
    CHECK(noModule.error().kind == ErrorKind::NotFound);
    auto noFunction = interp->call("game", "missing");
    REQUIRE_FALSE(noFunction.has_value());
    CHECK(noFunction.error().kind == ErrorKind::NotFound);
    CHECK(noFunction.error().message.find("missing") != std::string::npos);
    CHECK(interp->call("game", "bad name").error().kind == ErrorKind::Usage);
    CHECK(interp->call("game..x", "f").error().kind == ErrorKind::Usage);
    CHECK(interp->importModule("nothere").error().kind == ErrorKind::NotFound);
}

TEST_CASE("script runtime: exceptions carry type, message and traceback with files and lines") {
    auto interp = makeInterpreter();
    REQUIRE(interp->addFile("ai/orders.py",
                            "def pick(fleets):\n"
                            "    return fleets[0]\n"
                            "\n"
                            "def plan():\n"
                            "    return pick([])\n")
                .has_value());
    auto r = interp->call("ai.orders", "plan");
    REQUIRE_FALSE(r.has_value());
    const Error& e = r.error();
    CHECK(e.kind == ErrorKind::Exception);
    CHECK(e.type == "IndexError");
    CHECK(e.message.find("index out of range") != std::string::npos);
    CHECK(e.traceback.find("Traceback (most recent call last):") != std::string::npos);
    CHECK(e.traceback.find("File \"ai/orders.py\", line 5, in plan") != std::string::npos);
    CHECK(e.traceback.find("File \"ai/orders.py\", line 2, in pick") != std::string::npos);
    CHECK(e.describe() == "Exception: IndexError: " + e.message);

    // An exception whose __str__ fails or never ends still gets described.
    execOk(*interp,
           "class Odd(Exception):\n"
           "    def __str__(self):\n"
           "        while True:\n            pass\n");
    auto odd = interp->exec("raise Odd()");
    REQUIRE_FALSE(odd.has_value());
    CHECK(odd.error().type == "Odd");
    CHECK(odd.error().message == "(the message could not be written)");

    // The interpreter works on after an exception.
    CHECK(evalOk(*interp, "6 * 7") == Value(42));
    // SystemExit is an exception like any other.
    auto exitCall = interp->exec("import sys\nsys.exit(3)");
    REQUIRE_FALSE(exitCall.has_value());
    CHECK(exitCall.error().type == "SystemExit");
}

TEST_CASE("script runtime: syntax errors name the file and line") {
    auto interp = makeInterpreter();
    REQUIRE(interp->addFile("broken.py", "def f():\n    return 1\n\ndef g(:\n    pass\n").has_value());
    auto r = interp->importModule("broken");
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().kind == ErrorKind::Syntax);
    CHECK(r.error().type == "SyntaxError");
    CHECK(r.error().traceback.find("File \"broken.py\", line 4") != std::string::npos);
    // ... the same the second time (nothing broken is cached)
    auto again = interp->call("broken", "f");
    REQUIRE_FALSE(again.has_value());
    CHECK(again.error().kind == ErrorKind::Syntax);

    auto indent = interp->exec("if True:\n    x = 1\n  y = 2\n");
    REQUIRE_FALSE(indent.has_value());
    CHECK(indent.error().kind == ErrorKind::Syntax);
    CHECK(indent.error().type == "IndentationError");
}

TEST_CASE("script runtime: native functions") {
    auto interp = makeInterpreter();
    std::vector<Value> seen;
    REQUIRE(interp->addNativeFunction("engine", "add",
                                      [&](std::span<const Value> args) -> Value {
                                          seen.assign(args.begin(), args.end());
                                          int64_t sum = 0;
                                          for (const Value& v : args) sum += v.asInt();
                                          return Value(sum);
                                      })
                .has_value());
    REQUIRE(interp->addNativeFunction("engine", "fleet",
                                      [](std::span<const Value> args) -> Value {
                                          if (args.size() != 1 || !args[0].isInt())
                                              throw NativeError("TypeError", "fleet(id) takes one number");
                                          if (args[0].asInt() != 7) throw NativeError("KeyError", "no fleet " + std::to_string(args[0].asInt()));
                                          Value f = Value::emptyMap();
                                          f.set("id", 7);
                                          f.set("ships", ValueList{Value("Dauntless"), Value("Valiant")});
                                          return f;
                                      })
                .has_value());
    REQUIRE(interp->addNativeFunction("engine", "broken",
                                      [](std::span<const Value>) -> Value { throw std::runtime_error("disk on fire"); })
                .has_value());
    REQUIRE(interp->addNativeFunction("engine", "bad_result",
                                      [](std::span<const Value>) -> Value { return Value(std::string("\xff\xfe")); })
                .has_value());

    CHECK(evalOk(*interp, "__import__('engine').add(1, 2, 3)") == Value(6));
    CHECK(seen == std::vector<Value>{Value(1), Value(2), Value(3)});
    execOk(*interp, "import engine\nf = engine.fleet(7)\nnames = ', '.join(f['ships'])");
    CHECK(evalOk(*interp, "names") == Value("Dauntless, Valiant"));
    CHECK(evalOk(*interp, "f['id']") == Value(7));

    auto missing = interp->exec("engine.fleet(8)");
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().type == "KeyError");
    CHECK(missing.error().message.find("no fleet 8") != std::string::npos);
    // ... which the script can catch
    execOk(*interp, "try:\n    engine.fleet(9)\n    caught = False\nexcept KeyError:\n    caught = True\n");
    CHECK(evalOk(*interp, "caught") == Value(true));

    auto typeError = interp->exec("engine.fleet('x')");
    CHECK(typeError.error().type == "TypeError");
    auto cpp = interp->exec("engine.broken()");
    CHECK(cpp.error().type == "RuntimeError");
    CHECK(cpp.error().message.find("disk on fire") != std::string::npos);
    auto badResult = interp->exec("engine.bad_result()");
    CHECK(badResult.error().type == "RuntimeError");
    CHECK(badResult.error().message.find("UTF-8") != std::string::npos);
    auto kwargs = interp->exec("engine.add(a=1)");
    CHECK(kwargs.error().type == "TypeError");
    auto floatArg = interp->exec("engine.add(1.5)");
    CHECK(floatArg.error().type == "TypeError");
    CHECK(floatArg.error().message.find("argument 1 of engine.add") != std::string::npos);
    CHECK(evalOk(*interp, "repr(engine.add)") == Value("<engine function add>"));

    // Names are checked.
    CHECK(interp->addNativeFunction("engine", "add", nullptr).error().kind == ErrorKind::Usage);
    CHECK(interp->addNativeFunction("math", "x", nullptr).error().kind == ErrorKind::Usage);
    CHECK(interp->addNativeFunction("a.b", "x", nullptr).error().kind == ErrorKind::Usage);
    REQUIRE(interp->addFile("mine.py", "").has_value());
    CHECK(interp->addNativeFunction("mine", "x", nullptr).error().kind == ErrorKind::Usage);
    CHECK(interp->addFile("engine.py", "").error().kind == ErrorKind::Usage);
}

TEST_CASE("script runtime: a native function cannot re-enter its interpreter") {
    auto interp = makeInterpreter();
    Error inner;
    REQUIRE(interp->addNativeFunction("engine", "reenter",
                                      [&](std::span<const Value>) -> Value {
                                          auto r = interp->eval("1");
                                          if (!r) inner = r.error();
                                          return Value();
                                      })
                .has_value());
    execOk(*interp, "import engine\nengine.reenter()");
    CHECK(inner.kind == ErrorKind::Usage);
}

TEST_CASE("script runtime: print output is kept up to its limit") {
    Limits limits;
    limits.outputBytes = 100;
    auto interp = makeInterpreter(limits);
    execOk(*interp, "for i in range(100):\n    print('line', i)");
    CHECK(interp->output().size() < 200);
    CHECK(interp->output().starts_with("line 0\nline 1\n"));
    CHECK(interp->output().ends_with("[further output dropped]\n"));
}
