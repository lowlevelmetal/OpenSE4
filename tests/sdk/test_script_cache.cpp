// The per-process bytecode cache (src/script/runtime.hpp) and what interpreters cost.

#include "script_test_util.hpp"

#include <chrono>
#include <format>

using namespace opense4::script;
using namespace opense4::script::test;

namespace {

// A module of about 2000 lines: classes, functions, data.
std::string bigModule() {
    std::string s = "TABLE = {\n";
    for (int i = 0; i < 400; ++i) s += std::format("    'key{}': ({}, {}, 'name{}'),\n", i, i, i * 3, i);
    s += "}\n\n";
    for (int i = 0; i < 160; ++i) {
        s += std::format("class Unit{}:\n", i);
        s += "    def __init__(self, hp, attack):\n";
        s += "        self.hp = hp\n";
        s += "        self.attack = attack\n";
        s += std::format("    def strength(self):\n        return self.hp * {} + self.attack\n", i + 1);
        s += "    def describe(self):\n";
        s += std::format("        return 'unit {}: ' + str(self.strength())\n\n", i);
    }
    for (int i = 0; i < 100; ++i)
        s += std::format("def helper{}(values):\n    total = 0\n    for v in values:\n        total += v * {}\n    return total\n\n", i, i);
    s += "def run():\n    return sum(Unit7(3, 4).strength() for _ in range(3)) + helper5([1, 2, 3])\n";
    return s;
}

} // namespace

TEST_CASE("script cache: compiled once per process, then reused") {
    std::string source = bigModule();
    std::string source2 = source + "\nEXTRA = 1\n";
    clearBytecodeCache();
    BytecodeCacheStats before = bytecodeCacheStats();
    Value results[3];
    int64_t budgets[3] = {0, 0, 0};
    for (int i = 0; i < 3; ++i) {
        auto interp = makeInterpreter();
        REQUIRE(interp->addFile("big.py", i < 2 ? source : source2).has_value());
        auto r = interp->call("big", "run");
        REQUIRE_MESSAGE(r.has_value(), explain(r.error()));
        results[i] = *r;
        budgets[i] = interp->budgetUsed();
    }
    BytecodeCacheStats after = bytecodeCacheStats();
    CHECK(after.misses - before.misses == 2);   // the first time, and the changed text
    CHECK(after.hits - before.hits == 1);
    CHECK(after.entries >= 2);
    CHECK(after.bytes > source.size());
    // the same result and the same budget, from the cache or not
    CHECK(results[0] == results[1]);
    CHECK(results[0] == Value(int64_t{3 * (3 * 8 + 4) + 5 * 6}));
    CHECK(budgets[0] == budgets[1]);
    CHECK(budgets[2] > budgets[0]);   // one more statement
    clearBytecodeCache();
    CHECK(bytecodeCacheStats().entries == 0);
}

TEST_CASE("script cache: what interpreters cost (measured, not checked)") {
    std::string source = bigModule();
    clearBytecodeCache();
    using Clock = std::chrono::steady_clock;
    auto ms = [](Clock::duration d) { return std::chrono::duration<double, std::milli>(d).count(); };
    auto createAndImport = [&] {
        auto start = Clock::now();
        auto interp = makeInterpreter();
        REQUIRE(interp->addFile("big.py", source).has_value());
        REQUIRE(interp->importModule("big").has_value());
        return ms(Clock::now() - start);
    };
    double cold = createAndImport();
    double warm = 1e9;
    for (int i = 0; i < 5; ++i) warm = std::min(warm, createAndImport());
    double empty = 1e9;
    for (int i = 0; i < 5; ++i) {
        auto start = Clock::now();
        auto interp = makeInterpreter();
        empty = std::min(empty, ms(Clock::now() - start));
    }
    auto interp = makeInterpreter();
    execOk(*interp, "def loop(n):\n    i = 0\n    while i < n:\n        i += 1\n");
    auto start = Clock::now();
    auto r = interp->call("__main__", "loop", std::vector<Value>{Value(200000)});
    double loopMs = ms(Clock::now() - start);
    REQUIRE(r.has_value());
    int64_t bytecodes = interp->lastCallBudget();
    MESSAGE(std::format("2000-line module: create + import {:.2f} ms cold, {:.2f} ms warm; an empty interpreter "
                        "{:.3f} ms; {} bytecodes in {:.2f} ms ({:.1f} ns each)",
                        cold, warm, empty, bytecodes, loopMs, loopMs * 1e6 / static_cast<double>(bytecodes)));
    CHECK(warm < cold);
}
