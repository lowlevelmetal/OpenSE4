#include "client/classic/screens/setup_model.hpp"

#include "datafile/datafile.hpp"
#include "game/ai.hpp"
#include "game/ai_data.hpp"
#include "game/economy.hpp"
#include "ruleset/ruleset.hpp"

#include <toml++/toml.hpp>

#include <algorithm>
#include <array>
#include <climits>
#include <format>
#include <fstream>
#include <sstream>

namespace opense4::client::classic::setup {

namespace {

using datafile::keysEqual;

constexpr std::array<std::string_view, 3> kLevelNames{"Low", "Medium", "High"};
// 2: designs saved with the empire (spec 06 §7 Q48); 3: the combat strategies,
// each design's strategy and creation date, no obsolete mark (§7 Q72).
// Format 1 and 2 files still load.
constexpr int kEmpireFileFormat = 3;

bool isNone(std::string_view s) { return s.empty() || keysEqual(s, "None"); }

std::optional<uint32_t> findTrait(const game::Rules& r, std::string_view name) {
    const auto& traits = r.data().racialTraits;
    for (uint32_t i = 0; i < traits.size(); ++i)
        if (keysEqual(traits[i].name, name)) return i;
    return std::nullopt;
}

bool hasTraitNamed(const game::Rules& r, const game::Race& race, std::string_view name) {
    for (uint32_t t : race.traits)
        if (t < r.data().racialTraits.size() && keysEqual(r.data().racialTraits[t].name, name)) return true;
    return false;
}

template <class T>
std::optional<uint32_t> indexByName(const std::vector<T>& list, std::string_view name) {
    for (uint32_t i = 0; i < list.size(); ++i)
        if (keysEqual(list[i].name, name)) return i;
    return std::nullopt;
}

void addUnique(std::vector<std::string>& list, std::string_view value) {
    if (value.empty()) return;
    for (const auto& v : list)
        if (keysEqual(v, value)) return;
    list.emplace_back(value);
}

// An empire's name as game::createGame will show it.
std::string effectiveName(const game::Rules& r, const game::EmpireSetup& e) {
    if (!e.name.empty()) return e.name;
    if (const ruleset::RacePreset* p = presetOf(r, e)) return p->empireName.empty() ? p->name : p->empireName;
    return e.customRace ? e.customRace->name : std::string{};
}

bool nameTaken(const std::vector<std::string>& names, std::string_view name) {
    for (const auto& n : names)
        if (keysEqual(n, name)) return true;
    return false;
}

std::string lowerAscii(std::string_view s) {
    std::string out(s);
    for (char& c : out)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return out;
}

// Picks a race preset for a random player (spec 05 §7.1): the personality
// group furthest below its target share, then an unused race of it.
const ruleset::RacePreset* pickRandomPreset(const game::Rules& r, Rng& rng, bool neutral, const std::vector<std::string>& usedStyles) {
    return game::ai::pickRandomRace(r, rng, neutral, usedStyles);
}

// Names the preset tier when the race is unmodified, keeps the custom race otherwise.
game::EmpireSetup collapse(const game::Rules& r, const EmpireDraft& d) {
    game::EmpireSetup out = d.setup;
    out.customRace.reset();
    if (const ruleset::RacePreset* p = presetOf(r, out)) {
        const int tiers = std::max<int>(1, static_cast<int>(p->tiers.size()));
        const int first = std::clamp(out.presetTier, 0, tiers - 1);
        if (sameRace(d.race, game::raceFromPreset(r, *p, first))) {
            out.presetTier = first;
            return out;
        }
        for (int t = 0; t < tiers; ++t)
            if (sameRace(d.race, game::raceFromPreset(r, *p, t))) {
                out.presetTier = t;
                return out;
            }
    }
    out.customRace = d.race;
    return out;
}

std::string fileStem(std::string_view name) {
    std::string out;
    for (char c : name) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
        if (ok) out += c;
        else if (c == ' ' && !out.empty() && out.back() != '_') out += '_';
    }
    while (!out.empty() && out.back() == '_') out.pop_back();
    return out.empty() ? std::string("Empire") : out;
}

} // namespace

// ---- Settings ----------------------------------------------------------------------------

NewGameSettings defaultSettings(const game::Rules& r, uint64_t seed) {
    NewGameSettings s;
    s.seed = seed ? seed : 1;
    game::GameOptions& o = s.options;
    if (!r.data().quadrantTypes.empty()) o.quadrantType = r.data().quadrantTypes.front().name;
    o.systemCount = 0;  // rolled from the Quadrant Size, as the original does
    o.maxShipsPerPlayer = static_cast<int>(r.setting("Default Number Of Ships Per Player", o.maxShipsPerPlayer));
    o.maxUnitsPerPlayer = static_cast<int>(r.setting("Default Number Of Units Per Player", o.maxUnitsPerPlayer));
    // The empire list starts empty (observed, spec 07 session 5).
    return s;
}

std::vector<size_t> quickStartStyles(const game::Rules& r) {
    std::vector<size_t> out;
    const auto& all = r.racePresets();
    auto add = [&](const ruleset::RacePreset* p) {
        if (!p || p->neutral) return;
        const size_t i = static_cast<size_t>(p - all.data());
        if (std::find(out.begin(), out.end(), i) == out.end()) out.push_back(i);
    };
    const int styles = static_cast<int>(r.setting("Number of Quick Start Styles", 0));
    for (int i = 1; i <= styles; ++i)
        if (auto style = r.data().settings.text(std::format("Quick Start Style {}", i))) add(game::findPreset(r, *style));
    if (out.empty())
        for (const auto& p : all) add(&p);
    return out;
}

