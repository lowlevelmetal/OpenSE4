#include "server/setup_file.hpp"

#include "datafile/datafile.hpp"
#include "game/ai_data.hpp"
#include "net/auth.hpp"

#include <toml++/toml.hpp>

#include <algorithm>
#include <array>
#include <format>
#include <fstream>
#include <limits>
#include <sstream>
#include <type_traits>

namespace opense4::server {

namespace {

struct IntOption {
    std::string_view key;
    int game::GameOptions::*member;
    int min;
    int max;
};

struct BoolOption {
    std::string_view key;
    bool game::GameOptions::*member;
};

using O = game::GameOptions;

constexpr std::array kIntOptions{
    IntOption{"systems", &O::systemCount, 0, 500},          // 0: rolled from quadrant_size (spec 01 §2.2)
    IntOption{"quadrant_size", &O::quadrantSize, 0, 2},
    IntOption{"events", &O::eventFrequency, 0, 3},
    IntOption{"max_event_severity", &O::maxEventSeverity, 0, 3},
    IntOption{"tech_cost", &O::techCost, 0, 2},
    IntOption{"start_tech", &O::startTechLevel, 0, 2},
    IntOption{"racial_points", &O::racialPoints, 0, 100000},
    IntOption{"home_planet_value", &O::homePlanetValue, 0, 2},
    IntOption{"starting_planets", &O::startingPlanets, 1, 20},
    IntOption{"max_ships", &O::maxShipsPerPlayer, 1, 100000},
    IntOption{"max_units", &O::maxUnitsPerPlayer, 1, 1000000},
    IntOption{"ai_difficulty", &O::aiDifficulty, 0, 2},     // Low, Medium, High (spec 05 §7.1)
    IntOption{"ai_bonus", &O::aiBonus, 0, 3},               // None, Low, Medium, High
    IntOption{"score_display", &O::scoreDisplay, 0, 2},
};

constexpr std::array kBoolOptions{
    BoolOption{"all_warp_points_connected", &O::allWarpPointsConnected},
    BoolOption{"all_planets_same_size", &O::allPlanetsSameSize},
    BoolOption{"no_warp_points", &O::noWarpPoints},
    BoolOption{"warp_points_anywhere", &O::warpPointsAnywhere},
    BoolOption{"all_systems_seen", &O::allSystemsSeen},
    BoolOption{"omnipresent", &O::omnipresent},
    BoolOption{"finite_resources", &O::finiteResources},
    BoolOption{"same_system_allowed", &O::sameSystemAllowed},
    BoolOption{"evenly_distributed", &O::evenlyDistributed},
    BoolOption{"no_tactical_combat", &O::noTacticalCombat},
    BoolOption{"allow_gifts", &O::allowGifts},
    BoolOption{"allow_tech_trades", &O::allowTechTrades},
    BoolOption{"allow_intel", &O::allowIntel},
    BoolOption{"allow_surrender", &O::allowSurrender},
    BoolOption{"no_ruins", &O::noRuins},
    BoolOption{"only_breathable", &O::onlyBreathable},
    BoolOption{"only_home_type", &O::onlyHomeType},
    BoolOption{"team_mode", &O::teamMode},
    BoolOption{"simultaneous", &O::simultaneous},
};

std::string where(const std::string& source, const toml::node& n) {
    const auto line = n.source().begin.line;
    return line ? std::format("{}:{}", source, line) : source;
}

class Parser {
public:
    Parser(std::string source, const game::Rules& rules) : source_(std::move(source)), rules_(rules) {}

