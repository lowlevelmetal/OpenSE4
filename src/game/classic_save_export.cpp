// Export of a GameState as a saved game of the original (docs/spec/08 §7):
// every section in the original's layout, with the values the original
// trusts computed as it computes them (design speed, cost and type code,
// ship maximum movement), defaults where OpenSE4 holds nothing, and a report
// of what could not be carried.

#include "game/classic_save.hpp"
#include "game/classic_save_internal.hpp"

#include "datafile/datafile.hpp"
#include "game/ai_planner.hpp"
#include "game/design.hpp"
#include "game/movement.hpp"
#include "game/query.hpp"
#include "game/rules.hpp"
#include "game/serialize.hpp"

#include <algorithm>
#include <cctype>
#include <format>
#include <limits>
#include <map>
#include <set>

namespace opense4::game::classic {

using namespace detail;
using datafile::keysEqual;

namespace {

int32_t clampInt(int64_t v) {
    return static_cast<int32_t>(std::clamp<int64_t>(v, std::numeric_limits<int32_t>::min(), std::numeric_limits<int32_t>::max()));
}
uint8_t clampByte(int64_t v) { return static_cast<uint8_t>(std::clamp<int64_t>(v, 0, 255)); }
uint16_t clampWord(int64_t v) { return static_cast<uint16_t>(std::clamp<int64_t>(v, 0, 65535)); }

class Exporter {
public:
    Exporter(const Rules& r, const GameState& s, ConversionReport& report, const ExportOptions& options)
        : r_(r), d_(r.data()), s_(s), report_(report), options_(options) {}

    std::expected<ClassicSave, std::string> run() {
        checkLimits();
        if (!error_.empty()) return std::unexpected(error_);
        numberObjects();
        numberFleets();
        startOfTurnMovement();
        out_.keys = drawKeys(options_.keySeed);
        out_.version = std::string(kVersion);
        out_.traitCount = d_.racialTraits.size();
        options();
        globals();
        systems();
        empires();
        designs();
        objects();
        launched();
        prologueAndSummary();
        if (!error_.empty()) return std::unexpected(error_);
        notes();
        return std::move(out_);
    }

private:
    const Rules& r_;
    const ruleset::Ruleset& d_;
    const GameState& s_;
    ConversionReport& report_;
    ExportOptions options_;
    ClassicSave out_;
    std::string error_;
    std::vector<uint16_t> objectId_;                 // per ObjectId: its id in the file (0 none)
    std::map<uint32_t, uint16_t> vehicleId_;         // per VehicleId value
    std::map<uint32_t, uint16_t> fleetNumber_;       // per FleetId value: its number in the owner's list
    std::vector<std::vector<FleetId>> fleetsOf_;     // per empire, in number order
    std::map<std::string, int> counts_;
    size_t replaced_ = 0;
    std::map<uint32_t, int> refilled_;               // per VehicleId value: movement after the start-of-turn refill

    // A turn-based game whose current player's turn has not started: the
    // original resumes inside that turn and never refills on loading, so the
    // player's vehicles get the movement the start of the turn gives them
    // (§9.1). Their orders are carried out at the next turn's start.
    void startOfTurnMovement() {
        if (s_.options.simultaneous) return;
        const EmpireId cur = currentPlayer();
        if (s_.playerTurn.empire == cur && s_.playerTurn.started) return;
        for (const auto& [id, points] : movement::refilledMovement(r_, s_, cur)) refilled_[id.value] = points;
        if (!refilled_.empty()) count("vehicles given their start-of-turn movement (the player's turn had not started)", static_cast<int>(refilled_.size()));
    }
    int movementOf(const Vehicle& v) const {
        const auto it = refilled_.find(v.id.value);
        return it != refilled_.end() ? it->second : v.movement;
    }

    void fail(std::string why) {
        if (error_.empty()) error_ = std::move(why);
    }
    void count(const std::string& what, int n = 1) { counts_[what] += n; }
    std::string text(std::string_view t) { return latin1Safe(t, &replaced_); }

    uint8_t sys(SystemId id) const { return id.valid() ? clampByte(int64_t{id.value} + 1) : 0; }
    static uint8_t sec(Sector x) {
        if (!x.valid()) return 84;
        return static_cast<uint8_t>(x.y * 13 + x.x);
    }
    uint8_t player(EmpireId e) const { return e.valid() && e.index() < s_.empires.size() ? static_cast<uint8_t>(e.value + 1) : 0; }
    uint16_t objectId(ObjectId o) const { return o.valid() && o.index() < objectId_.size() ? objectId_[o.index()] : 0; }
    uint16_t vehicleId(VehicleId v) const {
        const auto it = vehicleId_.find(v.value);
        return it != vehicleId_.end() ? it->second : 0;
    }
    uint16_t designId(DesignId d) const { return d.valid() ? clampWord(int64_t{d.value} + 1) : 0; }
    static int32_t date(uint32_t turn) { return clampInt(int64_t{kDateBase} + turn); }
    bool alive(const Vehicle& v) const { return v.count > 0; }

    // ---- Limits (§7.1 step 2) --------------------------------------------------------------------------

    void checkLimits() {
        if (s_.empires.empty()) return fail("the game has no empire");
        if (s_.empires.size() > static_cast<size_t>(kMaxPlayers))
            return fail(std::format("the game has {} empires; the original holds at most {}", s_.empires.size(), kMaxPlayers));
        if (s_.galaxy.systems.size() > 255) return fail(std::format("the game has {} systems; the original holds at most 255", s_.galaxy.systems.size()));
        if (s_.designs.size() > 65535) return fail(std::format("the game has {} designs; the original holds at most 65,535", s_.designs.size()));
        for (const auto& c : s_.colonies) {
            if (!c) continue;
            std::map<uint32_t, int> kinds;
            for (uint32_t f : c->facilities) ++kinds[f];
            if (kinds.size() > 255) return fail(std::format("{} has {} kinds of facilities; the original holds at most 255", s_.galaxy.object(c->planet).name, kinds.size()));
            for (const auto& [f, n] : kinds)
                if (n > 255) return fail(std::format("{} has {} facilities of one kind; the original holds at most 255", s_.galaxy.object(c->planet).name, n));
            if (c->queue.items.size() > 255) return fail(std::format("the queue of {} has more than 255 items", s_.galaxy.object(c->planet).name));
        }
        for (const Empire& e : s_.empires) {
            if (e.designTypes.size() > 250) return fail(std::format("the {} have {} design types; the original holds at most 250", e.name, e.designTypes.size()));
            if (e.colonyTypes.size() > 255 || e.systemsToAvoid.size() > 255) return fail(std::format("the {} have more than 255 colony types or systems to avoid", e.name));
            if (e.techLevels.size() != d_.techAreas.size())
                return fail(std::format("the {} know {} tech areas; the data set has {}", e.name, e.techLevels.size(), d_.techAreas.size()));
        }
        if (s_.pendingEvents.size() > 255) return fail("the game has more than 255 timed events");
        if (r_.gameRoot().empty()) report_.note("This game was not played with an installed game's data set: the original needs the very same data files.");
    }

    // The file's object list: every object by slot (§3.8); free slots are blanks.
    void numberObjects() {
        objectId_.assign(s_.galaxy.objects.size(), 0);
        size_t slots = 0;
        for (const ObjectRef& ref : objectOrder(s_)) {
            if (ref.vehicle.valid()) {
                const Vehicle* v = s_.vehicle(ref.vehicle);
                if (!v || !alive(*v)) continue;
            }
            slots = std::max<size_t>(slots, ref.slot + size_t{1});
        }
        if (slots > 65535) return fail(std::format("the game has {} object slots; the original holds at most 65,535", slots));
        for (const ObjectRef& ref : objectOrder(s_)) {
            if (ref.slot >= slots) continue;
            const auto id = static_cast<uint16_t>(ref.slot + 1);
            if (ref.object.valid()) objectId_[ref.object.index()] = id;
            else if (const Vehicle* v = s_.vehicle(ref.vehicle); v && alive(*v)) vehicleId_[ref.vehicle.value] = id;
        }
        out_.objects.assign(slots, ObjectRecord{});
    }