std::optional<game::EmpireSetup> firstStyleEmpire(const game::Rules& r, int racialPoints) {
    const std::vector<size_t> styles = quickStartStyles(r);
    if (styles.empty()) return std::nullopt;
    const ruleset::RacePreset& first = r.racePresets()[styles.front()];
    return collapse(r, draftFromPreset(r, first, bestTierWithin(r, first, racialPoints)));
}

std::optional<std::string> autosaveName(int everyTurns, uint32_t turn) {
    // Saved when the turns since 2400.0 are a multiple of N, into the file of
    // that number's last digit (spec 01 §2.2, §14 Q38, confirmed: binary).
    if (everyTurns <= 0 || turn == 0 || turn % static_cast<uint32_t>(everyTurns) != 0) return std::nullopt;
    return std::format("AutoSav{}", turn % 10);
}

int maxSystems(const game::Rules& r) { return game::maxSystemCount(r.data()); }

std::pair<int, int> quadrantSizeRange(const game::Rules& r, int quadrantSize) {
    return game::systemCountRange(r.data(), static_cast<game::QuadrantSize>(std::clamp(quadrantSize, 0, 2)));
}

game::QuadrantOptions quadrantOptions(const game::GameOptions& o) {
    game::QuadrantOptions q;
    q.quadrantType = o.quadrantType;
    q.systemCount = o.systemCount;
    q.size = static_cast<game::QuadrantSize>(std::clamp(o.quadrantSize, 0, 2));
    q.allWarpPointsConnected = o.allWarpPointsConnected;
    q.noWarpPoints = o.noWarpPoints;
    q.warpPointsAnywhere = o.warpPointsAnywhere;
    q.noRuins = o.noRuins;
    q.finiteResources = o.finiteResources;
    return q;
}

std::expected<game::Generated, std::string> previewQuadrant(const game::Rules& r, uint64_t seed, const game::GameOptions& o) {
    // Mirrors game::createGame: the game rng is seeded, and the quadrant uses its first fork.
    Rng rng;
    rng.reseed(seed);
    Rng galaxyRng = rng.fork();
    return game::generateQuadrant(r.data(), quadrantOptions(o), galaxyRng);
}

// ---- Maps --------------------------------------------------------------------------------

void useMap(NewGameSettings& s, game::QuadrantMap map) { s.map = std::move(map); }

void clearMap(NewGameSettings& s) { s.map.reset(); }

game::QuadrantMap mapToSave(const NewGameSettings& s, const game::Galaxy& preview, std::string name) {
    game::QuadrantMap m;
    if (s.map) m = *s.map;
    else m.galaxy = preview;
    m.name = std::move(name);
    return m;
}

std::vector<MapFileInfo> listMapFiles(const game::Rules& r, const std::filesystem::path& dir) {
    std::vector<MapFileInfo> out;
    std::error_code ec;
    for (const auto& f : std::filesystem::directory_iterator(dir, ec)) {
        if (!f.is_regular_file(ec) || lowerAscii(f.path().extension().string()) != game::kMapExtension) continue;
        auto loaded = game::loadMapFile(f.path(), r.data());
        if (!loaded) continue;
        out.push_back({f.path(), loaded->map.name, static_cast<int>(loaded->map.galaxy.systems.size()),
                       static_cast<int>(loaded->map.startingPoints.size())});
    }
    // Total orders throughout: the directory lists files in an order that differs
    // between platforms, and std::sort leaves ties in a library's own order.
    std::sort(out.begin(), out.end(), [](const MapFileInfo& a, const MapFileInfo& b) {
        return std::pair(lowerAscii(a.name), a.path.filename()) < std::pair(lowerAscii(b.name), b.path.filename());
    });
    return out;
}

std::filesystem::path mapFilePath(const std::filesystem::path& dir, std::string_view name) {
    // A file whose name differs only in case is the same file, as on Windows.
    return ruleset::childIgnoringCase(dir, game::mapFileStem(name) + std::string(game::kMapExtension));
}

// ---- Players -----------------------------------------------------------------------------

std::pair<int, int> randomPlayerRange(const game::Rules& r, bool neutral, int level) {
    // Our fallbacks when a data set has no such keys.
    static constexpr std::array<std::pair<int, int>, 3> kFallback{{{1, 2}, {3, 5}, {6, 9}}};
    const size_t l = static_cast<size_t>(std::clamp(level, 0, 2));
    const char* who = neutral ? "Neutral" : "Computer";
    const int lo = static_cast<int>(r.setting(std::format("Minimum {} Player {} Setting", who, kLevelNames[l]), kFallback[l].first));
    const int hi = static_cast<int>(r.setting(std::format("Maximum {} Player {} Setting", who, kLevelNames[l]), kFallback[l].second));
    return {std::max(0, lo), std::max({0, lo, hi})};
}

