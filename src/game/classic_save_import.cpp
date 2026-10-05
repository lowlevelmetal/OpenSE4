// Import of the original's saved games into a GameState (docs/spec/08 §3,
// §6): every field the spec maps, then OpenSE4's own load-time
// recomputation. What has no counterpart, or only an approximate one, is
// reported (ConversionReport).

#include "game/classic_save.hpp"
#include "game/classic_save_internal.hpp"

#include "datafile/datafile.hpp"
#include "game/design.hpp"
#include "game/diplomacy.hpp"
#include "game/economy.hpp"
#include "game/generate.hpp"
#include "game/log_picture.hpp"
#include "game/orders.hpp"
#include "game/query.hpp"
#include "game/rules.hpp"
#include "game/serialize.hpp"
#include "game/setup.hpp"
#include "game/sight.hpp"

#include <algorithm>
#include <format>
#include <map>

namespace opense4::game::classic {

using namespace detail;
using datafile::keysEqual;

namespace {

// A 1-based position within a list of n items.
constexpr bool inRange(size_t position, size_t n) { return position >= 1 && position <= n; }

// A reference to an object of the file's list: a stellar object or a vehicle.
struct ObjectRef {
    ObjectId object;
    VehicleId vehicle;
};

class Importer {
public:
    Importer(const Rules& r, const ClassicSave& in, ConversionReport& report) : r_(r), d_(r.data()), in_(in), report_(report) {}

    std::expected<GameState, std::string> run() {
        if (in_.version != kVersion || in_.prologue.version != kVersion)
            return std::unexpected(std::format("the game was saved by version {} of Space Empires IV; only version {} is supported", in_.prologue.version,
                                               kVersion));
        if (in_.traitCount != d_.racialTraits.size())
            return std::unexpected(std::format("the save was read for {} racial traits; the data set has {}", in_.traitCount,
                                               d_.racialTraits.size()));
        if (in_.empires.empty()) return std::unexpected(std::string("the save holds no empire"));
        if (in_.prologue.date < kDateBase) return std::unexpected(std::format("the game date {} is before 2400.0", in_.prologue.date));
        s_.turn = static_cast<uint32_t>(in_.prologue.date - kDateBase);
        s_.seed = static_cast<uint64_t>(std::max(0, in_.globals.seed));
        s_.rng.reseed(s_.seed);
        checkDataSet();
        options();
        systems();
        stellarObjects();
        empires();
        designs();
        vehicles();
        colonies();
        fleets();
        orders();
        logsAndMessages();
        events();
        knowledge();
        playerTurn();
        if (!error_.empty()) return std::unexpected(error_);
        finish();
        if (!error_.empty()) return std::unexpected(error_);
        notes();
        if (std::string problem = validateState(s_, &r_); !problem.empty())
            return std::unexpected(std::format("the imported game is inconsistent: {}", problem));
        return std::move(s_);
    }

private:
    const Rules& r_;
    const ruleset::Ruleset& d_;
    const ClassicSave& in_;
    ConversionReport& report_;
    GameState s_;
    std::string error_;
    std::vector<ObjectRef> refs_;                       // per file object
    std::map<std::pair<size_t, uint16_t>, FleetId> fleetOf_;   // (empire, number) -> fleet
    std::map<std::string, int> counts_;                 // approximations, for the notes

    // ---- Helpers ---------------------------------------------------------------------------------------

    void fail(std::string why) {
        if (error_.empty()) error_ = std::move(why);
    }
    void count(const std::string& what, int n = 1) { counts_[what] += n; }

    // A data-file position (1-based) as an index, or an error naming the record.
    std::optional<uint32_t> position(size_t pos, size_t size, std::string_view file, const std::string& where) {
        if (pos >= 1 && pos <= size) return static_cast<uint32_t>(pos - 1);
        fail(std::format("{} refers to record {} of {}, which has {} records: the save was made with another data set", where, pos, file, size));
        return std::nullopt;
    }

    SystemId system(size_t n, const std::string& where) {
        if (n == 0) return {};
        if (n > s_.galaxy.systems.size()) {
            fail(std::format("{} refers to system {}; the save has {}", where, n, s_.galaxy.systems.size()));
            return {};
        }
        return SystemId{n - 1};
    }
    static Sector sector(size_t b) {
        if (b > 168) b = 84;
        return Sector{static_cast<int>(b % 13), static_cast<int>(b / 13)};
    }
    EmpireId player(size_t p) const {
        if (p == 0 || p > s_.empires.size()) return {};
        return EmpireId{p - 1};
    }
    DesignId design(size_t id, const std::string& where) {
        if (id == 0) return {};
        if (id > s_.designs.size()) {
            fail(std::format("{} refers to design {}; the save has {}", where, id, s_.designs.size()));
            return {};
        }
        return DesignId{id - 1};
    }
    ObjectRef objectRef(size_t id) const {
        if (id == 0 || id > refs_.size()) return {};
        return refs_[id - 1];
    }
    uint32_t date(int32_t d) const { return d > kDateBase ? static_cast<uint32_t>(d - kDateBase) : 0; }

    static ruleset::Ability ability(const Ability& a) {
        return {std::string(abilityName(a.id)), a.description, std::to_string(a.value1), std::to_string(a.value2)};
    }
    std::vector<ruleset::Ability> abilities(const std::vector<Ability>& list) {
        std::vector<ruleset::Ability> out;
        for (const Ability& a : list) {
            if (a.id >= kAbilityIds) count("abilities with an id the table lacks");
            out.push_back(ability(a));
        }
        return out;
    }

    // ---- The data set (§4) ------------------------------------------------------------------------------

    void checkDataSet() {
        for (const EmpireRecord& e : in_.empires)
            if (e.techLevels.size() != d_.techAreas.size())
                return fail(std::format("the empires know {} tech areas; the data set has {}: the save was made with another data set",
                                        e.techLevels.size(), d_.techAreas.size()));
    }

    // ---- Options (§3.2, §3.3) ----------------------------------------------------------------------------

    void options() {
        const Options& o = in_.options;
        GameOptions& g = s_.options;
        if (inRange(o.quadrantType, d_.quadrantTypes.size())) g.quadrantType = d_.quadrantTypes[o.quadrantType - 1u].name;
        s_.galaxy.quadrantType = g.quadrantType;
        auto code = [](uint8_t c, int hi) { return std::clamp(int{c} - 1, 0, hi); };
        g.quadrantSize = code(o.quadrantSize, 2);
        g.allWarpPointsConnected = o.allWarpPointsConnected;
        g.noWarpPoints = o.noWarpPoints;
        g.warpPointsAnywhere = o.warpPointsAnywhere;
        g.allSystemsSeen = o.allSystemsSeen;
        g.omnipresent = o.omnipresent;
        g.finiteResources = o.finiteResources;
        g.allPlanetsSameSize = o.allPlanetsSameSize;
        g.eventFrequency = code(o.eventFrequency, 3);
        g.maxEventSeverity = code(o.maxEventSeverity, 3);
        g.techCost = code(o.techCost, 2);
        g.techAreasAllowed.clear();
        bool all = true;
        std::vector<uint8_t> allowed(d_.techAreas.size(), 1);
        for (size_t i = 0; i < d_.techAreas.size(); ++i) {
            if (!d_.techAreas[i].canBeRemoved) continue;
            allowed[i] = std::any_of(o.techAreasAllowed.begin(), o.techAreasAllowed.end(),
                                     [&](const std::string& n) { return keysEqual(n, d_.techAreas[i].name); });
            all = all && allowed[i];
        }
        if (!all) g.techAreasAllowed = allowed;
        static constexpr std::array<int64_t, 3> kResources{5000, 20000, 100000};
        const int64_t start = kResources[static_cast<size_t>(code(o.startingResources, 2))];
        g.startingResources = Resources{start, start, start};
        g.homePlanetValue = code(o.homePlanetValue, 2);
        static constexpr std::array<int, 4> kPlanets{1, 3, 5, 10};
        g.startingPlanets = kPlanets[static_cast<size_t>(code(o.startingPlanets, 3))];
        g.sameSystemAllowed = o.sameSystemAllowed;
        g.evenlyDistributed = o.evenlyDistributed;
        g.scoreDisplay = code(o.scoreDisplay, 2);
        g.startTechLevel = code(o.startTechLevel, 2);
        static constexpr std::array<int, 4> kPoints{0, 2000, 3000, 5000};
        g.racialPoints = kPoints[static_cast<size_t>(code(o.racialPoints, 3))];
        g.aiDifficulty = code(o.aiDifficulty, 2);
        g.aiBonus = code(o.aiBonus, 255);
        g.maxUnitsPerPlayer = o.maxUnitsPerPlayer;
        g.maxShipsPerPlayer = o.maxShipsPerPlayer;
        g.teamMode = o.teamMode;
        g.noTacticalCombat = o.noTacticalCombat;
        g.completeTechTree = o.completeTechTree;
        g.allowGifts = o.allowGifts;
        g.allowTechTrades = o.allowTechTrades;
        g.allowSurrender = o.allowSurrender;
        g.allowIntel = o.allowIntel;
        g.noRuins = o.noRuins;
        g.onlyBreathable = o.onlyBreathable;
        g.onlyHomeType = o.onlyHomeType;
        g.playersCanSaveMap = o.playersCanSaveMap;
        g.autosaveTurns = o.autosaveTurns;
        g.simultaneous = o.simultaneous;
        if (!o.gameMasterPassword.empty()) count("the game master password");
        if (o.cheatCodes) count("cheat codes allowed");

        const Victory& v = in_.victory;
        VictoryConditions& w = g.victory;
        auto years = [&](int32_t turns) {
            if (turns % 10 != 0) count("victory years that are not whole years");
            return static_cast<int>(xmath::divRoundHalfEven(turns, 10));
        };
        w.score = v.score;
        w.scoreValue = v.scoreValue;
        w.years = v.years;
        w.yearsValue = years(v.yearsTurns);
        w.percentOfSecond = v.percentOfSecond;
        w.percentOfSecondValue = v.percentOfSecondValue;
        w.techPercent = v.techPercent;
        w.techPercentValue = v.techPercentValue;
        w.peace = v.peace;
        w.peaceYears = years(v.peaceTurns);
        w.delay = v.delay;
        w.delayYears = years(v.delayTurns);
        s_.peacefulTurns = static_cast<uint32_t>(std::max(0, v.peacefulTurns));
        s_.gameOver = v.completed;
    }

