#include "mods/patch.hpp"

#include "datafile/datafile.hpp"
#include "script/value.hpp"

#include <toml++/toml.hpp>

#include <algorithm>
#include <format>

namespace opense4::mods {

const Node* Node::find(std::string_view key) const {
    for (const auto& [k, v] : map)
        if (k == key) return &v;
    return nullptr;
}

std::string Origin::at(int line) const {
    if (generated) return std::format("mod {}, {} (generated)", mod, file);
    return line > 0 ? std::format("mod {}, {}:{}", mod, file, line) : std::format("mod {}, {}", mod, file);
}

std::string Selector::describe() const {
    if (name) return std::format("'{}'", *name);
    if (index > 0) return std::format("record {}", index);
    std::string out = "match {";
    for (size_t i = 0; i < match.size(); ++i) out += std::format("{}{} = {}", i ? ", " : "", match[i].key, match[i].value);
    return out + "}";
}

std::string tableList() {
    std::string out;
    for (const TableSpec& t : tables()) out += std::format("{}{}", out.empty() ? "" : ", ", t.name);
    return out;
}

namespace {

Node fromToml(const toml::node& n) {
    Node out;
    out.line = static_cast<int>(n.source().begin.line);
    if (const auto* s = n.as_string()) {
        out.type = Node::Type::String;
        out.s = std::string(s->get());
    } else if (const auto* i = n.as_integer()) {
        out.type = Node::Type::Int;
        out.i = i->get();
    } else if (const auto* b = n.as_boolean()) {
        out.type = Node::Type::Bool;
        out.b = b->get();
    } else if (const auto* a = n.as_array()) {
        out.type = Node::Type::List;
        for (const toml::node& e : *a) out.list.push_back(fromToml(e));
    } else if (const auto* t = n.as_table()) {
        out.type = Node::Type::Map;
        for (const auto& [k, v] : *t) out.map.emplace_back(std::string(k.str()), fromToml(v));
        // toml++ keeps a table's keys sorted: put them back in the order written.
        std::stable_sort(out.map.begin(), out.map.end(), [](const auto& x, const auto& y) { return x.second.line < y.second.line; });
    } else if (n.is_floating_point()) {
        out.type = Node::Type::Other;
        out.s = "a number with a fraction (data files hold whole numbers; write text in quotes if you mean it)";
    } else {
        out.type = Node::Type::Other;
        out.s = "a date or time";
    }
    return out;
}

const char* typeName(const Node& n) {
    switch (n.type) {
        case Node::Type::Null: return "nothing";
        case Node::Type::Bool: return "true or false";
        case Node::Type::Int: return "a whole number";
        case Node::Type::String: return "text";
        case Node::Type::List: return "a list";
        case Node::Type::Map: return "a table";
        case Node::Type::Other: return n.s.c_str();
    }
    return "?";
}

class Parser {
public:
    Parser(const Origin& origin, PatchSet& out, std::vector<std::string>& errors) : origin_(origin), out_(out), errors_(errors) {}

    void root(const Node& root) {
        if (root.type != Node::Type::Map) return error(root, "a patch is a table of tables (such as [[components.change]])");
        const size_t first = out_.ops.size();
        for (const auto& [key, value] : root.map) {
            if (key == "abilities") declarations(value);
            else if (key == "ai") ai(value);
            else if (const TableSpec* t = findTable(key); t && !t->ai) table(*t, value);
            else error(value, std::format("unknown table '{}' (the tables: abilities, {})", key, tableList()));
        }
        // In the order the file has them, whatever table they belong to.
        std::stable_sort(out_.ops.begin() + static_cast<std::ptrdiff_t>(first), out_.ops.end(),
                         [](const PatchOp& a, const PatchOp& b) { return lineOf(a) < lineOf(b); });
    }

private:
    static int lineOf(const PatchOp& op) {
        return std::visit([](const auto& o) { return o.line; }, op);
    }

    void error(const Node& n, std::string_view what) { errors_.push_back(std::format("{}: {}", origin_.at(n.line), what)); }