std::expected<game::GameSetup, std::string> buildGameSetup(const game::Rules& r, const NewGameSettings& s) {
    game::GameSetup g;
    g.seed = s.seed;
    g.options = s.options;
    g.map = s.map;
    game::GameOptions& o = g.options;
    if (o.systemCount > 0) o.systemCount = std::min(o.systemCount, maxSystems(r));  // 0: rolled from the Quadrant Size
    o.quadrantSize = std::clamp(o.quadrantSize, 0, 2);
    o.victory.percentOfSecondValue = std::max(100, o.victory.percentOfSecondValue);
    o.victory.techPercentValue = std::clamp(o.victory.techPercentValue, 1, 100);
    o.startingPlanets = std::max(1, o.startingPlanets);
    // Technology Areas Allowed: one flag per area; areas that cannot be removed stay allowed.
    const auto& areas = r.data().techAreas;
    if (!o.techAreasAllowed.empty()) {
        o.techAreasAllowed.resize(areas.size(), 1);
        for (size_t i = 0; i < areas.size(); ++i)
            if (!areas[i].canBeRemoved) o.techAreasAllowed[i] = 1;
        if (std::all_of(o.techAreasAllowed.begin(), o.techAreasAllowed.end(), [](uint8_t v) { return v != 0; })) o.techAreasAllowed.clear();
    }

    std::vector<std::string> names;
    int humans = 0;
    for (const game::EmpireSetup& e : s.players) {
        const std::string name = effectiveName(r, e);
        if (name.empty()) return std::unexpected("Every empire needs a name.");
        if (nameTaken(names, name)) return std::unexpected(std::format("Two empires are named {}; rename one of them.", name));
        const int cost = game::racialPointCost(r, raceOf(r, e));
        if (cost > o.racialPoints)
            return std::unexpected(std::format("{} spends {} racial points, but this game allows {}. Edit the empire or raise Racial Points.",
                                               name, cost, o.racialPoints));
        if (e.kind == game::PlayerKind::Human) ++humans;
        names.push_back(name);
        g.empires.push_back(e);
    }
    if (humans == 0) return std::unexpected("A game needs at least one human player. Add an empire or switch one to human.");
    if (static_cast<int>(g.empires.size()) > kMaxEmpires)
        return std::unexpected(std::format("A game holds at most {} empires.", kMaxEmpires));
    addRandomPlayers(r, g, s.computers, s.neutrals);
    return g;
}

void addRandomPlayers(const game::Rules& r, game::GameSetup& g, const RandomPlayers& computers, const RandomPlayers& neutrals) {
    game::GameOptions& o = g.options;
    std::vector<std::string> names, styles;
    for (const game::EmpireSetup& e : g.empires) {
        names.push_back(effectiveName(r, e));
        if (const ruleset::RacePreset* p = presetOf(r, e)) styles.push_back(p->folder);
    }
    // Random computer and neutral players, rolled from the seed.
    Rng rng(g.seed ^ 0x6a09e667f3bcc909ull);
    for (const bool neutral : {false, true}) {
        const RandomPlayers& rp = neutral ? neutrals : computers;
        if (!rp.enabled) continue;
        const auto [lo, hi] = randomPlayerRange(r, neutral, rp.level);
        const int count = rng.rangeInt(lo, hi);
        for (int i = 0; i < count && static_cast<int>(g.empires.size()) < kMaxEmpires; ++i) {
            const ruleset::RacePreset* p = pickRandomPreset(r, rng, neutral, styles);
            if (!p) break;
            game::EmpireSetup e;
            e.preset = p->folder;
            // The preset's Race Opt set of the racial-point level (spec 05 §7.1).
            e.customRace = game::ai::randomPlayerRace(r, *p, o.racialPoints, rng);
            e.kind = neutral ? game::PlayerKind::Neutral : game::PlayerKind::Computer;
            // Only random players get the chosen Computer Player Difficulty.
            o.randomAiPlayers.resize(g.empires.size() + 1, 0);
            o.randomAiPlayers[g.empires.size()] = 1;
            std::string name = effectiveName(r, e);
            for (int n = 2; nameTaken(names, name); ++n) name = std::format("{} {}", effectiveName(r, e), n);
            e.name = name;
            names.push_back(name);
            styles.push_back(p->folder);
            g.empires.push_back(std::move(e));
        }
    }
}

game::GameSetup quickStartGame(const game::Rules& r, std::string_view preset, uint64_t seed, std::optional<int> opponents) {
    game::GameSetup setup;
    setup.seed = seed;
    game::EmpireSetup me;
    me.preset = std::string(preset);
    me.kind = game::PlayerKind::Human;
    setup.empires.push_back(me);
    if (!opponents) {
        const NewGameSettings fresh = defaultSettings(r, seed);
        setup.options = fresh.options;
        addRandomPlayers(r, setup, fresh.computers, fresh.neutrals);
        return setup;
    }
    // Every other setting keeps its default (a rolled Medium quadrant).
    std::vector<std::string> pool;
    for (const auto& p : r.racePresets())
        if (!p.neutral && !keysEqual(p.folder, preset)) pool.push_back(p.folder);
    Rng rng(seed ^ 0x9e3779b97f4a7c15ull);
    rng.shuffle(pool);
    for (int i = 0; i < *opponents && static_cast<size_t>(i) < pool.size(); ++i) {
        game::EmpireSetup e;
        e.preset = pool[static_cast<size_t>(i)];
        e.kind = game::PlayerKind::Computer;
        setup.empires.push_back(e);
    }
    return setup;
}