    std::expected<SetupFile, std::string> run(const toml::table& root) {
        allowOnly(root, "", {"name", "seed", "master_password", "master_password_hash", "options", "empire"});
        out_.gameName = string(root, "name").value_or("OpenSE4 game");
        if (const toml::node* n = root.get("seed")) {
            if (const auto* i = n->as_integer()) out_.seed = static_cast<uint64_t>(i->get());
            else error(*n, "'seed' must be an integer");
        }
        if (auto pw = string(root, "master_password")) out_.masterPasswordHash = net::hashPassword(*pw);
        if (auto h = string(root, "master_password_hash")) out_.masterPasswordHash = *h;
        if (const toml::node* n = root.get("options")) {
            if (const auto* t = n->as_table()) options(*t);
            else error(*n, "[options] must be a table");
        }
        if (const toml::node* n = root.get("empire")) {
            const auto* arr = n->as_array();
            if (!arr) error(*n, "empires are written as [[empire]] tables");
            else
                for (const toml::node& e : *arr) {
                    if (const auto* t = e.as_table()) empire(*t);
                    else error(e, "empires are written as [[empire]] tables");
                }
        }
        if (!errors_.empty()) {
            std::string all;
            for (const auto& e : errors_) all += (all.empty() ? "" : "\n") + e;
            return std::unexpected(all);
        }
        return std::move(out_);
    }

private:
    void error(const toml::node& n, std::string_view message) { errors_.push_back(std::format("{}: {}", where(source_, n), message)); }

    void allowOnly(const toml::table& t, std::string_view table, std::initializer_list<std::string_view> keys) {
        for (auto&& [key, node] : t)
            if (std::find(keys.begin(), keys.end(), key.str()) == keys.end())
                error(node, std::format("unknown key '{}'{}", key.str(), table.empty() ? std::string{} : std::format(" in [{}]", table)));
    }

    std::optional<std::string> string(const toml::table& t, std::string_view key) {
        const toml::node* n = t.get(key);
        if (!n) return std::nullopt;
        if (const auto* s = n->as_string()) return s->get();
        error(*n, std::format("'{}' must be a string", key));
        return std::nullopt;
    }

    std::optional<int64_t> integer(const toml::table& t, std::string_view key, int64_t min, int64_t max) {
        const toml::node* n = t.get(key);
        if (!n) return std::nullopt;
        const auto* i = n->as_integer();
        if (!i) {
            error(*n, std::format("'{}' must be an integer", key));
            return std::nullopt;
        }
        if (i->get() < min || i->get() > max) {
            error(*n, std::format("'{}' must be between {} and {}", key, min, max));
            return std::nullopt;
        }
        return i->get();
    }

    void options(const toml::table& t) {
        std::vector<std::string_view> keys{"quadrant", "starting_resources", "victory"};
        for (const auto& o : kIntOptions) keys.push_back(o.key);
        for (const auto& o : kBoolOptions) keys.push_back(o.key);
        for (auto&& [key, node] : t)
            if (std::find(keys.begin(), keys.end(), key.str()) == keys.end()) error(node, std::format("unknown option '{}'", key.str()));

        game::GameOptions& o = out_.options;
        if (auto q = string(t, "quadrant")) {
            const auto& list = rules_.data().quadrantTypes;
            if (std::none_of(list.begin(), list.end(), [&](const auto& x) { return x.name == *q; }))
                error(*t.get("quadrant"), std::format("the data set has no quadrant type '{}'", *q));
            o.quadrantType = *q;
        }
        for (const auto& opt : kIntOptions)
            if (auto v = integer(t, opt.key, opt.min, opt.max)) o.*opt.member = static_cast<int>(*v);
        for (const auto& opt : kBoolOptions)
            if (const toml::node* n = t.get(opt.key)) {
                if (const auto* b = n->as_boolean()) o.*opt.member = b->get();
                else error(*n, std::format("'{}' must be true or false", opt.key));
            }
        if (const toml::node* n = t.get("starting_resources")) {
            const auto* arr = n->as_array();
            if (!arr || arr->size() != 3 || !arr->is_homogeneous(toml::node_type::integer)) {
                error(*n, "'starting_resources' must be three integers (minerals, organics, radioactives)");
            } else {
                for (size_t i = 0; i < 3; ++i) o.startingResources.v[i] = std::max<int64_t>(0, arr->get(i)->as_integer()->get());
            }
        }
        if (const toml::node* n = t.get("victory")) {
            const auto* v = n->as_table();
            if (!v) {
                error(*n, "[options.victory] must be a table");
                return;
            }
            allowOnly(*v, "options.victory", {"score", "years", "percent_of_second", "tech_percent", "peace_years", "delay_years"});
            game::VictoryConditions& vc = o.victory;
            // A key switches its condition on with that value.
            auto condition = [&](std::string_view key, int64_t min, int64_t max, bool& on, auto& value) {
                if (auto x = integer(*v, key, min, max)) {
                    on = true;
                    value = static_cast<std::remove_reference_t<decltype(value)>>(*x);
                }
            };
            condition("score", 1, std::numeric_limits<int64_t>::max(), vc.score, vc.scoreValue);
            condition("years", 1, 10000, vc.years, vc.yearsValue);
            condition("percent_of_second", 101, 100000, vc.percentOfSecond, vc.percentOfSecondValue);
            condition("tech_percent", 1, 100, vc.techPercent, vc.techPercentValue);
            condition("peace_years", 1, 10000, vc.peace, vc.peaceYears);
            condition("delay_years", 0, 10000, vc.delay, vc.delayYears);
        }
    }

