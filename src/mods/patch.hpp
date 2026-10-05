#pragma once

// Data patches (docs/MODDING_SDK.md §5, docs/sdk/packages-and-data.md): what
// a mod's data/*.toml files (and its generators' output) say to change in the
// data files and the computer players' tables, parsed into operations. The
// operations apply to the parsed "Key := Value" records before the typed
// loaders read them (mods/data_set.hpp), so every table is changed the same
// way, with the data files' own field names.
//
//     [[components.add]]                 # a new record
//     name = "Heavy Fighter Bay"
//     copy_from = "Fighter Bay"          # start from a copy of another
//     after = "Fighter Bay"              # where it goes (default: the end)
//     set = { "Tonnage Space Taken" = 40 }
//
//     [[components.change]]              # chosen by name, match = {...} or index = N
//     name = "Ion Engine I"
//     set = { "Supply Amount Used" = 3 }
//     remove = { abilities = ["Movement Bonus"] }
//     add = { abilities = [{ "Ability Type" = "Movement Bonus", "Ability Val 1" = 2 }] }
//
//     [[tech_areas.remove]]
//     name = "Old Science"
//     cascade = true                     # also remove what needs it
//
//     [system_names]                     # name lists
//     add = ["Kepler"]
//     remove = ["Vega"]
//
//     [[ai.research.change]]             # a computer players' table
//     files = "default"                  # or "race:<folder>", "style:<folder>"; default: every file
//     match = { "Tech Area Name" = "Armor" }
//     set = { "Tech Area Min Percent" = 20 }
//
//     [[abilities.declare]]              # a new ability name
//     name = "Hyperspace Anchor"
//     combine = "max"                    # sum (the default), max or min

#include "mods/tables.hpp"
#include "ruleset/mods.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace opense4::script {
class Value;
}

namespace opense4::mods {

// A map entry of a Node. Not std::pair: a pair of an incomplete Node isn't
// allowed by every standard library.
struct NodeEntry;

// A patch file as a tree: TOML's values, or a generator's script::Value.
struct Node {
    enum class Type : uint8_t { Null, Bool, Int, String, List, Map, Other };
    Type type = Type::Null;
    bool b = false;
    int64_t i = 0;
    std::string s;  // String; Other: what it was ("a floating-point number")
    std::vector<Node> list;
    std::vector<NodeEntry> map;  // in the order written
    int line = 0;   // in the patch file; 0 when not known

    const Node* find(std::string_view key) const;
};

struct NodeEntry {
    std::string first;
    Node second;
};

// Where a patch comes from.
struct Origin {
    std::string mod;      // the mod's id
    std::string file;     // its path in the package: "data/carriers.toml"
    bool generated = false;
    // "mod example.carriers, data/carriers.toml:12" (or "... data/gen.py (generated)").
    std::string at(int line) const;
};

struct FieldValue {
    std::string key;
    std::string value;
    int line = 0;
};

// Which records an operation applies to.
struct Selector {
    std::optional<std::string> name;  // the table's key field
    std::vector<FieldValue> match;    // fields that must all hold these values
    int index = 0;                    // 1-based position in the file
    bool all = false;                 // every record that matches, not exactly one
    bool empty() const { return !name && match.empty() && index == 0; }
    std::string describe() const;     // for messages: "'Ion Engine I'", "match {AI State = Attack}"
};

struct EntryAdd {
    const GroupSpec* group = nullptr;
    std::vector<std::vector<FieldValue>> entries;  // keys are the group's item formats ("Ability {} Type")
    int line = 0;
};

struct EntryRemove {
    struct Item {
        std::vector<FieldValue> match;  // keys are item formats; all must match
        int index = 0;                  // or the entry's position, 1-based
        int line = 0;
    };
    const GroupSpec* group = nullptr;
    std::vector<Item> items;
};

enum class OpKind : uint8_t { Add, Change, Remove };

struct RecordOp {
    OpKind kind = OpKind::Change;
    const TableSpec* table = nullptr;
    Origin origin;                  // the patch file
    std::string where;              // origin.at(line): the operation, for messages
    int line = 0;
    Selector select;                // change, remove
    std::string name;               // add: the new record's name
    std::optional<Selector> copyFrom;  // add
    std::string after, before;      // add: placed after or before this record
    std::vector<FieldValue> set;
    std::vector<EntryRemove> removes;
    std::vector<EntryAdd> adds;
    bool cascade = false;           // remove: remove what refers to it as well
    std::vector<std::string> files; // AI tables: "default", "race:<folder>", "style:<folder>"; empty: every file
};

struct ListOp {
    const TableSpec* table = nullptr;
    Origin origin;
    std::string where;
    int line = 0;
    struct Name {
        std::string name;
        int line = 0;
    };
    std::vector<Name> add, remove;
};

using PatchOp = std::variant<RecordOp, ListOp>;

struct Declaration {
    ruleset::DeclaredAbility ability;
    std::string where;
};

// One mod's patches, in the order they apply.
struct PatchSet {
    std::vector<PatchOp> ops;
    std::vector<Declaration> abilities;
};

// Parses a patch file's TOML into a tree; problems (with lines) go to `errors`.
std::optional<Node> parsePatchToml(std::string_view text, const Origin& origin, std::vector<std::string>& errors);
// A generator's output as a tree (no lines).
Node nodeFromValue(const script::Value& value);
// Adds the operations of one patch file, in the order the file has them.
// Unknown tables, operations, keys and list fields are errors.
void parsePatch(const Node& root, const Origin& origin, PatchSet& out, std::vector<std::string>& errors);

// The names of the patch tables, for messages: "components, facilities, ...".
std::string tableList();

} // namespace opense4::mods
