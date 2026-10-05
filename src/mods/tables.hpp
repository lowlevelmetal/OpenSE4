#pragma once

// The tables data patches change (docs/sdk/packages-and-data.md): each data
// file and each of the computer players' tables under a patch name, how its
// records are named, its lists of numbered fields ("Ability 3 Type"), and the
// fields that name records of other tables (for the checks when a record is
// removed). Field names are the data files' own.

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace opense4::mods {

// A list of numbered fields in a record: "Number of Abilities" and "Ability N
// Type", "Ability N Descr", ... Each item's key has "{}" where the number goes.
struct GroupSpec {
    std::string_view name;                   // patch name: "abilities"
    std::string_view countKey;               // empty: the entries run from 1 while one is there
    std::vector<std::string_view> items;     // key formats
    size_t keyItem = 0;                      // the item that names an entry (for `remove`)
};

enum class TableKind : uint8_t {
    Records,  // records, each named by its key field (or chosen by match / index)
    Single,   // one record (Settings.txt, most AI tables)
    List,     // a plain list of names, one per line
};

struct TableSpec {
    std::string_view name;      // patch name: "components", "ai.research"
    std::string_view file;      // "Components.txt"; for AI tables the table's file suffix ("Research")
    bool ai = false;            // a computer players' table (<prefix>_AI_<file>.txt)
    TableKind kind = TableKind::Records;
    std::string_view keyField;  // "Name"; empty: records have no name
    // The reader takes any key (Settings.txt, Happiness.txt, ...): a patched
    // key must then be one the install's or a mod's files of the table use.
    bool openFields = false;
    std::vector<GroupSpec> groups;

    const GroupSpec* group(std::string_view groupName) const;
};

std::span<const TableSpec> tables();
const TableSpec* findTable(std::string_view patchName);
// The table of a data folder file ("Components.txt", any case).
const TableSpec* tableOfDataFile(std::string_view fileName);
// The table of an AI table name ("Research", any case).
const TableSpec* tableOfAiFile(std::string_view aiTable);
// The AI table a file name holds ("Terran_AI_Research.txt" -> "Research"), if any.
std::optional<std::string> aiTableOfFileName(std::string_view fileName);

// What removing a referred-to record does to a reference when the removal
// asks for `cascade = true`.
enum class Cascade : uint8_t {
    RemoveRecord,  // the record holding the reference goes too (a component needing a removed tech)
    RemoveEntry,   // the list entry holding it goes (a quadrant's system type)
    ClearField,    // the field gets `clearTo` (a stellar ability type becomes None)
    Refuse,        // cannot be fixed automatically: change the record first
};

struct ReferenceSpec {
    std::string_view from;       // the table holding the reference
    std::string_view keyFormat;  // its field, "{}" for a number: "Tech Area Req {}"
    std::string_view to;         // the table referred to
    Cascade cascade = Cascade::Refuse;
    std::string_view group;      // RemoveEntry: the list the entry belongs to
    std::string_view clearTo;    // ClearField: the new value
};
std::span<const ReferenceSpec> references();

// Key matching with numbers: the number in `key` where `format` has "{}"
// ("Ability 3 Val 1" against "Ability {} Val 1" gives 3); keys compare in any
// case with runs of spaces as one. A format without "{}" matches itself (0).
std::optional<int> matchNumbered(std::string_view key, std::string_view format);
// The key for entry `n`: "Ability {} Type", 3 -> "Ability 3 Type".
std::string numberedKey(std::string_view format, int n);
// An entry's field name in a patch: the format without its number ("Ability Type").
std::string entryFieldName(std::string_view format);

} // namespace opense4::mods