    bool expect(const Node& n, Node::Type type, std::string_view what) {
        if (n.type == type) return true;
        error(n, std::format("{} should be {}, not {}", what, type == Node::Type::Map ? "a table" : type == Node::Type::List ? "a list" : "text",
                             typeName(n)));
        return false;
    }

    // A value as the data files write it.
    std::optional<std::string> scalar(const Node& n, std::string_view key) {
        switch (n.type) {
            case Node::Type::String: return n.s;
            case Node::Type::Int: return std::to_string(n.i);
            case Node::Type::Bool: return std::string(n.b ? "True" : "False");
            default: break;
        }
        error(n, std::format("'{}' should be text, a whole number or true/false, not {}", key, typeName(n)));
        return std::nullopt;
    }

    // The operations of a table: a list of tables, or one table.
    std::vector<const Node*> opList(const Node& n, std::string_view what) {
        std::vector<const Node*> out;
        if (n.type == Node::Type::Map) {
            out.push_back(&n);
        } else if (n.type == Node::Type::List) {
            for (const Node& e : n.list)
                if (expect(e, Node::Type::Map, what)) out.push_back(&e);
        } else {
            error(n, std::format("{} should be tables, such as [[{}]]", what, what));
        }
        return out;
    }

    void declarations(const Node& n) {
        if (!expect(n, Node::Type::Map, "[abilities]")) return;
        for (const auto& [key, value] : n.map) {
            if (key != "declare") {
                error(value, std::format("unknown operation 'abilities.{}' (abilities.declare)", key));
                continue;
            }
            for (const Node* d : opList(value, "abilities.declare")) {
                Declaration decl;
                decl.where = origin_.at(d->line);
                decl.ability.mod = origin_.mod;
                bool named = false;
                for (const auto& [k, v] : d->map) {
                    if (k == "name") {
                        if (expect(v, Node::Type::String, "'name'")) decl.ability.name = v.s, named = !v.s.empty();
                    } else if (k == "combine") {
                        if (!expect(v, Node::Type::String, "'combine'")) continue;
                        if (v.s == "sum") decl.ability.combine = ruleset::Combine::Sum;
                        else if (v.s == "max") decl.ability.combine = ruleset::Combine::Max;
                        else if (v.s == "min") decl.ability.combine = ruleset::Combine::Min;
                        else error(v, std::format("combine = '{}': write \"sum\", \"max\" or \"min\"", v.s));
                    } else {
                        error(v, std::format("unknown key '{}' in abilities.declare (name, combine)", k));
                    }
                }
                if (!named) error(*d, "abilities.declare needs a name");
                else out_.abilities.push_back(std::move(decl));
            }
        }
    }

    void ai(const Node& n) {
        if (!expect(n, Node::Type::Map, "[ai]")) return;
        for (const auto& [key, value] : n.map) {
            const TableSpec* t = findTable("ai." + key);
            if (!t) {
                std::string names;
                for (const TableSpec& s : tables())
                    if (s.ai) names += std::format("{}{}", names.empty() ? "" : ", ", s.name.substr(3));
                error(value, std::format("unknown AI table 'ai.{}' (the AI tables: {})", key, names));
                continue;
            }
            table(*t, value);
        }
    }

    void table(const TableSpec& t, const Node& n) {
        if (!expect(n, Node::Type::Map, std::format("[{}]", t.name))) return;
        if (t.kind == TableKind::List) return list(t, n);
        for (const auto& [key, value] : n.map) {
            OpKind kind;
            if (key == "add") kind = OpKind::Add;
            else if (key == "change") kind = OpKind::Change;
            else if (key == "remove") kind = OpKind::Remove;
            else {
                error(value, std::format("unknown operation '{}.{}' (add, change, remove)", t.name, key));
                continue;
            }
            for (const Node* op : opList(value, std::format("{}.{}", t.name, key))) record(t, kind, *op);
        }
    }

