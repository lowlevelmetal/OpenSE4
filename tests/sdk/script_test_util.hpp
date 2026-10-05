#pragma once

// Helpers for the script runtime's tests (tests/sdk).

#include "script/runtime.hpp"

#include <doctest/doctest.h>

#include <memory>
#include <string>
#include <string_view>

namespace opense4::script::test {

inline std::unique_ptr<Interpreter> makeInterpreter(const Limits& limits = {}) {
    auto interp = Interpreter::create(limits);
    REQUIRE_MESSAGE(interp.has_value(), (interp ? std::string() : interp.error().describe()));
    return std::move(*interp);
}

inline std::string explain(const Error& e) {
    return e.describe() + "\n" + e.traceback;
}

inline Value evalOk(Interpreter& interp, std::string_view expr) {
    auto r = interp.eval(expr);
    REQUIRE_MESSAGE(r.has_value(), (r ? std::string() : explain(r.error())));
    return *r;
}

inline void execOk(Interpreter& interp, std::string_view code) {
    auto r = interp.exec(code);
    REQUIRE_MESSAGE(r.has_value(), (r ? std::string() : explain(r.error())));
}

inline Error execFails(Interpreter& interp, std::string_view code, const CallOptions& options = {}) {
    auto r = interp.exec(code, options);
    REQUIRE_MESSAGE(!r.has_value(), "the code should have failed");
    return r.error();
}

// 64-bit FNV-1a, for golden checksums of script output.
inline uint64_t fnv1a(std::string_view s) {
    uint64_t h = 14695981039346656037ull;
    for (unsigned char c : s) {
        h ^= c;
        h *= 1099511628211ull;
    }
    return h;
}

} // namespace opense4::script::test
