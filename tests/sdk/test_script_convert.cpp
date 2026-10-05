// Values crossing between the engine and Python (src/script/runtime.hpp): both
// directions, the order of maps, and every conversion error.

#include "script_test_util.hpp"

#include <limits>

using namespace opense4::script;
using namespace opense4::script::test;

namespace {

// Calls identity(value) in Python and returns what comes back.
Result<Value> roundTrip(Interpreter& interp, const Value& v) {
    return interp.call("conv", "identity", std::vector<Value>{v});
}

std::unique_ptr<Interpreter> converter() {
    auto interp = makeInterpreter();
    REQUIRE(interp->addFile("conv.py",
                            "def identity(v):\n    return v\n"
                            "def describe(v):\n    return [type(v).__name__, repr(v)]\n")
                .has_value());
    return interp;
}

} // namespace

TEST_CASE("script convert: every kind of value crosses both ways") {
    auto interp = converter();
    Value map = Value::emptyMap();
    map.set("zeta", 1);
    map.set("alpha", ValueList{Value(), Value(true), Value(false)});
    map.set("text", "Ünïcödé ✓ 🚀");
    map.set("nested", Value::emptyMap());
    map.set("min", std::numeric_limits<int64_t>::min());
    map.set("max", std::numeric_limits<int64_t>::max());
    map.set("big", int64_t{1} << 40);
    map.set("empty", Value::emptyList());
    map.set("", "empty key");
    auto r = roundTrip(*interp, map);
    REQUIRE_MESSAGE(r.has_value(), explain(r.error()));
    CHECK(*r == map);   // maps keep their order there and back (dicts are insertion-ordered)

    auto d = interp->call("conv", "describe", std::vector<Value>{Value(int64_t{1} << 40)});
    REQUIRE(d.has_value());
    CHECK(*d == Value(ValueList{Value("int"), Value("1099511627776")}));
    d = interp->call("conv", "describe", std::vector<Value>{Value(nullptr)});
    CHECK(*d == Value(ValueList{Value("NoneType"), Value("None")}));
    d = interp->call("conv", "describe", std::vector<Value>{Value(ValueList{1, 2})});
    CHECK(*d == Value(ValueList{Value("list"), Value("[1, 2]")}));
    d = interp->call("conv", "describe", std::vector<Value>{map});
    CHECK(d->asList()[0] == Value("dict"));
}

TEST_CASE("script convert: Python values come back as values") {
    auto interp = converter();
    CHECK(evalOk(*interp, "None") == Value());
    CHECK(evalOk(*interp, "True") == Value(true));
    CHECK(evalOk(*interp, "-5") == Value(-5));
    CHECK(evalOk(*interp, "2**63 - 1") == Value(std::numeric_limits<int64_t>::max()));
    CHECK(evalOk(*interp, "-2**63") == Value(std::numeric_limits<int64_t>::min()));
    CHECK(evalOk(*interp, "2**64 // 2**60") == Value(16));    // big intermediate, small result
    CHECK(evalOk(*interp, "(1, (2, 3))") == Value(ValueList{1, ValueList{2, 3}}));
    CHECK(evalOk(*interp, "'é' + '✓'") == Value("é✓"));
    // dicts: insertion order; OrderedDict likewise
    Value m = evalOk(*interp, "{'b': 1, 'a': 2, 'c': {'y': 0, 'x': 9}}");
    REQUIRE(m.isMap());
    CHECK(m.asMap()[0].first == "b");
    CHECK(m.asMap()[1].first == "a");
    CHECK(m.find("c")->asMap()[0].first == "y");
    execOk(*interp, "from collections import OrderedDict, namedtuple, defaultdict, Counter");
    Value od = evalOk(*interp, "OrderedDict([('z', 1), ('a', 2)])");
    CHECK(od.asMap()[0].first == "z");
    // a namedtuple is a tuple: a list
    CHECK(evalOk(*interp, "namedtuple('P', 'x y')(3, 4)") == Value(ValueList{3, 4}));
    // dict subclasses are their dicts
    Value counted = evalOk(*interp, "Counter('abca')");
    CHECK(counted.find("a")->asInt() == 2);
    CHECK(counted.asMap()[0].first == "a");
    CHECK(evalOk(*interp, "defaultdict(list, {'k': [1]})") == evalOk(*interp, "{'k': [1]}"));
    // a deleted and re-added key goes to the end, as in CPython
    Value moved = evalOk(*interp, "(lambda d: (d.pop('a'), d.__setitem__('a', 3), d)[2])({'a': 1, 'b': 2})");
    CHECK(moved.asMap()[0].first == "b");
    CHECK(moved.asMap()[1].first == "a");
}

TEST_CASE("script convert: what can't cross is an error that says where") {
    auto interp = converter();
    auto expectError = [&](std::string_view expr, std::string_view needle) {
        auto r = interp->eval(expr);
        REQUIRE_FALSE(r.has_value());
        CHECK(r.error().kind == ErrorKind::Conversion);
        CHECK_MESSAGE(r.error().message.find(needle) != std::string::npos, r.error().message);
    };
    expectError("1.5", "the result is a float");
    expectError("[1, 2, 0.5]", "the result[2] is a float");
    expectError("{'ships': [{'hp': 2.0}]}", "the result['ships'][0]['hp'] is a float");
    expectError("2**63", "doesn't fit in 64 bits");
    expectError("-2**63 - 1", "doesn't fit in 64 bits");
    expectError("{1: 'a'}", "has a key of type int");
    expectError("{(1, 2): 'a'}", "has a key of type tuple");
    expectError("{1, 2}", "is a set");
    expectError("b'x'", "is a bytes");
    expectError("object()", "is a object");
    expectError("len", "is a function");
    // nested too deep, or containing itself
    execOk(*interp, "loop = []\nloop.append(loop)\ndeep = []\nfor i in range(150):\n    deep = [deep]\n");
    expectError("loop", "nested more than 100 levels");
    expectError("deep", "nested more than 100 levels");
    // Arguments from the engine are checked before Python sees them.
    auto bad = roundTrip(*interp, Value(std::string("\xc3\x28")));
    REQUIRE_FALSE(bad.has_value());
    CHECK(bad.error().kind == ErrorKind::Conversion);
    CHECK(bad.error().message.find("argument 1 is not valid UTF-8") != std::string::npos);
    Value badKey = Value::emptyMap();
    badKey.set(std::string("\xe2\x82"), 1);
    CHECK(roundTrip(*interp, badKey).error().kind == ErrorKind::Conversion);
    Value deepValue = Value::emptyList();
    for (int i = 0; i < 120; ++i) deepValue = Value(ValueList{deepValue});
    CHECK(roundTrip(*interp, deepValue).error().kind == ErrorKind::Conversion);
    // The interpreter is fine afterwards.
    CHECK(evalOk(*interp, "1") == Value(1));
}

TEST_CASE("script convert: big values cross") {
    auto interp = converter();
    ValueList big;
    for (int i = 0; i < 20000; ++i) big.push_back(Value(i));
    auto r = roundTrip(*interp, Value(big));
    REQUIRE(r.has_value());
    CHECK(r->asList().size() == 20000);
    CHECK(r->asList()[19999] == Value(19999));
}