    void list(const TableSpec& t, const Node& n) {
        ListOp op;
        op.table = &t;
        op.origin = origin_;
        op.line = n.line;
        op.where = origin_.at(n.line);
        for (const auto& [key, value] : n.map) {
            std::vector<ListOp::Name>* into = key == "add" ? &op.add : key == "remove" ? &op.remove : nullptr;
            if (!into) {
                error(value, std::format("unknown key '{}' in [{}] (add, remove: lists of names)", key, t.name));
                continue;
            }
            if (!expect(value, Node::Type::List, std::format("'{}'", key))) continue;
            for (const Node& e : value.list)
                if (expect(e, Node::Type::String, "a name")) into->push_back({e.s, e.line > 0 ? e.line : value.line});
            if (op.line == 0 || (value.line > 0 && value.line < op.line)) op.line = value.line;
        }
        out_.ops.push_back(std::move(op));
    }

    // "Ability Type" -> the group's item format "Ability {} Type".
    std::optional<std::string_view> itemOf(const GroupSpec& g, std::string_view field) {
        for (std::string_view item : g.items)
            if (datafile::keysEqual(entryFieldName(item), field)) return item;
        return std::nullopt;
    }

    std::string itemNames(const GroupSpec& g) {
        std::string out;
        for (std::string_view item : g.items) out += std::format("{}'{}'", out.empty() ? "" : ", ", entryFieldName(item));
        return out;
    }

    const GroupSpec* groupOf(const TableSpec& t, const std::string& name, const Node& at) {
        if (const GroupSpec* g = t.group(name)) return g;
        std::string names;
        for (const GroupSpec& g : t.groups) names += std::format("{}{}", names.empty() ? "" : ", ", g.name);
        error(at, names.empty() ? std::format("{} has no lists to add to or remove from (set its fields with set)", t.name)
                                : std::format("{} has no list '{}' (its lists: {})", t.name, name, names));
        return nullptr;
    }

    std::vector<FieldValue> fieldsOf(const Node& n, std::string_view what) {
        std::vector<FieldValue> out;
        if (!expect(n, Node::Type::Map, what)) return out;
        for (const auto& [k, v] : n.map)
            if (auto value = scalar(v, k)) out.push_back({k, *value, v.line > 0 ? v.line : n.line});
        return out;
    }

    void entryAdds(const TableSpec& t, const Node& n, RecordOp& op) {
        if (!expect(n, Node::Type::Map, "'add'")) return;
        for (const auto& [groupName, entries] : n.map) {
            const GroupSpec* g = groupOf(t, groupName, entries);
            if (!g || !expect(entries, Node::Type::List, std::format("add.{}", groupName))) continue;
            EntryAdd add;
            add.group = g;
            add.line = entries.line;
            for (const Node& e : entries.list) {
                if (!expect(e, Node::Type::Map, std::format("an entry of {}", groupName))) continue;
                std::vector<FieldValue> fields;
                bool named = false;
                for (const auto& [k, v] : e.map) {
                    const auto item = itemOf(*g, k);
                    if (!item) {
                        error(v, std::format("'{}' is not a field of an entry of {} ({})", k, groupName, itemNames(*g)));
                        continue;
                    }
                    if (auto value = scalar(v, k)) fields.push_back({std::string(*item), *value, v.line > 0 ? v.line : e.line});
                    named = named || *item == g->items[g->keyItem];
                }
                if (!named) error(e, std::format("an entry of {} needs '{}'", groupName, entryFieldName(g->items[g->keyItem])));
                else add.entries.push_back(std::move(fields));
            }
            if (!add.entries.empty()) op.adds.push_back(std::move(add));
        }
    }