// ---- Races --------------------------------------------------------------------------------

const ruleset::RacePreset* presetOf(const game::Rules& r, const game::EmpireSetup& e) {
    return e.preset.empty() ? nullptr : game::findPreset(r, e.preset);
}

game::Race raceOf(const game::Rules& r, const game::EmpireSetup& e) {
    if (e.customRace) return *e.customRace;
    if (const ruleset::RacePreset* p = presetOf(r, e)) return game::raceFromPreset(r, *p, e.presetTier);
    game::Race race;
    race.name = e.name;
    return race;
}

bool sameRace(const game::Race& a, const game::Race& b) {
    auto sorted = [](std::vector<uint32_t> v) {
        std::sort(v.begin(), v.end());
        v.erase(std::unique(v.begin(), v.end()), v.end());
        return v;
    };
    return a.name == b.name && a.style == b.style && a.biology == b.biology && a.society == b.society && a.history == b.history &&
           a.characteristics == b.characteristics && sorted(a.traits) == sorted(b.traits) && a.culture == b.culture &&
           a.happinessModel == b.happinessModel && keysEqual(a.nativeSurface, b.nativeSurface) && keysEqual(a.atmosphere, b.atmosphere) &&
           a.demeanor == b.demeanor && keysEqual(a.designNameFile, b.designNameFile);
}

std::vector<int> tierCosts(const game::Rules& r, const ruleset::RacePreset& p) {
    std::vector<int> out;
    for (size_t t = 0; t < p.tiers.size(); ++t) out.push_back(game::racialPointCost(r, game::raceFromPreset(r, p, static_cast<int>(t))));
    return out;
}

int bestTierWithin(const game::Rules& r, const ruleset::RacePreset& p, int budget) {
    int best = 0, bestCost = -1;
    const std::vector<int> costs = tierCosts(r, p);
    for (size_t t = 0; t < costs.size(); ++t)
        if (costs[t] <= budget && costs[t] > bestCost) {
            best = static_cast<int>(t);
            bestCost = costs[t];
        }
    return best;
}

CharacteristicLimits characteristicLimits(const game::Rules& r, game::Characteristic c) {
    const std::string name{game::displayName(c)};
    CharacteristicLimits l;
    l.min = static_cast<int>(r.setting(std::format("Characteristic {} Min Pct", name), l.min));
    l.max = static_cast<int>(r.setting(std::format("Characteristic {} Max Pct", name), l.max));
    if (l.max < l.min) std::swap(l.min, l.max);
    return l;
}

int characteristicCost(const game::Rules& r, game::Characteristic c, int value) {
    game::Race probe;  // every characteristic at 100 %, no traits
    probe.characteristics[static_cast<size_t>(c)] = value;
    return game::racialPointCost(r, probe);
}

int racialPointsLeft(const game::Rules& r, const game::Race& race, int budget) { return budget - game::racialPointCost(r, race); }

bool hasTrait(const game::Race& race, uint32_t trait) { return std::find(race.traits.begin(), race.traits.end(), trait) != race.traits.end(); }

TraitCheck canAddTrait(const game::Rules& r, const game::Race& race, uint32_t trait) {
    const auto& traits = r.data().racialTraits;
    if (trait >= traits.size()) return {false, "Unknown trait"};
    if (hasTrait(race, trait)) return {};
    const ruleset::RacialTrait& t = traits[trait];
    for (const auto& need : t.requiredTraits)
        if (!isNone(need) && !hasTraitNamed(r, race, need)) return {false, std::format("Requires {}", need)};
    for (const auto& no : t.restrictedTraits)
        if (!isNone(no) && hasTraitNamed(r, race, no)) return {false, std::format("Cannot be combined with {}", no)};
    for (uint32_t other : race.traits) {
        if (other >= traits.size()) continue;
        for (const auto& no : traits[other].restrictedTraits)
            if (!isNone(no) && keysEqual(no, t.name)) return {false, std::format("Cannot be combined with {}", traits[other].name)};
    }
    return {};
}

bool addTrait(const game::Rules& r, game::Race& race, uint32_t trait) {
    if (hasTrait(race, trait)) return true;
    if (!canAddTrait(r, race, trait).ok) return false;
    race.traits.push_back(trait);
    return true;
}

void removeTrait(const game::Rules& r, game::Race& race, uint32_t trait) {
    std::erase(race.traits, trait);
    // Drop whatever no longer has its requirements, until nothing changes.
    const auto& traits = r.data().racialTraits;
    for (bool changed = true; changed;) {
        changed = false;
        for (size_t i = 0; i < race.traits.size(); ++i) {
            const uint32_t t = race.traits[i];
            if (t >= traits.size()) continue;
            const bool missing = std::any_of(traits[t].requiredTraits.begin(), traits[t].requiredTraits.end(),
                                             [&](const std::string& need) { return !isNone(need) && !hasTraitNamed(r, race, need); });
            if (missing) {
                race.traits.erase(race.traits.begin() + static_cast<std::ptrdiff_t>(i));
                changed = true;
                break;
            }
        }
    }
}

// ---- Empire drafts -----------------------------------------------------------------------------

