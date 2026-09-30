// Computer player: anger (spec 05 §7.3), AI state transitions (§7.2) and the
// Mega Evil Empire (§7.6). These run in turn phase 12 with a mutable state.

#include "datafile/datafile.hpp"
#include "game/ai.hpp"
#include "game/ai_data.hpp"
#include "game/design.hpp"
#include "game/query.hpp"
#include "game/score.hpp"
#include "game/turn.hpp"

#include <algorithm>
#include <array>
#include <deque>

namespace opense4::game::ai {

namespace {

// Anger is kept between the floor and this cap. The spec's thresholds imply
// a 0..100 scale; the cap leaves room above it for the stronger-player
// modifiers (inferred).
constexpr int kMaxAnger = 200;

bool armedDesign(const Rules& r, const Design& d) {
    for (const auto& en : d.entries)
        if (r.component(en.component).isWeapon()) return true;
    return false;
}

int64_t vehicleStrength(const Rules& r, const GameState& s, const Vehicle& v) {
    const Design& d = s.design(v.design);
    int64_t attack = 0;
    for (const auto& en : d.entries) {
        const auto& c = r.component(en.component);
        if (!c.isWeapon()) continue;
        int best = 0;
        for (int range = 1; range <= std::max(1, weaponMaxRange(r, en)); ++range) best = std::max(best, weaponDamageAtRange(r, en, range));
        attack += int64_t{best} * 10 / std::max(1, c.weapon.reloadRate);
    }
    const int64_t structure = std::max(0, vehicleStructure(r, s, v) - vehicleDamageTaken(s, v));
    return (attack + structure / 2) * std::max(1, v.count);
}

std::vector<uint8_t> colonySystems(const GameState& s, EmpireId e) {
    std::vector<uint8_t> own(s.galaxy.systems.size(), 0);
    for (const auto& c : s.colonies)
        if (c && c->owner == e) own[s.galaxy.object(c->planet).system.index()] = 1;
    return own;
}

// Is any other living empire's colony reachable over the warp network?
bool connectedToOthers(const GameState& s, EmpireId e) {
    const auto own = colonySystems(s, e);
    std::vector<uint8_t> seen(s.galaxy.systems.size(), 0);
    std::deque<SystemId> queue;
    for (size_t i = 0; i < own.size(); ++i)
        if (own[i]) {
            seen[i] = 1;
            queue.push_back(SystemId{i});
        }
    if (queue.empty()) return true;
    std::vector<uint8_t> others(s.galaxy.systems.size(), 0);
    bool anyOther = false;
    for (const auto& c : s.colonies)
        if (c && c->owner != e && c->owner.valid() && s.empire(c->owner).alive) {
            others[s.galaxy.object(c->planet).system.index()] = 1;
            anyOther = true;
        }
    if (!anyOther) return true;  // nobody else to be cut off from
    while (!queue.empty()) {
        const SystemId sys = queue.front();
        queue.pop_front();
        if (others[sys.index()]) return true;
        for (SystemId n : s.galaxy.neighbors(sys))
            if (!seen[n.index()]) {
                seen[n.index()] = 1;
                queue.push_back(n);
            }
    }
    return false;
}

bool frontierLeft(const GameState& s, const Empire& e) {
    for (size_t i = 0; i < s.galaxy.systems.size(); ++i) {
        if (!e.hasExplored(SystemId{i})) continue;
        for (SystemId n : s.galaxy.neighbors(SystemId{i}))
            if (!e.hasExplored(n)) return true;
    }
    return false;
}

size_t surfaceIndex(std::string_view surface) {
    return datafile::keysEqual(surface, "Rock") ? 0 : datafile::keysEqual(surface, "Ice") ? 1 : 2;
}

// Which planet surfaces (Rock, Ice, Gas) the empire has colony modules for.
std::array<bool, 3> colonizableSurfaces(const Rules& r, const Empire& e) {
    std::array<bool, 3> out{};
    static constexpr std::array<AbilityKind, 3> kKinds{AbilityKind::ColonizeRock, AbilityKind::ColonizeIce, AbilityKind::ColonizeGas};
    for (uint32_t c = 0; c < r.data().components.size(); ++c) {
        if (!r.componentAvailable(e, c)) continue;
        for (size_t i = 0; i < 3; ++i) out[i] = out[i] || hasAbility(r.componentAbilities(c), kKinds[i]);
    }
    return out;
}

int angerChange(const Rules& r, const GameState& s, const Empire& e, const Empire& other, const AiProfile& prof, EmpireId mee,
                const std::array<bool, 3>& surfaces) {
    const AngerTable& t = prof.anger;
    const Relation& rel = e.relation(other.id);
    int delta = t.regularDecrease;

    // Planets of theirs we would like to have (spec 05 §7.3).
    const bool friendly = rel.treaty >= Treaty::NonAggression;
    const SettingsTable& st = prof.settings;
    if (friendly ? st.angryOverAlliedPlanets : st.angryOverEnemyPlanets) {
        int coveted = 0;
        for (const auto& c : s.colonies) {
            if (!c || c->owner != other.id) continue;
            const SpaceObject& planet = s.galaxy.object(c->planet);
            if (e.hasExplored(planet.system) && surfaces[surfaceIndex(planet.surface)]) ++coveted;
        }
        delta += t.perAttackLocation * (coveted * (friendly ? st.alliedPlanetsPercent : st.enemyPlanetsPercent) / 100);
    }

    // Their ships inside our claimed systems.
    int ships = 0;
    for (VehicleId vid : e.knowledge.visibleVehicles) {
        const Vehicle* v = s.vehicle(vid);
        if (!v || v->owner != other.id || isUnitType(vehicleType(r, s, *v))) continue;
        if (std::binary_search(e.claimedSystems.begin(), e.claimedSystems.end(), v->location.system)) ++ships;
    }
    const int perShip = rel.treaty == Treaty::War ? t.perEnemyShip : rel.treaty >= Treaty::NonAggression ? t.perAllyShip : t.perNoTreatyShip;
    delta += perShip * ships;

    if (other.id == mee) delta += t.megaEvilEmpire;

    // Battles of this turn between us.
    const auto own = colonySystems(s, e.id);
    for (const CombatRecord& rec : s.combats) {
        const bool both = std::find(rec.participants.begin(), rec.participants.end(), e.id) != rec.participants.end() &&
                          std::find(rec.participants.begin(), rec.participants.end(), other.id) != rec.participants.end();
        if (!both) continue;
        int ourLosses = 0, theirLosses = 0;
        for (const CombatEvent& ev : rec.events) {
            if (ev.kind != CombatEvent::Kind::Destroyed && ev.kind != CombatEvent::Kind::Captured) continue;
            if (ev.piece >= rec.pieces.size()) continue;
            const EmpireId owner = rec.pieces[ev.piece].owner;
            ourLosses += owner == e.id;
            theirLosses += owner == other.id;
        }
        const SystemId where = rec.location.system;
        const bool defending = (where.index() < own.size() && own[where.index()]) ||
                               std::binary_search(e.claimedSystems.begin(), e.claimedSystems.end(), where);
        if (ourLosses < theirLosses) delta += defending ? t.defendingWon : t.attackingWon;
        else if (ourLosses > theirLosses) delta += defending ? t.defendingLost : t.attackingLost;
        else delta += defending ? t.defendingStalemate : t.attackingStalemate;
    }

    // Detected intelligence operations: this turn's Intelligence log entries
    // naming the other empire (contract with the intel module).
    for (const LogEntry& l : e.log)
        if (l.turn == s.turn && l.category == LogCategory::Intelligence && !other.name.empty() &&
            (l.title.find(other.name) != std::string::npos || l.text.find(other.name) != std::string::npos))
            delta += t.intelligenceAgainstUs;

    // Messages from them delivered this turn (phase 2 delivers what was sent in phase 1).
    for (const DiplomaticMessage& m : s.messages) {
        if (m.to != e.id || m.from != other.id || !m.delivered || m.sentTurn != s.turn) continue;
        int v = t.receive[static_cast<size_t>(m.type)];
        if (m.type == MessageType::AcceptGift || m.type == MessageType::RefuseGift) {
            // A reply to our tribute uses the tribute values.
            for (const DiplomaticMessage& orig : s.messages)
                if (orig.id == m.inReplyTo && orig.type == MessageType::Tribute)
                    v = m.type == MessageType::AcceptGift ? t.receiveAcceptTribute : t.receiveRefuseTribute;
        }
        delta += v;
    }
    return delta;
}

} // namespace

std::vector<int64_t> politicalScores(const Rules& r, const GameState& s) {
    std::vector<int64_t> scores(s.empires.size(), 0);
    bool scored = false;
    for (const Empire& e : s.empires) {
        scores[e.id.index()] = score::empireScore(r, s, e.id);
        scored = scored || scores[e.id.index()] != 0;
    }
    if (scored) return scores;
    // The score module has nothing yet: estimate from the score window's
    // statistics (inferred weights).
    for (const Empire& e : s.empires)
        for (int level : e.techLevels) scores[e.id.index()] += int64_t{level} * 100;
    for (const auto& c : s.colonies)
        if (c && c->owner.valid() && c->owner.index() < scores.size())
            scores[c->owner.index()] += 1000 + c->totalPopulation() + static_cast<int64_t>(c->facilities.size()) * 100;
    std::vector<int64_t> designCost(s.designs.size(), -1);
    for (const Vehicle& v : s.vehicles) {
        if (!v.owner.valid() || v.owner.index() >= scores.size()) continue;
        int64_t& cost = designCost[v.design.index()];
        if (cost < 0) cost = computeDesignStats(r, nullptr, s.design(v.design)).cost.total();
        scores[v.owner.index()] += cost * std::max(1, v.count) / 10;
    }
    return scores;
}

int64_t politicalScore(const Rules& r, const GameState& s, EmpireId e) {
    if (!e.valid() || e.index() >= s.empires.size()) return 0;
    return politicalScores(r, s)[e.index()];
}

EmpireId megaEvilEmpire(const Rules& r, const GameState& s) {
    if (!r.settingFlag("AI Uses Mega Evil Empire", true)) return {};
    EmpireId top;
    int64_t best = 0, second = 0;
    const std::vector<int64_t> scores = politicalScores(r, s);
    for (const Empire& e : s.empires) {
        if (!e.alive || e.kind == PlayerKind::Neutral) continue;
        const int64_t v = scores[e.id.index()];
        if (!top.valid() || v > best) {
            second = top.valid() ? best : second;
            top = e.id;
            best = v;
        } else if (v > second) {
            second = v;
        }
    }
    if (!top.valid()) return {};
    // Our own fallbacks when Settings.txt lacks the keys.
    if (best < r.setting("AI Mega Evil Empire Threshold Score Thousands", 400) * 1000) return {};
    const int64_t pct = s.empire(top).kind == PlayerKind::Human ? r.setting("AI Human Mega Evil Empire Score Percent", 160)
                                                                 : r.setting("AI Computer Mega Evil Empire Score Percent", 240);
    if (second > 0 && best * 100 < pct * second) return {};
    return top;
}

AiState nextState(const Rules& r, const GameState& s, EmpireId id) {
    const Empire& e = s.empire(id);
    const AiState cur = stateOf(e);
    const int inState = e.aiTurnsInState;
    const AiProfile& prof = profileFor(r, e);

    // Armed hostiles in our colony systems.
    const auto own = colonySystems(s, id);
    int64_t threat = 0;
    for (VehicleId vid : e.knowledge.visibleVehicles) {
        const Vehicle* v = s.vehicle(vid);
        if (!v || !hostile(s, id, v->owner) || !armedDesign(r, s.design(v->design))) continue;
        if (v->location.system.index() < own.size() && own[v->location.system.index()]) threat += vehicleStrength(r, s, *v);
    }
    const bool defendingNow = cur == AiState::DefendShortTerm || cur == AiState::DefendLongTerm;
    if (threat > 0) {
        if (cur == AiState::DefendLongTerm || (cur == AiState::DefendShortTerm && inState >= 10)) return AiState::DefendLongTerm;
        return AiState::DefendShortTerm;
    }
    if (!connectedToOthers(s, id)) return AiState::NotConnected;

    std::vector<EmpireId> wars;
    for (const Empire& o : s.empires)
        if (o.alive && o.id != id && e.relation(o.id).treaty == Treaty::War) wars.push_back(o.id);
    if (!wars.empty()) {
        if (defendingNow) return AiState::PrepareForDefense;
        int64_t ours = 0;
        int warships = 0;
        for (const Vehicle& v : s.vehicles)
            if (v.owner == id && armedDesign(r, s.design(v.design)) && !isUnitType(vehicleType(r, s, v))) {
                ours += vehicleStrength(r, s, v);
                ++warships;
            }
        int64_t theirs = 0;
        for (VehicleId vid : e.knowledge.visibleVehicles)
            if (const Vehicle* v = s.vehicle(vid); v && std::find(wars.begin(), wars.end(), v->owner) != wars.end() &&
                                                   armedDesign(r, s.design(v->design)))
                theirs += vehicleStrength(r, s, *v);
        static constexpr std::array<int64_t, 4> kRatio{160, 130, 115, 105};  // (inferred)
        const bool stronger = warships > 0 && ours * 100 >= theirs * kRatio[static_cast<size_t>(std::clamp(s.options.aiDifficulty, 0, 3))];
        if (e.kind == PlayerKind::Neutral) return AiState::PrepareForDefense;
        switch (cur) {
            case AiState::PrepareForAttack:
                if (stronger && inState >= 3) return AiState::Attack;
                if (!stronger && inState >= 15) return AiState::PrepareForDefense;
                return AiState::PrepareForAttack;
            case AiState::Attack:
                if (inState >= 15 || (!stronger && inState >= 5)) return AiState::SecureHoldings;
                return AiState::Attack;
            case AiState::SecureHoldings:
                if (inState >= std::max(1, prof.settings.turnsBetweenAttacks)) return stronger ? AiState::PrepareForAttack : AiState::Incursion;
                return AiState::SecureHoldings;
            case AiState::Incursion:
                if (stronger) return AiState::PrepareForAttack;
                if (inState >= 20) return AiState::PrepareForDefense;
                return AiState::Incursion;
            case AiState::PrepareForDefense:
                if (stronger) return AiState::PrepareForAttack;
                if (warships >= 3 && inState >= 5) return AiState::Incursion;
                return AiState::PrepareForDefense;
            default: return stronger ? AiState::PrepareForAttack : AiState::PrepareForDefense;
        }
    }

    // Peace.
    if (defendingNow || (cur == AiState::PrepareForDefense && inState < 5)) return AiState::PrepareForDefense;
    if (e.kind != PlayerKind::Neutral && s.turn < 30 && frontierLeft(s, e)) return AiState::Exploration;
    return AiState::Infrastructure;
}

void updateAnger(TurnContext& ctx) {
    const Rules& r = ctx.rules;
    GameState& s = ctx.state;
    const EmpireId mee = megaEvilEmpire(r, s);
    // Compute every change first so empires later in the list see the same state.
    std::vector<std::vector<int>> changes(s.empires.size());
    for (const Empire& e : s.empires) {
        if (!e.alive || (e.kind == PlayerKind::Human && !e.ministerAll)) continue;
        const AiProfile& prof = profileFor(r, e);
        const std::array<bool, 3> surfaces = colonizableSurfaces(r, e);
        changes[e.id.index()].assign(s.empires.size(), 0);
        for (const Empire& other : s.empires)
            if (other.alive && other.id != e.id)
                changes[e.id.index()][other.id.index()] = angerChange(r, s, e, other, prof, mee, surfaces);
    }
    std::vector<AiState> next(s.empires.size(), AiState::Exploration);
    for (const Empire& e : s.empires)
        if (e.alive) next[e.id.index()] = nextState(r, s, e.id);

    for (Empire& e : s.empires) {
        if (!e.alive) continue;
        if (!changes[e.id.index()].empty()) {
            const int floor = profileFor(r, e).anger.minimum;
            for (size_t o = 0; o < s.empires.size() && o < e.relations.size(); ++o) {
                if (o == e.id.index()) continue;
                Relation& rel = e.relations[o];
                rel.anger = std::clamp(rel.anger + changes[e.id.index()][o], floor, std::max(floor, kMaxAnger));
            }
        }
        const AiState n = next[e.id.index()];
        if (static_cast<int>(n) != e.aiState) {
            e.aiState = static_cast<int>(n);
            e.aiTurnsInState = 0;
        } else {
            ++e.aiTurnsInState;
        }
    }
}

} // namespace opense4::game::ai
