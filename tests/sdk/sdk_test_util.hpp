#pragma once

// Helpers for the SDK's tests: the schema the docs give (docs/sdk/view.md and
// docs/sdk/commands.md), read from their tables, and a check of values
// against it. The docs are the schema, so a field the code adds without
// documenting it, or documents without making it, fails a test.
//
// The docs' format: a type is a `### \`name\`` heading followed by a table.
// A struct's table starts "| Field | Type | ...": one row per key, the key in
// backticks, then its type. An enumeration's table starts "| Value | ...":
// one row per name, in backticks. Types:
//   int, bool, text, int or text
//   <thing> id     a whole number naming something the view must list
//                  (system, object, empire, vehicle, fleet, design, message)
//   <thing> ref    a whole number naming something that need not be listed
//   <table> index  a position in a table of the rules view (an int)
//   list of <type>, <type>, or null, command, and every documented type.

#include "game/rules.hpp"
#include "game/state.hpp"
#include "script/value.hpp"

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace opense4::sdktest {

struct Schema {
    struct Row {
        std::string field;
        std::string type;
    };
    struct Section {
        std::string name;
        std::string file;
        int line = 0;
        bool isEnum = false;
        std::vector<Row> rows;             // structs
        std::vector<std::string> values;   // enumerations
    };
    std::map<std::string, Section, std::less<>> sections;
    std::vector<std::string> problems;   // what the docs got wrong (a type twice, a table missing)

    const Section* find(std::string_view name) const {
        auto it = sections.find(name);
        return it == sections.end() ? nullptr : &it->second;
    }
};

// docs/sdk/commands.md and docs/sdk/view.md, read once.
const Schema& docsSchema();

// The ids a value names, by kind ("vehicle", "system", ...), from its `id` fields.
struct References {
    std::vector<std::pair<std::string, int64_t>> named;
};

// Every way `v` differs from `type`: "vehicles[3].cargo: expected a map".
std::vector<std::string> validate(const Schema& schema, const script::Value& v, std::string_view type, References* refs = nullptr);

// The ids of the elements of a list (their `id` keys).
std::set<int64_t> idsOf(const script::Value& list);

std::string joined(const std::vector<std::string>& lines, size_t limit = 20);

// A game of the engine fixture with some of everything for a view: three
// empires (0 human, 1 and 2 computer) after five busy turns of empire 0
// (test::busyOrders), a foreign ship that empire 0 sees at its home, one in
// a system it has not explored, and a battle it fought.
struct SdkGame {
    game::GameState state;
    game::VehicleId seenForeign;     // empire 1's, at empire 0's home
    game::VehicleId hiddenForeign;   // empire 1's, in `unexplored`
    game::SystemId unexplored;       // a system empire 0 has not explored
};
SdkGame sdkGame();

// Map lookups that fail the test with the path when a key is missing.
const script::Value& at(const script::Value& v, std::string_view key);
int64_t intAt(const script::Value& v, std::string_view key);

} // namespace opense4::sdktest
