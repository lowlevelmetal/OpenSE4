// script::Value as strict JSON (src/script/json.hpp), for external bots.

#include "script/json.hpp"

#include <doctest/doctest.h>

#include <limits>
#include <ostream>   // doctest streams the std::string_view (MSVC needs the stream declared)

using namespace opense4::script;

namespace {

Value parsed(std::string_view text) {
    auto r = parseJson(text);
    REQUIRE_MESSAGE(r.has_value(), (r ? std::string() : r.error().describe()));
    return *r;
}

JsonError parseError(std::string_view text, int maxDepth = kJsonMaxDepth) {
    auto r = parseJson(text, maxDepth);
    REQUIRE_MESSAGE(!r.has_value(), "should not parse: " << text);
    return r.error();
}

} // namespace

TEST_CASE("script json: values round trip, maps in their order") {
    Value v = Value::emptyMap();
    v.set("kind", "SetOrders");
    v.set("vehicle", 12);
    v.set("orders", ValueList{Value(ValueMap{{"move", Value(ValueList{3, -4})}}), Value(nullptr)});
    v.set("repeat", false);
    v.set("big", std::numeric_limits<int64_t>::min());
    v.set("max", std::numeric_limits<int64_t>::max());
    v.set("text", "quote \" backslash \\ slash / tab \t newline \n bell \x07 é ✓ 🚀");
    v.set("empty", Value::emptyMap());
    v.set("none", Value::emptyList());
    auto text = toJson(v);
    REQUIRE(text.has_value());
    CHECK(*text ==
          "{\"kind\":\"SetOrders\",\"vehicle\":12,\"orders\":[{\"move\":[3,-4]},null],\"repeat\":false,"
          "\"big\":-9223372036854775808,\"max\":9223372036854775807,"
          "\"text\":\"quote \\\" backslash \\\\ slash / tab \\t newline \\n bell \\u0007 é ✓ 🚀\","
          "\"empty\":{},\"none\":[]}");
    CHECK(parsed(*text) == v);
    auto pretty = toJson(v, true);
    REQUIRE(pretty.has_value());
    CHECK(pretty->find("{\n  \"kind\": \"SetOrders\",\n  \"vehicle\": 12,") == 0);
    CHECK(parsed(*pretty) == v);
}

TEST_CASE("script json: reading") {
    CHECK(parsed(" \t\r\n null \n") == Value());
    CHECK(parsed("[true,false]") == Value(ValueList{true, false}));
    CHECK(parsed("-0") == Value(0));
    CHECK(parsed("\"\\u00e9\\u2713\\ud83d\\ude80\\/\\b\\f\\r\"") == Value("é✓🚀/\b\f\r"));
    Value m = parsed("{\"b\": 1, \"a\": {\"y\": [], \"x\": {}}}");
    CHECK(m.asMap()[0].first == "b");
    CHECK(m.find("a")->asMap()[0].first == "y");
    // nesting exactly at the limit is fine
    std::string deep(100, '[');
    deep += std::string(100, ']');
    CHECK(parseJson(deep).has_value());
}

TEST_CASE("script json: strict errors with their place") {
    JsonError e = parseError("{\n  \"x\": 1.5\n}");
    CHECK(e.line == 2);
    CHECK(e.column == 9);
    CHECK(e.message.find("fraction") != std::string::npos);
    CHECK(e.describe() == "line 2, column 9: a number with a fraction (only whole numbers)");
    CHECK(parseError("1e5").message.find("exponent") != std::string::npos);
    CHECK(parseError("9223372036854775808").message.find("64 bits") != std::string::npos);
    CHECK(parseError("-9223372036854775809").message.find("64 bits") != std::string::npos);
    CHECK(parseError("012").message.find("leading zero") != std::string::npos);
    CHECK(parseError("-").message.find("without digits") != std::string::npos);
    CHECK(parseError("[1,]").message.find("comma") != std::string::npos);
    CHECK(parseError("{\"a\":1,}").message.find("comma") != std::string::npos);
    CHECK(parseError("{\"a\":1,\"a\":2}").message.find("appears twice") != std::string::npos);
    CHECK(parseError("{a:1}").message.find("key in quotes") != std::string::npos);
    CHECK(parseError("[1 2]").message.find("expected ','") != std::string::npos);
    CHECK(parseError("\"abc").message.find("ends inside a string") != std::string::npos);
    CHECK(parseError("\"a\nb\"").message.find("control character") != std::string::npos);
    CHECK(parseError("\"\\x\"").message.find("unknown escape") != std::string::npos);
    CHECK(parseError("\"\\ud83d\"").message.find("surrogate") != std::string::npos);
    CHECK(parseError("\"\\ude80\"").message.find("lone low surrogate") != std::string::npos);
    CHECK(parseError("\"\\u12\"").message.find("four hex digits") != std::string::npos);
    CHECK(parseError("\"\xff\"").message.find("UTF-8") != std::string::npos);
    CHECK(parseError("\"\xc0\xaf\"").message.find("UTF-8") != std::string::npos);   // overlong
    CHECK(parseError("\"\xed\xa0\x80\"").message.find("UTF-8") != std::string::npos);   // a surrogate
    CHECK(parseError("\xEF\xBB\xBF{}").message.find("byte order mark") != std::string::npos);
    CHECK(parseError("nul").message.find("unknown word") != std::string::npos);
    CHECK(parseError("{} {}").message.find("more text") != std::string::npos);
    CHECK(parseError("").message.find("ends where a value") != std::string::npos);
    CHECK(parseError("// comment\n1").message.find("unexpected character") != std::string::npos);
    std::string tooDeep(101, '[');
    tooDeep += std::string(101, ']');
    CHECK(parseError(tooDeep).message.find("nested more than 100") != std::string::npos);
    CHECK(parseError("[[1]]", 1).message.find("nested more than 1") != std::string::npos);
}

TEST_CASE("script json: writing refuses text that isn't UTF-8") {
    Value v = Value::emptyMap();
    v.set("ok", "fine");
    v.set("bad", ValueList{Value(std::string("x\xff"))});
    auto r = toJson(v);
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error() == "the value['bad'][0] is not valid UTF-8 text");
}