    void numberFleets() {
        fleetsOf_.assign(s_.empires.size(), {});
        for (const Fleet& f : s_.fleets) {
            if (!f.owner.valid() || f.owner.index() >= s_.empires.size()) continue;
            auto& list = fleetsOf_[f.owner.index()];
            list.push_back(f.id);
            fleetNumber_[f.id.value] = static_cast<uint16_t>(list.size());
        }
    }

    // ---- Options, victory, globals (§3.2 - §3.4) ---------------------------------------------------------

    void options() {
        const GameOptions& g = s_.options;
        Options& o = out_.options;
        o.quadrantType = 1;
        for (size_t i = 0; i < d_.quadrantTypes.size(); ++i)
            if (keysEqual(d_.quadrantTypes[i].name, g.quadrantType)) o.quadrantType = static_cast<uint16_t>(i + 1);
        auto code = [](int v, int hi) { return static_cast<uint8_t>(std::clamp(v, 0, hi) + 1); };
        o.quadrantSize = code(g.quadrantSize, 2);
        o.allWarpPointsConnected = g.allWarpPointsConnected;
        o.noWarpPoints = g.noWarpPoints;
        o.warpPointsAnywhere = g.warpPointsAnywhere;
        o.allSystemsSeen = g.allSystemsSeen;
        o.omnipresent = g.omnipresent;
        o.finiteResources = g.finiteResources;
        o.allPlanetsSameSize = g.allPlanetsSameSize;
        o.eventFrequency = code(g.eventFrequency, 3);
        o.maxEventSeverity = code(g.maxEventSeverity, 3);
        o.techCost = code(g.techCost, 2);
        for (size_t i = 0; i < d_.techAreas.size(); ++i) {
            if (!d_.techAreas[i].canBeRemoved) continue;
            const bool allowed = g.techAreasAllowed.empty() || (i < g.techAreasAllowed.size() && g.techAreasAllowed[i]);
            if (allowed) o.techAreasAllowed.push_back(text(d_.techAreas[i].name));
        }
        std::sort(o.techAreasAllowed.begin(), o.techAreasAllowed.end(), [](const std::string& a, const std::string& b) {
            return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(), [](char x, char y) {
                return std::tolower(static_cast<unsigned char>(x)) < std::tolower(static_cast<unsigned char>(y));
            });
        });
        auto nearest = [](int64_t v, std::span<const int64_t> values) {
            size_t best = 0;
            for (size_t i = 1; i < values.size(); ++i)
                if (std::llabs(values[i] - v) < std::llabs(values[best] - v)) best = i;
            return static_cast<uint8_t>(best + 1);
        };
        static constexpr std::array<int64_t, 3> kStartResourceAmounts{5000, 20000, 100000};
        o.startingResources = nearest(g.startingResources.v[0], kStartResourceAmounts);
        o.homePlanetValue = code(g.homePlanetValue, 2);
        static constexpr std::array<int64_t, 4> kPlanets{1, 3, 5, 10};
        o.startingPlanets = nearest(g.startingPlanets, kPlanets);
        o.sameSystemAllowed = g.sameSystemAllowed;
        o.evenlyDistributed = g.evenlyDistributed;
        o.scoreDisplay = code(g.scoreDisplay, 2);
        o.startTechLevel = code(g.startTechLevel, 2);
        static constexpr std::array<int64_t, 4> kPoints{0, 2000, 3000, 5000};
        o.racialPoints = nearest(g.racialPoints, kPoints);
        o.randomComputerEmpires = true;
        o.randomNeutralEmpires = true;
        o.computerPlayers = 2;
        o.aiDifficulty = code(g.aiDifficulty, 2);
        o.aiBonus = code(g.aiBonus, 254);
        o.maxUnitsPerPlayer = clampWord(g.maxUnitsPerPlayer);
        o.maxShipsPerPlayer = clampWord(g.maxShipsPerPlayer);
        o.teamMode = g.teamMode;
        o.noTacticalCombat = g.noTacticalCombat;
        o.completeTechTree = g.completeTechTree;
        o.allowGifts = g.allowGifts;
        o.allowTechTrades = g.allowTechTrades;
        o.allowSurrender = g.allowSurrender;
        o.allowIntel = g.allowIntel;
        o.noRuins = g.noRuins;
        o.onlyBreathable = g.onlyBreathable;
        o.onlyHomeType = g.onlyHomeType;
        o.playersCanSaveMap = g.playersCanSaveMap;
        o.playStyle = 1;
        o.connection = 1;
        o.autosaveTurns = clampByte(g.autosaveTurns);
        o.turnBased = !g.simultaneous;
        o.simultaneous = g.simultaneous;
        // A simultaneous game's host saves under its game name (§1.1): the
        // name of the file written (§11.1 Q15).
        if (g.simultaneous) o.gameName = text(options_.gameName);
        // The data-set checksums, without which no player signs in to a
        // simultaneous game (§3.2.1, §7.3).
        if (auto sums = dataSetChecksums(d_.dataDir)) {
            o.checksums = *sums;
        } else {
            o.checksums = {};
            if (g.simultaneous)
                report_.note(std::format("The data-set checksums could not be computed ({}): players can sign in to this game in the original only "
                                         "after its host has processed a turn.",
                                         sums.error()));
        }

        const VictoryConditions& w = g.victory;
        Victory& v = out_.victory;
        v.score = w.score;
        v.scoreValue = clampInt(w.scoreValue);
        v.years = w.years;
        v.yearsTurns = clampInt(int64_t{w.yearsValue} * 10);
        v.percentOfSecond = w.percentOfSecond;
        v.percentOfSecondValue = w.percentOfSecondValue;
        v.techPercent = w.techPercent;
        v.techPercentValue = clampWord(w.techPercentValue);
        v.peace = w.peace;
        v.peaceTurns = clampInt(int64_t{w.peaceYears} * 10);
        v.peacefulTurns = clampInt(s_.peacefulTurns);
        v.delay = w.delay;
        v.delayTurns = clampInt(int64_t{w.delayYears} * 10);
        v.completed = s_.gameOver;
    }

    EmpireId currentPlayer() const {
        if (!s_.options.simultaneous && s_.playerTurn.empire.valid() && s_.playerTurn.empire.index() < s_.empires.size())
            return s_.playerTurn.empire;
        if (s_.options.simultaneous) return EmpireId{s_.empires.size() - 1};
        for (const Empire& e : s_.empires)
            if (e.alive && e.kind == PlayerKind::Human) return e.id;
        return EmpireId{0u};
    }

    void globals() {
        Globals& g = out_.globals;
        const EmpireId cur = currentPlayer();
        // Simultaneous: the empire count, as the host's own saves (§6.1, observed).
        g.currentPlayer = s_.options.simultaneous ? clampByte(static_cast<int64_t>(s_.empires.size())) : player(cur);
        const Empire& shown = s_.empire(cur);
        g.viewSystem = sys(shown.homeSystem);
        g.viewSector = sec(shown.homeSector);
        g.seed = static_cast<int32_t>(s_.seed % 9999);
        g.scenario = false;
        g.tutorial = false;
        g.scenarioPage = 1;
        // Timed events (§3.4).
        for (const PendingEvent& pe : s_.pendingEvents) {
            TimedEvent t;
            t.event = clampWord(int64_t{pe.eventType} + 1);
            t.date = date(pe.fireTurn);
            t.player = player(pe.empire);
            std::optional<Location> where;
            if (pe.object.valid() && pe.object.index() < s_.galaxy.objects.size()) where = locationOf(s_.galaxy, pe.object);
            if (const Vehicle* v = pe.vehicle.valid() ? s_.vehicle(pe.vehicle) : nullptr) where = v->location;
            t.system = sys(pe.system.valid() ? pe.system : where ? where->system : SystemId{});
            t.sector = where ? sec(where->sector) : 0;
            switch (eventTarget(r_, pe.eventType)) {
                case EventTarget::Vehicle: t.target = vehicleId(pe.vehicle); break;
                case EventTarget::Object: t.target = objectId(pe.object); break;
                case EventTarget::System: t.target = sys(pe.system); break;
                case EventTarget::Empire: t.target = player(pe.empire); break;
            }
            out_.events.push_back(t);
        }
    }

    // ---- Systems (§3.5) -----------------------------------------------------------------------------------