    // ---- Systems and stellar objects (§3.5, §3.8) -------------------------------------------------------

    void systems() {
        s_.galaxy.width = kQuadrantWidth;
        s_.galaxy.height = kQuadrantHeight;
        for (size_t i = 0; i < in_.systems.size(); ++i) {
            const SystemRecord& y = in_.systems[i];
            StarSystem sys;
            sys.id = SystemId{i};
            sys.name = y.name;
            sys.position = GalaxyPos{std::max(0, int{y.x} - 1), std::max(0, int{y.y} - 1)};
            sys.physicalType = std::string(kPhysicalTypes[static_cast<size_t>(std::clamp(int{y.physicalType}, 1, 3) - 1)]);
            sys.abilities = abilities(y.abilities);
            sys.type = systemType(y);
            if (size_t{y.number} != i + 1) count("system numbers that differ from their positions");
            s_.galaxy.systems.push_back(std::move(sys));
        }
    }

    // §3.5: the type is not stored; match its copied attributes (inferred rule, §11.1 Q13).
    ruleset::SystemTypeId systemType(const SystemRecord& y) {
        const std::string_view physical = kPhysicalTypes[static_cast<size_t>(std::clamp(int{y.physicalType}, 1, 3) - 1)];
        std::optional<uint32_t> fallback;
        for (uint32_t t = 0; t < d_.systemTypes.size(); ++t) {
            const ruleset::SystemType& st = d_.systemTypes[t];
            if (!keysEqual(st.physicalType, physical)) continue;
            if (!fallback) fallback = t;
            if (keysEqual(st.description, y.typeDescription) && keysEqual(st.backgroundBitmap, y.backgroundBitmap)) return ruleset::SystemTypeId{t};
        }
        count("systems whose type was guessed from the physical type");
        return fallback ? ruleset::SystemTypeId{*fallback} : ruleset::SystemTypeId{};
    }

    void stellarObjects() {
        refs_.assign(in_.objects.size(), {});
        for (size_t i = 0; i < in_.objects.size() && error_.empty(); ++i) {
            const ObjectRecord& o = in_.objects[i];
            const ObjectClass c = o.objectClass();
            if (c == ObjectClass::Ship || c == ObjectClass::MineField || c == ObjectClass::SatelliteGroup || c == ObjectClass::FighterGroup ||
                c == ObjectClass::DroneGroup || o.blank())
                continue;
            const std::string where = std::format("object {} ({})", i + 1, displayName(c));
            if (size_t{o.id} != i + 1) count("object ids that differ from their positions");
            const auto type = position(o.sectorType, d_.sectorObjectTypes.size(), "SectType.txt", where);
            const SystemId sys = system(o.system, where);
            if (!type || !sys.valid()) {
                if (!sys.valid()) fail(std::format("{} is in no system", where));
                return;
            }
            SpaceObject obj;
            obj.id = ObjectId{s_.galaxy.objects.size()};
            obj.slot = static_cast<uint32_t>(i);
            obj.system = sys;
            obj.sector = sector(o.sector);
            const auto recordKind = parseObjectKind(d_.sectorObjectTypes[*type].physicalType);
            switch (c) {
                case ObjectClass::Star: obj.kind = recordKind == ObjectKind::DestroyedStar ? ObjectKind::DestroyedStar : ObjectKind::Star; break;
                case ObjectClass::WarpPoint: obj.kind = ObjectKind::WarpPoint; break;
                case ObjectClass::Storm: obj.kind = ObjectKind::Storm; break;
                case ObjectClass::Planet: obj.kind = recordKind == ObjectKind::Asteroids ? ObjectKind::Asteroids : ObjectKind::Planet; break;
                case ObjectClass::Comet: obj.kind = ObjectKind::Comet; break;
                default: break;
            }
            applySectorType(d_, obj, *type);
            obj.abilities = abilities(o.abilities);
            switch (c) {
                case ObjectClass::Star:
                case ObjectClass::Planet: obj.name = o.name; break;
                case ObjectClass::Storm: obj.name = "Storm"; break;
                case ObjectClass::WarpPoint: obj.name = "Warp Point"; break;
                default: obj.name = s_.galaxy.system(sys).name; break;
            }
            if (c == ObjectClass::Planet) {
                obj.conditions = Conditions{xmath::toDoubleBits(fromFloat80(o.conditions))};
                obj.value = o.value;
            }
            refs_[i].object = obj.id;
            s_.galaxy.system(sys).objects.push_back(obj.id);
            s_.galaxy.objects.push_back(std::move(obj));
        }
        // Warp links (§3.8.3): the far end is the warp point at the stored location.
        for (size_t i = 0; i < in_.objects.size() && error_.empty(); ++i) {
            const ObjectRecord& o = in_.objects[i];
            if (!refs_[i].object.valid() || o.objectClass() != ObjectClass::WarpPoint) continue;
            SpaceObject& wp = s_.galaxy.object(refs_[i].object);
            const SystemId to = inRange(o.destSystem, s_.galaxy.systems.size()) ? SystemId{o.destSystem - 1u} : SystemId{};
            ObjectId far;
            if (to.valid())
                for (ObjectId x : s_.galaxy.system(to).objects) {
                    const SpaceObject& cand = s_.galaxy.object(x);
                    if (cand.kind != ObjectKind::WarpPoint || cand.sector != sector(o.destSector)) continue;
                    // Prefer the one that leads back here.
                    const ObjectRecord& back = in_.objects[cand.slot];
                    const bool pairs = size_t{back.destSystem} == wp.system.index() + 1 && sector(back.destSector) == wp.sector;
                    if (!far.valid() || pairs) far = x;
                    if (pairs) break;
                }
            if (!far.valid()) {
                count("one-way warp points without a far end");
                report_.detail(std::format("The warp point in {} at ({}, {}) leads to a sector without a warp point; it leads nowhere now.",
                                           s_.galaxy.system(wp.system).name, wp.sector.x, wp.sector.y));
            }
            wp.destination = far;
        }
        s_.colonies.resize(s_.galaxy.objects.size());
    }

    // ---- Empires (§3.6) -----------------------------------------------------------------------------------

