#include "sdk/sdk_test_util.hpp"

#include "command_samples.hpp"
#include "engine_fixture.hpp"

#include "game/query.hpp"
#include "game/sight.hpp"
#include "game/turn.hpp"

#include <doctest/doctest.h>

#include <filesystem>
#include <format>
#include <fstream>

namespace opense4::sdktest {

using script::Value;

namespace {

std::string trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
    return std::string(s);
}

std::string withoutTicks(std::string s) {
    std::erase(s, '`');
    return s;
}

// The cells of a table row: "| a | b |" gives {"a", "b"}.
std::vector<std::string> cells(std::string_view line) {
    std::vector<std::string> out;
    size_t start = line.find('|');
    while (start != std::string_view::npos) {
        const size_t next = line.find('|', start + 1);
        if (next == std::string_view::npos) break;
        out.push_back(trim(line.substr(start + 1, next - start - 1)));
        start = next;
    }
    return out;
}

void parseFile(Schema& schema, const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in) {
        schema.problems.push_back(std::format("cannot read {}", path.string()));
        return;
    }
    const std::string file = path.filename().string();
    enum class State { Prose, Header, Rows, Done };
    Schema::Section* cur = nullptr;
    State state = State::Prose;
    std::string line;
    int n = 0;
    while (std::getline(in, line)) {
        ++n;
        if (line.starts_with("### ")) {
            cur = nullptr;
            const size_t a = line.find('`');
            const size_t b = a == std::string::npos ? a : line.find('`', a + 1);
            if (b == std::string::npos) continue;   // a heading that names no type
            const std::string name = line.substr(a + 1, b - a - 1);
            if (schema.sections.contains(name)) {
                schema.problems.push_back(std::format("{}:{}: `{}` is documented twice", file, n, name));
                continue;
            }
            cur = &schema.sections[name];
            cur->name = name;
            cur->file = file;
            cur->line = n;
            state = State::Prose;
            continue;
        }
        if (line.starts_with("# ") || line.starts_with("## ")) {
            cur = nullptr;
            continue;
        }
        if (!cur) continue;
        switch (state) {
            case State::Prose:
                if (line.starts_with("| Field |")) {
                    state = State::Header;
                } else if (line.starts_with("| Value |")) {
                    cur->isEnum = true;
                    state = State::Header;
                }
                break;
            case State::Header: state = State::Rows; break;   // the |---| line
            case State::Rows: {
                if (!line.starts_with("|")) {
                    state = State::Done;
                    break;
                }
                const std::vector<std::string> c = cells(line);
                if (c.empty()) break;
                const std::string name = withoutTicks(c[0]);
                if (cur->isEnum) {
                    cur->values.push_back(name);
                } else if (c.size() < 2) {
                    schema.problems.push_back(std::format("{}:{}: a row without a type", file, n));
                } else {
                    for (const auto& row : cur->rows)
                        if (row.field == name) schema.problems.push_back(std::format("{}:{}: `{}` twice in `{}`", file, n, name, cur->name));
                    cur->rows.push_back({name, withoutTicks(c[1])});
                }
                break;
            }
            case State::Done: break;
        }
    }
    for (const auto& [name, s] : schema.sections)
        if (s.file == file && s.rows.empty() && s.values.empty())
            schema.problems.push_back(std::format("{}:{}: `{}` has no table", file, s.line, name));
}

class Validator {
public:
    Validator(const Schema& schema, References* refs) : schema_(schema), refs_(refs) {}

    std::vector<std::string> problems;

    void check(const Value& v, std::string_view type) {
        if (problems.size() > 200) return;
        constexpr std::string_view kNullable = ", or null";
        if (type.ends_with(kNullable)) {
            if (v.isNull()) return;
            type.remove_suffix(kNullable.size());
        }
        if (type.starts_with("list of ")) {
            if (!v.isList()) return fail(std::format("expected a list ({})", type));
            const std::string_view element = type.substr(8);
            for (size_t i = 0; i < v.asList().size(); ++i) {
                const size_t keep = path_.size();
                path_ += std::format("[{}]", i);
                check(v.asList()[i], element);
                path_.resize(keep);
            }
            return;
        }
        if (type == "int") return expect(v.isInt(), type);
        if (type == "bool") return expect(v.isBool(), type);
        if (type == "text") return expect(v.isString(), type);
        if (type == "int or text") return expect(v.isInt() || v.isString(), type);
        if (type == "map") return expect(v.isMap(), type);
        if (type.ends_with(" index") || type.ends_with(" ref")) return expect(v.isInt(), type);
        if (type.ends_with(" id")) {
            if (!v.isInt()) return fail(std::format("expected {}", type));
            if (refs_) refs_->named.emplace_back(std::string(type.substr(0, type.size() - 3)), v.asInt());
            return;
        }
        if (type == "command") {
            const Value* kind = v.find("kind");
            if (!kind || !kind->isString()) return fail("expected a command with a kind");
            const Schema::Section* s = schema_.find(kind->asString());
            if (!s || s->isEnum) return fail(std::format("command kind '{}' is not documented", kind->asString()));
            return checkStruct(v, *s, "kind");
        }
        const Schema::Section* s = schema_.find(type);
        if (!s) return fail(std::format("the docs have no type `{}`", type));
        if (s->isEnum) {
            if (!v.isString()) return fail(std::format("expected a `{}` name", type));
            if (std::find(s->values.begin(), s->values.end(), v.asString()) == s->values.end())
                return fail(std::format("'{}' is not a documented `{}` ({}:{})", v.asString(), type, s->file, s->line));
            return;
        }
        checkStruct(v, *s, {});
    }

private:
    const Schema& schema_;
    References* refs_;
    std::string path_;