EmpireDraft draftFromPreset(const game::Rules& r, const ruleset::RacePreset& p, int tier) {
    EmpireDraft d;
    d.setup.name = p.empireName.empty() ? p.name : p.empireName;
    d.setup.empireType = p.empireType;
    d.setup.leaderTitle = p.emperorTitle;
    d.setup.leaderName = p.emperorName;
    d.setup.preset = p.folder;
    d.setup.presetTier = std::clamp(tier, 0, std::max(0, static_cast<int>(p.tiers.size()) - 1));
    d.setup.kind = game::PlayerKind::Human;
    d.race = game::raceFromPreset(r, p, d.setup.presetTier);
    return d;
}

EmpireDraft blankDraft(const game::Rules& r, const ruleset::RacePreset* style) {
    EmpireDraft d;
    if (!style)
        for (const auto& p : r.racePresets())
            if (!p.neutral) {
                style = &p;
                break;
            }
    d.setup.kind = game::PlayerKind::Human;
    game::Race& race = d.race;   // every characteristic at 100 %, no trait
    if (style) {
        d.setup.preset = style->folder;
        race.style = style->folder;
        race.designNameFile = style->designNameFile;
    }
    auto byName = [](const auto& list, std::string_view name) -> uint32_t {
        for (uint32_t i = 0; i < list.size(); ++i)
            if (keysEqual(list[i].name, name)) return i;
        return 0;
    };
    race.culture = byName(r.data().cultures, "Neutral");
    race.happinessModel = byName(r.data().happinessModels, "Peaceful");
    const auto& demeanors = r.data().names.demeanors;
    race.demeanor = demeanors.empty() ? std::string("Neutral") : demeanors.front();
    for (const std::string& dm : demeanors)
        if (keysEqual(dm, "Neutral")) race.demeanor = dm;
    const std::vector<std::string> atm = atmospheresInSetupOrder(r), surf = surfacesInSetupOrder(r);
    race.atmosphere = atm.empty() ? std::string("Oxygen") : atm.front();
    for (const std::string& a : atm)
        if (keysEqual(a, "Oxygen")) race.atmosphere = a;
    race.nativeSurface = surf.empty() ? std::string("Rock") : surf.front();
    for (const std::string& s : surf)
        if (keysEqual(s, "Rock")) race.nativeSurface = s;
    return d;
}

namespace {

std::vector<std::string> inOrder(std::vector<std::string> all, std::initializer_list<std::string_view> order) {
    std::vector<std::string> out;
    for (std::string_view want : order)
        for (const std::string& a : all)
            if (keysEqual(a, want)) addUnique(out, a);
    for (const std::string& a : all) addUnique(out, a);
    return out;
}

} // namespace

std::vector<std::string> atmospheresInSetupOrder(const game::Rules& r) {
    return inOrder(atmospheres(r), {"None", "Methane", "Oxygen", "Hydrogen", "Carbon Dioxide"});
}

std::vector<std::string> surfacesInSetupOrder(const game::Rules& r) { return inOrder(planetSurfaces(r), {"Rock", "Ice", "Gas Giant"}); }

EmpireDraft draftFromSetup(const game::Rules& r, const game::EmpireSetup& e) {
    EmpireDraft d;
    d.setup = e;
    d.race = raceOf(r, e);
    d.setup.customRace.reset();
    if (const ruleset::RacePreset* p = presetOf(r, e)) {
        if (d.setup.name.empty()) d.setup.name = p->empireName.empty() ? p->name : p->empireName;
        if (d.setup.empireType.empty()) d.setup.empireType = p->empireType;
        if (d.setup.leaderTitle.empty()) d.setup.leaderTitle = p->emperorTitle;
        if (d.setup.leaderName.empty()) d.setup.leaderName = p->emperorName;
    }
    return d;
}

std::expected<game::EmpireSetup, std::string> finishDraft(const game::Rules& r, const EmpireDraft& d, int racialPoints) {
    if (d.setup.name.empty()) return std::unexpected("Enter a name for the empire.");
    const int left = racialPointsLeft(r, d.race, racialPoints);
    if (left < 0)
        return std::unexpected(std::format("This race costs {} racial points more than the {} available. Lower a characteristic or drop a trait.",
                                           -left, racialPoints));
    if (!d.race.name.empty()) return collapse(r, d);
    // A race without a name of its own (an empty Add New) takes the empire's.
    EmpireDraft named = d;
    named.race.name = d.setup.name;
    return collapse(r, named);
}

std::string hashPassword(std::string_view password) { return game::hashPassword(password); }

// ---- Choice lists ---------------------------------------------------------------------------

std::vector<std::string> planetSurfaces(const game::Rules& r) {
    std::vector<std::string> out;
    for (const auto& t : r.data().sectorObjectTypes)
        if (keysEqual(t.physicalType, "Planet") && !isNone(t.planetPhysicalType)) addUnique(out, t.planetPhysicalType);
    for (const auto& p : r.racePresets())
        if (!isNone(p.planetType)) addUnique(out, p.planetType);
    if (out.empty()) out = {"Rock", "Ice", "Gas Giant"};
    return out;
}

std::vector<std::string> atmospheres(const game::Rules& r) {
    std::vector<std::string> out;
    for (const auto& t : r.data().sectorObjectTypes)
        if (keysEqual(t.physicalType, "Planet") && !t.planetAtmosphere.empty()) addUnique(out, t.planetAtmosphere);
    for (const auto& p : r.racePresets())
        if (!p.atmosphere.empty()) addUnique(out, p.atmosphere);
    if (out.empty()) out = {"Oxygen"};
    return out;
}