    void empires() {
        const size_t n = in_.empires.size();
        for (size_t i = 0; i < n && error_.empty(); ++i) {
            const EmpireRecord& in = in_.empires[i];
            const std::string where = std::format("empire {} ({})", i + 1, in.name);
            if (size_t{in.player} != i + 1) fail(std::format("{} has player number {}", where, in.player));
            Empire e;
            e.id = EmpireId{i};
            e.name = in.name;
            e.empireType = in.type;
            e.leaderTitle = in.leaderTitle;
            e.leaderName = in.leaderName;
            e.email = cleanEmail(in.email);
            race(in, e.race, where);
            e.color = defaultEmpireColor(i);
            if (in.computer) e.kind = in.neutral ? PlayerKind::Neutral : PlayerKind::Computer;
            else e.kind = PlayerKind::Human;
            e.neutral = !in.computer && in.neutral;
            e.alive = !in.destroyed;
            if (const std::string pw = trimmed(in.password); !pw.empty()) {
                std::string lower = pw;
                for (char& ch : lower)
                    if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
                e.passwordHash = hashPassword(lower);
                count("passwords (type them in lower case in OpenSE4)");
            }
            e.stockpile = Resources{in.stored[0], in.stored[1], in.stored[2]};
            e.researchPool = in.researchPoints;
            e.intelPool = in.intelPoints;
            // Research (§3.6.2).
            e.repeatResearch = in.repeatResearch;
            e.researchEvenly = in.researchEvenly;
            for (uint16_t u : in.uniqueAreas) e.uniqueAreasUnlocked.push_back(u);
            for (uint16_t l : in.techLevels) e.techLevels.push_back(l);
            for (const ResearchItem& p : in.research) {
                const auto area = position(p.area, d_.techAreas.size(), "TechArea.txt", where + " research");
                if (!area) return;
                e.research.push_back({ruleset::TechAreaId{*area}, p.spent});
                if (p.weight != 2) count("research projects with a share weight other than 2");
            }
            // Politics (§3.6.4) and the computer players' memory of each empire (§3.6.9).
            e.relations.assign(n, Relation{});
            for (size_t j = 0; j < n; ++j) {
                Relation& rel = e.relations[j];
                const PoliticsEntry& p = in.politics[j];
                rel.contact = j != i && p.treaty != kNoContact;
                rel.treaty = rel.contact ? treatyFromCode(p.treaty).value_or(Treaty::None) : Treaty::None;
                rel.dominant = p.dominant;
                rel.tradeTurns = p.tradeCounter;
                rel.treatyTurn = s_.turn;
                rel.agedTreaty = rel.treaty;
                rel.anger = in.anger[j];
                rel.turnsSinceWar = in.turnsSinceWar[j];
                if (rel.turnsSinceWar < 999) rel.lastWarTurn = static_cast<int32_t>(s_.turn) - rel.turnsSinceWar;
            }
            e.homeSystem = system(in.homeSystem, where + " home system");
            e.homeSector = sector(in.homeSector);
            for (uint8_t sys : in.systemsToAvoid)
                if (SystemId x = system(sys, where + " systems to avoid"); x.valid()) e.systemsToAvoid.push_back(x);
            for (const auto& [sys, sec] : in.taggedMinefields)
                if (SystemId x = system(sys, where + " tagged mine fields"); x.valid()) e.taggedMinefields.push_back({x, sector(sec)});
            for (size_t w = 0; w < e.waypoints.size(); ++w) {
                const Waypoint10& wp = in.waypoints[w];
                e.waypoints[w].name = wp.name;
                e.waypoints[w].set = wp.system != 0;
                if (wp.system != 0) e.waypoints[w].location = {system(wp.system, where + " waypoints"), sector(wp.sector)};
            }
            e.designTypes = in.designTypes;
            e.colonyTypes = in.colonyTypes;
            if (!in.queueTemplates.empty()) count("saved construction queue templates", static_cast<int>(in.queueTemplates.size()));
            e.repairPriorities = in.repairPriorities;
            for (const StrategyRecord& t : in.strategies) e.strategies.push_back(strategy(t));
            if (e.strategies.empty()) e.strategies.push_back({"Default", {}});
            e.experience = in.experience;
            // Computer player state (§3.6.9).
            e.aiState = std::max(0, int{in.aiState} - 1);
            e.aiTurnsInState = in.turnsInState;
            e.aiMemory.staging = system(in.staging, where);
            e.aiMemory.secured = system(in.secured, where);
            e.aiMemory.afterAttack = in.afterAttack;
            for (uint8_t x : in.defend)
                if (SystemId y = system(x, where); y.valid()) e.aiMemory.defend.push_back(y);
            for (uint8_t x : in.targets)
                if (SystemId y = system(x, where); y.valid()) e.aiMemory.targets.push_back(y);
            e.aiMinimalChanges = in.aiMinimalChanges;
            e.aiDifficulty = std::clamp(int{in.difficulty} - 1, 0, 2);
            e.ministerStyle = in.ministerStyle;
            e.useRaceMinisterStyle = in.useRaceMinisterStyle;
            e.ministersForNewVehicles = in.ministersForNewVehicles;
            e.ministers = 0;
            for (size_t k = 0; k < in.ministers.size(); ++k)
                if (in.ministers[k]) e.ministers |= uint32_t{1} << k;
            e.ministerAll = e.ministers == kAllMinisters;
            // Options (§3.6.7).
            const EmpireOptions& o = in.options;
            e.clearOrdersOnEncounter = o.clearOnAny ? EncounterClear::Any : o.clearOnEnemy ? EncounterClear::Enemy : EncounterClear::Never;
            e.avoidTaggedMinefields = o.avoidTaggedMines;
            e.avoidRestrictedSystems = o.avoidRestricted;
            e.chooseColonyType = o.chooseColonyType;
            e.interfaceOptions = interfaceOptions(o);
            s_.empires.push_back(std::move(e));
        }
        // Intelligence (§3.6.3), once every empire exists.
        for (size_t i = 0; i < n && error_.empty(); ++i) {
            const EmpireRecord& in = in_.empires[i];
            Empire& e = s_.empires[i];
            e.repeatIntel = in.repeatIntel;
            e.intelEvenly = in.intelEvenly;
            for (const IntelItem& p : in.intel) {
                const auto project = position(p.project, d_.intelProjects.size(), "IntelProjects.txt", std::format("empire {} intelligence", i + 1));
                if (!project) return;
                IntelProjectOrder order;
                order.project = *project;
                order.target = player(p.target);
                order.progress = p.spent;
                pendingIntel_.push_back({i, e.intel.size(), p.specific});
                e.intel.push_back(order);
            }
        }
    }

    struct PendingIntel {
        size_t empire = 0, index = 0;
        int32_t specific = kAnyTarget;
    };
    std::vector<PendingIntel> pendingIntel_;

    void race(const EmpireRecord& in, Race& race, const std::string& where) {
        const ruleset::RacePreset* preset = findPreset(r_, in.raceFolder);
        race.style = preset ? preset->folder : in.raceFolder;
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
        for (uint32_t h = 0; h < d_.happinessModels.size(); ++h)
            if (d_.happinessModels[h].name == in.happinessType) race.happinessModel = h;
        if (in.culture == 0) {
            race.culture = 0;
            count("empires without a culture (given the first)");
        } else if (auto c = position(in.culture, d_.cultures.size(), "Cultures.txt", where)) {
            race.culture = *c;
        }
        race.traits.clear();
        for (size_t t = 0; t < in.traits.size(); ++t)
            if (in.traits[t]) race.traits.push_back(static_cast<uint32_t>(t));
        for (size_t c = 0; c < race.characteristics.size(); ++c) race.characteristics[c] = in.characteristics[c];
        if (!in.artFolder.empty() && !keysEqual(in.artFolder, in.raceFolder)) count("empires whose art came from another race");
        if (!in.emblemFolder.empty()) count("rebel empires' own emblems");
    }

    static InterfaceOptions interfaceOptions(const EmpireOptions& o) {
        InterfaceOptions u;
        u.showLogAtTurnStart = o.showLogAtStart;
        u.confirmEndTurn = o.confirmEndTurn;
        u.confirmScrap = o.confirmScrap;
        u.confirmStellarManipulation = o.confirmStellar;
        u.confirmDeleteResearch = o.confirmDeleteResearch;
        u.confirmDeleteIntel = o.confirmDeleteIntel;
        u.confirmDeleteFirstQueueItem = o.confirmDeleteFirstQueueItem;
        u.noteSimilarAbilities = o.noteSimilar;
        u.skipUnderConstruction = o.skipUnderConstruction;
        u.skipDamaged = o.skipDamaged;
        u.stopOncePerLocation = o.stopOncePerLocation;
        u.skipInFleets = o.skipInFleets;
        u.warpPointNames = o.warpPointNames;
        u.planetNames = o.planetNames;
        u.colonizableMarkers = o.colonizableMarkers;
        u.systemGrid = o.systemGrid;
        u.coordinateLocation = o.coordinateLocation;
        u.facilityMarkers = o.facilityMarker1 ? 1 : 0;
        for (size_t k = 0; k < o.facilityMarkers2to12.size(); ++k)
            if (o.facilityMarkers2to12[k]) u.facilityMarkers = static_cast<uint16_t>(u.facilityMarkers | (1u << (k + 1)));
        u.galaxyGridLines = o.galaxyGridLines;
        u.galaxyWarpLines = o.galaxyWarpLines;
        u.latestConstructionOnly = o.latestConstruction;
        u.latestComponentsOnly = o.latestComponents;
        u.autoClaimColonized = o.autoClaim;
        u.logFilter = static_cast<uint8_t>(std::clamp(int{o.logFilter} - 1, 0, 7));
        u.planetsTab = static_cast<uint8_t>(std::max(0, int{o.planetsTab} - 1));
        u.planetsNoSysToAvoid = o.planetsHideAvoided;
        u.queuesTab = static_cast<uint8_t>(std::max(0, int{o.queuesTab} - 1));
        u.queuesShown = 0;
        for (size_t k = 0; k < o.queuesShown.size(); ++k)
            if (o.queuesShown[k]) u.queuesShown = static_cast<uint8_t>(u.queuesShown | (1u << k));
        u.simulatorNoObsolete = o.simulatorNoObsolete;
        u.shipsTab = static_cast<uint8_t>(std::max(0, int{o.shipsTab} - 1));
        u.shipsShown = 0;
        for (size_t k = 0; k < o.shipsShown.size(); ++k)
            if (o.shipsShown[k]) u.shipsShown = static_cast<uint8_t>(u.shipsShown | (1u << k));
        // The sort keys' column numbers are not known to match OpenSE4's
        // (§11.1 Q4): the windows start sorted by name.
        u.replayAnimate = o.replayAnimate;
        u.replayFast = o.replayFast;
        u.replayViewRect = o.replayViewRect;
        u.replayGrid = o.replayGrid;
        u.designToHit = o.designerToHit;
        u.designCondensed = o.designerCondensed;
        return u;
    }

