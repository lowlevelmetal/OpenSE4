#include "mods/apply.hpp"

#include "datafile/reader.hpp"

#include <algorithm>
#include <format>
#include <limits>

namespace opense4::mods {

using datafile::DataFile;
using datafile::Field;
using datafile::keysEqual;
using datafile::Record;

std::string recordLabel(const Record& record) { return record.fields.empty() ? std::string{} : record.fields.front().value; }

std::string describeRecord(const DataFile& file, const Record& record) {
    if (!record.origin.empty()) return std::format("{} [{}] ({})", file.name, recordLabel(record), record.origin);
    if (!file.origin.empty()) return std::format("{}:{} [{}] ({})", file.name, record.line, recordLabel(record), file.origin);
    return std::format("{}:{} [{}]", file.name, record.line, recordLabel(record));
}

std::string describeField(const DataFile& file, const Record& record, const Field& field) {
    if (!field.origin.empty()) return std::format("{} [{}] ({})", file.name, recordLabel(record), field.origin);
    if (!record.origin.empty()) return describeRecord(file, record);
    if (!file.origin.empty()) return std::format("{}:{} [{}] ({})", file.name, field.line, recordLabel(record), file.origin);
    return std::format("{}:{} [{}]", file.name, field.line, recordLabel(record));
}

std::vector<GroupEntry> readGroup(const Record& record, const GroupSpec& group) {
    int count = 0;
    if (!group.countKey.empty()) {
        if (const Field* c = record.find(group.countKey)) count = static_cast<int>(std::clamp<int64_t>(datafile::parseInteger(c->value).value_or(0), 0, 10'000));
    } else {
        auto has = [&](int n) {
            return std::any_of(record.fields.begin(), record.fields.end(), [&](const Field& f) {
                return std::any_of(group.items.begin(), group.items.end(), [&](std::string_view item) { return matchNumbered(f.key, item) == n; });
            });
        };
        while (count < 10'000 && has(count + 1)) ++count;
    }
    std::vector<GroupEntry> entries(static_cast<size_t>(count));
    for (const Field& f : record.fields)
        for (size_t i = 0; i < group.items.size(); ++i)
            if (const auto n = matchNumbered(f.key, group.items[i]); n && *n >= 1 && *n <= count) {
                entries[static_cast<size_t>(*n - 1)].fields.push_back(f);
                entries[static_cast<size_t>(*n - 1)].items.push_back(i);
                break;
            }
    return entries;
}

void writeGroup(Record& record, const GroupSpec& group, const std::vector<GroupEntry>& entries, const std::string& origin) {
    auto inGroup = [&](const Field& f) {
        if (!group.countKey.empty() && keysEqual(f.key, group.countKey)) return true;
        return std::any_of(group.items.begin(), group.items.end(), [&](std::string_view item) { return matchNumbered(f.key, item).has_value(); });
    };
    std::vector<Field> kept;
    size_t at = std::numeric_limits<size_t>::max();
    for (Field& f : record.fields) {
        if (inGroup(f)) {
            if (at == std::numeric_limits<size_t>::max()) at = kept.size();
            continue;
        }
        kept.push_back(std::move(f));
    }
    if (at == std::numeric_limits<size_t>::max()) at = kept.size();
    std::vector<Field> block;
    if (!group.countKey.empty()) {
        const std::string count = std::to_string(entries.size());
        block.push_back(Field{std::string(group.countKey), count, 0, " " + count, origin});
    }
    for (size_t n = 0; n < entries.size(); ++n)
        for (size_t k = 0; k < entries[n].fields.size(); ++k) {
            Field f = entries[n].fields[k];
            f.key = numberedKey(group.items[entries[n].items[k]], static_cast<int>(n + 1));
            block.push_back(std::move(f));
        }
    kept.insert(kept.begin() + static_cast<std::ptrdiff_t>(at), std::make_move_iterator(block.begin()), std::make_move_iterator(block.end()));
    record.fields = std::move(kept);
}

namespace {

bool matches(const Record& r, const TableSpec& t, const Selector& s, size_t index) {
    if (s.index > 0 && index + 1 != static_cast<size_t>(s.index)) return false;
    if (s.name) {
        const Field* f = t.keyField.empty() ? nullptr : r.find(t.keyField);
        if (!f || !keysEqual(f->value, *s.name)) return false;
    }
    for (const FieldValue& m : s.match) {
        const Field* f = r.find(m.key);
        if (!f || !keysEqual(f->value, m.value)) return false;
    }
    return true;
}

std::vector<size_t> selectRecords(const DataFile& file, const TableSpec& t, const Selector& s) {
    std::vector<size_t> out;
    if (t.kind == TableKind::Single && s.empty()) {
        if (!file.records.empty()) out.push_back(0);
        return out;
    }
    for (size_t i = 0; i < file.records.size(); ++i)
        if (matches(file.records[i], t, s, i)) out.push_back(i);
    return out;
}

std::optional<size_t> named(const DataFile& file, const TableSpec& t, std::string_view name) {
    if (t.keyField.empty()) return std::nullopt;
    for (size_t i = 0; i < file.records.size(); ++i)
        if (const Field* f = file.records[i].find(t.keyField); f && keysEqual(f->value, name)) return i;
    return std::nullopt;
}

void setField(Record& r, const FieldValue& v, const std::string& origin) {
    for (Field& f : r.fields)
        if (keysEqual(f.key, v.key)) {
            f.value = v.value;
            f.raw = " " + v.value;
            f.origin = origin;
            f.line = v.line;
            return;
        }
    r.fields.push_back(Field{v.key, v.value, v.line, " " + v.value, origin});
}

// Puts a new record's fields in the order the file's first record has them,
// so a dump reads naturally (and the file's first key comes first).
void orderLike(Record& r, const Record& model) {
    auto rank = [&](const Field& f) {
        for (size_t i = 0; i < model.fields.size(); ++i)
            if (keysEqual(model.fields[i].key, f.key)) return i;
        return model.fields.size();
    };
    std::stable_sort(r.fields.begin(), r.fields.end(), [&](const Field& a, const Field& b) { return rank(a) < rank(b); });
}

// Where a field the operation writes comes from: its own line in the patch.
std::string fieldOrigin(const RecordOp& op, int line) { return op.origin.at(line > 0 ? line : op.line); }

// set, then the list entries removed, then those added.
void edit(DataFile& file, Record& r, const RecordOp& op, ApplyContext& ctx) {
    const TableSpec& t = *op.table;
    for (const FieldValue& v : op.set) {
        if (t.openFields && ctx.knownPatterns && !ctx.knownPatterns->contains(datafile::keyPattern(v.key))) {
            ctx.errors->push_back(std::format("{}: {} has no field '{}' (no file of the table uses it; check the spelling)", fieldOrigin(op, v.line),
                                              ctx.fileLabel, v.key));
            continue;
        }
        if (op.kind == OpKind::Change && !t.keyField.empty() && keysEqual(v.key, t.keyField) && ctx.removals)
            if (const Field* old = r.find(t.keyField); old && !keysEqual(old->value, v.value) && t.kind == TableKind::Records)
                ctx.removals->push_back({&t, old->value, op.where, false});  // a rename: what named the old name is checked
        setField(r, v, fieldOrigin(op, v.line));
    }
    for (const EntryRemove& rm : op.removes) {
        std::vector<GroupEntry> entries = readGroup(r, *rm.group);
        std::vector<bool> drop(entries.size(), false);
        for (const EntryRemove::Item& item : rm.items) {
            bool found = false;
            for (size_t n = 0; n < entries.size(); ++n) {
                bool hit = item.index > 0 ? n + 1 == static_cast<size_t>(item.index) : true;
                for (const FieldValue& m : item.match) {
                    bool fieldHit = false;
                    for (size_t k = 0; k < entries[n].fields.size(); ++k)
                        if (rm.group->items[entries[n].items[k]] == m.key && keysEqual(entries[n].fields[k].value, m.value)) fieldHit = true;
                    hit = hit && fieldHit;
                }
                if (hit) {
                    drop[n] = true;
                    found = true;
                }
            }
            if (!found) {
                std::string what = item.index > 0 ? std::format("entry {}", item.index) : std::string{};
                for (const FieldValue& m : item.match) what += std::format("{}{} = '{}'", what.empty() ? "" : ", ", entryFieldName(m.key), m.value);
                ctx.errors->push_back(std::format("{}: {} has no {} entry {} to remove ({} entries)", op.where, describeRecord(file, r),
                                                  rm.group->name, what, entries.size()));
            }
        }
        std::vector<GroupEntry> keep;
        for (size_t n = 0; n < entries.size(); ++n)
            if (!drop[n]) keep.push_back(std::move(entries[n]));
        writeGroup(r, *rm.group, keep, op.where);
    }
    for (const EntryAdd& add : op.adds) {
        std::vector<GroupEntry> entries = readGroup(r, *add.group);
        for (const std::vector<FieldValue>& fields : add.entries) {
            GroupEntry e;
            for (const FieldValue& v : fields) {
                size_t item = 0;
                while (item < add.group->items.size() && add.group->items[item] != v.key) ++item;
                e.fields.push_back(Field{v.key, v.value, v.line, " " + v.value, fieldOrigin(op, v.line)});
                e.items.push_back(item);
            }
            entries.push_back(std::move(e));
        }
        writeGroup(r, *add.group, entries, op.where);
    }
}

} // namespace

size_t applyRecordOp(DataFile& file, const RecordOp& op, ApplyContext& ctx, bool required) {
    const TableSpec& t = *op.table;
    const bool keyed = !t.keyField.empty() && t.kind == TableKind::Records;
    if (op.kind == OpKind::Add) {
        if (keyed)
            if (const auto existing = named(file, t, op.name)) {
                ctx.errors->push_back(std::format("{}: {} already has '{}' ({}): change it instead of adding it", op.where, ctx.fileLabel, op.name,
                                                  describeRecord(file, file.records[*existing])));
                return 0;
            }
        Record r;
        r.origin = op.where;
        r.line = op.line;
        if (op.copyFrom) {
            const std::vector<size_t> from = selectRecords(file, t, *op.copyFrom);
            if (from.size() != 1) {
                ctx.errors->push_back(std::format("{}: copy_from {}: {} has {}", op.where, op.copyFrom->describe(), ctx.fileLabel,
                                                  from.empty() ? "no such record" : "several such records"));
                return 0;
            }
            r.fields = file.records[from.front()].fields;
        }
        if (keyed) {
            bool placed = false;
            for (Field& f : r.fields)
                if (keysEqual(f.key, t.keyField)) {
                    f = Field{f.key, op.name, op.line, " " + op.name, op.where};
                    placed = true;
                    break;
                }
            if (!placed) r.fields.insert(r.fields.begin(), Field{std::string(t.keyField), op.name, op.line, " " + op.name, op.where});
        }
        edit(file, r, op, ctx);
        if (!op.copyFrom && !file.records.empty()) orderLike(r, file.records.front());
        if (r.fields.empty()) {
            ctx.errors->push_back(std::format("{}: the new record has no fields (give them with set, or copy_from)", op.where));
            return 0;
        }
        size_t at = file.records.size();
        if (!op.after.empty() || !op.before.empty()) {
            const std::string& anchor = op.after.empty() ? op.before : op.after;
            const auto i = named(file, t, anchor);
            if (!i) {
                ctx.errors->push_back(std::format("{}: {} has no '{}' to place the new record {}", op.where, ctx.fileLabel, anchor,
                                                  op.after.empty() ? "before" : "after"));
                return 0;
            }
            at = op.after.empty() ? *i : *i + 1;
        }
        file.records.insert(file.records.begin() + static_cast<std::ptrdiff_t>(at), std::move(r));
        if (!file.hasDataSection) file.hasDataSection = true;
        return 1;
    }

    std::vector<size_t> chosen = selectRecords(file, t, op.select);
    if (chosen.empty()) {
        if (required)
            ctx.errors->push_back(std::format("{}: {} has no record {} to {}", op.where, ctx.fileLabel, op.select.describe(),
                                              op.kind == OpKind::Change ? "change" : "remove"));
        return 0;
    }
    if (chosen.size() > 1 && !op.select.all) {
        ctx.errors->push_back(std::format("{}: {} records of {} match {}: add fields to match, or all = true", op.where, chosen.size(), ctx.fileLabel,
                                          op.select.describe()));
        return 0;
    }
    if (op.kind == OpKind::Change) {
        for (size_t i : chosen) edit(file, file.records[i], op, ctx);
        return chosen.size();
    }
    for (auto it = chosen.rbegin(); it != chosen.rend(); ++it) {
        const Record& r = file.records[*it];
        if (keyed && ctx.removals)
            if (const Field* f = r.find(t.keyField)) ctx.removals->push_back({&t, f->value, op.where, op.cascade});
        file.records.erase(file.records.begin() + static_cast<std::ptrdiff_t>(*it));
    }
    return chosen.size();
}

void applyListOp(DataFile& file, const ListOp& op, ApplyContext& ctx) {
    // A name list is plain lines; one with a data section keeps one name per field.
    for (const auto& [name, line] : op.remove) {
        size_t removed = 0;
        if (file.hasDataSection) {
            for (Record& r : file.records) removed += std::erase_if(r.fields, [&](const Field& f) { return keysEqual(f.value, name); });
            std::erase_if(file.records, [](const Record& r) { return r.fields.empty(); });
        } else {
            removed = std::erase_if(file.entries, [&](const std::string& e) { return keysEqual(e, name); });
        }
        if (removed == 0) ctx.errors->push_back(std::format("{}: {} has no '{}' to remove", op.origin.at(line), ctx.fileLabel, name));
    }
    for (const auto& [name, line] : op.add) {
        if (file.hasDataSection) {
            const std::string key = file.records.empty() || file.records.front().fields.empty() ? "Name" : file.records.front().fields.front().key;
            Record r;
            r.origin = op.origin.at(line);
            r.fields.push_back(Field{key, name, line, " " + name, r.origin});
            file.records.push_back(std::move(r));
        } else {
            file.entries.push_back(name);
        }
    }
}

} // namespace opense4::mods