    void empire(const toml::table& t) {
        allowOnly(t, "empire", {"name", "race", "tier", "kind", "player", "password", "password_hash", "color", "empire_type", "leader",
                                "leader_title", "minister_style", "use_race_minister_style"});
        SetupEmpire e;
        game::EmpireSetup& s = e.setup;
        s.name = string(t, "name").value_or("");
        s.empireType = string(t, "empire_type").value_or("");
        s.leaderName = string(t, "leader").value_or("");
        s.leaderTitle = string(t, "leader_title").value_or("");
        if (auto race = string(t, "race")) {
            if (!game::findPreset(rules_, *race)) error(*t.get("race"), std::format("the data set has no race '{}'", *race));
            s.preset = *race;
        }
        if (auto tier = integer(t, "tier", 0, 2)) s.presetTier = static_cast<int>(*tier);
        if (auto kind = string(t, "kind")) {
            if (*kind == "human") s.kind = game::PlayerKind::Human;
            else if (*kind == "computer") s.kind = game::PlayerKind::Computer;
            else if (*kind == "neutral") s.kind = game::PlayerKind::Neutral;
            else error(*t.get("kind"), "'kind' must be \"human\", \"computer\" or \"neutral\"");
        }
        if (auto color = integer(t, "color", 0, 0xffffff)) s.color = static_cast<uint32_t>(*color);
        // The minister style (spec 02 §9): a folder under Ai/ of the install.
        if (auto style = string(t, "minister_style")) {
            const std::vector<std::string> styles = game::ai::ministerStyles(rules_);
            const auto known = std::find_if(styles.begin(), styles.end(), [&](const std::string& st) { return datafile::keysEqual(st, *style); });
            if (known == styles.end()) error(*t.get("minister_style"), std::format("the data set has no minister style '{}'", *style));
            else s.ministerStyle = *known;
        }
        if (const toml::node* n = t.get("use_race_minister_style")) {
            if (const auto* b = n->as_boolean()) s.useRaceMinisterStyle = b->get();
            else error(*n, "'use_race_minister_style' must be true or false");
        }
        e.player = string(t, "player").value_or("");
        if (auto pw = string(t, "password")) s.passwordHash = net::hashPassword(*pw);
        if (auto h = string(t, "password_hash")) s.passwordHash = *h;
        out_.empires.push_back(std::move(e));
    }

    std::string source_;
    const game::Rules& rules_;
    SetupFile out_;
    std::vector<std::string> errors_;
};

} // namespace

std::expected<SetupFile, std::string> parseSetup(std::string_view text, const std::string& sourceName, const game::Rules& rules) {
    toml::table root;
    try {
        root = toml::parse(text, sourceName);
    } catch (const toml::parse_error& e) {
        const auto& begin = e.source().begin;
        return std::unexpected(std::format("{}:{}:{}: {}", sourceName, begin.line, begin.column, e.description()));
    }
    return Parser(sourceName, rules).run(root);
}

std::expected<SetupFile, std::string> loadSetupFile(const std::filesystem::path& file, const game::Rules& rules) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return std::unexpected(std::format("{}: cannot open the file", file.string()));
    std::stringstream buffer;
    buffer << in.rdbuf();
    return parseSetup(buffer.str(), file.string(), rules);
}

} // namespace opense4::server