    void systems() {
        const size_t n = s_.empires.size();
        for (size_t i = 0; i < s_.galaxy.systems.size(); ++i) {
            const StarSystem& sys = s_.galaxy.systems[i];
            SystemRecord y;
            y.name = text(sys.name);
            y.number = static_cast<uint8_t>(i + 1);
            y.x = clampByte(sys.position.x + 1);
            y.y = clampByte(sys.position.y + 1);
            y.physicalType = 1;
            for (size_t k = 0; k < kPhysicalTypes.size(); ++k)
                if (keysEqual(kPhysicalTypes[k], sys.physicalType)) y.physicalType = static_cast<uint8_t>(k + 1);
            if (sys.type.valid() && sys.type.index() < d_.systemTypes.size()) {
                // The type's attributes are copied into the record (§3.5).
                const ruleset::SystemType& t = d_.systemTypes[sys.type.index()];
                y.typeDescription = text(t.description);
                y.canStart = t.empiresCanStartIn;
                y.maskBackground = t.maskBackgroundObjects;
                y.nonTiledCenter = t.nonTiledCenterPicture;
                y.backgroundBitmap = text(t.backgroundBitmap);
            }
            y.abilities = abilities(sys.abilities);
            y.explored = BitSet::sized(static_cast<uint16_t>(kMaxPlayers));
            // The claimed set's capacity varies in the original (§3.5): grown as needed here.
            for (size_t p = 0; p < n; ++p) {
                const Empire& e = s_.empires[p];
                if (e.hasExplored(SystemId{i})) y.explored.set(p);
                if (std::find(e.claimedSystems.begin(), e.claimedSystems.end(), SystemId{i}) != e.claimedSystems.end()) y.claimed.set(p);
                if (i < e.knowledge.notes.size()) y.notes[p] = text(e.knowledge.notes[i]);
            }
            out_.systems.push_back(std::move(y));
        }
        for (const StartingPoint& p : s_.startingPoints) {
            StartPointRecord rec{static_cast<uint16_t>(sys(p.system)), sec(p.sector), 0};
            if (p.player == kCommonStart) {
                out_.commonStarts.push_back(rec);
            } else {
                rec.player = clampByte(p.player + 1);
                out_.specificStarts.push_back(rec);
            }
        }
    }

    std::vector<Ability> abilities(const std::vector<ruleset::Ability>& list) {
        std::vector<Ability> out;
        for (const ruleset::Ability& a : list) {
            Ability x;
            const auto id = abilityId(a.type);
            if (!id && !a.type.empty()) count("abilities the original's table lacks (written as none)");
            x.id = id.value_or(0);
            x.description = text(a.description);
            x.value1 = clampInt(a.number1());
            x.value2 = clampInt(a.number2());
            out.push_back(std::move(x));
        }
        if (out.size() > 255) {
            out.resize(255);
            count("ability lists cut to 255");
        }
        return out;
    }

    // ---- Empires (§3.6) ---------------------------------------------------------------------------------------

    void empires() {
        for (const Empire& e : s_.empires) out_.empires.push_back(empire(e));
    }