std::vector<std::string> ministerStyleChoices(const game::Rules& r) { return game::ai::ministerStyles(r); }

void pickMinisterStyle(game::EmpireSetup& e, std::string_view style) {
    if (!style.empty()) e.ministerStyle = std::string(style);
}

void setUseRaceMinisterStyle(game::EmpireSetup& e, bool on) {
    e.useRaceMinisterStyle = on;
    if (on) e.ministerStyle.clear();
}

std::vector<std::string> designNameFiles(const game::Rules& r) {
    std::vector<std::string> out;
    if (r.gameRoot().empty()) return out;
    std::error_code ec;
    for (const auto& dir : std::filesystem::directory_iterator(r.gameRoot(), ec)) {
        if (!dir.is_directory(ec) || lowerAscii(dir.path().filename().string()) != "dsgnname") continue;
        for (const auto& f : std::filesystem::directory_iterator(dir.path(), ec))
            if (f.is_regular_file(ec) && lowerAscii(f.path().extension().string()) == ".txt") out.push_back(f.path().filename().string());
    }
    std::sort(out.begin(), out.end(), [](const std::string& a, const std::string& b) { return std::pair(lowerAscii(a), a) < std::pair(lowerAscii(b), b); });
    return out;
}

std::optional<int> planetPicture(const game::Rules& r, std::string_view surface, std::string_view atmosphere) {
    std::optional<int> any;
    for (const auto& t : r.data().sectorObjectTypes) {
        if (!keysEqual(t.physicalType, "Planet") || !keysEqual(t.planetPhysicalType, surface) || !keysEqual(t.planetAtmosphere, atmosphere))
            continue;
        if (keysEqual(t.planetSize, "Large") || keysEqual(t.planetSize, "Medium")) return t.picture;
        if (!any) any = t.picture;
    }
    return any;
}

// ---- Empire files ---------------------------------------------------------------------------

std::string empireToToml(const game::Rules& r, const game::EmpireSetup& e) {
    const game::Race race = raceOf(r, e);
    toml::table chars;
    for (size_t i = 0; i < game::kCharacteristics; ++i)
        chars.insert(std::string(game::displayName(static_cast<game::Characteristic>(i))), static_cast<int64_t>(race.characteristics[i]));
    toml::array traits;
    for (uint32_t t : race.traits)
        if (t < r.data().racialTraits.size()) traits.push_back(r.data().racialTraits[t].name);
    const auto& cultures = r.data().cultures;
    const auto& moods = r.data().happinessModels;
    toml::table raceTable{
        {"name", race.name},
        {"style", race.style},
        {"native_surface", race.nativeSurface},
        {"atmosphere", race.atmosphere},
        {"culture", race.culture < cultures.size() ? cultures[race.culture].name : std::string{}},
        {"happiness", race.happinessModel < moods.size() ? moods[race.happinessModel].name : std::string{}},
        {"demeanor", race.demeanor},
        {"design_name_file", race.designNameFile},
        {"biology", race.biology},
        {"society", race.society},
        {"history", race.history},
        {"traits", std::move(traits)},
        {"characteristics", std::move(chars)},
    };
    toml::table root{
        {"format", kEmpireFileFormat},
        {"name", e.name},
        {"empire_type", e.empireType},
        {"leader_title", e.leaderTitle},
        {"leader_name", e.leaderName},
        {"computer", e.kind != game::PlayerKind::Human},
        {"preset", e.preset},
        {"preset_tier", e.presetTier},
        {"color", static_cast<int64_t>(e.color)},
        {"password_hash", e.passwordHash},
        {"minister_style", e.ministerStyle},
        {"use_race_minister_style", e.useRaceMinisterStyle},
        {"experience", static_cast<int64_t>(e.experience)},
        {"race", std::move(raceTable)},
    };
    // The combat strategies saved with the empire (spec 06 §7 Q72): each a
    // name and its settings, in order (designs refer to them by place).
    if (!e.strategies.empty()) {
        toml::array strategies;
        for (const ruleset::CombatStrategy& st : e.strategies) {
            toml::array settings;
            for (const auto& [key, value] : st.settings) settings.push_back(toml::array{key, value});
            strategies.push_back(toml::table{{"name", st.name}, {"settings", std::move(settings)}});
        }
        root.insert("strategy", std::move(strategies));
    }
    // Designs saved with the empire (spec 06 §7 Q48, Q72), by the data set's
    // names, each with its strategy and creation date; none is obsolete.
    if (!e.designs.empty()) {
        const auto& data = r.data();
        toml::array designs;
        for (const game::Design& d : e.designs) {
            if (d.hull >= data.vehicleSizes.size()) continue;
            toml::array components, mounts;
            bool anyMount = false;
            for (const game::DesignEntry& entry : d.entries) {
                if (entry.component >= data.components.size()) continue;
                components.push_back(data.components[entry.component].name);
                const bool mounted = entry.mount >= 0 && static_cast<size_t>(entry.mount) < data.weaponMounts.size();
                mounts.push_back(mounted ? data.weaponMounts[static_cast<size_t>(entry.mount)].longName : std::string{});
                anyMount = anyMount || mounted;
            }
            toml::table t{
                {"name", d.name},
                {"type", d.designType},
                {"hull", data.vehicleSizes[d.hull].name},
                {"strategy", static_cast<int64_t>(d.strategy)},
                {"created", static_cast<int64_t>(d.createdTurn)},
                {"components", std::move(components)},
            };
            if (anyMount) t.insert("mounts", std::move(mounts));
            designs.push_back(std::move(t));
        }
        root.insert("design", std::move(designs));
    }
    std::ostringstream os;
    os << "# OpenSE4 empire file\n" << root << "\n";
    return os.str();
}

