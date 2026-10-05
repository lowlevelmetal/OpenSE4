#pragma once

// Applying data patches (mods/patch.hpp) to parsed data files: records added,
// changed and removed, list entries added and removed (and the list
// renumbered), name lists edited. Every field a patch writes carries the
// patch's place in Field::origin, so the loaders' messages and the checks
// after loading can name it.

#include "datafile/datafile.hpp"
#include "mods/patch.hpp"

#include <set>
#include <string>
#include <vector>

namespace opense4::mods {

// A record name a patch removed (or renamed): references to it are checked
// once every mod is applied.
struct Removal {
    const TableSpec* table = nullptr;
    std::string name;
    std::string where;   // the operation, for messages
    bool cascade = false;
};

struct ApplyContext {
    std::vector<std::string>* errors = nullptr;
    std::vector<Removal>* removals = nullptr;
    // For tables whose reader takes any key: the keys (datafile::keyPattern)
    // the install's and mods' files of the table use. Null: not checked here.
    const std::set<std::string>* knownPatterns = nullptr;
    std::string fileLabel;  // "Components.txt", "Ai/Default_AI_Research.txt"
};

// Applies one operation to one file. `required`: a selector that matches
// nothing is an error (for the AI tables, an operation spread over several
// files matches in some). Returns how many records it applied to.
size_t applyRecordOp(datafile::DataFile& file, const RecordOp& op, ApplyContext& ctx, bool required = true);
void applyListOp(datafile::DataFile& file, const ListOp& op, ApplyContext& ctx);

// One entry of a numbered list: its fields, as the record has them.
struct GroupEntry {
    std::vector<datafile::Field> fields;
    std::vector<size_t> items;  // per field: which of the group's items it is
};
std::vector<GroupEntry> readGroup(const datafile::Record& record, const GroupSpec& group);
// Rewrites the list: the count key and the entries numbered from 1, where
// the list was (or at the end). Fields keep their origins; the count gets `origin`.
void writeGroup(datafile::Record& record, const GroupSpec& group, const std::vector<GroupEntry>& entries, const std::string& origin);

// The record's label for messages: its first field's value.
std::string recordLabel(const datafile::Record& record);
// Where a record or field is, for messages: "Components.txt:120 [Laser]", or
// the patch that made it.
std::string describeRecord(const datafile::DataFile& file, const datafile::Record& record);
std::string describeField(const datafile::DataFile& file, const datafile::Record& record, const datafile::Field& field);

} // namespace opense4::mods