    EmpireRecord empire(const Empire& e) {
        EmpireRecord x;
        const size_t i = e.id.index();
        x.leaderName = text(e.leaderName);
        x.leaderTitle = text(e.leaderTitle);
        x.name = text(e.name);
        x.type = text(e.empireType);
        x.player = static_cast<uint8_t>(i + 1);
        x.computer = e.kind != PlayerKind::Human;
        x.useRaceMinisterStyle = e.useRaceMinisterStyle;
        x.raceFolder = text(e.race.style);
        x.artFolder = x.raceFolder;
        x.shipNameFile = text(e.race.designNameFile);
        x.homeSystem = sys(e.homeSystem);
        x.homeSector = sec(e.homeSector);
        x.atmosphere = 3;
        bool atmosphereKnown = false;
        for (size_t k = 0; k < kAtmospheres.size(); ++k)
            if (keysEqual(kAtmospheres[k], e.race.atmosphere)) {
                x.atmosphere = static_cast<uint8_t>(k + 1);
                atmosphereKnown = true;
            }
        if (!atmosphereKnown) count("races breathing an atmosphere the original lacks (written as Oxygen)");
        x.surface = 1;
        for (size_t k = 0; k < kSurfaces.size(); ++k)
            if (keysEqual(kSurfaces[k], e.race.nativeSurface) || (k == 2 && keysEqual(e.race.nativeSurface, "Gas"))) x.surface = static_cast<uint8_t>(k + 1);
        x.biology = text(e.race.biology);
        x.society = text(e.race.society);
        x.history = text(e.race.history);
        x.demeanor = text(e.race.demeanor);
        if (e.race.happinessModel < d_.happinessModels.size()) x.happinessType = text(d_.happinessModels[e.race.happinessModel].name);
        x.experience = clampInt(e.experience);
        x.neutral = isNeutral(e);
        x.difficulty = static_cast<uint8_t>(e.aiDifficulty < 0 ? 2 : std::clamp(e.aiDifficulty, 0, 2) + 1);
        x.maintenancePercent = clampByte(r_.setting("Empire Starting Percent Maint Cost", 25));
        x.reproductionPercent = clampByte(r_.setting("Empire Starting Percent Reproduction", 10));
        for (size_t k = 0; k < 3; ++k) x.stored[k] = clampInt(std::min<int64_t>(e.stockpile.v[k], 2'000'000'000));
        x.researchPoints = clampInt(e.researchPool);
        x.intelPoints = clampInt(e.intelPool);
        // Derived (§3.6.1): the native surface and the colonization components researched.
        x.canColonize[x.surface - 1u] = true;
        for (uint32_t c = 0; c < d_.components.size(); ++c) {
            if (!r_.componentAvailable(e, c)) continue;
            const auto abilities = r_.componentAbilities(c);
            if (hasAbility(abilities, AbilityKind::ColonizeRock)) x.canColonize[0] = true;
            if (hasAbility(abilities, AbilityKind::ColonizeIce)) x.canColonize[1] = true;
            if (hasAbility(abilities, AbilityKind::ColonizeGas)) x.canColonize[2] = true;
        }
        x.breathes[x.atmosphere - 1u] = true;
        if (!e.passwordHash.empty()) count("passwords (not exported)");
        x.email = text(e.email);
        // Research (§3.6.2).
        x.repeatResearch = e.repeatResearch;
        x.researchEvenly = e.researchEvenly;
        for (int u : e.uniqueAreasUnlocked) x.uniqueAreas.push_back(clampWord(u));
        for (int l : e.techLevels) x.techLevels.push_back(clampWord(l));
        for (const ResearchProject& p : e.research) x.research.push_back({clampWord(int64_t{p.area.value} + 1), 2, clampInt(p.progress)});
        // Intelligence (§3.6.3).
        for (const IntelProjectOrder& p : e.intel) {
            IntelItem item;
            item.project = clampWord(int64_t{p.project} + 1);
            item.target = player(p.target);
            item.spent = clampInt(p.progress);
            item.specific = kAnyTarget;
            switch (intelTarget(r_, p.project)) {
                case IntelTarget::Vehicle:
                    if (p.targetVehicle.valid()) item.specific = vehicleId(p.targetVehicle);
                    break;
                case IntelTarget::Planet:
                    if (p.targetPlanet.valid()) item.specific = objectId(p.targetPlanet);
                    break;
                case IntelTarget::Tech:
                    if (p.targetTech.valid()) item.specific = static_cast<int32_t>(p.targetTech.value + 1);
                    break;
                case IntelTarget::Empire:
                    if (p.thirdEmpire.valid()) item.specific = player(p.thirdEmpire);
                    break;
                default: break;
            }
            if (item.specific == 0) item.specific = kAnyTarget;
            x.intel.push_back(item);
        }
        x.repeatIntel = e.repeatIntel;
        x.intelEvenly = e.intelEvenly;
        // Politics (§3.6.4) and the per-player memory (§3.6.9).
        for (size_t j = 0; j < static_cast<size_t>(kMaxPlayers); ++j) {
            x.anger[j] = 50;
            x.turnsSinceWar[j] = 999;
            if (j >= e.relations.size() || j == i) continue;
            const Relation& rel = e.relations[j];
            x.politics[j].treaty = rel.contact ? treatyCode(rel.treaty) : kNoContact;
            x.politics[j].dominant = rel.dominant;
            x.politics[j].tradeCounter = clampWord(std::min(rel.tradeTurns, 60000));
            x.anger[j] = clampByte(rel.anger);
            x.turnsSinceWar[j] = clampWord(rel.turnsSinceWar);
        }
        // Race (§3.6.5).
        x.culture = clampWord(int64_t{e.race.culture} + 1);
        x.traits.assign(d_.racialTraits.size(), false);
        for (uint32_t t : e.race.traits)
            if (t < x.traits.size()) x.traits[t] = true;
        for (size_t c = 0; c < x.characteristics.size(); ++c) x.characteristics[c] = e.race.characteristics[c];
        // Name lists (§3.6.6).
        for (const std::string& t : e.designTypes) x.designTypes.push_back(text(t));
        for (const std::string& t : e.colonyTypes) x.colonyTypes.push_back(text(t));
        // Options (§3.6.7).
        x.options = empireOptions(e);
        // Waypoints, repair priorities, mine fields, systems to avoid (§3.6.8).
        for (size_t w = 0; w < x.waypoints.size(); ++w) {
            const Waypoint& wp = e.waypoints[w];
            x.waypoints[w].name = text(wp.name);
            if (wp.set) {
                x.waypoints[w].system = sys(wp.location.system);
                x.waypoints[w].sector = sec(wp.location.sector);
            }
        }
        for (const std::string& p : e.repairPriorities) x.repairPriorities.push_back(text(p));
        for (const Location& m : e.taggedMinefields) x.taggedMinefields.emplace_back(sys(m.system), sec(m.sector));
        for (SystemId a : e.systemsToAvoid) x.systemsToAvoid.push_back(sys(a));
        // Computer player memory and ministers (§3.6.9).
        x.aiState = clampByte(e.aiState + 1);
        x.staging = sys(e.aiMemory.staging);
        x.secured = sys(e.aiMemory.secured);
        x.turnsInState = clampWord(e.aiTurnsInState);
        x.afterAttack = clampWord(e.aiMemory.afterAttack);
        for (SystemId y : e.aiMemory.defend) x.defend.push_back(sys(y));
        for (SystemId y : e.aiMemory.targets) x.targets.push_back(sys(y));
        x.ministerStyle = text(e.ministerStyle);
        for (size_t k = 0; k < x.ministers.size(); ++k) x.ministers[k] = e.ministerAll || ((e.ministers >> k) & 1u) != 0;
        x.ministersForNewVehicles = e.ministersForNewVehicles;
        x.aiMinimalChanges = e.aiMinimalChanges;
        int drones = 0;
        for (const Vehicle& v : s_.vehicles)
            if (alive(v) && v.owner == e.id && vehicleType(r_, s_, v) == ruleset::VehicleType::Drone) ++drones;
        x.droneNameCounter = clampWord(drones);
        x.destroyed = !e.alive;
        // The log (§3.6.11).
        log(e, x);
        // Fleets (§3.6.12).
        for (FleetId id : fleetsOf_[i]) {
            const Fleet& f = *s_.fleet(id);
            FleetRecord rec;
            rec.number = fleetNumber_[id.value];
            rec.owner = x.player;
            rec.name = text(f.name);
            rec.system = sys(f.location.system);
            rec.sector = sec(f.location.sector);
            rec.experience = tenthsToFloat80(int64_t{f.experience} * 10 + f.experienceTenths);
            rec.formation = clampWord(int64_t{f.formation} + 1);
            rec.strategy = clampWord(int64_t{f.strategy} + 1);
            rec.minister = f.minister;
            rec.leader = vehicleId(f.leader);
            x.fleets.push_back(std::move(rec));
        }
        x.fleetsCreated = clampWord(static_cast<int64_t>(x.fleets.size()));
        // Strategies (§3.6.13).
        for (size_t k = 0; k < e.strategies.size(); ++k) x.strategies.push_back(strategy(e.strategies[k], k));
        return x;
    }

    EmpireOptions empireOptions(const Empire& e) {
        const InterfaceOptions& u = e.interfaceOptions;
        EmpireOptions o;
        o.showLogAtStart = u.showLogAtTurnStart;
        o.confirmEndTurn = u.confirmEndTurn;
        o.confirmScrap = u.confirmScrap;
        o.confirmStellar = u.confirmStellarManipulation;
        o.confirmDeleteResearch = u.confirmDeleteResearch;
        o.confirmDeleteIntel = u.confirmDeleteIntel;
        o.confirmDeleteFirstQueueItem = u.confirmDeleteFirstQueueItem;
        o.noteSimilar = u.noteSimilarAbilities;
        o.skipUnderConstruction = u.skipUnderConstruction;
        o.avoidTaggedMines = e.avoidTaggedMinefields;
        o.avoidRestricted = e.avoidRestrictedSystems;
        o.skipDamaged = u.skipDamaged;
        o.stopOncePerLocation = u.stopOncePerLocation;
        o.skipInFleets = u.skipInFleets;
        o.clearOnEnemy = e.clearOrdersOnEncounter != EncounterClear::Never;
        o.clearOnAny = e.clearOrdersOnEncounter == EncounterClear::Any;
        o.warpPointNames = u.warpPointNames;
        o.planetNames = u.planetNames;
        o.facilityMarker1 = (u.facilityMarkers & 1u) != 0;
        o.colonizableMarkers = u.colonizableMarkers;
        o.coordinateLocation = u.coordinateLocation;
        for (size_t k = 0; k < o.facilityMarkers2to12.size(); ++k) o.facilityMarkers2to12[k] = ((u.facilityMarkers >> (k + 1)) & 1u) != 0;
        o.galaxyGridLines = u.galaxyGridLines;
        o.galaxyWarpLines = u.galaxyWarpLines;
        o.latestConstruction = u.latestConstructionOnly;
        o.latestComponents = u.latestComponentsOnly;
        o.chooseColonyType = e.chooseColonyType;
        o.turnEndSystem = sys(e.homeSystem);
        o.turnEndSector = sec(e.homeSector);
        o.autoClaim = u.autoClaimColonized;
        o.planetsTab = clampByte(u.planetsTab + 1);
        o.shipsTab = clampByte(u.shipsTab + 1);
        o.queuesTab = clampByte(u.queuesTab + 1);
        o.logFilter = clampByte(u.logFilter + 1);
        for (size_t k = 0; k < o.shipsShown.size(); ++k) o.shipsShown[k] = ((u.shipsShown >> k) & 1u) != 0;
        for (size_t k = 0; k < o.queuesShown.size(); ++k) o.queuesShown[k] = ((u.queuesShown >> k) & 1u) != 0;
        o.designerCondensed = u.designCondensed;
        o.designerToHit = u.designToHit;
        o.planetsHideAvoided = u.planetsNoSysToAvoid;
        o.simulatorNoObsolete = u.simulatorNoObsolete;
        o.systemGrid = u.systemGrid;
        o.replayFast = u.replayFast;
        o.replayAnimate = u.replayAnimate;
        o.replayGrid = u.replayGrid;
        o.replayViewRect = u.replayViewRect;
        return o;
    }

    StrategyRecord strategy(const ruleset::CombatStrategy& cs, size_t index) {
        const combat::Strategy st = combat::parseStrategy(cs);
        StrategyRecord t;
        t.position = clampWord(static_cast<int64_t>(index) + 1);
        t.name = text(cs.name);
        t.primary = static_cast<uint8_t>(static_cast<uint8_t>(st.primary) + 1);
        t.secondary = static_cast<uint8_t>(static_cast<uint8_t>(st.secondary) + 1);
        t.typePriorityFirst = st.typePriorityFirst;
        for (size_t k = 0; k < t.targeting.size(); ++k) t.targeting[k] = static_cast<uint8_t>(st.targeting[k]);
        for (size_t f = 0; f < kStrategyCategoryOrder.size(); ++f) {
            const auto c = static_cast<size_t>(kStrategyCategoryOrder[f]);
            t.typePriority[f] = clampByte(st.typePriority[c]);
            t.dontFireOn[f] = st.dontFireOn[c];
            t.breakFormation[f] = st.breakFormation[c];
        }
        // The seekers' break-formation flags stay on in the original (§3.6.13).
        t.breakFormation[11] = true;
        t.breakFormation[12] = true;
        t.fighterGroup = clampWord(st.fighterLaunchGroup);
        t.dronesPerTarget = clampWord(st.dronesPerTarget);
        t.damagePercent = {clampByte(st.damagePercentShip), clampByte(st.damagePercentPlanet), clampByte(st.damagePercentFighters),
                           clampByte(st.damagePercentSatellites)};
        t.damageUntilWeaponsGone = st.damageUntilWeaponsGone;
        return t;
    }

    // ---- The log and messages (§3.6.11) --------------------------------------------------------------------------

    static uint8_t gotoCode(LogGoto g) {
        switch (g) {
            case LogGoto::None: return 0;
            case LogGoto::Location: return 1;
            case LogGoto::ConstructionQueues: return 2;
            case LogGoto::Research: return 3;
            case LogGoto::Intelligence: return 4;
            case LogGoto::EmpireOptions: return 5;
            case LogGoto::Designs: return 6;
            case LogGoto::Empires: return 7;
        }
        return 0;
    }

    const DiplomaticMessage* findMessage(MessageId id) const {
        for (const DiplomaticMessage& m : s_.messages)
            if (m.id == id) return &m;
        return nullptr;
    }

    void log(const Empire& e, EmpireRecord& x) {
        std::set<uint32_t> written;
        auto entry = [&](const LogEntry& l) {
            LogRecord rec;
            rec.owner = x.player;
            if (l.location) {
                rec.system = sys(l.location->system);
                rec.sector = sec(l.location->sector);
            }
            rec.date = date(l.turn);
            rec.title = text(l.title);
            rec.text = text(l.text);
            rec.target = gotoCode(l.target);
            rec.category = static_cast<uint8_t>(static_cast<uint8_t>(l.category) + 1);
            // OpenSE4's entries unread: the original's Log lists only entries
            // read never or this turn (§3.6.11, observed).
            rec.dateRead = 0;
            if (l.classic.kind != 0) {
                // An entry imported from the original: what it came with (§7.3).
                const ClassicLogFields& c = l.classic;
                rec.kind = c.kind;
                rec.owner = c.owner;
                rec.system = c.system;
                rec.sector = c.sector;
                // Its read mark as the original had it: entries read on an
                // earlier turn stay hidden in the Log and are not counted again
                // by the computer players' anger (spec 05 §7.3).
                rec.dateRead = c.dateRead;
                rec.picture = c.pictureKey;
                rec.otherEmpire = player(c.otherEmpire);
                rec.eventNotice = c.eventNotice;
                rec.eventKind = c.eventKind;
                rec.techArea = c.techArea;
                if (c.battle.size() == static_cast<size_t>(kMaxPlayers)) {
                    BattleRecord& b = rec.battle.emplace();
                    b.number = c.battleNumber;
                    for (size_t i = 0; i < b.sides.size(); ++i) {
                        const ClassicBattleSide& from = c.battle[i];
                        BattleSide& to = b.sides[i];
                        to.player = from.player;
                        to.tookPart = from.tookPart;
                        for (const ClassicBattleShip& sh : from.forces) to.forces.push_back({text(sh.name), text(sh.hullCode), sh.hull});
                        for (const ClassicBattleSurvivor& sv : from.survivors) to.survivors.push_back({text(sv.name), sv.damage});
                    }
                }
            } else {
                rec.kind = 36;   // a generic entry: no picture key, not counted by the computer players (§3.6.11)
            }
            if (const DiplomaticMessage* m = l.message.valid() ? findMessage(l.message) : nullptr; m && m->to == e.id) {
                rec.kind = 19;
                rec.message = message(*m);
                written.insert(m->id.value);
            } else if (rec.kind == 19) {
                rec.kind = 36;   // its message is answered or gone: no record to write
                rec.picture = 0;
            }
            return rec;
        };
        for (const LogEntry& l : e.log) x.log.push_back(entry(l));
        // Delivered messages still unanswered whose entry the log no longer holds.
        for (const DiplomaticMessage& m : s_.messages) {
            if (m.to != e.id || written.contains(m.id.value)) continue;
            if (!m.delivered) {
                count("messages not yet delivered (not exported)");
                continue;
            }
            if (m.answered) continue;
            LogEntry l;
            l.turn = m.dated;
            l.category = LogCategory::Politics;
            l.title = "Message";
            l.text = m.text;
            l.target = LogGoto::Empires;
            l.message = m.id;
            x.log.push_back(entry(l));
        }
    }

    MessageRecord message(const DiplomaticMessage& m) {
        MessageRecord rec;
        rec.type = messageTypeCode(m.type);
        rec.sender = player(m.from);
        rec.recipient = player(m.to);
        rec.tone = static_cast<uint8_t>(std::clamp(m.tone, 0, 2) + 1);
        // Treaty proposals and their answers name one; a broken treaty names none (observed).
        const bool treatyMessage = m.type == MessageType::ProposeTreaty || m.type == MessageType::AcceptTreaty ||
                                   m.type == MessageType::RefuseTreaty || m.type == MessageType::CounterTreaty;
        rec.treaty = treatyMessage || m.treaty != Treaty::None ? treatyCode(m.treaty) : 0;
        rec.third = player(m.thirdEmpire);
        rec.system = sys(m.system);
        rec.planet = objectId(m.planet);
        for (const PackageItem& p : m.offer) items(p, rec.offered);
        for (const PackageItem& p : m.request) items(p, rec.requested);
        return rec;
    }

    // A package item as the original's items (resources: one per resource).
    void items(const PackageItem& p, std::vector<PackageItemRecord>& out) {
        auto add = [&](uint8_t kind, int64_t value, uint8_t quantity, std::string shown) {
            out.push_back({text(shown), kind, clampWord(value), quantity});
        };
        switch (p.kind) {
            case PackageItem::Kind::System:
                add(1, sys(p.system), 0, p.system.valid() ? s_.galaxy.system(p.system).name : std::string{});
                break;
            case PackageItem::Kind::Planet:
                add(2, objectId(p.planet), 0, p.planet.valid() && p.planet.index() < s_.galaxy.objects.size() ? s_.galaxy.object(p.planet).name : std::string{});
                break;
            case PackageItem::Kind::Resources:
                for (size_t k = 0; k < 3; ++k) {
                    const int64_t amount = p.resources.v[k];
                    if (amount <= 0) continue;
                    if (amount % 1000 != 0 || amount > 255'000) count("resource amounts in messages rounded to thousands (at most 255,000)");
                    const int64_t thousands = std::clamp<int64_t>(xmath::divRoundHalfEven(amount, 1000), 1, 255);
                    add(3, static_cast<int64_t>(k) + 1, static_cast<uint8_t>(thousands),
                        std::format("{} {}", thousands * 1000, displayName(static_cast<Resource>(k))));
                }
                break;
            case PackageItem::Kind::Technology:
                add(4, p.tech.valid() ? int64_t{p.tech.value} + 1 : 0, 0,
                    p.tech.valid() && p.tech.index() < d_.techAreas.size() ? d_.techAreas[p.tech.index()].name : std::string{});
                break;
            case PackageItem::Kind::Vehicle: {
                const Vehicle* v = p.vehicle.valid() ? s_.vehicle(p.vehicle) : nullptr;
                add(5, vehicleId(p.vehicle), 0, v ? v->name : std::string{});
                break;
            }
            case PackageItem::Kind::StarChart:
                add(7, sys(p.system), 0, p.system.valid() ? s_.galaxy.system(p.system).name : std::string{});
                break;
            case PackageItem::Kind::Treaty: add(8, treatyCode(p.treaty), 0, std::string(displayName(p.treaty))); break;
            case PackageItem::Kind::CommChannel:
                add(9, player(p.empire), 0, p.empire.valid() && p.empire.index() < s_.empires.size() ? s_.empire(p.empire).name : std::string{});
                break;
        }
    }

    // ---- Designs (§3.7) ---------------------------------------------------------------------------------------------

    void designs() {
        // A design no empire lists is a free slot, unless something still uses it.
        std::vector<uint8_t> listed(s_.designs.size(), 0);
        for (const Empire& e : s_.empires)
            for (DesignId d : e.designs)
                if (d.valid() && d.index() < listed.size() && s_.design(d).owner == e.id) listed[d.index()] = 1;
        auto use = [&](DesignId d) {
            if (d.valid() && d.index() < listed.size()) listed[d.index()] = 1;
        };
        auto useCargo = [&](const Cargo& c) {
            for (const UnitStack& u : c.units) use(u.design);
        };
        auto useQueue = [&](const ConstructionQueue& q) {
            for (const QueueItem& it : q.items)
                if (it.kind == QueueItem::Kind::Vehicle) use(it.design);
        };
        for (const Vehicle& v : s_.vehicles) {
            if (!alive(v)) continue;
            for (const UnitStack& st : groupStacks(v)) use(st.design);
            useCargo(v.cargo);
            useQueue(v.queue);
        }
        for (const auto& c : s_.colonies)
            if (c) {
                useCargo(c->cargo);
                useQueue(c->queue);
                for (const UnitStack& u : c->landedTroops) use(u.design);
            }
        for (const Design& d : s_.designs) {
            DesignRecord rec;
            rec.id = clampWord(int64_t{d.id.value} + 1);
            const bool free = !listed[d.id.index()];
            rec.owner = free ? 0 : player(d.owner);
            rec.hull = clampWord(int64_t{d.hull} + 1);
            rec.type = text(d.designType);
            rec.templateName = text(d.templateName);
            rec.name = text(d.name);
            rec.created = date(d.createdTurn);
            rec.obsolete = d.obsolete;
            rec.everBuilt = d.everBuilt;
            if (d.hull < d_.vehicleSizes.size()) {
                // The values the original trusts and never recomputes (§3.7, §7.2).
                const DesignStats st = computeDesignStats(r_, nullptr, d);
                rec.speed = clampByte(designMovement(r_, d.hull, d.entries));
                for (size_t k = 0; k < 3; ++k) rec.cost[k] = clampInt(st.cost.v[k]);
                rec.typeCode = designTypeCode(d.designType).value_or(designTypeCode(ai::detail::aiTypeOf(r_, d, st)).value_or(1));
            }
            for (const DesignEntry& entry : d.entries) rec.parts.push_back({clampWord(int64_t{entry.component} + 1), clampByte(entry.mount + 1)});
            rec.strategy = clampWord(int64_t{d.strategy} + 1);
            // Last seen: the other players' knowledge; the owner's own entry, its last battle.
            for (size_t p = 0; p < s_.empires.size(); ++p) {
                const Empire& e = s_.empires[p];
                if (e.id == d.owner) {
                    for (const SeenDesign& f : e.aiMemory.designsFought)
                        if (f.design == d.id) rec.lastSeen[p] = date(f.turn);
                } else if (const auto seen = designSeenTurn(e.knowledge, d.id)) {
                    rec.lastSeen[p] = date(*seen);
                }
            }
            rec.built = d.built;
            rec.lost = d.lost;
            rec.scrapped = d.scrapped;
            rec.tonnageDestroyed = clampInt(d.enemyTonnageDestroyed);
            out_.designs.push_back(std::move(rec));
        }
    }

    // ---- Objects (§3.8) ----------------------------------------------------------------------------------------------

    void objects() {
        // Every slot starts as a blank storm (§3.8, inferred safe).
        for (size_t i = 0; i < out_.objects.size(); ++i) {
            ObjectRecord& o = out_.objects[i];
            o.cls = o.recordClass = static_cast<uint8_t>(ObjectClass::Storm);
            o.id = static_cast<uint16_t>(i + 1);
        }
        for (const SpaceObject& obj : s_.galaxy.objects) {
            const uint16_t id = objectId(obj.id);
            if (id == 0) continue;   // gone from its system
            out_.objects[id - 1u] = stellar(obj, id);
        }
        for (const Vehicle& v : s_.vehicles) {
            const uint16_t id = vehicleId(v.id);
            if (id == 0) continue;
            out_.objects[id - 1u] = vehicle(v, id);
        }
    }

    ObjectRecord stellar(const SpaceObject& obj, uint16_t id) {
        ObjectRecord o;
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
        o.cls = o.recordClass = static_cast<uint8_t>(c);
        o.id = id;
        o.system = sys(obj.system);
        o.sector = sec(obj.sector);
        o.abilities = abilities(obj.abilities);
        o.sectorType = clampWord(int64_t{obj.sectorType} + 1);
        if (c == ObjectClass::Star || c == ObjectClass::Planet) o.name = text(obj.name);
        if (c == ObjectClass::WarpPoint && obj.destination.valid() && obj.destination.index() < s_.galaxy.objects.size()) {
            const SpaceObject& far = s_.galaxy.object(obj.destination);
            o.destSystem = sys(far.system);
            o.destSector = sec(far.sector);
        }
        if (c == ObjectClass::Planet) {
            o.conditions = toFloat80(xmath::fromDoubleBits(obj.conditions.bits));
            for (size_t k = 0; k < 3; ++k) o.value[k] = obj.value[k];
            if (const Colony* col = s_.colony(obj.id)) o.colony = colony(*col);
        }
        return o;
    }

    ColonyRecord colony(const Colony& c) {
        ColonyRecord rec;
        rec.owner = player(c.owner);
        rec.type = text(c.colonyType);
        rec.population = population(c.population);
        rec.anger = clampByte(c.anger);
        rec.plague = clampByte(c.plagueLevel);
        rec.cloaked = c.cloaked;
        rec.atmosphereTurns = clampByte(std::min(c.atmosphereTurns, 200));
        rec.cargo = cargo(c.cargo);
        for (uint32_t f : c.facilities) {
            auto it = std::find_if(rec.facilities.begin(), rec.facilities.end(), [&](const FacilityEntry& x) { return x.facility == f + 1; });
            if (it == rec.facilities.end()) rec.facilities.push_back({clampWord(int64_t{f} + 1), 1, 0});
            else ++it->count;
        }
        // The never-reset destroyed counts (§3.8.5, §11.2), on the entries of their kinds.
        for (const DestroyedFacilities& d : c.destroyedFacilities) {
            auto it = std::find_if(rec.facilities.begin(), rec.facilities.end(), [&](const FacilityEntry& x) { return x.facility == d.facility + 1; });
            if (it != rec.facilities.end()) it->destroyed = clampByte(d.count);
        }
        rec.queue = queue(c.queue);
        rec.landedTroops = units(c.landedTroops);
        rec.invader = player(c.invader);
        rec.militia = rec.invader ? clampWord(c.militia) : 0;
        rec.orders = orderList(c.orders, c.repeatOrders, c.owner, &c);
        rec.capital = c.homeworld;
        rec.minister = c.minister;
        return rec;
    }

    std::vector<PopulationEntry> population(const std::vector<PopulationGroup>& list) {
        std::vector<PopulationEntry> out;
        for (const PopulationGroup& p : list)
            if (p.millions > 0) out.push_back({player(p.race), clampInt(p.millions)});
        return out;
    }
    std::vector<UnitEntry> units(const std::vector<UnitStack>& list) {
        std::vector<UnitEntry> out;
        for (const UnitStack& u : list)
            if (u.count > 0) out.push_back({designId(u.design), clampWord(u.count), 0});
        return out;
    }
    CargoRecord cargo(const Cargo& c) {
        CargoRecord rec;
        rec.population = population(c.population);
        rec.units = units(c.units);
        rec.hasUnits = !rec.units.empty();
        return rec;
    }
    QueueRecord queue(const ConstructionQueue& q) {
        QueueRecord rec;
        rec.onHold = q.onHold;
        rec.emergency = q.emergency;
        rec.repeat = q.repeat;
        rec.rallyWaypoint = q.autoWaypoint >= 0 && q.autoWaypoint < 10 ? static_cast<uint8_t>(q.autoWaypoint + 1) : 0;
        rec.counter = clampByte(q.emergency ? q.emergencyTurns : q.slowTurns);
        if (!q.items.empty())
            for (size_t k = 0; k < 3; ++k) rec.spent[k] = clampInt(q.items.front().spent.v[k]);
        for (size_t k = 1; k < q.items.size(); ++k)
            if (!q.items[k].spent.isZero()) count("construction spent on items below the top of a queue (only the top item's is kept)");
        for (const QueueItem& it : q.items) {
            QueueEntry e;
            e.count = clampWord(it.count);
            switch (it.kind) {
                case QueueItem::Kind::Vehicle: e.kind = 2; e.item = designId(it.design); break;
                case QueueItem::Kind::Facility: e.kind = 1; e.item = clampWord(int64_t{it.facility} + 1); break;
                case QueueItem::Kind::Upgrade: e.kind = 3; e.item = clampWord(int64_t{it.facility} + 1); break;
            }
            rec.items.push_back(e);
        }
        return rec;
    }

    ObjectRecord vehicle(const Vehicle& v, uint16_t id) {
        ObjectRecord o;
        const ruleset::VehicleType type = vehicleType(r_, s_, v);
        ObjectClass c = ObjectClass::Ship;
        switch (type) {
            case ruleset::VehicleType::Ship:
            case ruleset::VehicleType::Base: c = ObjectClass::Ship; break;
            case ruleset::VehicleType::Fighter: c = ObjectClass::FighterGroup; break;
            case ruleset::VehicleType::Satellite: c = ObjectClass::SatelliteGroup; break;
            case ruleset::VehicleType::Mine: c = ObjectClass::MineField; break;
            case ruleset::VehicleType::Drone: c = ObjectClass::DroneGroup; break;
            default:
                count("troops or weapon platforms in space (not exported)");
                o.cls = o.recordClass = static_cast<uint8_t>(ObjectClass::Storm);
                o.id = id;
                return o;
        }
        o.cls = o.recordClass = static_cast<uint8_t>(c);
        o.id = id;
        o.system = sys(v.location.system);
        o.sector = sec(v.location.sector);
        o.owner = player(v.owner);
        o.minister = v.minister;
        o.cloaked = v.status == VehicleStatus::Cloaked;
        o.orders = orderList(v.orders, v.repeatOrders, v.owner, nullptr, &v);
        if (v.immobileUntil > s_.turn) count("vehicles held in place by sabotage or an event (they move again in the original)");
        if (c == ObjectClass::Ship) {
            const Design& d = s_.design(v.design);
            o.design = designId(v.design);
            o.heading = kHeadingCode[v.heading & 7u];
            o.movement = clampByte(movementOf(v));
            o.maxMovement = clampByte(vehicleMaxMovement(r_, s_, v));   // trusted as stored (§7.2)
            o.supply = clampInt(v.supply);
            o.status = v.status == VehicleStatus::Mothballed ? 3 : 0;
            o.experience = tenthsToFloat80(int64_t{v.experience} * 10 + v.experienceTenths);
            o.name = text(v.name);
            o.fleet = v.fleet.valid() ? fleetNumber_[v.fleet.value] : 0;
            o.cargo = cargo(v.cargo);
            o.destroyedParts = BitSet::sized(clampWord(static_cast<int64_t>(d.entries.size())));
            bool partial = false;
            for (size_t p = 0; p < d.entries.size() && p < v.damage.size(); ++p) {
                if (v.damage[p] <= 0) continue;
                const int structure = entryStructure(r_, d, p);
                if (v.damage[p] >= structure) o.destroyedParts.set(p);
                else partial = true;
            }
            if (partial) count("ships with partly damaged components (written as intact)");
            if (!v.queue.items.empty() || computeDesignStats(r_, nullptr, d).spaceYard) o.queue = queue(v.queue);
            return o;
        }
        o.units = units(groupStacks(v));
        if (c == ObjectClass::FighterGroup || c == ObjectClass::DroneGroup) {
            o.movement = clampByte(movementOf(v));
            o.maxMovement = clampByte(vehicleMaxMovement(r_, s_, v));
            o.supply = clampInt(v.supply);
            o.heading = kHeadingCode[v.heading & 7u];
            o.fleet = v.fleet.valid() ? fleetNumber_[v.fleet.value] : 0;
            if (c == ObjectClass::DroneGroup) o.name = text(v.name);
        }
        return o;
    }

    // ---- Orders (§3.8.8) ---------------------------------------------------------------------------------------------

    OrderList orderList(const std::vector<Order>& orders, bool repeat, EmpireId owner, const Colony* colony, const Vehicle* vehicle = nullptr) {
        OrderList list;
        list.repeat = repeat;
        list.current = 1;
        for (const Order& o : orders)
            if (auto rec = order(o, owner, colony, vehicle)) list.orders.push_back(std::move(*rec));
        return list;
    }

    std::string objectName(ObjectId o) const { return o.valid() && o.index() < s_.galaxy.objects.size() ? s_.galaxy.object(o).name : std::string{}; }

    std::optional<OrderRecord> order(const Order& o, EmpireId owner, const Colony* colony, const Vehicle* vehicle) {
        OrderRecord rec;
        auto place = [&](const Location& l) {
            if (!l.system.valid()) return;
            rec.system = sys(l.system);
            rec.sector = sec(l.sector);
        };
        // The target, its name and its place (observed: a Colonize order
        // carries the planet's place without its name).
        auto targetObject = [&](ObjectId obj, bool named = true) {
            rec.target = objectId(obj);
            if (named) rec.targetName = text(objectName(obj));
            if (obj.valid() && obj.index() < s_.galaxy.objects.size()) place(locationOf(s_.galaxy, obj));
        };
        auto targetVehicle = [&](VehicleId v) {
            rec.target = vehicleId(v);
            if (const Vehicle* t = v.valid() ? s_.vehicle(v) : nullptr) {
                rec.targetName = text(t->name);
                place(t->location);
            }
        };
        auto unitKind = [&](DesignId d) -> uint8_t {
            if (!d.valid() || d.index() >= s_.designs.size()) return 0;
            // The cargo-kind numbering of Load and Drop (§3.8.8, Q16).
            return cargoKindOf(r_.hull(s_.design(d).hull).type);
        };
        switch (o.kind) {
            case OrderKind::MoveTo: rec.kind = 1; place(o.location); break;
            case OrderKind::MoveToWaypoint: rec.kind = 2; rec.target = clampWord(o.amount + 1); break;
            case OrderKind::Warp: rec.kind = 3; targetObject(o.object); break;
            case OrderKind::Colonize: rec.kind = 4; targetObject(o.object, false); break;
            case OrderKind::LoadCargo:
            case OrderKind::DropCargo: {
                rec.kind = o.kind == OrderKind::LoadCargo ? 6 : 7;
                rec.extra = 1;
                if (o.design.valid() && o.design.index() < s_.designs.size()) {
                    rec.extra = cargoKindOf(r_.hull(s_.design(o.design).hull).type);
                    count("load and drop orders of one design (now of every unit of its kind)");
                }
                // A drop names its planet alone (observed: no system or sector).
                if (o.kind == OrderKind::DropCargo && o.object.valid()) {
                    rec.target = objectId(o.object);
                    rec.targetName = text(objectName(o.object));
                } else {
                    place(o.location);
                }
                if (o.amount >= 0) count("load and drop orders of a set amount (now as much as fits)");
                break;
            }
            case OrderKind::Attack:
                // On a target: the pursuit, kind 11 with the target's place and
                // name. Without one: kind 8, which attacks where the group
                // stands and names nothing (§3.8.8, Q17).
                if (o.vehicle.valid()) {
                    rec.kind = 11;
                    targetVehicle(o.vehicle);
                } else if (o.object.valid()) {
                    rec.kind = 11;
                    targetObject(o.object);
                } else {
                    rec.kind = 8;
                    if (o.location.system.valid()) count("Attack orders on a place (now where the group stands)");
                }
                break;
            case OrderKind::Scrap: rec.kind = 9; break;
            case OrderKind::Seek:
                // The ministers' pursuits, which last one movement phase (kinds 12, 13).
                if (o.vehicle.valid()) {
                    rec.kind = 13;
                    targetVehicle(o.vehicle);
                } else if (o.object.valid()) {
                    rec.kind = 13;
                    targetObject(o.object);
                } else {
                    rec.kind = 12;
                    place(o.location);
                }
                break;
            case OrderKind::SweepMines: rec.kind = 15; break;
            case OrderKind::StellarManipulation: {
                switch (static_cast<StellarAction>(o.amount)) {
                    case StellarAction::OpenWarpPoint: rec.kind = 17; rec.target = sys(o.location.system); break;
                    case StellarAction::CloseWarpPoint: rec.kind = 18; targetObject(o.object); break;
                    case StellarAction::CreateStorm: rec.kind = 19; place(o.location); break;
                    case StellarAction::DestroyStorm: rec.kind = 20; targetObject(o.object); break;
                    case StellarAction::CreatePlanet: rec.kind = 21; targetObject(o.object); if (!o.object.valid()) place(o.location); break;
                    case StellarAction::DestroyPlanet: rec.kind = 22; targetObject(o.object); break;
                    case StellarAction::CreateStar: rec.kind = 23; place(o.location); break;
                    case StellarAction::DestroyStar: rec.kind = 24; targetObject(o.object); break;
                    case StellarAction::CreateNebulae: rec.kind = 60; targetObject(o.object); break;
                    case StellarAction::DestroyNebulae: rec.kind = 61; place(o.location); break;
                    case StellarAction::CreateBlackHole: rec.kind = 62; targetObject(o.object); break;
                    case StellarAction::DestroyBlackHole: rec.kind = 63; place(o.location); break;
                    case StellarAction::CreateConstructedPlanet: rec.kind = 66; targetObject(o.object); break;
                    case StellarAction::Count: return std::nullopt;
                }
                break;
            }
            case OrderKind::Sentry: rec.kind = 25; break;
            case OrderKind::LaunchUnits:
            case OrderKind::RecoverUnits:
                rec.kind = o.kind == OrderKind::LaunchUnits ? 34 : 35;
                rec.extra = unitKind(o.design);
                if (rec.extra == 0) return std::nullopt;
                count("launch and recover orders of one design (now of every unit of its kind)");
                break;
            case OrderKind::Analyze: rec.kind = 42; break;
            case OrderKind::Mothball: rec.kind = 43; break;
            case OrderKind::Unmothball: rec.kind = 44; break;
            case OrderKind::SelfDestruct: rec.kind = 45; break;
            case OrderKind::FireOn: rec.kind = 46; break;
            case OrderKind::JoinFleet: {
                const auto it = fleetNumber_.find(static_cast<uint32_t>(o.amount));
                if (it == fleetNumber_.end()) return std::nullopt;
                rec.kind = 47;
                rec.target = it->second;
                if (const Fleet* f = s_.fleet(FleetId{static_cast<uint32_t>(o.amount)})) {
                    rec.targetName = text(f->name);
                    place(f->location);
                }
                break;
            }
            case OrderKind::Retrofit:
                rec.kind = 52;
                rec.target = designId(o.design);
                if (o.design.valid() && o.design.index() < s_.designs.size()) rec.targetName = text(s_.design(o.design).name);
                break;
            case OrderKind::UseComponent: rec.kind = 58; rec.target = clampWord(o.amount + 1); break;
            case OrderKind::UseFacility: {
                rec.kind = 59;
                // Its position in the grouped facility list (one entry per kind).
                int grouped = o.amount;
                if (colony && o.amount >= 0 && static_cast<size_t>(o.amount) < colony->facilities.size()) {
                    std::vector<uint32_t> kinds;
                    for (uint32_t f : colony->facilities)
                        if (std::find(kinds.begin(), kinds.end(), f) == kinds.end()) kinds.push_back(f);
                    grouped = static_cast<int>(std::find(kinds.begin(), kinds.end(), colony->facilities[static_cast<size_t>(o.amount)]) - kinds.begin());
                }
                rec.target = clampWord(grouped + 1);
                break;
            }
            case OrderKind::ConvertResources:
                rec.kind = 65;
                rec.extra = static_cast<uint8_t>(std::min<int>(o.from, 2) + 1);
                rec.system = static_cast<uint8_t>(std::min<int>(o.to, 2) + 1);
                rec.target = clampWord(std::min(o.amount, 65000));
                break;
            case OrderKind::Resupply:
            case OrderKind::Repair:
            case OrderKind::Explore:
            case OrderKind::Cloak:
            case OrderKind::Decloak:
                // Never stored by the original (§3.8.8).
                count("Explore, Resupply, Repair, Cloak and Decloak orders (not exported)");
                return std::nullopt;
            case OrderKind::Count: return std::nullopt;
        }
        (void)owner;
        (void)vehicle;
        return rec;
    }

    // ---- The closing list (§3.9) ---------------------------------------------------------------------------------------

    void launched() {
        if (s_.options.simultaneous) return;
        for (const TurnLaunches& l : s_.playerTurn.launched) {
            LaunchRecord rec;
            rec.launcher = l.vehicle.valid() ? vehicleId(l.vehicle) : objectId(l.planet);
            switch (static_cast<AbilityKind>(l.kind)) {
                case AbilityKind::LaunchRecoverFighters: rec.kind = unitKindOf(ruleset::VehicleType::Fighter); break;
                case AbilityKind::LaunchRecoverSatellites: rec.kind = unitKindOf(ruleset::VehicleType::Satellite); break;
                case AbilityKind::LayMines: rec.kind = unitKindOf(ruleset::VehicleType::Mine); break;
                case AbilityKind::LaunchDrones: rec.kind = unitKindOf(ruleset::VehicleType::Drone); break;
                default: continue;
            }
            rec.count = clampWord(l.count);
            if (rec.launcher != 0) out_.launched.push_back(rec);
        }
    }

    // ---- Prologue and summary (§2.6, §3.1) -------------------------------------------------------------------------------

    void prologueAndSummary() {
        const auto n = static_cast<uint8_t>(s_.empires.size());
        const int32_t today = date(s_.turn);
        Prologue& p = out_.prologue;
        p.empireCountCopy = n;
        p.currentPlayerCopy = out_.globals.currentPlayer;
        p.dateCopy = today;
        p.empireCount = n;
        p.date = today;
        p.turnCounter = clampInt(int64_t{kTurnCounterBase} + s_.turn);
        Summary& sum = out_.summary;
        sum.date = today;
        sum.empires = n;
        sum.simultaneous = s_.options.simultaneous;
        sum.differentMachines = false;
        sum.humans = 0;
        for (const EmpireRecord& e : out_.empires) {
            const std::string full = trimmed(e.name + " " + e.type);
            const std::string leader = trimmed(e.leaderTitle + " " + e.leaderName);
            p.copies.push_back({full, leader, e.raceFolder});
            sum.rows.push_back({e.player, full, leader, e.email, !e.destroyed});
            if (!e.computer) ++sum.humans;
        }
    }

    // ---- Notes ----------------------------------------------------------------------------------------------------------

    void notes() {
        for (const auto& [what, n] : counts_)
            if (n > 0) report_.detail(std::format("{}: {}", what, n));
        if (replaced_ > 0) report_.detail(std::format("characters the original cannot show, written as '?': {}", replaced_));
        auto has = [&](std::string_view prefix) {
            for (const auto& [what, n] : counts_)
                if (n > 0 && what.starts_with(prefix)) return true;
            return false;
        };
        if (has("passwords")) report_.note("Passwords are not exported: every empire is open in the original. Set new passwords there (Change Password).");
        if (has("ships with partly damaged")) report_.note("Partly damaged components are written as intact: the original stores only destroyed parts.");
        if (has("load and drop orders") || has("launch and recover orders")) report_.note("Load, drop, launch and recover orders now act on every unit of their kind.");
        if (has("messages not yet delivered")) report_.note("Messages not yet delivered are not exported.");
        if (has("Explore, Resupply")) report_.note("Explore, Resupply, Repair, Cloak and Decloak orders are not exported; give them again in the original.");
        if (has("vehicles held in place")) report_.note("Vehicles held in place by sabotage or an event can move again in the original.");
        if (replaced_ > 0) report_.note("Characters the original cannot show are written as '?'.");
        report_.note("OpenSE4's own log entries are exported as plain entries, without pictures or battle details; entries that came from the original keep theirs.");
        report_.note("History and score graphs are not exported; the original's graphs start at the export date.");
        report_.note("The original restarts its random numbers from the game's seed, so its next turn differs from OpenSE4's.");
    }
};

} // namespace

std::expected<ClassicSave, std::string> exportClassicSave(const Rules& rules, const GameState& s, ConversionReport& report, const ExportOptions& options) {
    Exporter exporter(rules, s, report, options);
    return exporter.run();
}

std::expected<void, std::string> writeClassicGame(const Rules& rules, const GameState& s, const std::filesystem::path& file, ConversionReport& report,
                                                  const ExportOptions& options) {
    ExportOptions o = options;
    if (o.gameName.empty()) o.gameName = file.stem().string();
    auto save = exportClassicSave(rules, s, report, o);
    if (!save) return std::unexpected(save.error());
    auto bytes = encodeClassicSave(*save);
    if (!bytes) return std::unexpected(bytes.error());
    return writeFileAtomic(file, *bytes);
}

} // namespace opense4::game::classic
