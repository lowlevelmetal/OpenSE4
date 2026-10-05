#pragma once

// script::Value as JSON text, for external bots (docs/MODDING_SDK.md, section 14.2):
// the same tree the in-game scripts see, written and read strictly.
//
// Reading accepts RFC 8259 JSON in UTF-8 with these restrictions, each reported
// with its place in the text: numbers must be whole and fit in 64 bits (no
// fraction, no exponent), objects may not repeat a key, and values nest at most
// `maxDepth` deep. No byte order mark, comments or trailing commas. Objects keep
// their keys in the order of the text.
//
// Writing produces compact JSON (or indented with `pretty`): keys in the map's
// order, strings with the escapes JSON requires (other characters as UTF-8).

#include "script/value.hpp"

#include <cstddef>
#include <expected>
#include <string>
#include <string_view>

namespace opense4::script {

struct JsonError {
    std::string message;
    size_t offset = 0;   // bytes into the text
    size_t line = 1;     // 1-based
    size_t column = 1;   // 1-based, in bytes
    // "line 3, column 14: a number with a fraction (only whole numbers)"
    std::string describe() const;
};

constexpr int kJsonMaxDepth = 100;

std::expected<Value, JsonError> parseJson(std::string_view text, int maxDepth = kJsonMaxDepth);

// Fails only for text that is not valid UTF-8 (the message names where).
std::expected<std::string, std::string> toJson(const Value& value, bool pretty = false);

} // namespace opense4::script