    void entryRemoves(const TableSpec& t, const Node& n, RecordOp& op) {
        if (!expect(n, Node::Type::Map, "'remove'")) return;
        for (const auto& [groupName, items] : n.map) {
            const GroupSpec* g = groupOf(t, groupName, items);
            if (!g || !expect(items, Node::Type::List, std::format("remove.{}", groupName))) continue;
            EntryRemove rm;
            rm.group = g;
            for (const Node& e : items.list) {
                EntryRemove::Item item;
                item.line = e.line;
                if (e.type == Node::Type::String) {
                    item.match.push_back({std::string(g->items[g->keyItem]), e.s, e.line});
                } else if (e.type == Node::Type::Int && e.i > 0 && e.i < 100000) {
                    item.index = static_cast<int>(e.i);
                } else if (e.type == Node::Type::Map) {
                    for (const auto& [k, v] : e.map) {
                        const auto it = itemOf(*g, k);
                        if (!it) error(v, std::format("'{}' is not a field of an entry of {} ({})", k, groupName, itemNames(*g)));
                        else if (auto value = scalar(v, k)) item.match.push_back({std::string(*it), *value, v.line});
                    }
                    if (item.match.empty()) continue;
                } else {
                    error(e, std::format("an entry to remove from {} is its '{}' (text), its position (a number from 1) or a table of its fields",
                                         groupName, entryFieldName(g->items[g->keyItem])));
                    continue;
                }
                rm.items.push_back(std::move(item));
            }
            op.removes.push_back(std::move(rm));
        }
    }

    void record(const TableSpec& t, OpKind kind, const Node& n) {
        RecordOp op;
        op.kind = kind;
        op.table = &t;
        op.origin = origin_;
        op.line = n.line;
        op.where = origin_.at(n.line);
        const std::string opName = std::format("{}.{}", t.name, kind == OpKind::Add ? "add" : kind == OpKind::Change ? "change" : "remove");
        auto refuse = [&](const Node& v, std::string_view key) { error(v, std::format("'{}' has no meaning in {}", key, opName)); };
        for (const auto& [key, value] : n.map) {
            if (op.line == 0 || (value.line > 0 && value.line < op.line)) op.line = value.line;
            if (key == "name") {
                if (!expect(value, Node::Type::String, "'name'")) continue;
                if (kind == OpKind::Add) op.name = value.s;
                else op.select.name = value.s;
            } else if (key == "match") {
                if (kind == OpKind::Add) refuse(value, key);
                else op.select.match = fieldsOf(value, "'match'");
            } else if (key == "index") {
                if (kind == OpKind::Add) refuse(value, key);
                else if (value.type == Node::Type::Int && value.i > 0 && value.i < 1'000'000) op.select.index = static_cast<int>(value.i);
                else error(value, "'index' should be a record's position, a whole number from 1");
            } else if (key == "all") {
                if (kind == OpKind::Add) refuse(value, key);
                else if (value.type == Node::Type::Bool) op.select.all = value.b;
                else error(value, "'all' should be true or false");
            } else if (key == "set") {
                if (kind == OpKind::Remove) refuse(value, key);
                else op.set = fieldsOf(value, "'set'");
            } else if (key == "add") {
                if (kind == OpKind::Remove) refuse(value, key);
                else entryAdds(t, value, op);
            } else if (key == "remove") {
                if (kind == OpKind::Remove) refuse(value, key);
                else entryRemoves(t, value, op);
            } else if (key == "copy_from") {
                if (kind != OpKind::Add) refuse(value, key);
                else if (value.type == Node::Type::String) op.copyFrom = Selector{value.s, {}, 0, false};
                else if (value.type == Node::Type::Int && value.i > 0 && value.i < 1'000'000) op.copyFrom = Selector{std::nullopt, {}, static_cast<int>(value.i), false};
                else error(value, "'copy_from' should be the name of a record (or its position, a whole number from 1)");
            } else if (key == "after" || key == "before") {
                if (kind != OpKind::Add) refuse(value, key);
                else if (expect(value, Node::Type::String, std::format("'{}'", key))) (key == "after" ? op.after : op.before) = value.s;
            } else if (key == "cascade") {
                if (kind != OpKind::Remove) refuse(value, key);
                else if (value.type == Node::Type::Bool) op.cascade = value.b;
                else error(value, "'cascade' should be true or false");
            } else if (key == "files") {
                if (!t.ai) {
                    refuse(value, key);
                    continue;
                }
                std::vector<const Node*> specs;
                if (value.type == Node::Type::String) specs.push_back(&value);
                else if (value.type == Node::Type::List)
                    for (const Node& e : value.list) specs.push_back(&e);
                for (const Node* s : specs) {
                    if (s->type != Node::Type::String) {
                        error(*s, "'files' names files as \"default\", \"race:<folder>\" or \"style:<folder>\"");
                        continue;
                    }
                    if (s->s == "all") continue;
                    if (s->s == "default" || (s->s.starts_with("race:") && s->s.size() > 5) || (s->s.starts_with("style:") && s->s.size() > 6))
                        op.files.push_back(s->s);
                    else error(*s, std::format("files = '{}': write \"default\", \"race:<folder>\", \"style:<folder>\" or \"all\"", s->s));
                }
            } else {
                error(value, std::format("unknown key '{}' in {}", key, opName));
            }
        }
        // What each operation needs.
        const bool keyed = !t.keyField.empty() && t.kind == TableKind::Records;
        if (kind == OpKind::Add) {
            if (t.kind == TableKind::Single) return error(n, std::format("{} holds one record: change it instead", t.name));
            if (keyed && op.name.empty()) return error(n, std::format("{} needs a name (the new record's '{}')", opName, t.keyField));
            if (!keyed && !op.name.empty())
                return error(n, std::format("{}'s records have no name: give the new one's fields with set", t.name));
            if (!keyed && op.copyFrom && op.copyFrom->name) return error(n, std::format("{}'s records have no name: copy_from takes a position", t.name));
            if (!op.after.empty() && !op.before.empty()) return error(n, "give 'after' or 'before', not both");
            if ((!op.after.empty() || !op.before.empty()) && !keyed) return error(n, std::format("{}'s records have no name to place it after", t.name));
        } else {
            if (kind == OpKind::Remove && t.kind == TableKind::Single) return error(n, std::format("{} holds one record: it cannot be removed", t.name));
            if (t.kind == TableKind::Records && op.select.empty())
                return error(n, keyed ? std::format("{} needs the record's name (or match = {{...}} or index = N)", opName)
                                      : std::format("{} needs match = {{...}} or index = N ({}'s records have no name)", opName, t.name));
            if (op.select.name && !keyed) return error(n, std::format("{}'s records have no name: choose with match = {{...}} or index = N", t.name));
            if (kind == OpKind::Change && op.set.empty() && op.adds.empty() && op.removes.empty())
                return error(n, std::format("{} changes nothing (give set, add or remove)", opName));
        }
        out_.ops.push_back(std::move(op));
    }