std::expected<LoadedEmpire, std::string> empireFromToml(const game::Rules& r, std::string_view text) {
    toml::table root;
    try {
        root = toml::parse(text);
    } catch (const toml::parse_error& err) {
        return std::unexpected(std::format("Not a valid empire file (line {}): {}", err.source().begin.line, err.description()));
    }
    const int64_t format = root["format"].value_or(int64_t{0});
    if (format < 1 || format > kEmpireFileFormat) return std::unexpected("Not an OpenSE4 empire file, or one from a newer version.");

    LoadedEmpire out;
    EmpireDraft d;
    d.setup.name = root["name"].value_or(std::string{});
    d.setup.empireType = root["empire_type"].value_or(std::string{});
    d.setup.leaderTitle = root["leader_title"].value_or(std::string{});
    d.setup.leaderName = root["leader_name"].value_or(std::string{});
    d.setup.kind = root["computer"].value_or(false) ? game::PlayerKind::Computer : game::PlayerKind::Human;
    d.setup.preset = root["preset"].value_or(std::string{});
    d.setup.presetTier = static_cast<int>(root["preset_tier"].value_or(int64_t{0}));
    d.setup.color = static_cast<uint32_t>(root["color"].value_or(int64_t{0}) & 0xffffff);
    d.setup.passwordHash = root["password_hash"].value_or(std::string{});
    d.setup.useRaceMinisterStyle = root["use_race_minister_style"].value_or(false);
    // Experience builds up over the games the empire is saved from (spec 02 §9).
    d.setup.experience = static_cast<int>(std::clamp<int64_t>(root["experience"].value_or(int64_t{0}), 0, game::economy::kMaxEmpireExperience));
    if (d.setup.name.empty()) return std::unexpected("The empire file has no empire name.");
    const std::string style = root["minister_style"].value_or(std::string{});
    if (!style.empty()) {
        const std::vector<std::string> styles = ministerStyleChoices(r);
        const auto known = std::find_if(styles.begin(), styles.end(), [&](const std::string& st) { return keysEqual(st, style); });
        if (known != styles.end()) d.setup.ministerStyle = *known;
        else out.warnings.push_back(std::format("The minister style {} is not installed; the race's own is used.", style));
    }
    if (!d.setup.preset.empty() && !game::findPreset(r, d.setup.preset)) {
        out.warnings.push_back(std::format("The race style {} is not installed; generic art is used.", d.setup.preset));
    }

    const toml::table* rt = root["race"].as_table();
    if (!rt) return std::unexpected("The empire file has no [race] table.");
    game::Race& race = d.race;
    race.name = (*rt)["name"].value_or(d.setup.name);
    race.style = (*rt)["style"].value_or(d.setup.preset);
    race.nativeSurface = (*rt)["native_surface"].value_or(race.nativeSurface);
    race.atmosphere = (*rt)["atmosphere"].value_or(race.atmosphere);
    race.demeanor = (*rt)["demeanor"].value_or(std::string{});
    race.designNameFile = (*rt)["design_name_file"].value_or(std::string{});
    race.biology = (*rt)["biology"].value_or(std::string{});
    race.society = (*rt)["society"].value_or(std::string{});
    race.history = (*rt)["history"].value_or(std::string{});
    const std::string culture = (*rt)["culture"].value_or(std::string{});
    if (auto i = indexByName(r.data().cultures, culture)) race.culture = *i;
    else if (!culture.empty()) out.warnings.push_back(std::format("Culture {} is not in this data set; using {}.", culture,
                                                                   r.data().cultures.empty() ? "none" : r.data().cultures.front().name));
    const std::string mood = (*rt)["happiness"].value_or(std::string{});
    if (auto i = indexByName(r.data().happinessModels, mood)) race.happinessModel = *i;
    else if (!mood.empty()) out.warnings.push_back(std::format("Happiness type {} is not in this data set.", mood));

    if (const toml::table* chars = (*rt)["characteristics"].as_table())
        for (const auto& [key, node] : *chars) {
            game::Characteristic c;
            if (!game::parseCharacteristic(key.str(), c)) {
                out.warnings.push_back(std::format("Unknown characteristic {} ignored.", key.str()));
                continue;
            }
            const int value = static_cast<int>(node.value_or(int64_t{100}));
            const CharacteristicLimits l = characteristicLimits(r, c);
            const int clamped = std::clamp(value, l.min, l.max);
            if (clamped != value)
                out.warnings.push_back(std::format("{} {}% is outside {}..{}%; set to {}%.", key.str(), value, l.min, l.max, clamped));
            race.characteristics[static_cast<size_t>(c)] = clamped;
        }
    if (const toml::array* traits = (*rt)["traits"].as_array()) {
        // Added in two passes so a trait listed before its requirement still fits.
        std::vector<uint32_t> wanted;
        for (const auto& node : *traits) {
            const std::string name = node.value_or(std::string{});
            if (auto t = findTrait(r, name)) wanted.push_back(*t);
            else out.warnings.push_back(std::format("Trait {} is not in this data set; dropped.", name));
        }
        for (int pass = 0; pass < 2; ++pass)
            for (uint32_t t : wanted) addTrait(r, race, t);
        for (uint32_t t : wanted)
            if (!hasTrait(race, t))
                out.warnings.push_back(std::format("Trait {} dropped: {}.", r.data().racialTraits[t].name, canAddTrait(r, race, t).reason));
    }
    out.empire = collapse(r, d);
    // The combat strategies (format 3, spec 06 §7 Q72).
    if (const toml::array* strategies = root["strategy"].as_array())
        for (const toml::node& node : *strategies) {
            const toml::table* t = node.as_table();
            if (!t) continue;
            ruleset::CombatStrategy st;
            st.name = (*t)["name"].value_or(std::string{});
            if (const toml::array* settings = (*t)["settings"].as_array())
                for (const toml::node& pair : *settings)
                    if (const toml::array* kv = pair.as_array(); kv && kv->size() == 2)
                        st.settings.emplace_back((*kv)[0].value_or(std::string{}), (*kv)[1].value_or(std::string{}));
            out.empire.strategies.push_back(std::move(st));
        }
    // Designs saved with the empire: each one the data set can still build
    // (its hull, components and mounts by name) comes along (spec 06 §7 Q48).
    if (const toml::array* designs = root["design"].as_array())
        for (const toml::node& node : *designs) {
            const toml::table* t = node.as_table();
            if (!t) continue;
            game::Design design;
            design.name = (*t)["name"].value_or(std::string{});
            design.designType = (*t)["type"].value_or(std::string{});
            // A saved design comes back current: an old file's obsolete mark is not read (spec 06 §7 Q72).
            design.strategy = static_cast<uint32_t>(std::clamp<int64_t>((*t)["strategy"].value_or(int64_t{0}), 0, INT32_MAX));
            design.createdTurn = static_cast<uint32_t>(std::clamp<int64_t>((*t)["created"].value_or(int64_t{0}), 0, INT32_MAX));
            const std::string hull = (*t)["hull"].value_or(std::string{});
            std::string missing;
            if (auto h = indexByName(r.data().vehicleSizes, hull)) design.hull = *h;
            else missing = std::format("the hull {}", hull);
            const toml::array* components = (*t)["components"].as_array();
            const toml::array* mounts = (*t)["mounts"].as_array();
            for (size_t k = 0; components && k < components->size() && missing.empty(); ++k) {
                const std::string name = (*components)[k].value_or(std::string{});
                game::DesignEntry entry;
                if (auto c = indexByName(r.data().components, name)) entry.component = *c;
                else missing = std::format("the component {}", name);
                const std::string mount = mounts && k < mounts->size() ? (*mounts)[k].value_or(std::string{}) : std::string{};
                if (!mount.empty()) {
                    const auto& all = r.data().weaponMounts;
                    const auto m = std::find_if(all.begin(), all.end(), [&](const ruleset::WeaponMount& x) { return keysEqual(x.longName, mount); });
                    if (m != all.end()) entry.mount = static_cast<int32_t>(m - all.begin());
                    else missing = std::format("the mount {}", mount);
                }
                design.entries.push_back(entry);
            }
            if (design.name.empty()) continue;
            if (!missing.empty()) {
                out.warnings.push_back(std::format("Design {} left out: this data set has no {}.", design.name, missing));
                continue;
            }
            out.empire.designs.push_back(std::move(design));
        }
    return out;
}

