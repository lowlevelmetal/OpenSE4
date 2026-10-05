// The same script output on every computer (docs/sdk/runtime.md): the workload of
// tests/sdk/python/determinism.py gives the same text and uses the same budget on
// Linux x86_64, Windows (the dist-windows build under Wine) and 32-bit ARM (the
// dist-linux-armhf build under QEMU), and with or without the bytecode cache.
//
// If a change to the runtime changes the golden value, it changes for every
// platform alike: check the new value on all three before updating it.

#include "script_test_util.hpp"

#include <format>
#include <fstream>
#include <sstream>

using namespace opense4::script;
using namespace opense4::script::test;

namespace {

constexpr uint64_t kGoldenChecksum = 0xa1a0c57795b46fe6ull;
constexpr int64_t kGoldenBudget = 305950;

std::string determinismSource() {
    std::ifstream in(std::string(OPENSE4_SDK_TEST_DIR) + "/python/determinism.py", std::ios::binary);
    REQUIRE(in.good());
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

} // namespace

TEST_CASE("script determinism: the same output and budget on every platform") {
    std::string source = determinismSource();
    uint64_t checksums[2] = {0, 0};
    int64_t budgets[2] = {0, 0};
    clearBytecodeCache();
    for (int run = 0; run < 2; ++run) {   // compiled, then from the cache
        auto interp = makeInterpreter();
        REQUIRE(interp->addFile("determinism.py", source).has_value());
        auto r = interp->call("determinism", "run");
        REQUIRE_MESSAGE(r.has_value(), explain(r.error()));
        checksums[run] = fnv1a(r->asString());
        budgets[run] = interp->budgetUsed();
        if (run == 0) MESSAGE(std::format("determinism: {} bytes of output, checksum {:#018x}, budget {}", r->asString().size(), checksums[0], budgets[0]));
    }
    CHECK(checksums[0] == checksums[1]);
    CHECK(budgets[0] == budgets[1]);
    CHECK(checksums[0] == kGoldenChecksum);
    CHECK(budgets[0] == kGoldenBudget);
}