    const Origin& origin_;
    PatchSet& out_;
    std::vector<std::string>& errors_;
};

} // namespace

std::optional<Node> parsePatchToml(std::string_view text, const Origin& origin, std::vector<std::string>& errors) {
    try {
        const toml::table root = toml::parse(text, std::string_view(origin.file));
        return fromToml(root);
    } catch (const toml::parse_error& e) {
        errors.push_back(std::format("{}: {}", origin.at(static_cast<int>(e.source().begin.line)), e.description()));
        return std::nullopt;
    }
}

Node nodeFromValue(const script::Value& value) {
    Node out;
    switch (value.kind()) {
        case script::Kind::Null: break;
        case script::Kind::Bool:
            out.type = Node::Type::Bool;
            out.b = value.asBool();
            break;
        case script::Kind::Int:
            out.type = Node::Type::Int;
            out.i = value.asInt();
            break;
        case script::Kind::String:
            out.type = Node::Type::String;
            out.s = value.asString();
            break;
        case script::Kind::List:
            out.type = Node::Type::List;
            for (const script::Value& e : value.asList()) out.list.push_back(nodeFromValue(e));
            break;
        case script::Kind::Map:
            out.type = Node::Type::Map;
            for (const auto& [k, v] : value.asMap()) out.map.emplace_back(k, nodeFromValue(v));
            break;
    }
    return out;
}

void parsePatch(const Node& root, const Origin& origin, PatchSet& out, std::vector<std::string>& errors) { Parser(origin, out, errors).root(root); }

} // namespace opense4::mods
