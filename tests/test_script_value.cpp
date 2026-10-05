#include "script/value.hpp"

#include <doctest/doctest.h>

using namespace opense4::script;

TEST_CASE("script value: kinds, maps in insertion order, value semantics") {
    Value v = Value::emptyMap();
    v.set("kind", "SetOrders");
    v.set("vehicle", 12);
    v.set("repeat", false);
    v.set("vehicle", 13);   // replaced in place, order kept
    REQUIRE(v.isMap());
    CHECK(v.size() == 3);
    CHECK(v.asMap()[0].first == "kind");
    CHECK(v.asMap()[1].first == "vehicle");
    CHECK(v.find("vehicle")->asInt() == 13);
    CHECK(v.find("missing") == nullptr);

    Value orders = Value::emptyList();
    orders.push(Value(int64_t{1} << 40));
    orders.push(nullptr);
    v.set("orders", orders);
    Value copy = v;
    copy.set("kind", "Rename");
    CHECK(v.find("kind")->asString() == "SetOrders");   // the copy did not change the original
    CHECK(copy != v);
    CHECK(describe(v) == "{kind: \"SetOrders\", vehicle: 13, repeat: false, orders: [1099511627776, null]}");
    CHECK(describe(v, 10) == "{kind: \"Se...");
}
