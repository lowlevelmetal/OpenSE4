// Computer player: the AI step of the turn (spec 05 §7.1-§7.4, §7.6). It
// runs with a mutable state once per turn (ai::updateAnger): what the
// computer players decided this turn, the per-empire counters, the AI state
// machine (§7.2), the political step (territory and anger, §7.3) and the
// Mega Evil Empire (§7.6).

#include "datafile/datafile.hpp"
#include "game/ai.hpp"
#include "game/ai_data.hpp"
#include "game/ai_planner.hpp"
#include "game/commands.hpp"
#include "game/design.hpp"
#include "game/intel.hpp"
#include "game/query.hpp"
#include "game/score.hpp"
#include "game/sight.hpp"
#include "game/turn.hpp"
#include "game/xmath.hpp"

#include <algorithm>
#include <deque>
#include <optional>

namespace opense4::game::ai {

using namespace detail;

namespace {

constexpr int kMaxAnger = 100;
constexpr int kCounterCap = 1'000'000;

uint64_t mixBits(uint64_t x) {
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

Rng stepRng(const GameState& s, EmpireId e) { return Rng(mixBits(mixBits(s.seed ^ 0x416E676572ull) ^ s.turn) ^ (uint64_t{e.value} << 24)); }

bool politicsOn(const Empire& e) { return e.alive && ministerOn(e, Minister::Politics); }

bool contains(const std::vector<SystemId>& sorted, SystemId sys) { return std::binary_search(sorted.begin(), sorted.end(), sys); }

// "Wars" of spec 05 §7.4: empires we are at war with whose score is above a quarter of ours.
bool atWarWithStrongEnemy(const GameState& s, const Empire& e, const std::vector<int64_t>& scores) {
    const int64_t quarter = scores[e.id.index()] / 4;
    for (const Empire& x : s.empires)
        if (x.id != e.id && x.alive && e.relation(x.id).treaty == Treaty::War && scores[x.id.index()] > quarter) return true;
    return false;
}

// ---- What this turn's decisions set in motion ------------------------------------------------

const DiplomaticMessage* findMessage(const GameState& s, MessageId id) {
    for (const DiplomaticMessage& m : s.messages)
        if (m.id == id) return &m;
    return nullptr;
}

void addUnique(std::vector<SystemId>& list, SystemId sys) {
    if (sys.valid() && std::find(list.begin(), list.end(), sys) == list.end()) list.push_back(sys);
}

// Declaring war sets anger to 100; an accepted demand is carried out half of
// the time (spec 05 §7.3, §7.4).
void recordDecisions(TurnContext& ctx, EmpireId id, Rng& rng) {
    GameState& s = ctx.state;
    std::vector<DiplomaticMessage> sent;
    for (const DiplomaticMessage& m : s.messages)
        if (m.from == id && m.sentTurn == s.turn) sent.push_back(m);
    for (const DiplomaticMessage& m : sent) {
        if (!m.to.valid() || m.to.index() >= s.empires.size()) continue;
        if (m.type == MessageType::DeclareWar) {
            s.empire(id).relation(m.to).anger = kMaxAnger;
            continue;
        }
        if (m.type != MessageType::AcceptDemand || !politicsOn(s.empire(id))) continue;
        const DiplomaticMessage* demand = findMessage(s, m.inReplyTo);
        if (!demand || !isDemand(demand->type)) continue;
        const DiplomaticMessage d = *demand;
        if (!rng.percent(50)) continue;
        Empire& e = s.empire(id);
        auto third = [&]() -> Relation* {
            return d.thirdEmpire.valid() && d.thirdEmpire.index() < e.relations.size() && d.thirdEmpire != id ? &e.relation(d.thirdEmpire)
                                                                                                                : nullptr;
        };
        const SystemId planetSystem = d.planet.valid() && d.planet.index() < s.galaxy.objects.size() ? s.galaxy.object(d.planet).system
                                                                                                        : SystemId{};
        switch (d.type) {
            case MessageType::DemandRemoveShips:
            case MessageType::DemandRemoveColonies: addUnique(e.aiMemory.avoid, d.system); break;
            case MessageType::DemandLeavePlanet: addUnique(e.aiMemory.avoid, d.system.valid() ? d.system : planetSystem); break;
            case MessageType::RequestBreakTreaty:
                if (Relation* r = third()) r->queuedBreak = true;
                break;
            case MessageType::RequestDeclareWar:
            case MessageType::RequestSupport:
                if (Relation* r = third()) r->queuedWar = true;
                break;
            case MessageType::RequestMakePeace:
                if (Relation* r = third()) r->queuedPeace = true;
                break;
            case MessageType::RequestAttackEmpire: addUnique(e.aiMemory.attackSystems, d.system); break;
            case MessageType::RequestAttackPlanet: addUnique(e.aiMemory.attackSystems, d.system.valid() ? d.system : planetSystem); break;
            case MessageType::RequestStopHostilities: e.relation(d.from).promise = true; break;
            case MessageType::DemandStopEspionage:
            case MessageType::DemandStopSabotage: {
                std::vector<IntelProjectOrder> keep;
                for (const IntelProjectOrder& o : e.intel)
                    if (o.target != d.from) keep.push_back(o);
                if (keep.size() != e.intel.size()) {
                    const cmd::SetIntel c{keep, e.intelEvenly, e.repeatIntel};
                    const CommandResult res = apply(ctx.rules, s, id, c);
                    if (!res.ok) ctx.rejected.emplace_back(id, "SetIntel: " + res.error);
                }
                break;
            }
            default: break;  // stop attacks: nothing; gifts, tributes and surrender are sent by the planner
        }
    }
}

// Turns since war, treaty age, and the queues forgotten every 10 turns.
void keepCounters(GameState& s, Empire& e) {
    const bool forget = s.turn % 10 == 0;
    for (size_t i = 0; i < e.relations.size(); ++i) {
        if (i == e.id.index()) continue;
        Relation& rel = e.relations[i];
        rel.turnsSinceWar = rel.treaty == Treaty::War ? 0 : std::min(rel.turnsSinceWar + 1, kCounterCap);
        const bool bothHigh = rel.agedTreaty >= Treaty::TradeAlliance && rel.treaty >= Treaty::TradeAlliance;
        if (rel.treaty != rel.agedTreaty && !bothHigh) rel.treatyAge = 0;
        else rel.treatyAge = std::min(rel.treatyAge + 1, kCounterCap);
        rel.agedTreaty = rel.treaty;
        if (forget) {
            rel.promise = rel.queuedWar = rel.queuedBreak = rel.queuedPeace = false;
            rel.attackedUs = rel.spiedOnUs = false;
            rel.attackedIn = {};
        }
    }
    if (forget) {
        e.aiMemory.avoid.clear();
        e.aiMemory.attackSystems.clear();
    }
}

// ---- Combat results (spec 05 §7.3 step 1) ---------------------------------------------------

enum class Outcome : uint8_t { Won, Lost, Stalemate };

bool involves(const CombatRecord& rec, EmpireId e) { return std::find(rec.participants.begin(), rec.participants.end(), e) != rec.participants.end(); }

Outcome outcomeFor(const CombatRecord& rec, EmpireId e) {
    std::vector<uint8_t> gone(rec.pieces.size(), 0);
    for (const CombatEvent& ev : rec.events)
        if ((ev.kind == CombatEvent::Kind::Destroyed || ev.kind == CombatEvent::Kind::Captured) && ev.piece < gone.size()) gone[ev.piece] = 1;
    int ours = 0, ourSurvivors = 0, otherSurvivors = 0;
    for (size_t i = 0; i < rec.pieces.size(); ++i) {
        const CombatPiece& p = rec.pieces[i];
        if (p.kind == CombatPiece::Kind::Seeker) continue;
        if (p.owner == e) {
            ++ours;
            ourSurvivors += !gone[i];
        } else if (!gone[i]) {
            ++otherSurvivors;
        }
    }
    if (ours > 0 && ourSurvivors > 0 && otherSurvivors == 0) return Outcome::Won;
    if (ours > 0 && ourSurvivors == 0 && otherSurvivors > 0) return Outcome::Lost;
    return Outcome::Stalemate;
}

// ---- The AI state machine (spec 05 §7.2) -----------------------------------------------------

struct Decision {
    AiState next;
    AiMemory memory;
};

std::vector<int> jumpsFromOver(const GameState& s, EmpireId id, SystemId from) {
    std::vector<int> dist(s.galaxy.systems.size(), -1);
    if (!from.valid() || from.index() >= dist.size()) return dist;
    std::deque<SystemId> queue{from};
    dist[from.index()] = 0;
    while (!queue.empty()) {
        const SystemId at = queue.front();
        queue.pop_front();
        for (ObjectId wp : s.galaxy.system(at).objects) {
            const SpaceObject& o = s.galaxy.object(wp);
            if (o.kind != ObjectKind::WarpPoint || !o.destination.valid() || !sight::knowsWarpLink(s, id, wp)) continue;
            const SystemId to = s.galaxy.object(o.destination).system;
            if (dist[to.index()] >= 0) continue;
            dist[to.index()] = dist[at.index()] + 1;
            queue.push_back(to);
        }
    }
    return dist;
}

Decision decide(const GameState& s, EmpireId id, const Situation& sit, const AiProfile& prof) {
    const Empire& e = s.empire(id);
    Decision d{stateOf(e), e.aiMemory};
    AiMemory& m = d.memory;
    const int inState = e.aiTurnsInState;
    const bool enemyIn = !sit.enemyInTerritory.empty();
    const bool enemyNear = !sit.enemyNearby.empty();
    // The after-attack timer grows by one at the end of each turn while it runs.
    if (m.afterAttack != 0) m.afterAttack = std::min(m.afterAttack + 1, kCounterCap);
    const int wait = s.options.teamMode ? 1 : prof.settings.turnsBetweenAttacks;
    const bool gap = m.afterAttack == 0 || m.afterAttack > wait;
    auto hostile = [&](SystemId sys) { return sys.valid() && sys.index() < sit.hostile.size() ? sit.hostile[sys.index()] : 0; };
    auto ours = [&](SystemId sys) { return sys.valid() && sys.index() < sit.ours.size() ? sit.ours[sys.index()] : 0; };
    auto candidateLeft = [&]() {
        for (const Candidate& c : sit.candidates)
            if (hostileTo(e, c.owner) && std::find(m.targets.begin(), m.targets.end(), c.system) != m.targets.end()) return true;
        return false;
    };
    // Steps 1-4 shared by Prepare for Attack and Attack.
    auto common = [&](bool attacking) -> std::optional<AiState> {
        if (m.targets.empty() || !m.staging.valid()) return AiState::Infrastructure;
        if (enemyIn) return AiState::DefendShortTerm;
        const int64_t staging = ours(m.staging);
        std::erase_if(m.targets, [&](SystemId t) { return hostile(t) > 5 * staging; });
        if (m.targets.empty()) {
            if (attacking) m.afterAttack = 1;
            return AiState::Infrastructure;
        }
        if (inState >= 4) {  // from the 5th turn in this state
            std::vector<uint8_t> near(s.galaxy.systems.size(), 0);
            int64_t theirs = 0;
            for (SystemId t : m.targets) {
                theirs += hostile(t);
                const std::vector<int> j = jumpsFromOver(s, id, t);
                for (size_t i = 0; i < j.size(); ++i)
                    if (j[i] >= 0 && j[i] <= 4) near[i] = 1;
            }
            int64_t mine = 0;
            for (size_t i = 0; i < near.size(); ++i)
                if (near[i]) mine += sit.ours[i];
            if (!(mine > 3 * theirs)) return AiState::Infrastructure;
        }
        return std::nullopt;
    };

    AiState next = d.next;
    switch (d.next) {
        case AiState::Exploration:
            if (enemyIn) next = AiState::DefendShortTerm;
            else if (sit.notConnected) next = AiState::NotConnected;
            else if (sit.contact && (enemyNear || !sit.bordersUnexplored)) next = AiState::Infrastructure;
            break;
        case AiState::Infrastructure: {
            if (enemyIn) {
                next = AiState::DefendShortTerm;
                break;
            }
            std::vector<SystemId> targets;
            if (gap)
                for (const Candidate& c : sit.candidates)
                    if (hostileTo(e, c.owner) && targets.size() < 3 && std::find(targets.begin(), targets.end(), c.system) == targets.end())
                        targets.push_back(c.system);
            if (!targets.empty()) {
                m.targets = targets;
                m.staging = {};
                const std::vector<int> j = jumpsFromOver(s, id, targets.front());
                int best = -1;
                for (size_t i = 0; i < sit.territory.size(); ++i)
                    if (sit.territory[i] && SystemId{i} != targets.front() && j[i] >= 0 && (best < 0 || j[i] < best)) {
                        best = j[i];
                        m.staging = SystemId{i};
                    }
                next = AiState::PrepareForAttack;
            } else if (sit.notConnected) {
                next = AiState::NotConnected;
            }
            break;
        }
        case AiState::PrepareForAttack:
            if (auto r = common(false)) next = *r;
            else if (!candidateLeft()) next = AiState::Infrastructure;
            else if (ours(m.staging) > hostile(m.targets.front())) next = AiState::Attack;
            else if (inState > 20) next = AiState::Infrastructure;
            break;
        case AiState::Attack: {
            if (auto r = common(true)) {
                next = *r;
                break;
            }
            const bool cleared = std::all_of(m.targets.begin(), m.targets.end(), [&](SystemId t) { return hostile(t) == 0; });
            if (cleared) {
                m.secured = m.targets.front();
                next = AiState::SecureHoldings;
            } else if (!candidateLeft()) {
                next = AiState::Infrastructure;
            } else if (inState > 20) {
                m.afterAttack = 1;
                next = AiState::Infrastructure;
            }
            break;
        }
        case AiState::SecureHoldings: {
            if (enemyIn) {
                next = AiState::DefendShortTerm;
                break;
            }
            if (hostile(m.secured) > 0) {
                m.targets = {m.secured};
                m.staging = m.secured;
                next = AiState::Attack;
                break;
            }
            int64_t around = 0;
            const std::vector<int> j = jumpsFromOver(s, id, m.secured);
            for (size_t i = 0; i < j.size(); ++i)
                if (j[i] == 1) around = std::max(around, sit.hostile[i]);
            if (ours(m.secured) > std::max<int64_t>(10, around)) {
                m.afterAttack = 1;
                next = AiState::Infrastructure;
            }
            break;
        }
        case AiState::DefendShortTerm:
            if (!enemyIn) next = enemyNear || !sit.bordersUnexplored ? AiState::Infrastructure : AiState::Exploration;
            break;
        case AiState::NotConnected:
            if (!sit.notConnected) next = AiState::Infrastructure;
            break;
        default: next = AiState::Infrastructure; break;  // the states never entered (inferred fallback)
    }
    if (next != AiState::Infrastructure) m.afterAttack = 0;
    m.defend = sit.defend;
    d.next = next;
    return d;
}

// ---- Anger (spec 05 §7.3) --------------------------------------------------------------------

struct AngerInputs {
    std::vector<int64_t> scores;
    std::vector<std::vector<SystemId>> territory;   // per empire, sorted
    std::vector<EmpireId> mee;                      // per viewer
};

bool isStellarReport(std::string_view title) {
    return title.starts_with("Planet destroyed:") || title.starts_with("Star destroyed:") || title.starts_with("Black hole created") ||
           title.starts_with("Nebula created");
}

// Was the empire in that system when something happened there this turn?
// It is still there, or it logged something located there (inferred).
bool presentIn(const GameState& s, const Empire& e, SystemId sys) {
    for (const auto& c : s.colonies)
        if (c && c->owner == e.id && s.galaxy.object(c->planet).system == sys) return true;
    for (const Vehicle& v : s.vehicles)
        if (v.owner == e.id && v.location.system == sys) return true;
    for (const LogEntry& l : e.log)
        if (l.turn == s.turn && l.location && l.location->system == sys) return true;
    return false;
}

void updateAngerToward(const Rules& r, const GameState& s, Empire& e, const Empire& x, const AngerInputs& in, const AiProfile& prof) {
    const AngerTable& t = prof.anger;
    Relation& rel = e.relation(x.id);
    int anger = std::clamp(rel.anger, 0, kMaxAnger);
    auto add = [&](int64_t delta) { anger = static_cast<int>(std::clamp<int64_t>(anger + delta, 0, kMaxAnger)); };
    const bool belowNonAggression = treatyIsHostile(rel.treaty);
    const std::vector<SystemId>& ours = in.territory[e.id.index()];

    // 1. Combat. Attacking or defending is judged by where the battle was:
    // outside our territory we are the attacker (inferred; the engine does not
    // record who started a battle).
    if (belowNonAggression)
        for (const CombatRecord& rec : s.combats) {
            if (!involves(rec, e.id) || !involves(rec, x.id)) continue;
            const bool attacking = !contains(ours, rec.location.system);
            switch (outcomeFor(rec, e.id)) {
                case Outcome::Won: add(attacking ? t.attackingWon : t.defendingWon); break;
                case Outcome::Lost: add(attacking ? t.attackingLost : t.defendingLost); break;
                case Outcome::Stalemate: add(attacking ? t.attackingStalemate : t.defendingStalemate); break;
            }
        }
    // 2. Stellar manipulation reported to empires in that system.
    for (const LogEntry& l : x.log)
        if (l.turn == s.turn && l.category == LogCategory::Events && isStellarReport(l.title) && l.location &&
            presentIn(s, e, l.location->system))
            add(int64_t{2} * t.defendingLost);
    // 3. Successful operations traced to them: the victim's log names the
    // culprit (intel::namesCulprit); blocked attempts and counter-intelligence never do.
    for (const LogEntry& l : e.log)
        if (l.turn == s.turn && intel::namesCulprit(s, l, x.id)) add(t.intelligenceAgainstUs);
    // 4. The earliest message from them that arrived this turn.
    const DiplomaticMessage* first = nullptr;
    for (const DiplomaticMessage& m : s.messages)
        if (m.to == e.id && m.from == x.id && m.delivered && m.sentTurn == s.turn && (!first || m.id < first->id)) first = &m;
    if (first) {
        int v = t.receive[static_cast<size_t>(first->type)];
        if (first->type == MessageType::AcceptGift || first->type == MessageType::RefuseGift)
            if (const DiplomaticMessage* orig = findMessage(s, first->inReplyTo); orig && orig->type == MessageType::Tribute)
                v = first->type == MessageType::AcceptGift ? t.receiveAcceptTribute : t.receiveRefuseTribute;
        add(v);
    }
    // 5. Decay.
    add(t.regularDecrease);
    // 6. The Mega Evil Empire as we see it.
    if (in.mee[e.id.index()] == x.id) add(t.megaEvilEmpire);
    // 7. Team Mode.
    if (s.options.teamMode) add((e.kind == PlayerKind::Human) == (x.kind == PlayerKind::Human) ? -20 : 20);
    // 8. A promise to stop hostile actions.
    if (rel.promise) {
        add(-20);
        rel.promise = false;
    }
    // 9. Attack locations.
    {
        const bool friendOf = !belowNonAggression;
        const bool hostileX = hostileTo(e, x.id);
        int64_t n = 0;
        std::vector<uint8_t> counted(s.galaxy.systems.size(), 0);
        const bool farTerm = !atWarWithStrongEnemy(s, e, in.scores);
        std::vector<int> nearX(s.galaxy.systems.size(), -1);  // jumps from X's territory, up to 2
        if (farTerm) {
            std::deque<SystemId> queue;
            for (SystemId sys : in.territory[x.id.index()]) {
                nearX[sys.index()] = 0;
                queue.push_back(sys);
            }
            while (!queue.empty()) {
                const SystemId at = queue.front();
                queue.pop_front();
                if (nearX[at.index()] >= 2) continue;
                for (SystemId nb : s.galaxy.neighbors(at))
                    if (nearX[nb.index()] < 0) {
                        nearX[nb.index()] = nearX[at.index()] + 1;
                        queue.push_back(nb);
                    }
            }
        }
        for (const auto& c : s.colonies) {
            if (!c || c->owner != x.id) continue;
            if (hostileX && !notices(s, e.id, planetKey(c->planet))) continue;
            const SpaceObject& planet = s.galaxy.object(c->planet);
            const SystemId sys = planet.system;
            if (e.hasExplored(sys) && canSettle(r, s, e, planet)) n += 1;
            if (counted[sys.index()]) continue;
            if (contains(ours, sys)) {
                counted[sys.index()] = 1;
                n += 3;
            } else if (farTerm && nearX[sys.index()] >= 0) {
                counted[sys.index()] = 1;
                n += 10;
            }
        }
        const SettingsTable& st = prof.settings;
        if (friendOf ? !st.angryOverAlliedPlanets : !st.angryOverEnemyPlanets) n = 0;
        n = xmath::pctTrunc(n, friendOf ? st.alliedPlanetsPercent : st.enemyPlanetsPercent);
        add(int64_t{t.perAttackLocation} * n);
    }
    // 10. Their objects in our territory (below Non-Aggression only).
    if (belowNonAggression) {
        int64_t n = 0;
        for (VehicleId vid : e.knowledge.visibleVehicles) {
            const Vehicle* v = s.vehicle(vid);
            if (!v || v->owner != x.id || v->count <= 0 || !contains(ours, v->location.system)) continue;
            if (vehicleType(r, s, *v) == ruleset::VehicleType::Mine || !notices(s, e.id, vehicleKey(vid))) continue;
            ++n;
        }
        for (const auto& c : s.colonies) {
            if (!c || c->owner != x.id || c->totalPopulation() <= 0) continue;
            const SystemId sys = s.galaxy.object(c->planet).system;
            if (contains(ours, sys) && e.hasExplored(sys) && notices(s, e.id, planetKey(c->planet))) ++n;
        }
        add(int64_t{rel.treaty == Treaty::None ? t.perNoTreatyShip : t.perEnemyShip} * n);
    }
    // 11. Floor.
    anger = std::max(anger, std::clamp(t.minimum, 0, kMaxAnger));
    rel.anger = anger;
}

// What happened this turn that the AI remembers for its own demands (spec 05 §7.4).
void rememberEvents(GameState& s, Empire& e, const std::vector<SystemId>& territory) {
    for (size_t i = 0; i < e.relations.size(); ++i) {
        if (i == e.id.index()) continue;
        Relation& rel = e.relations[i];
        const EmpireId x{i};
        rel.combatsLastTurn = rel.combatsThisTurn;
        rel.combatsThisTurn = 0;
        for (const CombatRecord& rec : s.combats) {
            if (!involves(rec, e.id) || !involves(rec, x)) continue;
            ++rel.combatsThisTurn;
            if (contains(territory, rec.location.system)) {  // they came to us (inferred)
                rel.attackedUs = true;
                rel.attackedIn = rec.location.system;
            }
        }
        if (x.index() < s.empires.size())
            for (const LogEntry& l : e.log)
                if (l.turn == s.turn && intel::namesCulprit(s, l, x)) rel.spiedOnUs = true;
    }
    // Contract with the combat module: a ship struck by mines logs "Mines at <sector>".
    for (const LogEntry& l : e.log)
        if (l.turn == s.turn && l.category == LogCategory::Combat && l.title.starts_with("Mines at ")) e.aiMemory.metMinefield = true;
}

} // namespace

// ---- Public ----------------------------------------------------------------------------------------

std::vector<int64_t> politicalScores(const Rules& r, const GameState& s) {
    std::vector<int64_t> scores(s.empires.size(), 0);
    for (const Empire& e : s.empires)
        if (e.alive) scores[e.id.index()] = score::empireScore(r, s, e.id);
    return scores;
}

int64_t politicalScore(const Rules& r, const GameState& s, EmpireId e) {
    if (!e.valid() || e.index() >= s.empires.size() || !s.empire(e).alive) return 0;
    return score::empireScore(r, s, e);
}

EmpireId megaEvilEmpire(const Rules& r, const std::vector<int64_t>& scores, const GameState& s, EmpireId viewer) {
    if (!r.settingFlag("AI Uses Mega Evil Empire", true)) return {};
    // Our fallbacks when Settings.txt lacks the keys.
    const int64_t threshold = r.setting("AI Mega Evil Empire Threshold Score Thousands", 500) * 1000;
    EmpireId top;
    for (const Empire& e : s.empires) {
        if (!e.alive || e.id == viewer) continue;
        const int64_t v = scores[e.id.index()];
        if (v <= threshold) continue;
        if (!top.valid() || v > scores[top.index()]) top = e.id;
    }
    if (!top.valid()) return {};
    const int64_t pct = s.empire(top).kind == PlayerKind::Human ? r.setting("AI Human Mega Evil Empire Score Percent", 170)
                                                                 : r.setting("AI Computer Mega Evil Empire Score Percent", 250);
    for (const Empire& e : s.empires)
        if (e.alive && e.id != top && scores[top.index()] < xmath::pctRound(scores[e.id.index()], pct)) return {};
    return top;
}

EmpireId megaEvilEmpire(const Rules& r, const GameState& s, EmpireId viewer) {
    return megaEvilEmpire(r, politicalScores(r, s), s, viewer);
}

AiState nextState(const Rules& r, const GameState& s, EmpireId id) {
    if (!id.valid() || id.index() >= s.empires.size()) return AiState::Exploration;
    const AiProfile& prof = profileFor(r, s.empire(id));
    return decide(s, id, assess(r, s, id, prof), prof).next;
}

void recordAiDecisions(TurnContext& ctx) {
    GameState& s = ctx.state;
    // Difficulty (spec 05 §7.1): set once. An empire that appears after the
    // first turn, other than a random player, was founded by a revolt.
    const int rebels = rebelDifficulty(s);
    for (Empire& e : s.empires) {
        if (e.aiDifficulty >= 0 || e.kind == PlayerKind::Human) continue;
        const auto& random = s.options.randomAiPlayers;
        const bool randomPlayer = e.id.index() < random.size() && random[e.id.index()] != 0;
        e.aiDifficulty = randomPlayer || s.turn == 0 ? difficultyOf(s, e.id) : rebels;
    }
    // The counters (the queues are forgotten every 10 turns), then this turn's decisions.
    for (Empire& e : s.empires) {
        if (!e.alive) continue;
        keepCounters(s, s.empire(e.id));
        Rng rng = stepRng(s, e.id);
        recordDecisions(ctx, e.id, rng);
    }
}

void updateAiStates(TurnContext& ctx) {
    const Rules& r = ctx.rules;
    GameState& s = ctx.state;
    // Territory: computer players claim theirs anew each turn; the systems
    // they agreed to leave are their systems to avoid.
    for (Empire& e : s.empires) {
        if (!e.alive || e.kind == PlayerKind::Human) continue;
        e.claimedSystems = computeTerritory(s, e.id);
        e.systemsToAvoid = e.aiMemory.avoid;
        std::sort(e.systemsToAvoid.begin(), e.systemsToAvoid.end());
    }
    for (Empire& e : s.empires) {
        if (!e.alive) continue;
        const AiProfile& prof = profileFor(r, e);
        const Decision d = decide(s, e.id, assess(r, s, e.id, prof), prof);
        Empire& me = s.empire(e.id);
        me.aiMemory = d.memory;
        if (static_cast<int>(d.next) != me.aiState) {
            me.aiState = static_cast<int>(d.next);
            me.aiTurnsInState = 0;
        } else {
            me.aiTurnsInState = std::min(me.aiTurnsInState + 1, kCounterCap);
        }
    }
}

void politicalStep(TurnContext& ctx) {
    const Rules& r = ctx.rules;
    GameState& s = ctx.state;
    AngerInputs in;
    in.scores = politicalScores(r, s);
    in.territory.resize(s.empires.size());
    in.mee.resize(s.empires.size());
    for (const Empire& e : s.empires) {
        in.territory[e.id.index()] = computeTerritory(s, e.id);
        in.mee[e.id.index()] = megaEvilEmpire(r, in.scores, s, e.id);
    }
    // Anger toward every living empire in contact, for empires whose Politics minister is on.
    for (Empire& e : s.empires) {
        if (!politicsOn(e)) continue;
        const AiProfile& prof = profileFor(r, e);
        for (const Empire& x : s.empires) {
            if (x.id == e.id || !x.alive || x.id.index() >= e.relations.size() || !e.relation(x.id).contact) continue;
            updateAngerToward(r, s, e, x, in, prof);
        }
    }
    // An eliminated empire is forgotten: anger toward it is 0.
    for (Empire& e : s.empires)
        for (const Empire& x : s.empires)
            if (!x.alive && x.id != e.id && x.id.index() < e.relations.size()) e.relation(x.id).anger = 0;
}

void rememberAiEvents(TurnContext& ctx) {
    GameState& s = ctx.state;
    for (Empire& e : s.empires)
        if (e.alive) rememberEvents(s, e, computeTerritory(s, e.id));
}

void updateAnger(TurnContext& ctx) {
    recordAiDecisions(ctx);
    updateAiStates(ctx);  // the state machine comes before the political step (spec 05 §7.1)
    politicalStep(ctx);
    rememberAiEvents(ctx);
}

} // namespace opense4::game::ai