std::expected<std::filesystem::path, std::string> saveEmpireFile(const game::Rules& r, const std::filesystem::path& dir,
                                                                 const game::EmpireSetup& e) {
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) return std::unexpected(std::format("Cannot create {}: {}", dir.string(), ec.message()));
    // A file whose name differs only in case is replaced, as on Windows.
    const std::filesystem::path file = ruleset::childIgnoringCase(dir, fileStem(e.name) + ".toml");
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out << empireToToml(r, e);
    if (!out) return std::unexpected(std::format("Cannot write {}", file.string()));
    return file;
}

std::expected<LoadedEmpire, std::string> loadEmpireFile(const game::Rules& r, const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return std::unexpected(std::format("Cannot read {}", file.string()));
    std::stringstream ss;
    ss << in.rdbuf();
    return empireFromToml(r, ss.str());
}

std::vector<EmpireFileInfo> listEmpireFiles(const game::Rules& r, const std::filesystem::path& dir) {
    std::vector<EmpireFileInfo> out;
    std::error_code ec;
    for (const auto& f : std::filesystem::directory_iterator(dir, ec)) {
        if (!f.is_regular_file(ec) || lowerAscii(f.path().extension().string()) != ".toml") continue;
        auto loaded = loadEmpireFile(r, f.path());
        if (!loaded) continue;
        const game::Race race = raceOf(r, loaded->empire);
        out.push_back({f.path(), loaded->empire.name, race.name, race.style});
    }
    std::sort(out.begin(), out.end(), [](const EmpireFileInfo& a, const EmpireFileInfo& b) {
        return std::pair(lowerAscii(a.name), a.path.filename()) < std::pair(lowerAscii(b.name), b.path.filename());
    });
    return out;
}

} // namespace opense4::client::classic::setup