    // §3.6.13: a strategy record as the keys of the strategy data files.
    static ruleset::CombatStrategy strategy(const StrategyRecord& t) {
        ruleset::CombatStrategy out;
        out.name = t.name;
        auto add = [&](std::string key, std::string value) { out.settings.emplace_back(std::move(key), std::move(value)); };
        auto yesNo = [](bool b) { return std::string(b ? "True" : "False"); };
        auto movement = [](uint8_t c) { return std::string(kMovementNames[static_cast<size_t>(std::clamp(int{c}, 1, 8) - 1)]); };
        add("Primary Movement Strategy", movement(t.primary));
        add("Secondary Movement Strategy", movement(t.secondary));
        for (size_t i = 0; i < t.targeting.size(); ++i)
            add(std::format("Targeting Priority {}", i + 1), std::string(kTargetingNames[std::min<size_t>(t.targeting[i], 12)]));
        add("Use Type Priority First", yesNo(t.typePriorityFirst));
        // In the order of the data files (TargetCategory).
        std::array<size_t, kTargetCategories> fileIndex{};
        for (size_t f = 0; f < kStrategyCategoryOrder.size(); ++f) fileIndex[static_cast<size_t>(kStrategyCategoryOrder[f])] = f;
        for (size_t c = 0; c < kTargetCategories; ++c)
            add(std::format("Type Priority {}", identifier(static_cast<TargetCategory>(c))), std::to_string(t.typePriority[fileIndex[c]]));
        for (size_t c = 0; c < kTargetCategories; ++c)
            add(std::format("Dont Fire On {}", identifier(static_cast<TargetCategory>(c))), yesNo(t.dontFireOn[fileIndex[c]]));
        add("Fighters Launch Group Amount", std::to_string(t.fighterGroup));
        add("Drones Per Target", std::to_string(t.dronesPerTarget));
        for (size_t c = 0; c < kTargetCategories; ++c) {
            const auto cat = static_cast<TargetCategory>(c);
            // The seekers' flags are fixed in the original (§3.6.13).
            if (cat == TargetCategory::SeekersOnUs || cat == TargetCategory::SeekersOnOthers) continue;
            add(std::format("Break Formation {}", identifier(cat)), yesNo(t.breakFormation[fileIndex[c]]));
        }
        add("Damage Percent Per Ship", std::to_string(t.damagePercent[0]));
        add("Damage Percent Per Planet", std::to_string(t.damagePercent[1]));
        add("Damage Percent Per Fighter Group", std::to_string(t.damagePercent[2]));
        add("Damage Percent Per Satellite Group", std::to_string(t.damagePercent[3]));
        add("Damage Until All Weapons Gone", yesNo(t.damageUntilWeaponsGone));
        return out;
    }

    // ---- Designs (§3.7) -------------------------------------------------------------------------------------

    void designs() {
        for (size_t i = 0; i < in_.designs.size() && error_.empty(); ++i) {
            const DesignRecord& in = in_.designs[i];
            const std::string where = std::format("design {} ({})", i + 1, in.name);
            Design d;
            d.id = DesignId{i};
            d.name = in.name;
            d.designType = in.type;
            d.templateName = in.templateName;
            d.createdTurn = date(in.created);
            d.obsolete = in.obsolete;
            d.everBuilt = in.everBuilt;
            d.built = in.built;
            d.lost = in.lost;
            d.scrapped = in.scrapped;
            d.enemyTonnageDestroyed = in.tonnageDestroyed;
            d.retrofitted = in.everBuilt && in.built == 0;
            const bool free = in.owner == 0;
            if (free) {
                // A free slot (§3.7): a placeholder no empire lists; its old
                // fields are kept when they still fit the data set.
                d.owner = EmpireId{0u};
                d.obsolete = true;
                const bool fits = inRange(in.hull, d_.vehicleSizes.size()) &&
                                  std::all_of(in.parts.begin(), in.parts.end(), [&](const DesignPart& p) {
                                      return inRange(p.component, d_.components.size()) && size_t{p.mount} <= d_.weaponMounts.size();
                                  });
                if (fits) {
                    d.hull = in.hull - 1u;
                    for (const DesignPart& p : in.parts) d.entries.push_back({p.component - 1u, p.mount ? int32_t{p.mount} - 1 : -1});
                }
                s_.designs.push_back(std::move(d));
                continue;
            }
            const EmpireId owner = player(in.owner);
            if (!owner.valid()) return fail(std::format("{} belongs to player {}; the save has {} empires", where, in.owner, s_.empires.size()));
            d.owner = owner;
            const auto hull = position(in.hull, d_.vehicleSizes.size(), "VehicleSize.txt", where);
            if (!hull) return;
            d.hull = *hull;
            for (const DesignPart& p : in.parts) {
                const auto comp = position(p.component, d_.components.size(), "Components.txt", where);
                if (!comp) return;
                int32_t mount = -1;
                if (p.mount != 0) {
                    const auto m = position(p.mount, d_.weaponMounts.size(), "CompEnhancement.txt", where);
                    if (!m) return;
                    mount = static_cast<int32_t>(*m);
                }
                d.entries.push_back({*comp, mount});
            }
            const size_t strategies = s_.empire(owner).strategies.size();
            d.strategy = in.strategy >= 1 && in.strategy <= strategies ? in.strategy - 1u : 0;
            if (!in.abilities.empty()) count("designs with extra abilities");
            // Last seen (§3.7): other players' knowledge; the owner's own entry, its last battle.
            for (size_t p = 0; p < s_.empires.size(); ++p) {
                if (in.lastSeen[p] <= 0) continue;
                const uint32_t seen = date(in.lastSeen[p]);
                if (EmpireId{p} == owner) s_.empire(owner).aiMemory.designsFought.push_back({d.id, seen});
                else seeDesign(s_.empires[p].knowledge, d.id, seen);
            }
            s_.empire(owner).designs.push_back(d.id);
            s_.designs.push_back(std::move(d));
        }
    }

    // ---- Vehicles (§3.8.6, §3.8.9, §3.8.10) ----------------------------------------------------------------------