    void fail(std::string message) { problems.push_back(std::format("{}: {}", path_.empty() ? "(top)" : path_, message)); }
    void expect(bool ok, std::string_view type) {
        if (!ok) fail(std::format("expected {}", type));
    }

    void checkStruct(const Value& v, const Schema::Section& s, std::string_view skip) {
        if (!v.isMap()) return fail(std::format("expected a map (`{}`)", s.name));
        for (const auto& [key, x] : v.asMap()) {
            if (key == skip) continue;
            const auto row = std::find_if(s.rows.begin(), s.rows.end(), [&](const Schema::Row& r) { return r.field == key; });
            if (row == s.rows.end()) {
                fail(std::format("`{}` is not a documented field of `{}` ({}:{})", key, s.name, s.file, s.line));
                continue;
            }
            const size_t keep = path_.size();
            path_ += (path_.empty() ? "" : ".") + key;
            check(x, row->type);
            path_.resize(keep);
        }
        for (const Schema::Row& row : s.rows)
            if (!v.find(row.field)) fail(std::format("the documented field `{}` of `{}` is missing", row.field, s.name));
    }
};

} // namespace

const Schema& docsSchema() {
    static const Schema schema = [] {
        Schema s;
        const std::filesystem::path dir = std::filesystem::path(OPENSE4_DOCS_DIR) / "sdk";
        parseFile(s, dir / "commands.md");
        parseFile(s, dir / "view.md");
        return s;
    }();
    return schema;
}

std::vector<std::string> validate(const Schema& schema, const Value& v, std::string_view type, References* refs) {
    Validator val(schema, refs);
    val.check(v, type);
    return std::move(val.problems);
}

std::set<int64_t> idsOf(const Value& list) {
    std::set<int64_t> out;
    if (!list.isList()) return out;
    for (const Value& x : list.asList())
        if (const Value* i = x.find("id"); i && i->isInt()) out.insert(i->asInt());
    return out;
}

std::string joined(const std::vector<std::string>& lines, size_t limit) {
    std::string out;
    for (size_t i = 0; i < lines.size() && i < limit; ++i) out += (i ? "\n" : "") + lines[i];
    if (lines.size() > limit) out += std::format("\n... and {} more", lines.size() - limit);
    return out;
}

SdkGame sdkGame() {
    const game::Rules& r = test::engineRules();
    SdkGame g;
    game::GameState& s = g.state;
    s = test::newEngineGame(11, 3, 12, false);
    test::addHomeShips(s, r);
    for (int round = 0; round < 5; ++round) {
        std::vector<game::EmpireOrders> orders{test::busyOrders(r, s, game::EmpireId{0u}, round)};
        game::processTurn(r, s, orders);
    }
    const game::EmpireId me{0u}, other{1u};
    REQUIRE_FALSE(s.empire(other).designs.empty());
    const game::DesignId theirs = s.empire(other).designs.front();
    const game::Location home = game::locationOf(s.galaxy, test::homeworld(s, me).planet);
    g.seenForeign = test::addTestVehicle(s, r, theirs, home).id;
    for (const game::StarSystem& sys : s.galaxy.systems)
        if (!s.empire(me).hasExplored(sys.id)) g.unexplored = sys.id;
    REQUIRE(g.unexplored.valid());
    g.hiddenForeign = test::addTestVehicle(s, r, theirs, {g.unexplored, game::Sector{3, 3}}).id;
    game::sight::updateKnowledge(r, s);
    REQUIRE_FALSE(s.empire(me).hasExplored(g.unexplored));

    game::CombatRecord battle;
    battle.turn = s.turn;
    battle.location = home;
    battle.participants = {me, other};
    battle.pieces.push_back({game::CombatPiece::Kind::Vehicle, me, game::VehicleId{9999u}, {}, s.empire(me).designs.front(), "Lost Scout", 0, 0});
    battle.pieces.push_back({game::CombatPiece::Kind::Vehicle, other, g.seenForeign, {}, theirs, "Raider", 0, 0});
    battle.pieces[0].damage = 100;
    battle.pieces[1].survivor = other;
    battle.summary = {"A skirmish at home."};
    s.combats.push_back(battle);
    // A proposal waiting for an answer, and an intelligence project.
    game::DiplomaticMessage offer;
    offer.id = game::MessageId{s.nextMessageId++};
    offer.from = other;
    offer.to = me;
    offer.sentTurn = s.turn;
    offer.type = game::MessageType::ProposeTreaty;
    offer.treaty = game::Treaty::NonAggression;
    offer.text = "Shall we stop shooting?";
    offer.delivered = true;
    offer.dated = s.turn;
    s.messages.push_back(offer);
    game::addLog(s, me, game::LogCategory::Politics, "Proposal", "A treaty is offered.")->message = offer.id;
    s.empire(me).intel.push_back(game::IntelProjectOrder{0, other, {}, {}, {}, {}, 5});
    s.empire(me).knowledge.notes.assign(s.galaxy.systems.size(), {});
    s.empire(me).knowledge.notes[0] = "Home sweet home";
    return g;
}

const Value& at(const Value& v, std::string_view key) {
    static const Value kNull;
    const Value* x = v.find(key);
    REQUIRE_MESSAGE(x != nullptr, "missing key " << std::string(key) << " in " << script::describe(v));
    return x ? *x : kNull;
}

int64_t intAt(const Value& v, std::string_view key) {
    const Value& x = at(v, key);
    REQUIRE_MESSAGE(x.isInt(), std::string(key) << " is not an int: " << script::describe(x));
    return x.asInt();
}

} // namespace opense4::sdktest
