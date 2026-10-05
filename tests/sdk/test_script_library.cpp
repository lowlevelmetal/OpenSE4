// The script runtime's Python library (python/lib): tests/sdk/python/test_library.py,
// which also passes under CPython, run in the runtime.

#include "script_test_util.hpp"

#include <fstream>
#include <sstream>

using namespace opense4::script;
using namespace opense4::script::test;

namespace {

std::string readSdkTestFile(const char* name) {
    std::ifstream in(std::string(OPENSE4_SDK_TEST_DIR) + "/python/" + name, std::ios::binary);
    REQUIRE_MESSAGE(in.good(), name);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

} // namespace

TEST_CASE("script library: typing, dataclasses, itertools, functools, bisect, collections, heapq, math...") {
    Limits limits;
    limits.cStackBytes = size_t{1} << 20;   // sanitizer builds use more stack per call
    auto interp = makeInterpreter(limits);
    REQUIRE(interp->addFile("test_library.py", readSdkTestFile("test_library.py")).has_value());
    auto r = interp->call("test_library", "run");
    REQUIRE_MESSAGE(r.has_value(), explain(r.error()));
    REQUIRE(r->isList());
    for (const Value& failure : r->asList()) FAIL_CHECK(failure.asString());
    CHECK(r->size() == 0);
}

TEST_CASE("script library: every module imports, and none is a script's to replace") {
    auto interp = makeInterpreter();
    for (const LibraryFile& f : libraryFiles()) {
        std::string module(f.path.substr(0, f.path.size() - 3));
        CHECK_MESSAGE(interp->importModule(module).has_value(), module);
        CHECK(interp->addFile(f.path, "").error().kind == ErrorKind::Usage);
    }
    CHECK(libraryFiles().size() >= 14);
}