    void vehicles() {
        for (size_t i = 0; i < in_.objects.size() && error_.empty(); ++i) {
            const ObjectRecord& o = in_.objects[i];
            const ObjectClass c = o.objectClass();
            const bool ship = c == ObjectClass::Ship;
            const bool group = c == ObjectClass::MineField || c == ObjectClass::SatelliteGroup || c == ObjectClass::FighterGroup || c == ObjectClass::DroneGroup;
            if ((!ship && !group) || o.blank()) continue;
            const std::string where = std::format("object {} ({})", i + 1, displayName(c));
            if (size_t{o.id} != i + 1) count("object ids that differ from their positions");
            Vehicle v;
            v.id = VehicleId{s_.nextVehicleId++};
            v.slot = static_cast<uint32_t>(i);
            v.owner = player(o.owner);
            if (!v.owner.valid()) return fail(std::format("{} belongs to player {}; the save has {} empires", where, o.owner, s_.empires.size()));
            v.location = {system(o.system, where), sector(o.sector)};
            if (!v.location.system.valid()) return fail(std::format("{} is in no system", where));
            v.minister = o.minister;
            v.arrival = ++s_.arrivals;   // each system's list is in slot order (§3.8)
            if (ship) {
                v.design = design(o.design, where);
                if (!v.design.valid()) return;
                const Design& d = s_.design(v.design);
                v.name = o.name;
                v.count = 1;
                v.damage.assign(d.entries.size(), 0);
                if (size_t{o.destroyedParts.capacity} != d.entries.size()) count("ships whose destroyed-part set does not match the design");
                for (size_t p = 0; p < d.entries.size(); ++p)
                    if (o.destroyedParts.test(p)) v.damage[p] = entryStructure(r_, d, p);
                v.supply = o.supply;
                v.movement = o.movement;
                v.heading = headingFromCode(o.heading);
                const int64_t tenths = std::clamp<int64_t>(float80Tenths(o.experience), 0, 100'000);
                v.experience = static_cast<int>(tenths / 10);
                v.experienceTenths = static_cast<int>(tenths % 10);
                if (o.status == 3) v.status = VehicleStatus::Mothballed;
                else if (o.cloaked) v.status = VehicleStatus::Cloaked;
                if (o.status == 2) count("ships under construction (now finished)");
                v.cargo = cargo(o.cargo, where);
                if (o.queue) v.queue = queue(*o.queue, where);
            } else {
                std::vector<UnitStack> stacks;
                for (const UnitEntry& u : o.units) {
                    if (u.count == 0) continue;
                    const DesignId d = design(u.design, where);
                    if (!d.valid()) return;
                    if (u.killed) count("units marked killed in a battle in progress");
                    auto it = std::find_if(stacks.begin(), stacks.end(), [&](const UnitStack& st) { return st.design == d; });
                    if (it == stacks.end()) stacks.push_back({d, u.count});
                    else it->count += u.count;
                }
                if (stacks.empty()) {
                    count("unit groups without units (dropped)");
                    --s_.nextVehicleId;
                    --s_.arrivals;
                    continue;
                }
                v.design = stacks.front().design;
                v.count = 0;
                for (const UnitStack& st : stacks) v.count += st.count;
                if (stacks.size() > 1) v.mixed = stacks;
                v.damage.assign(s_.design(v.design).entries.size(), 0);
                v.name = c == ObjectClass::DroneGroup ? o.name : s_.design(v.design).name;
                if (c == ObjectClass::FighterGroup || c == ObjectClass::DroneGroup) {
                    v.supply = o.supply;
                    v.movement = o.movement;
                    v.heading = headingFromCode(o.heading);
                }
                if (o.cloaked) v.status = VehicleStatus::Cloaked;
            }
            refs_[i].vehicle = v.id;
            s_.vehicles.push_back(std::move(v));
        }
    }

    Cargo cargo(const CargoRecord& c, const std::string& where) {
        Cargo out;
        out.population = population(c.population);
        for (const UnitEntry& u : c.units) {
            if (u.count == 0) continue;
            if (u.design >= 65000) {
                count("militia units in cargo (dropped)");
                continue;
            }
            const DesignId d = design(u.design, where);
            if (!d.valid()) continue;
            auto it = std::find_if(out.units.begin(), out.units.end(), [&](const UnitStack& st) { return st.design == d; });
            if (it == out.units.end()) out.units.push_back({d, u.count});
            else it->count += u.count;
        }
        return out;
    }

    // §3.8.7: equal players merge (at most 60,000) and empty entries go.
    std::vector<PopulationGroup> population(const std::vector<PopulationEntry>& list) {
        std::vector<PopulationGroup> out;
        for (const PopulationEntry& p : list) {
            if (p.millions <= 0) continue;
            const EmpireId race = player(p.player);
            if (!race.valid()) {
                count("population of unknown players (dropped)");
                continue;
            }
            auto it = std::find_if(out.begin(), out.end(), [&](const PopulationGroup& g) { return g.race == race; });
            if (it == out.end()) out.push_back({race, p.millions});
            else it->millions = std::min<int64_t>(60'000, it->millions + p.millions);
        }
        return out;
    }

    ConstructionQueue queue(const QueueRecord& q, const std::string& where) {
        ConstructionQueue out;
        out.onHold = q.onHold;
        out.repeat = q.repeat;
        out.emergency = q.emergency;
        (q.emergency ? out.emergencyTurns : out.slowTurns) = q.counter;
        out.autoWaypoint = q.rallyWaypoint >= 1 && q.rallyWaypoint <= 10 ? q.rallyWaypoint - 1 : -1;
        for (const QueueEntry& e : q.items) {
            QueueItem item;
            item.count = e.count;
            if (e.kind == 2) {
                item.kind = QueueItem::Kind::Vehicle;
                item.design = design(e.item, where + " queue");
                if (!item.design.valid()) continue;
            } else if (e.kind == 1 || e.kind == 3) {
                item.kind = e.kind == 1 ? QueueItem::Kind::Facility : QueueItem::Kind::Upgrade;
                const auto f = position(e.item, d_.facilities.size(), "Facility.txt", where + " queue");
                if (!f) continue;
                item.facility = *f;
            } else {
                count("queue items of an unknown kind (dropped)");
                continue;
            }
            out.items.push_back(item);
        }
        if (!out.items.empty()) out.items.front().spent = Resources{q.spent[0], q.spent[1], q.spent[2]};
        return out;
    }

    // ---- Colonies (§3.8.5) --------------------------------------------------------------------------------------

    void colonies() {
        for (size_t i = 0; i < in_.objects.size() && error_.empty(); ++i) {
            const ObjectRecord& o = in_.objects[i];
            if (!o.colony || !refs_[i].object.valid()) continue;
            const ColonyRecord& in = *o.colony;
            const std::string where = std::format("the colony on {}", o.name);
            Colony c;
            c.planet = refs_[i].object;
            c.owner = player(in.owner);
            if (!c.owner.valid()) {
                count("colonies of unknown players (dropped)");
                continue;
            }
            c.colonyType = in.type;
            c.population = population(in.population);
            c.anger = std::clamp(int{in.anger}, 0, kMaxAnger);
            c.plagueLevel = in.plague;
            c.cloaked = in.cloaked;
            c.atmosphereTurns = in.atmosphereTurns;
            c.cargo = cargo(in.cargo, where);
            for (const FacilityEntry& f : in.facilities) {
                const auto index = position(f.facility, d_.facilities.size(), "Facility.txt", where);
                if (!index) return;
                for (int k = 0; k < f.count; ++k) c.facilities.push_back(*index);
                if (f.destroyed) count("facilities marked destroyed in a battle in progress");
            }
            c.queue = queue(in.queue, where);
            for (const UnitEntry& u : in.landedTroops) {
                if (u.count == 0) continue;
                if (u.design >= 65000) {
                    count("militia units of a ground battle in progress (dropped)");
                    continue;
                }
                const DesignId d = design(u.design, where);
                if (d.valid()) c.landedTroops.push_back({d, u.count});
            }
            c.invader = player(in.invader);
            c.militia = c.invader.valid() ? in.militia : -1;
            if (!c.invader.valid()) c.landedTroops.clear();
            c.homeworld = in.capital;
            c.minister = in.minister;
            s_.colonies[c.planet.index()] = std::move(c);
        }
    }

    // ---- Fleets (§3.6.12) -----------------------------------------------------------------------------------------

    void fleets() {
        for (size_t e = 0; e < in_.empires.size(); ++e) {
            const EmpireRecord& in = in_.empires[e];
            for (size_t k = 0; k < in.fleets.size(); ++k) {
                const FleetRecord& f = in.fleets[k];
                if (f.name.empty() && f.system == 0 && f.sector == 0) continue;   // a free slot
                Fleet fleet;
                fleet.id = FleetId{s_.nextFleetId++};
                fleet.owner = EmpireId{e};
                fleet.name = f.name;
                fleet.location = {system(f.system, std::format("fleet {} of empire {}", k + 1, e + 1)), sector(f.sector)};
                fleet.formation = inRange(f.formation, d_.formations.size()) ? f.formation - 1u : 0;
                const size_t strategies = s_.empires[e].strategies.size();
                fleet.strategy = f.strategy >= 1 && f.strategy <= strategies ? f.strategy - 1u : 0;
                const int64_t tenths = std::clamp<int64_t>(float80Tenths(f.experience), 0, 100'000);
                fleet.experience = static_cast<int>(tenths / 10);
                fleet.experienceTenths = static_cast<int>(tenths % 10);
                fleet.minister = f.minister;
                fleetOf_[{e, static_cast<uint16_t>(k + 1)}] = fleet.id;
                pendingLeaders_.push_back({fleet.id, f.leader});
                s_.fleets.push_back(std::move(fleet));
            }
        }
        // Members name their fleet (§3.8.6); slot order is object order.
        for (size_t i = 0; i < in_.objects.size(); ++i) {
            const ObjectRecord& o = in_.objects[i];
            if (!refs_[i].vehicle.valid() || o.fleet == 0) continue;
            Vehicle* v = s_.vehicle(refs_[i].vehicle);
            const auto it = fleetOf_.find({v->owner.index(), o.fleet});
            if (it == fleetOf_.end()) {
                count("vehicles naming a fleet that does not exist (released)");
                continue;
            }
            v->fleet = it->second;
            s_.fleet(it->second)->members.push_back(v->id);
        }
        // Fleets with no member in their own sector are disbanded; the others
        // move to their first member (§3.6.12, as the original does on load).
        std::vector<FleetId> gone;
        for (Fleet& f : s_.fleets) {
            const bool here = std::any_of(f.members.begin(), f.members.end(), [&](VehicleId m) { return s_.vehicle(m)->location == f.location; });
            if (f.members.empty() || !here || !f.location.system.valid()) {
                for (VehicleId m : f.members) s_.vehicle(m)->fleet = {};
                gone.push_back(f.id);
                count("fleets without a member in their sector (disbanded, as the original does)");
                continue;
            }
            f.location = s_.vehicle(f.members.front())->location;
        }
        std::erase_if(s_.fleets, [&](const Fleet& f) { return std::find(gone.begin(), gone.end(), f.id) != gone.end(); });
        for (const auto& [fleet, leader] : pendingLeaders_) {
            Fleet* f = s_.fleet(fleet);
            if (!f || leader == 0) continue;
            const ObjectRef ref = objectRef(leader);
            if (ref.vehicle.valid() && std::find(f->members.begin(), f->members.end(), ref.vehicle) != f->members.end()) f->leader = ref.vehicle;
        }
    }
    std::vector<std::pair<FleetId, uint16_t>> pendingLeaders_;

    // ---- Orders (§3.8.8) ---------------------------------------------------------------------------------------------

    void orders() {
        for (size_t i = 0; i < in_.objects.size(); ++i) {
            const ObjectRecord& o = in_.objects[i];
            if (refs_[i].vehicle.valid()) {
                Vehicle* v = s_.vehicle(refs_[i].vehicle);
                v->orders = orderList(o.orders, v->owner, v, nullptr);
                v->repeatOrders = o.orders.repeat;
            } else if (o.colony && refs_[i].object.valid()) {
                if (Colony* c = s_.colony(refs_[i].object)) {
                    c->orders = orderList(o.colony->orders, c->owner, nullptr, c);
                    c->repeatOrders = o.colony->orders.repeat;
                }
            }
        }
    }

    // A unit design of this kind for a load, drop, launch or recover: the
    // first stack of that kind in `cargo`, else the owner's newest design of it.
    DesignId unitOfKind(EmpireId owner, ruleset::VehicleType type, const Cargo* cargo) {
        if (cargo)
            for (const UnitStack& u : cargo->units)
                if (r_.hull(s_.design(u.design).hull).type == type) return u.design;
        const Empire& e = s_.empire(owner);
        for (auto it = e.designs.rbegin(); it != e.designs.rend(); ++it)
            if (r_.hull(s_.design(*it).hull).type == type) return *it;
        return {};
    }

    std::vector<Order> orderList(const OrderList& list, EmpireId owner, const Vehicle* vehicle, const Colony* colony) {
        std::vector<Order> out;
        std::vector<Order> converted;
        for (const OrderRecord& rec : list.orders) {
            std::optional<Order> o = order(rec, owner, vehicle, colony);
            if (o) converted.push_back(*o);
            else converted.push_back(Order{OrderKind::Count});   // a placeholder keeps the positions
        }
        // OpenSE4 keeps the current order first: rotate (§3.8.8).
        const size_t current = inRange(list.current, converted.size()) ? list.current - 1u : 0;
        if (current > 0) count("order lists turned to start at their current order");
        for (size_t k = 0; k < converted.size(); ++k) {
            const Order& o = converted[(current + k) % converted.size()];
            if (o.kind != OrderKind::Count) out.push_back(o);
        }
        return out;
    }

    std::optional<Order> order(const OrderRecord& rec, EmpireId owner, const Vehicle* vehicle, const Colony* colony) {
        Order o;
        const SystemId sys = inRange(rec.system, s_.galaxy.systems.size()) ? SystemId{rec.system - 1u} : SystemId{};
        const Location at{sys, sector(rec.sector)};
        const ObjectRef target = objectRef(rec.target);
        auto stellar = [&](StellarAction a) {
            o.kind = OrderKind::StellarManipulation;
            o.amount = static_cast<int>(a);
            return o;
        };
        auto dropped = [&](std::string what) {
            count(std::move(what));
            return std::optional<Order>{};
        };
        switch (rec.kind) {
            case 1:
                if (!sys.valid()) return dropped("orders with a missing target (dropped)");
                o.kind = OrderKind::MoveTo;
                o.location = at;
                return o;
            case 2:
                if (rec.target < 1 || rec.target > 10) return dropped("orders with a missing target (dropped)");
                o.kind = OrderKind::MoveToWaypoint;
                o.amount = rec.target - 1;
                return o;
            case 3:
                if (!target.object.valid()) return dropped("orders with a missing target (dropped)");
                o.kind = OrderKind::Warp;
                o.object = target.object;
                return o;
            case 4:
                if (!target.object.valid()) return dropped("orders with a missing target (dropped)");
                o.kind = OrderKind::Colonize;
                o.object = target.object;
                o.amount = kColonizeExpanded;   // its colonists were loaded when it was given
                return o;
            case 6:
            case 7: {
                o.kind = rec.kind == 6 ? OrderKind::LoadCargo : OrderKind::DropCargo;
                o.amount = -1;
                if (sys.valid()) o.location = at;
                if (rec.kind == 7) o.object = target.object;
                if (rec.extra != 1) {
                    const auto type = cargoKindType(rec.extra);
                    if (!type) return dropped("load and drop orders of an unknown cargo kind (dropped)");
                    o.design = unitOfKind(owner, *type, rec.kind == 7 && vehicle ? &vehicle->cargo : nullptr);
                    if (!o.design.valid()) return dropped("load and drop orders for units the empire has no design of (dropped)");
                    count("load and drop orders by cargo kind (now one design of that kind)");
                }
                return o;
            }
            case 8:
                o.kind = OrderKind::Attack;
                if (sys.valid()) o.location = at;
                o.vehicle = target.vehicle;
                o.object = target.object;
                return o;
            case 9: o.kind = OrderKind::Scrap; return o;
            case 10:
            case 12:
                if (!sys.valid()) return dropped("orders with a missing target (dropped)");
                o.kind = OrderKind::Seek;
                o.location = at;
                return o;
            case 11:
            case 13:
                if (!target.vehicle.valid() && !target.object.valid()) return dropped("orders with a missing target (dropped)");
                o.kind = OrderKind::Seek;
                o.vehicle = target.vehicle;
                o.object = target.object;
                if (sys.valid()) o.location = at;
                return o;
            case 15: o.kind = OrderKind::SweepMines; return o;
            case 17: {
                if (!inRange(rec.target, s_.galaxy.systems.size())) return dropped("orders with a missing target (dropped)");
                stellar(StellarAction::OpenWarpPoint);
                o.location = {SystemId{rec.target - 1u}, Sector{}};
                return o;
            }
            case 18: stellar(StellarAction::CloseWarpPoint); o.object = target.object; return o;
            case 20: stellar(StellarAction::DestroyStorm); o.object = target.object; return o;
            case 22: stellar(StellarAction::DestroyPlanet); o.object = target.object; return o;
            case 24: stellar(StellarAction::DestroyStar); o.object = target.object; return o;
            case 19: stellar(StellarAction::CreateStorm); if (sys.valid()) o.location = at; return o;
            case 21: stellar(StellarAction::CreatePlanet); o.object = target.object; if (sys.valid()) o.location = at; return o;
            case 23: stellar(StellarAction::CreateStar); if (sys.valid()) o.location = at; return o;
            case 61: stellar(StellarAction::DestroyNebulae); if (sys.valid()) o.location = at; return o;
            case 63: stellar(StellarAction::DestroyBlackHole); if (sys.valid()) o.location = at; return o;
            case 60: stellar(StellarAction::CreateNebulae); o.object = target.object; return o;
            case 62: stellar(StellarAction::CreateBlackHole); o.object = target.object; return o;
            case 66:
                stellar(StellarAction::CreateConstructedPlanet);
                o.object = target.object;
                count("stellar construction orders (their size parameter is not known, §11.1 Q7)");
                return o;
            case 25: o.kind = OrderKind::Sentry; return o;
            case 34:
            case 35: {
                o.kind = rec.kind == 34 ? OrderKind::LaunchUnits : OrderKind::RecoverUnits;
                o.amount = -1;
                const auto type = unitKindType(rec.extra);
                if (!type) return dropped("launch and recover orders of an unknown unit kind (dropped)");
                const Cargo* from = vehicle ? &vehicle->cargo : colony ? &colony->cargo : nullptr;
                o.design = unitOfKind(owner, *type, rec.kind == 34 ? from : nullptr);
                if (!o.design.valid()) return dropped("launch and recover orders for units the empire has no design of (dropped)");
                count("launch and recover orders by unit kind (now one design of that kind)");
                return o;
            }
            case 42: o.kind = OrderKind::Analyze; return o;
            case 43: o.kind = OrderKind::Mothball; return o;
            case 44: o.kind = OrderKind::Unmothball; return o;
            case 45: o.kind = OrderKind::SelfDestruct; return o;
            case 46: o.kind = OrderKind::FireOn; return o;
            case 47: {
                const auto it = fleetOf_.find({owner.index(), rec.target});
                if (it == fleetOf_.end() || !s_.fleet(it->second)) return dropped("Join Fleet orders for a fleet that is gone (dropped)");
                o.kind = OrderKind::JoinFleet;
                o.amount = static_cast<int>(it->second.value);
                return o;
            }
            case 52:
                o.kind = OrderKind::Retrofit;
                o.design = design(rec.target, "a retrofit order");
                if (!o.design.valid()) return dropped("orders with a missing target (dropped)");
                return o;
            case 58:
                o.kind = OrderKind::UseComponent;
                o.amount = std::max(0, int{rec.target} - 1);
                return o;
            case 59: {
                o.kind = OrderKind::UseFacility;
                // The position in the colony's grouped facility list: its first facility in ours.
                int position = std::max(0, int{rec.target} - 1);
                if (colony) {
                    std::vector<uint32_t> kinds;
                    int flat = -1;
                    for (size_t k = 0; k < colony->facilities.size(); ++k)
                        if (std::find(kinds.begin(), kinds.end(), colony->facilities[k]) == kinds.end()) {
                            if (static_cast<int>(kinds.size()) == position) flat = static_cast<int>(k);
                            kinds.push_back(colony->facilities[k]);
                        }
                    if (flat >= 0) position = flat;
                }
                o.amount = position;
                return o;
            }
            case 64: return dropped("Abandon Planet orders (OpenSE4 abandons at once; dropped)");
            case 65:
                if (rec.extra < 1 || rec.extra > 3 || rec.system < 1 || rec.system > 3) return dropped("orders with a missing target (dropped)");
                o.kind = OrderKind::ConvertResources;
                o.amount = rec.target;
                o.from = static_cast<uint8_t>(rec.extra - 1);
                o.to = static_cast<uint8_t>(rec.system - 1);
                return o;
            default: return dropped(std::format("orders of kind {} (dropped)", rec.kind));
        }
    }

    // ---- The log and messages (§3.6.11) ----------------------------------------------------------------------------

    std::optional<PackageItem> packageItem(const PackageItemRecord& p) {
        PackageItem item;
        switch (p.kind) {
            case 1:
                item.kind = PackageItem::Kind::System;
                item.system = inRange(p.value, s_.galaxy.systems.size()) ? SystemId{p.value - 1u} : SystemId{};
                return item;
            case 2:
                item.kind = PackageItem::Kind::Planet;
                item.planet = objectRef(p.value).object;
                return item;
            case 3:
                if (p.value < 1 || p.value > 3) break;
                item.kind = PackageItem::Kind::Resources;
                item.resources.v[p.value - 1u] = int64_t{p.quantity} * 1000;
                return item;
            case 4:
                if (!inRange(p.value, d_.techAreas.size())) break;
                item.kind = PackageItem::Kind::Technology;
                item.tech = ruleset::TechAreaId{p.value - 1u};
                return item;
            case 5:
            case 6:
                item.kind = PackageItem::Kind::Vehicle;
                item.vehicle = objectRef(p.value).vehicle;
                return item;
            case 7:
                item.kind = PackageItem::Kind::StarChart;
                item.system = inRange(p.value, s_.galaxy.systems.size()) ? SystemId{p.value - 1u} : SystemId{};
                return item;
            case 8:
                item.kind = PackageItem::Kind::Treaty;
                item.treaty = treatyFromCode(static_cast<uint8_t>(p.value)).value_or(Treaty::None);
                return item;
            case 9:
                item.kind = PackageItem::Kind::CommChannel;
                item.empire = player(p.value);
                return item;
            default: break;
        }
        count("message package items without an OpenSE4 counterpart (dropped)");
        return std::nullopt;
    }

    void logsAndMessages() {
        static constexpr std::array<LogGoto, 8> kGoto{LogGoto::None, LogGoto::Location, LogGoto::ConstructionQueues, LogGoto::Research,
                                                      LogGoto::Intelligence, LogGoto::EmpireOptions, LogGoto::Designs, LogGoto::Empires};
        for (size_t e = 0; e < in_.empires.size() && error_.empty(); ++e) {
            for (const LogRecord& l : in_.empires[e].log) {
                LogEntry entry;
                entry.turn = date(l.date);
                entry.category = static_cast<LogCategory>(std::clamp(int{l.category}, 1, 7) - 1);
                entry.title = l.title;
                entry.text = l.text;
                if (inRange(l.system, s_.galaxy.systems.size())) entry.location = Location{SystemId{l.system - 1u}, sector(l.sector)};
                entry.target = size_t{l.target} < kGoto.size() ? kGoto[l.target] : LogGoto::None;
                entry.picture = picture(l);
                if (l.battle) count("battle details of combat log entries (the entries keep their text)");
                if (l.message) {
                    if (auto m = message(*l.message, l, EmpireId{e})) {
                        entry.message = m->id;
                        s_.messages.push_back(std::move(*m));
                    }
                }
                s_.empires[e].log.push_back(std::move(entry));
            }
        }
    }

    std::string picture(const LogRecord& l) {
        const size_t key = l.picture;
        const auto& d = d_;
        switch (l.kind) {
            case 4:
                if (inRange(key, s_.designs.size())) return logpicture::hull(DesignId{key - 1});
                break;
            case 5:
                if (inRange(key, d.facilities.size())) return logpicture::facility(static_cast<uint32_t>(key - 1));
                break;
            case 29:
                if (inRange(key, d.components.size())) return logpicture::developed(r_, key - 1);
                break;
            case 30:
                if (inRange(key, d.facilities.size())) return logpicture::developed(r_, d.components.size() + key - 1);
                break;
            case 31:
                if (inRange(key, d.vehicleSizes.size())) return logpicture::developed(r_, d.components.size() + d.facilities.size() + key - 1);
                break;
            case 37:
                if (inRange(key, d.intelProjects.size()))
                    return logpicture::developed(r_, d.components.size() + d.facilities.size() + d.vehicleSizes.size() + key - 1);
                break;
            case 19:
                if (l.message && inRange(l.message->sender, s_.empires.size()))
                    return logpicture::race(EmpireId{l.message->sender - 1u});
                break;
            default: break;
        }
        return {};
    }

    std::optional<DiplomaticMessage> message(const MessageRecord& in, const LogRecord& l, EmpireId logOwner) {
        DiplomaticMessage m;
        m.from = player(in.sender);
        m.to = player(in.recipient);
        if (!m.from.valid() || !m.to.valid() || m.to != logOwner) return std::nullopt;
        const auto type = messageTypeFromCode(in.type);
        if (!type) count("messages of a type OpenSE4 lacks (now General)");
        m.id = MessageId{s_.nextMessageId++};
        m.type = type.value_or(MessageType::General);
        m.sentTurn = date(l.date);
        m.dated = date(l.date);
        m.tone = std::clamp(int{in.tone} - 1, 0, 2);
        m.text = l.text;
        m.treaty = in.treaty == kNoContact ? Treaty::None : treatyFromCode(in.treaty).value_or(Treaty::None);
        m.thirdEmpire = player(in.third);
        m.system = inRange(in.system, s_.galaxy.systems.size()) ? SystemId{in.system - 1u} : SystemId{};
        m.planet = objectRef(in.planet).object;
        for (const PackageItemRecord& p : in.offered)
            if (auto item = packageItem(p)) m.offer.push_back(*item);
        for (const PackageItemRecord& p : in.requested)
            if (auto item = packageItem(p)) m.request.push_back(*item);
        m.delivered = true;
        m.answered = !answerableMessage(m.type);
        return m;
    }

    // ---- Timed events, starting points (§3.4, §3.5) ---------------------------------------------------------------

    void events() {
        for (const TimedEvent& t : in_.events) {
            if (t.event == 0) continue;   // a free slot
            const auto type = position(t.event, d_.eventTypes.size(), "Events.txt", "a timed event");
            if (!type) return;
            PendingEvent pe;
            pe.eventType = *type;
            pe.fireTurn = date(t.date);
            pe.system = inRange(t.system, s_.galaxy.systems.size()) ? SystemId{t.system - 1u} : SystemId{};
            pe.empire = player(t.player);
            switch (eventTarget(r_, *type)) {
                case EventTarget::Vehicle: pe.vehicle = objectRef(t.target).vehicle; break;
                case EventTarget::Object: pe.object = objectRef(t.target).object; break;
                case EventTarget::System:
                    if (inRange(t.target, s_.galaxy.systems.size())) pe.system = SystemId{t.target - 1u};
                    break;
                case EventTarget::Empire:
                    if (EmpireId e = player(t.target); e.valid()) pe.empire = e;
                    break;
            }
            s_.pendingEvents.push_back(pe);
        }
        const size_t slots = in_.events.size();
        if (slots >= 5) count("timed-event slots: the original schedules no more timed events in this game (spec 08 §11.2)");
        auto add = [&](const StartPointRecord& p, int who) {
            if (!inRange(p.system, s_.galaxy.systems.size())) return;
            const StartingPoint sp{SystemId{p.system - 1u}, sector(p.sector), who};
            if (std::find(s_.startingPoints.begin(), s_.startingPoints.end(), sp) == s_.startingPoints.end()) s_.startingPoints.push_back(sp);
        };
        for (const StartPointRecord& p : in_.specificStarts) add(p, p.player >= 1 ? p.player - 1 : kCommonStart);
        for (const StartPointRecord& p : in_.commonStarts) add(p, kCommonStart);
    }

    // ---- Knowledge (§3.5, §6.3) -----------------------------------------------------------------------------------------

    void knowledge() {
        const size_t nSys = s_.galaxy.systems.size();
        for (Empire& e : s_.empires) {
            const size_t p = e.id.index();
            Knowledge& k = e.knowledge;
            k.explored.assign(nSys, 0);
            k.present.assign(nSys, 0);
            k.lastSeen.assign(nSys, 0);
            k.notes.assign(nSys, {});
            for (size_t i = 0; i < nSys; ++i) {
                const SystemRecord& y = in_.systems[i];
                k.explored[i] = y.explored.test(p) || s_.options.allSystemsSeen ? 1 : 0;
                if (k.explored[i]) k.lastSeen[i] = s_.turn;   // not stored: the import date (inferred, §6.3)
                k.notes[i] = y.notes[p];
                if (y.claimed.test(p)) e.claimedSystems.push_back(SystemId{i});
            }
            // Known warp links: those of explored systems (inferred, §6.3).
            k.knownWarpLink.assign(s_.galaxy.objects.size(), 0);
            for (const SpaceObject& o : s_.galaxy.objects)
                if (o.kind == ObjectKind::WarpPoint && k.explored[o.system.index()]) k.knownWarpLink[o.id.index()] = 1;
            std::sort(k.seenDesigns.begin(), k.seenDesigns.end(), [](const SeenDesign& a, const SeenDesign& b) { return a.design < b.design; });
        }
        // Intelligence projects' specific targets, now that every list exists (§3.6.3).
        for (const PendingIntel& p : pendingIntel_) {
            IntelProjectOrder& order = s_.empires[p.empire].intel[p.index];
            if (p.specific == kAnyTarget || p.specific <= 0) continue;
            const auto specific = static_cast<size_t>(p.specific);
            switch (intelTarget(r_, order.project)) {
                case IntelTarget::Vehicle: order.targetVehicle = objectRef(specific).vehicle; break;
                case IntelTarget::Planet: order.targetPlanet = objectRef(specific).object; break;
                case IntelTarget::Tech:
                    if (specific <= d_.techAreas.size()) order.targetTech = ruleset::TechAreaId{specific - 1};
                    break;
                case IntelTarget::Empire: order.thirdEmpire = player(specific); break;
                case IntelTarget::System: count("intelligence projects aimed at a specific system (now any)"); break;
                case IntelTarget::None: break;
            }
        }
    }

    // ---- The player turn (§3.4, §3.9) ---------------------------------------------------------------------------------------

    void playerTurn() {
        if (s_.options.simultaneous) return;
        s_.playerTurn.empire = player(in_.globals.currentPlayer);
        // The original saves within a player's turn (inferred, §11.1 Q14).
        s_.playerTurn.started = s_.playerTurn.empire.valid();
        for (const LaunchRecord& l : in_.launched) {
            const ObjectRef from = objectRef(l.launcher);
            TurnLaunches t;
            t.vehicle = from.vehicle;
            t.planet = from.vehicle.valid() ? ObjectId{} : from.object;
            const auto type = unitKindType(l.kind);
            if ((!t.vehicle.valid() && !t.planet.valid()) || !type) continue;
            AbilityKind k = AbilityKind::Unknown;
            switch (*type) {
                case ruleset::VehicleType::Fighter: k = AbilityKind::LaunchRecoverFighters; break;
                case ruleset::VehicleType::Satellite: k = AbilityKind::LaunchRecoverSatellites; break;
                case ruleset::VehicleType::Mine: k = AbilityKind::LayMines; break;
                case ruleset::VehicleType::Drone: k = AbilityKind::LaunchDrones; break;
                default: break;
            }
            if (k == AbilityKind::Unknown) continue;
            t.kind = static_cast<uint16_t>(k);
            t.count = l.count;
            s_.playerTurn.launched.push_back(t);
        }
    }

    // ---- OpenSE4's own recomputation after loading (§5.3, §6) ---------------------------------------------------------------

    void finish() {
        for (Empire& e : s_.empires) e.racialPointsSpent = racialPointCost(r_, e.race);
        std::sort(s_.vehicles.begin(), s_.vehicles.end(), [](const Vehicle& a, const Vehicle& b) { return a.id < b.id; });
        std::sort(s_.fleets.begin(), s_.fleets.end(), [](const Fleet& a, const Fleet& b) { return a.id < b.id; });
        sight::updateKnowledge(r_, s_);
        diplomacy::recalculateColonies(r_, s_);
        economy::updateReports(r_, s_);
    }

    void notes() {
        for (const auto& [what, n] : counts_) {
            if (n <= 0) continue;
            report_.detail(std::format("{}: {}", what, n));
        }
        std::vector<std::string> shortNotes;
        auto has = [&](std::string_view prefix) {
            for (const auto& [what, n] : counts_)
                if (n > 0 && what.starts_with(prefix)) return true;
            return false;
        };
        if (has("load and drop orders by cargo kind") || has("launch and recover orders by unit kind"))
            report_.note("Load, drop, launch and recover orders now name one unit design of their kind.");
        if (has("one-way warp points")) report_.note("One-way warp points of the original lead nowhere in OpenSE4.");
        if (has("passwords")) report_.note("Empire passwords carry over; type them in lower case.");
        if (has("battle details")) report_.note("Combat log entries keep their text but not their battle details.");
        if (has("ships under construction")) report_.note("Ships the original had under construction are finished.");
        if (has("saved construction queue templates")) report_.note("Saved construction queue templates are not carried over.");
        if (has("fleets without a member")) report_.note("Fleets with no ship in their own sector were disbanded, as the original does on loading.");
        report_.note("The computer players start with partly empty memories, and the random sequence starts again from the game's seed.");
        report_.note("History graphs start at the import date.");
    }
};

} // namespace

std::expected<GameState, std::string> importClassicSave(const Rules& rules, const ClassicSave& save, ConversionReport& report) {
    Importer importer(rules, save, report);
    return importer.run();
}

std::expected<GameState, std::string> readClassicGame(const Rules& rules, const std::filesystem::path& file, ConversionReport& report) {
    auto bytes = readFileBytes(file);
    if (!bytes) return std::unexpected(bytes.error());
    const size_t traits = rules.data().racialTraits.size();
    auto save = decodeClassicSave(*bytes, traits);
    if (!save) {
        // Damage, or a data set with another number of racial traits (§3.6.5)?
        const std::vector<size_t> fits = traitCountsThatDecode(*bytes);
        if (!fits.empty() && std::find(fits.begin(), fits.end(), traits) == fits.end())
            return std::unexpected(std::format("{}: the game was saved with a data set of {} racial traits; the loaded data set has {}. "
                                               "Load it with the data set (mod) it was played with.",
                                               file.string(), fits.front(), traits));
        return std::unexpected(std::format("{}: the saved game is damaged: {}", file.string(), save.error()));
    }
    auto state = importClassicSave(rules, *save, report);
    if (!state) return std::unexpected(std::format("{}: {}", file.string(), state.error()));
    return state;
}

} // namespace opense4::game::classic
