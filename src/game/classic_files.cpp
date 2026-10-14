// The original's empire files and map files as OpenSE4's (docs/spec/08 §13,
// §14): the import into an empire of Game Setup or a map, the export back,
// Add Existing's checks, and the files on disk.

#include "game/classic_files.hpp"
#include "game/classic_save_internal.hpp"

#include "datafile/datafile.hpp"
#include "game/ai_data.hpp"
#include "game/economy.hpp"
#include "game/generate.hpp"
#include "game/rules.hpp"
#include "game/serialize.hpp"

#include <algorithm>
#include <format>
#include <limits>
#include <map>
#include <set>

namespace opense4::game::classic {

using namespace detail;
using datafile::keysEqual;

namespace {

constexpr bool inRange(size_t position, size_t n) { return position >= 1 && position <= n; }
uint8_t clampByte(int64_t v) { return static_cast<uint8_t>(std::clamp<int64_t>(v, 0, 255)); }
uint16_t clampWord(int64_t v) { return static_cast<uint16_t>(std::clamp<int64_t>(v, 0, 65535)); }
int32_t clampInt(int64_t v) {
    return static_cast<int32_t>(std::clamp<int64_t>(v, std::numeric_limits<int32_t>::min(), std::numeric_limits<int32_t>::max()));
}
uint8_t sectorByte(Sector x) { return x.valid() ? static_cast<uint8_t>(x.y * 13 + x.x) : 84; }
Sector sectorOf(size_t b) {
    if (b > 168) b = 84;
    return Sector{static_cast<int>(b % 13), static_cast<int>(b / 13)};
}

// "12 designs left out: ..." from counted approximations.
void countedNotes(ConversionReport& report, const std::map<std::string, int>& counts) {
    for (const auto& [what, n] : counts) report.note(std::format("{} {}", n, what));
}

std::string lowerTrimmed(std::string_view s) {
    std::string out = trimmed(s);
    for (char& c : out)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return out;
}

// The race an empire of Game Setup plays, as createGame gives it.
const ruleset::RacePreset* presetOf(const Rules& r, const EmpireSetup& e) { return e.preset.empty() ? nullptr : findPreset(r, e.preset); }
Race raceOf(const Rules& r, const EmpireSetup& e) {
    if (e.customRace) return *e.customRace;
    if (const ruleset::RacePreset* p = presetOf(r, e)) return raceFromPreset(r, *p, e.presetTier);
    Race race;
    race.name = e.name;
    return race;
}

} // namespace

// ---- Empire files: import (§13.3, §13.6) ----------------------------------------------------------------

std::expected<EmpireSetup, std::string> importClassicEmpire(const Rules& r, const ClassicEmpireFile& f, ConversionReport& report) {
    const ruleset::Ruleset& d = r.data();
    const EmpireRecord& in = f.empire;
    if (auto problem = versionProblem(f.version, "empire file")) return std::unexpected(std::move(*problem));
    if (in.traits.size() != d.racialTraits.size())
        return std::unexpected(std::format("the empire file was read for {} racial traits; the data set has {}", in.traits.size(),
                                           d.racialTraits.size()));
    if (trimmed(in.name).empty()) return std::unexpected(std::string("the empire file has no empire name"));
    std::map<std::string, int> counts;
    auto count = [&](const std::string& what, int n = 1) { counts[what] += n; };

    EmpireSetup e;
    e.name = in.name;
    e.empireType = in.type;
    e.leaderTitle = in.leaderTitle;
    e.leaderName = in.leaderName;
    // Save Empire always clears computer control (§13.2); a file that has it
    // keeps it, and the computer player uses the data set's strategies (§13.6).
    e.kind = !in.computer ? PlayerKind::Human : in.neutral ? PlayerKind::Neutral : PlayerKind::Computer;

    // The race (§3.6.1, §3.6.5).
    Race race;
    const ruleset::RacePreset* preset = findPreset(r, in.raceFolder);
    race.style = preset ? preset->folder : in.raceFolder;
    if (!preset) report.note(std::format("The race style {} is not installed; generic art is used.", in.raceFolder));
    race.name = preset && !preset->name.empty() ? preset->name : in.name;
    race.biology = in.biology;
    race.society = in.society;
    race.history = in.history;
    race.demeanor = in.demeanor;
    race.designNameFile = in.shipNameFile;
    race.atmosphere = std::string(kAtmospheres[static_cast<size_t>(std::clamp(int{in.atmosphere}, 1, 5) - 1)]);
    race.nativeSurface = std::string(kSurfaces[static_cast<size_t>(std::clamp(int{in.surface}, 1, 3) - 1)]);
    // Exact name, the last match winning; an unknown name takes the first record (§3.6.1).
    race.happinessModel = 0;
    for (uint32_t h = 0; h < d.happinessModels.size(); ++h)
        if (d.happinessModels[h].name == in.happinessType) race.happinessModel = h;
    if (in.culture == 0) {
        count("empires without a culture (given the first)");
    } else if (!inRange(in.culture, d.cultures.size())) {
        return std::unexpected(std::format("the empire's culture is record {} of Cultures.txt, which has {} records: the file was made with "
                                           "another data set",
                                           in.culture, d.cultures.size()));
    } else {
        race.culture = in.culture - 1u;
    }
    for (size_t t = 0; t < in.traits.size(); ++t)
        if (in.traits[t]) race.traits.push_back(static_cast<uint32_t>(t));
    // Characteristics within the Settings.txt limits, as for OpenSE4's own empire files.
    for (size_t c = 0; c < race.characteristics.size(); ++c) {
        const std::string name{displayName(static_cast<Characteristic>(c))};
        int lo = static_cast<int>(r.setting(std::format("Characteristic {} Min Pct", name), 50));
        int hi = static_cast<int>(r.setting(std::format("Characteristic {} Max Pct", name), 150));
        if (hi < lo) std::swap(lo, hi);
        const int value = clampInt(in.characteristics[c]);
        race.characteristics[c] = std::clamp(value, lo, hi);
        if (race.characteristics[c] != value)
            report.note(std::format("{} {}% is outside {}..{}%; set to {}%.", name, value, lo, hi, race.characteristics[c]));
    }
    if (!in.artFolder.empty() && !keysEqual(in.artFolder, in.raceFolder)) count("empires whose art came from another race");
    e.preset = race.style;
    e.customRace = race;

    // The player's own (§3.6.1, §3.6.9).
    if (const std::string pw = lowerTrimmed(in.password); !pw.empty()) {
        e.passwordHash = hashPassword(pw);
        report.note("The empire has a password: type it in lower case in OpenSE4.");
    }
    e.email = cleanEmail(in.email);
    if (!in.ministerStyle.empty()) {
        const std::vector<std::string> styles = ai::ministerStyles(r);
        const auto known = std::find_if(styles.begin(), styles.end(), [&](const std::string& s) { return keysEqual(s, in.ministerStyle); });
        if (known != styles.end()) e.ministerStyle = *known;
        else report.note(std::format("The minister style {} is not installed; the race's own is used.", in.ministerStyle));
    }
    e.useRaceMinisterStyle = in.useRaceMinisterStyle;
    e.experience = static_cast<int>(std::clamp<int64_t>(in.experience, 0, economy::kMaxEmpireExperience));

    // Strategies (§3.6.13): a computer player's come from the data set.
    if (e.kind == PlayerKind::Human)
        for (const StrategyRecord& t : in.strategies) e.strategies.push_back(strategyFromRecord(t));

    // Name lists (§3.6.6, §3.6.8). An older file's list gets the two drone types (§5.2);
    // an empty one stays empty, which stands for the data set's.
    e.designTypes = in.designTypes;
    if (!versionAtLeast(f.version, release::kDrones) && !e.designTypes.empty())
        for (std::string_view t : kOldDesignTypesAdded)
            if (std::none_of(e.designTypes.begin(), e.designTypes.end(), [&](const std::string& x) { return keysEqual(x, t); }))
                e.designTypes.emplace_back(t);
    e.colonyTypes = in.colonyTypes;
    e.repairPriorities = in.repairPriorities;
    if (!in.queueTemplates.empty())
        count("construction queue templates not carried (OpenSE4 keeps its templates on this computer)", static_cast<int>(in.queueTemplates.size()));

    // Settings (§3.6.7, §3.6.9).
    EmpireSettings s;
    s.ministers = 0;
    for (size_t k = 0; k < in.ministers.size(); ++k)
        if (in.ministers[k]) s.ministers |= uint32_t{1} << k;
    s.ministerAll = s.ministers == kAllMinisters;
    s.ministersForNewVehicles = in.ministersForNewVehicles;
    s.aiMinimalChanges = in.aiMinimalChanges;
    const EmpireOptions& o = in.options;
    s.clearOrdersOnEncounter = o.clearOnAny ? EncounterClear::Any : o.clearOnEnemy ? EncounterClear::Enemy : EncounterClear::Never;
    s.avoidTaggedMinefields = o.avoidTaggedMines;
    s.avoidRestrictedSystems = o.avoidRestricted;
    s.chooseColonyType = o.chooseColonyType;
    s.interfaceOptions = interfaceOptionsFromRecord(o);
    e.settings = s;

    // Designs (§13.3): positions in the data files; checked again when the game begins.
    for (size_t i = 0; i < f.designs.size(); ++i) {
        const DesignRecord& rec = f.designs[i];
        const bool fits = inRange(rec.hull, d.vehicleSizes.size()) && std::all_of(rec.parts.begin(), rec.parts.end(), [&](const DesignPart& p) {
                              return inRange(p.component, d.components.size()) && size_t{p.mount} <= d.weaponMounts.size();
                          });
        if (!fits || rec.name.empty()) {
            report.detail(std::format("design {} ({}) names a hull or part this data set lacks; left out", i + 1, rec.name));
            count("designs left out (their hull or parts are not in this data set)");
            continue;
        }
        Design design;
        design.name = rec.name;
        design.designType = rec.type;
        design.templateName = rec.templateName;
        design.hull = rec.hull - 1u;
        for (const DesignPart& p : rec.parts) design.entries.push_back({p.component - 1u, p.mount ? int32_t{p.mount} - 1 : -1});
        design.strategy = rec.strategy >= 1 ? rec.strategy - 1u : 0;
        design.createdTurn = rec.created > kDateBase ? static_cast<uint32_t>(rec.created - kDateBase) : 0;
        e.designs.push_back(std::move(design));
    }
    countedNotes(report, counts);
    return e;
}

// ---- Empire files: export (§13.2, §13.6) ----------------------------------------------------------------

std::expected<ClassicEmpireFile, std::string> exportClassicEmpire(const Rules& r, const EmpireSetup& e, ConversionReport& report,
                                                                  const EmpireExportOptions& options) {
    const ruleset::Ruleset& d = r.data();
    size_t replaced = 0;
    auto text = [&](std::string_view t) { return latin1Safe(t, &replaced); };
    std::map<std::string, int> counts;
    auto count = [&](const std::string& what, int n = 1) { counts[what] += n; };
    const ruleset::RacePreset* preset = presetOf(r, e);
    const Race race = raceOf(r, e);

    ClassicEmpireFile f;
    f.keys = drawKeys(options.keySeed);
    f.version = std::string(kVersion);
    f.traitCount = d.racialTraits.size();
    EmpireRecord& x = f.empire;
    // Identity, as the new game would name the empire (createGame).
    x.name = text(!e.name.empty() ? e.name : preset && !preset->empireName.empty() ? preset->empireName : race.name);
    x.type = text(!e.empireType.empty() ? e.empireType : preset ? preset->empireType : std::string{});
    x.leaderTitle = text(!e.leaderTitle.empty() ? e.leaderTitle : preset ? preset->emperorTitle : std::string{});
    x.leaderName = text(!e.leaderName.empty() ? e.leaderName : preset ? preset->emperorName : std::string{});
    if (x.name.empty()) return std::unexpected(std::string("the empire has no name"));
    x.player = std::max<uint8_t>(1, std::min<uint8_t>(options.player, static_cast<uint8_t>(kMaxPlayers)));
    // Computer control, neutrality and destruction off; the pools empty (§13.2).
    x.useRaceMinisterStyle = e.useRaceMinisterStyle;
    x.raceFolder = text(race.style);
    x.artFolder = x.raceFolder;
    x.shipNameFile = text(race.designNameFile);
    x.atmosphere = 3;
    bool atmosphereKnown = false;
    for (size_t k = 0; k < kAtmospheres.size(); ++k)
        if (keysEqual(kAtmospheres[k], race.atmosphere)) {
            x.atmosphere = static_cast<uint8_t>(k + 1);
            atmosphereKnown = true;
        }
    if (!atmosphereKnown) count("races breathing an atmosphere the original lacks (written as Oxygen)");
    x.surface = 1;
    for (size_t k = 0; k < kSurfaces.size(); ++k)
        if (keysEqual(kSurfaces[k], race.nativeSurface) || (k == 2 && keysEqual(race.nativeSurface, "Gas"))) x.surface = static_cast<uint8_t>(k + 1);
    x.biology = text(race.biology);
    x.society = text(race.society);
    x.history = text(race.history);
    x.demeanor = text(race.demeanor);
    if (race.happinessModel < d.happinessModels.size()) x.happinessType = text(d.happinessModels[race.happinessModel].name);
    x.experience = clampInt(e.experience);
    x.maintenancePercent = clampByte(r.setting("Empire Starting Percent Maint Cost", 25));
    x.reproductionPercent = clampByte(r.setting("Empire Starting Percent Reproduction", 10));
    if (!e.passwordHash.empty()) report.note("The password is not written: OpenSE4 keeps only a check of it. Set it again in the original.");
    x.email = text(e.email);
    // Research: one level per tech area, all 0 (§13.2).
    x.techLevels.assign(d.techAreas.size(), 0);
    // Politics: no contact with anyone; the ministers' memory of each player at a new empire's values.
    x.anger.fill(50);
    x.turnsSinceWar.fill(999);
    // Race (§3.6.5).
    x.culture = clampWord(int64_t{race.culture} + 1);
    x.traits.assign(d.racialTraits.size(), false);
    for (uint32_t t : race.traits)
        if (t < x.traits.size()) x.traits[t] = true;
    for (size_t c = 0; c < x.characteristics.size(); ++c) x.characteristics[c] = race.characteristics[c];
    // Name lists: the empire's, else the data set's as the game would give it (§13.6).
    for (const std::string& t : !e.designTypes.empty() ? e.designTypes : d.names.designTypes) x.designTypes.push_back(text(t));
    for (const std::string& t : !e.colonyTypes.empty() ? e.colonyTypes : d.names.colonyTypes) x.colonyTypes.push_back(text(t));
    for (const std::string& t : !e.repairPriorities.empty() ? e.repairPriorities : d.names.repairPriorities) x.repairPriorities.push_back(text(t));
    if (x.designTypes.size() > 250) return std::unexpected(std::format("the empire has {} design types; the original holds at most 250", x.designTypes.size()));
    if (x.colonyTypes.size() > 255) return std::unexpected(std::format("the empire has {} colony types; the original holds at most 255", x.colonyTypes.size()));
    // Settings (§3.6.7, §3.6.9): the empire file's, else a new empire's.
    const EmpireSettings s = e.settings.value_or(EmpireSettings{});
    x.options = empireOptionsRecord(s.interfaceOptions, s.clearOrdersOnEncounter, s.avoidTaggedMinefields, s.avoidRestrictedSystems, s.chooseColonyType);
    x.ministerStyle = text(e.ministerStyle);
    for (size_t k = 0; k < x.ministers.size(); ++k) x.ministers[k] = s.ministerAll || ((s.ministers >> k) & 1u) != 0;
    x.ministersForNewVehicles = s.ministersForNewVehicles;
    x.aiMinimalChanges = s.aiMinimalChanges;
    // Strategies (§3.6.13): the empire's, else the data set's.
    const std::vector<ruleset::CombatStrategy>& strategies = !e.strategies.empty() ? e.strategies : d.combatStrategies;
    for (size_t k = 0; k < strategies.size(); ++k) x.strategies.push_back(strategyRecord(strategies[k], k, &replaced));
    // Designs (§13.2): copies of the player's, current and never built.
    if (options.designs)
        for (const Design& design : e.designs) {
            DesignRecord rec;
            rec.id = 0;
            rec.owner = x.player;
            rec.hull = clampWord(int64_t{design.hull} + 1);
            rec.type = text(design.designType);
            rec.templateName = text(design.templateName);
            rec.name = text(design.name);
            rec.created = clampInt(int64_t{kDateBase} + design.createdTurn);
            cachedDesignValues(r, design, rec);
            for (const DesignEntry& entry : design.entries) rec.parts.push_back({clampWord(int64_t{entry.component} + 1), clampByte(entry.mount + 1)});
            rec.strategy = clampWord(int64_t{design.strategy} + 1);
            rec.built = clampInt(design.built);
            rec.lost = clampInt(design.lost);
            rec.scrapped = clampInt(design.scrapped);
            rec.tonnageDestroyed = clampInt(design.enemyTonnageDestroyed);
            f.designs.push_back(std::move(rec));
        }
    if (f.designs.size() > 65535) return std::unexpected(std::string("the empire has more designs than the original holds"));
    if (replaced) count("characters the original's text lacks (written as '?')", static_cast<int>(replaced));
    countedNotes(report, counts);
    return f;
}

// ---- Add Existing's checks (§13.4) -----------------------------------------------------------------------------

int64_t empireTechSum(const Rules& r, std::span<const uint16_t> techLevels) {
    const auto& areas = r.data().techAreas;
    int64_t sum = 0;
    for (size_t i = 0; i < techLevels.size() && i < areas.size(); ++i) sum += std::min<int64_t>(techLevels[i], std::max(0, areas[i].maxLevel));
    return sum;
}

int64_t allowedTechSum(const Rules& r, const GameOptions& o) {
    const auto& areas = r.data().techAreas;
    int64_t sum = 0;
    for (size_t i = 0; i < areas.size(); ++i) {
        const bool allowed = !areas[i].canBeRemoved || o.techAreasAllowed.empty() || (i < o.techAreasAllowed.size() && o.techAreasAllowed[i] != 0);
        if (!allowed) continue;
        // Low: the area's start level; Medium: its raise level; High: its maximum.
        sum += o.startTechLevel <= 0 ? areas[i].startLevel : o.startTechLevel == 1 ? areas[i].raiseLevel : areas[i].maxLevel;
    }
    return sum;
}

std::optional<std::string> addExistingProblem(const Rules& r, const GameOptions& o, const EmpireSetup& e, std::span<const uint16_t> techLevels) {
    const int cost = racialPointCost(r, raceOf(r, e));
    if (cost > o.racialPoints)
        return std::format("This empire spent {} racial points, more than the {} this game allows for a new player.", cost, o.racialPoints);
    if (!techLevels.empty() && empireTechSum(r, techLevels) > allowedTechSum(r, o))
        return std::string("This empire's technology goes beyond what this game gives a new player.");
    return std::nullopt;
}

// ---- Empire files on disk ---------------------------------------------------------------------------------------

std::expected<LoadedClassicEmpire, std::string> readClassicEmpire(const Rules& r, const std::filesystem::path& file, ConversionReport& report) {
    auto bytes = readFileBytes(file, size_t{64} << 20);
    if (!bytes) return std::unexpected(bytes.error());
    const size_t traits = r.data().racialTraits.size();
    auto decoded = decodeClassicEmpire(*bytes, traits);
    if (!decoded) {
        // Damage, or a data set with another number of racial traits (§3.6.5)?
        const std::vector<size_t> fits = empireTraitCountsThatDecode(*bytes);
        if (!fits.empty() && std::find(fits.begin(), fits.end(), traits) == fits.end())
            return std::unexpected(std::format("{}: the empire was saved with a data set of {} racial traits; the loaded data set has {}. "
                                               "Load it with the data set (mod) it was made with.",
                                               file.filename().string(), fits.front(), traits));
        return std::unexpected(std::format("{}: the empire file could not be loaded: {}", file.filename().string(), decoded.error()));
    }
    auto empire = importClassicEmpire(r, *decoded, report);
    if (!empire) return std::unexpected(std::format("{}: {}", file.filename().string(), empire.error()));
    return LoadedClassicEmpire{std::move(*decoded), std::move(*empire)};
}

std::expected<void, std::string> writeClassicEmpire(const Rules& r, const EmpireSetup& e, const std::filesystem::path& file,
                                                    ConversionReport& report, const EmpireExportOptions& options) {
    auto f = exportClassicEmpire(r, e, report, options);
    if (!f) return std::unexpected(f.error());
    auto bytes = encodeClassicEmpire(*f);
    if (!bytes) return std::unexpected(bytes.error());
    std::error_code ec;
    if (file.has_parent_path()) std::filesystem::create_directories(file.parent_path(), ec);
    return writeFileAtomic(file, *bytes);
}

// ---- Map files: import (§14.2, §14.5) ----------------------------------------------------------------------------

std::expected<QuadrantMap, std::string> importClassicMap(const ruleset::Ruleset& rs, const ClassicMapFile& f, ConversionReport& report,
                                                         std::string name) {
    if (auto problem = versionProblem(f.version, "map file")) return std::unexpected(std::move(*problem));
    if (f.systems.empty()) return std::unexpected(std::string("the map has no systems"));
    std::map<std::string, int> counts;
    auto count = [&](const std::string& what, int n = 1) { counts[what] += n; };
    auto abilities = [&](const std::vector<Ability>& list) {
        std::vector<ruleset::Ability> out;
        for (const Ability& a : list) {
            if (a.id >= kAbilityIds) count("abilities with an id the table lacks");
            out.push_back({std::string(abilityName(a.id)), a.description, std::to_string(a.value1), std::to_string(a.value2)});
        }
        return out;
    };
    QuadrantMap m;
    m.name = std::move(name);
    Galaxy& g = m.galaxy;
    g.width = kQuadrantWidth;
    g.height = kQuadrantHeight;
    // Systems (§3.5): new ones, nobody's knowledge.
    for (size_t i = 0; i < f.systems.size(); ++i) {
        const SystemRecord& y = f.systems[i];
        StarSystem sys;
        sys.id = SystemId{i};
        sys.name = y.name;
        sys.position = GalaxyPos{std::clamp(int{y.x} - 1, 0, g.width - 1), std::clamp(int{y.y} - 1, 0, g.height - 1)};
        const std::string_view physical = kPhysicalTypes[static_cast<size_t>(std::clamp(int{y.physicalType}, 1, 3) - 1)];
        sys.physicalType = std::string(physical);
        sys.abilities = abilities(y.abilities);
        // The type is not stored: the first with the same description and
        // background, else the first of the physical type (§12, §11.1 Q13).
        std::optional<uint32_t> fallback, match;
        for (uint32_t t = 0; t < rs.systemTypes.size() && !match; ++t) {
            const ruleset::SystemType& st = rs.systemTypes[t];
            if (!keysEqual(st.physicalType, physical)) continue;
            if (!fallback) fallback = t;
            if (keysEqual(st.description, y.typeDescription) && keysEqual(st.backgroundBitmap, y.backgroundBitmap)) match = t;
        }
        if (!match) count("systems whose type was guessed from the physical type");
        if (!match && !fallback && rs.systemTypes.empty()) return std::unexpected(std::string("the data set has no system types"));
        sys.type = ruleset::SystemTypeId{match.value_or(fallback.value_or(0))};
        g.systems.push_back(std::move(sys));
    }
    // Objects (§3.8): numbered by position, the stored ids ignored.
    std::vector<ObjectId> refs(f.objects.size());
    for (size_t i = 0; i < f.objects.size(); ++i) {
        const ObjectRecord& o = f.objects[i];
        const ObjectClass c = o.objectClass();
        if (c != ObjectClass::Star && c != ObjectClass::WarpPoint && c != ObjectClass::Storm && c != ObjectClass::Planet && c != ObjectClass::Comet) {
            count("objects that are not stellar objects (left out)");
            continue;
        }
        if (o.blank()) continue;
        const std::string where = std::format("object {} ({})", i + 1, displayName(c));
        if (!inRange(o.sectorType, rs.sectorObjectTypes.size()))
            return std::unexpected(std::format("{} is SectType.txt record {}, which has {} records: the map was made with another data set", where,
                                               o.sectorType, rs.sectorObjectTypes.size()));
        if (!inRange(o.system, g.systems.size())) return std::unexpected(std::format("{} is in system {}; the map has {}", where, o.system, g.systems.size()));
        SpaceObject obj;
        obj.id = ObjectId{g.objects.size()};
        obj.slot = static_cast<uint32_t>(obj.id.value);
        obj.system = SystemId{o.system - 1u};
        obj.sector = sectorOf(o.sector);
        const uint32_t type = o.sectorType - 1u;
        const auto recordKind = parseObjectKind(rs.sectorObjectTypes[type].physicalType);
        switch (c) {
            case ObjectClass::Star: obj.kind = recordKind == ObjectKind::DestroyedStar ? ObjectKind::DestroyedStar : ObjectKind::Star; break;
            case ObjectClass::WarpPoint: obj.kind = ObjectKind::WarpPoint; break;
            case ObjectClass::Storm: obj.kind = ObjectKind::Storm; break;
            case ObjectClass::Planet: obj.kind = recordKind == ObjectKind::Asteroids ? ObjectKind::Asteroids : ObjectKind::Planet; break;
            default: obj.kind = ObjectKind::Comet; break;
        }
        applySectorType(rs, obj, type);
        obj.abilities = abilities(o.abilities);
        switch (c) {
            case ObjectClass::Star:
            case ObjectClass::Planet: obj.name = o.name; break;
            case ObjectClass::Storm: obj.name = "Storm"; break;
            case ObjectClass::WarpPoint: obj.name = "Warp Point"; break;
            default: obj.name = g.systems[obj.system.index()].name; break;
        }
        if (c == ObjectClass::Planet) {
            obj.conditions = Conditions{xmath::toDoubleBits(fromFloat80(o.conditions))};
            obj.value = o.value;
            if (o.colony) count("colonies in the map (left out)");
        }
        refs[i] = obj.id;
        g.systems[obj.system.index()].objects.push_back(obj.id);
        g.objects.push_back(std::move(obj));
    }
    // Warp links (§3.8.3): the warp point at the stored place, the one leading back preferred.
    for (size_t i = 0; i < f.objects.size(); ++i) {
        const ObjectRecord& o = f.objects[i];
        if (!refs[i].valid() || o.objectClass() != ObjectClass::WarpPoint) continue;
        SpaceObject& wp = g.object(refs[i]);
        ObjectId far;
        if (inRange(o.destSystem, g.systems.size()))
            for (size_t k = 0; k < f.objects.size(); ++k) {
                if (!refs[k].valid() || f.objects[k].objectClass() != ObjectClass::WarpPoint) continue;
                const SpaceObject& cand = g.object(refs[k]);
                if (cand.system.index() + 1 != o.destSystem || cand.sector != sectorOf(o.destSector) || cand.id == wp.id) continue;
                const ObjectRecord& back = f.objects[k];
                const bool pairs = size_t{back.destSystem} == wp.system.index() + 1 && sectorOf(back.destSector) == wp.sector;
                if (!far.valid() || pairs) far = cand.id;
                if (pairs) break;
            }
        if (!far.valid()) count("one-way warp points without a far end");
        wp.destination = far;
    }
    // Starting points (§3.5): exact duplicates dropped.
    auto start = [&](const StartPointRecord& p, int player) -> std::optional<std::string> {
        if (!inRange(p.system, g.systems.size())) return std::format("a starting point is in system {}; the map has {}", p.system, g.systems.size());
        StartingPoint s{SystemId{p.system - 1u}, sectorOf(p.sector), player};
        if (std::find(m.startingPoints.begin(), m.startingPoints.end(), s) == m.startingPoints.end()) m.startingPoints.push_back(s);
        return std::nullopt;
    };
    for (const StartPointRecord& p : f.specificStarts) {
        if (p.player < 1 || p.player > kMaxPlayers) return std::unexpected(std::format("a starting point is for player {}", p.player));
        if (auto problem = start(p, int{p.player} - 1)) return std::unexpected(std::move(*problem));
    }
    for (const StartPointRecord& p : f.commonStarts)
        if (auto problem = start(p, kCommonStart)) return std::unexpected(std::move(*problem));
    countedNotes(report, counts);
    return m;
}

// ---- Map files: export (§14.1, §14.5) ---------------------------------------------------------------------------

std::expected<ClassicMapFile, std::string> exportClassicMap(const ruleset::Ruleset& rs, const QuadrantMap& map, ConversionReport& report,
                                                            uint64_t keySeed) {
    const Galaxy& g = map.galaxy;
    if (g.systems.empty()) return std::unexpected(std::string("the map has no systems"));
    if (g.systems.size() > 255) return std::unexpected(std::format("the map has {} systems; the original holds at most 255", g.systems.size()));
    size_t replaced = 0;
    auto text = [&](std::string_view t) { return latin1Safe(t, &replaced); };
    std::map<std::string, int> counts;
    auto count = [&](const std::string& what, int n = 1) { counts[what] += n; };
    auto abilities = [&](const std::vector<ruleset::Ability>& list) {
        std::vector<Ability> out;
        for (const ruleset::Ability& a : list) {
            const auto id = abilityId(a.type);
            if (!id && !a.type.empty()) count("abilities the original's table lacks (written as none)");
            out.push_back({id.value_or(0), text(a.description), clampInt(a.number1()), clampInt(a.number2())});
        }
        if (out.size() > 255) {
            out.resize(255);
            count("ability lists cut to 255");
        }
        return out;
    };
    ClassicMapFile f;
    f.keys = drawKeys(keySeed);
    f.version = std::string(kVersion);
    for (size_t i = 0; i < g.systems.size(); ++i) {
        const StarSystem& sys = g.systems[i];
        SystemRecord y;
        y.name = text(sys.name);
        y.number = static_cast<uint8_t>(i + 1);
        y.x = clampByte(sys.position.x + 1);
        y.y = clampByte(sys.position.y + 1);
        for (size_t k = 0; k < kPhysicalTypes.size(); ++k)
            if (keysEqual(kPhysicalTypes[k], sys.physicalType)) y.physicalType = static_cast<uint8_t>(k + 1);
        if (sys.type.valid() && sys.type.index() < rs.systemTypes.size()) {
            // The type's attributes are copied into the record (§3.5).
            const ruleset::SystemType& t = rs.systemTypes[sys.type.index()];
            y.typeDescription = text(t.description);
            y.canStart = t.empiresCanStartIn;
            y.maskBackground = t.maskBackgroundObjects;
            y.nonTiledCenter = t.nonTiledCenterPicture;
            y.backgroundBitmap = text(t.backgroundBitmap);
        }
        y.abilities = abilities(sys.abilities);
        f.systems.push_back(std::move(y));
    }
    for (const StartingPoint& p : map.startingPoints) {
        if (!p.system.valid() || p.system.index() >= g.systems.size()) continue;
        StartPointRecord rec{static_cast<uint16_t>(p.system.index() + 1), sectorByte(p.sector), 0};
        if (p.player == kCommonStart) {
            f.commonStarts.push_back(rec);
        } else {
            rec.player = clampByte(p.player + 1);
            f.specificStarts.push_back(rec);
        }
    }
    // The objects the systems list, by slot, numbered by position (§14.5).
    std::vector<const SpaceObject*> listed;
    for (const StarSystem& sys : g.systems)
        for (ObjectId id : sys.objects)
            if (id.index() < g.objects.size()) listed.push_back(&g.object(id));
    std::stable_sort(listed.begin(), listed.end(), [](const SpaceObject* a, const SpaceObject* b) { return a->slot < b->slot; });
    if (listed.size() > 65535) return std::unexpected(std::format("the map has {} objects; the original holds at most 65,535", listed.size()));
    std::map<uint32_t, size_t> position;   // ObjectId value -> place in the file
    for (size_t i = 0; i < listed.size(); ++i) position[listed[i]->id.value] = i;
    for (size_t i = 0; i < listed.size(); ++i) {
        const SpaceObject& obj = *listed[i];
        ObjectClass c = ObjectClass::Storm;
        switch (obj.kind) {
            case ObjectKind::Star:
            case ObjectKind::DestroyedStar: c = ObjectClass::Star; break;
            case ObjectKind::WarpPoint: c = ObjectClass::WarpPoint; break;
            case ObjectKind::Storm: c = ObjectClass::Storm; break;
            case ObjectKind::Planet:
            case ObjectKind::Asteroids: c = ObjectClass::Planet; break;
            case ObjectKind::Comet: c = ObjectClass::Comet; break;
            case ObjectKind::Count: break;
        }
        ObjectRecord o;
        o.cls = o.recordClass = static_cast<uint8_t>(c);
        o.id = static_cast<uint16_t>(i + 1);
        o.system = clampByte(int64_t{obj.system.value} + 1);
        o.sector = sectorByte(obj.sector);
        o.abilities = abilities(obj.abilities);
        o.sectorType = clampWord(int64_t{obj.sectorType} + 1);
        if (c == ObjectClass::Star || c == ObjectClass::Planet) o.name = text(obj.name);
        if (c == ObjectClass::WarpPoint && obj.destination.valid() && obj.destination.index() < g.objects.size()) {
            const SpaceObject& far = g.object(obj.destination);
            if (position.contains(far.id.value)) {
                o.destSystem = clampByte(int64_t{far.system.value} + 1);
                o.destSector = sectorByte(far.sector);
            }
        }
        if (c == ObjectClass::Planet) {
            o.conditions = toFloat80(xmath::fromDoubleBits(obj.conditions.bits));
            for (size_t k = 0; k < 3; ++k) o.value[k] = obj.value[k];
        }
        f.objects.push_back(std::move(o));
    }
    if (replaced) count("characters the original's text lacks (written as '?')", static_cast<int>(replaced));
    countedNotes(report, counts);
    return f;
}

// ---- Map files on disk --------------------------------------------------------------------------------------------

std::expected<QuadrantMap, std::string> readClassicMap(const ruleset::Ruleset& rs, const std::filesystem::path& file, ConversionReport& report) {
    auto bytes = readFileBytes(file, size_t{64} << 20);
    if (!bytes) return std::unexpected(bytes.error());
    auto decoded = decodeClassicMap(*bytes);
    if (!decoded) return std::unexpected(std::format("{}: the map could not be loaded: {}", file.filename().string(), decoded.error()));
    auto map = importClassicMap(rs, *decoded, report, file.stem().string());
    if (!map) return std::unexpected(std::format("{}: the map could not be loaded: {}", file.filename().string(), map.error()));
    return map;
}

std::expected<void, std::string> writeClassicMap(const ruleset::Ruleset& rs, const QuadrantMap& map, const std::filesystem::path& file,
                                                 ConversionReport& report, uint64_t keySeed) {
    auto f = exportClassicMap(rs, map, report, keySeed);
    if (!f) return std::unexpected(f.error());
    auto bytes = encodeClassicMap(*f);
    if (!bytes) return std::unexpected(bytes.error());
    std::error_code ec;
    if (file.has_parent_path()) std::filesystem::create_directories(file.parent_path(), ec);
    return writeFileAtomic(file, *bytes);
}

std::string classicFileName(std::string_view name, std::string_view extension) {
    std::string out;
    for (const char ch : name) {
        const auto c = static_cast<unsigned char>(ch);
        const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || ch == '-' || ch == '_' || ch == ' ' || ch == '.' || ch == '(' || ch == ')';
        if (c >= 'A' && c <= 'Z') out += static_cast<char>(c - 'A' + 'a');
        else if (ok) out += ch;
    }
    while (!out.empty() && (out.back() == ' ' || out.back() == '.')) out.pop_back();
    while (!out.empty() && out.front() == ' ') out.erase(out.begin());
    if (out.empty()) out = extension == kMapFileExtension ? "map" : "empire";
    return out + std::string(extension);
}

} // namespace opense4::game::classic
