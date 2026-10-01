// Computer player: the parts of the turn that change the AI's own memory
// (spec 05 §7.1-§7.4, §7.6): what the computer players decided this turn and
// the per-empire counters, the AI state machine (§7.2), the political step
// (territory and anger, §7.3) with the Mega Evil Empire (§7.6), and what the
// AI remembers of the turn's battles and spies. processTurn runs them at
// their places in the spec 05 §8 order; ai::updateAnger runs them together.

#include "datafile/datafile.hpp"
#include "game/ai.hpp"
#include "game/ai_data.hpp"
#include "game/ai_planner.hpp"
#include "game/commands.hpp"
#include "game/design.hpp"
#include "game/intel.hpp"
#include "game/movement.hpp"
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

// Declaring war sets anger to 100 (spec 05 §7.3, §7.4). Accepted demands are
// carried out by the Politics minister itself (cmd::CarryOutDemand), before
// it replies.
void recordDecisions(TurnContext& ctx, EmpireId id, Rng&) {
    GameState& s = ctx.state;
    for (const DiplomaticMessage& m : s.messages)
        if (m.from == id && m.sentTurn == s.turn && m.type == MessageType::DeclareWar && m.to.valid() && m.to.index() < s.empires.size())
            s.empire(id).relation(m.to).anger = kMaxAnger;
}

// Turns since war, treaty age, and the attacks and spies the AI noted for
// its own demands (OpenSE4's record of what its log shows), forgotten every
// 10 turns.
void keepCounters(GameState& s, Empire& e) {
    const bool forget = aiDate(s) % 10 == 0;  // "every 10 turns" reads the date the ministers see
    for (size_t i = 0; i < e.relations.size(); ++i) {
        if (i == e.id.index()) continue;
        Relation& rel = e.relations[i];
        rel.turnsSinceWar = rel.treaty == Treaty::War ? 0 : std::min(rel.turnsSinceWar + 1, kCounterCap);
        const bool bothHigh = rel.agedTreaty >= Treaty::TradeAlliance && rel.treaty >= Treaty::TradeAlliance;
        if (rel.treaty != rel.agedTreaty && !bothHigh) rel.treatyAge = 0;
        else rel.treatyAge = std::min(rel.treatyAge + 1, kCounterCap);
        rel.agedTreaty = rel.treaty;
        if (forget) {
            rel.attackedUs = rel.spiedOnUs = false;
            rel.attackedIn = {};
        }
    }
}

// The demand lists (war, break, peace and the promises) and the systems
// marked to avoid or to attack are emptied in the start-of-turn step of every
// turn whose date (the ministers') is a multiple of 10, before the Politics
// minister acts (spec 05 §7.4 "Demand lists", confirmed: binary).
void forgetDemands(const GameState& s, Empire& e) {
    if (aiDate(s) % 10 != 0) return;
    for (Relation& rel : e.relations) rel.promises = rel.queuedWar = rel.queuedBreak = rel.queuedPeace = 0;
    e.aiMemory.avoid.clear();
    e.aiMemory.attackSystems.clear();
}

// ---- Combat results (spec 05 §7.3 step 1) ---------------------------------------------------

enum class Outcome : uint8_t { Won, Lost, Stalemate };

bool involves(const CombatRecord& rec, EmpireId e) { return std::find(rec.participants.begin(), rec.participants.end(), e) != rec.participants.end(); }

// The battle's verdict for `e`, the one its report gives (spec 04 §15 "The
// verdict", confirmed: binary): its survivors (pieces it took during the
// battle counted, seekers left out) against those of every other empire,
// whatever the treaty; neutral obstacles belong to no empire.
Outcome outcomeFor(const CombatRecord& rec, EmpireId e) {
    std::vector<uint8_t> gone(rec.pieces.size(), 0);
    std::vector<EmpireId> owner(rec.pieces.size());
    for (size_t i = 0; i < rec.pieces.size(); ++i) owner[i] = rec.pieces[i].owner;
    for (const CombatEvent& ev : rec.events) {
        if (ev.piece >= gone.size()) continue;
        if (ev.kind == CombatEvent::Kind::Destroyed) gone[ev.piece] = 1;
        else if (ev.kind == CombatEvent::Kind::Captured) owner[ev.piece] = EmpireId{static_cast<uint32_t>(ev.amount)};
    }
    int ours = 0, ourSurvivors = 0, otherSurvivors = 0;
    for (size_t i = 0; i < rec.pieces.size(); ++i) {
        const CombatPiece& p = rec.pieces[i];
        if (p.kind == CombatPiece::Kind::Seeker || p.kind == CombatPiece::Kind::Obstacle) continue;
        if (p.owner == e) ++ours;
        if (gone[i] || !owner[i].valid()) continue;
        if (owner[i] == e) ++ourSurvivors;
        else ++otherSurvivors;
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
        // Once 5 full turns have been spent in this state (from the 6th turn):
        // our strength within 4 jumps of each target, counted per target, so a
        // system near two targets counts twice (confirmed: binary).
        if (inState >= 5) {
            int64_t theirs = 0, mine = 0;
            for (SystemId t : m.targets) {
                theirs += hostile(t);
                const std::vector<int> j = jumpsOver(s, t);
                for (size_t i = 0; i < j.size(); ++i)
                    if (j[i] <= 4) mine += sit.ours[i];
            }
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
                // The staging system: of our territory, other than the first
                // target, the fewest jumps to it over every link.
                const std::vector<int> j = jumpsOver(s, targets.front());
                int best = 0;
                for (size_t i = 0; i < sit.territory.size(); ++i)
                    if (sit.territory[i] && SystemId{i} != targets.front() && (!m.staging.valid() || j[i] < best)) {
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
            const std::vector<int> j = jumpsOver(s, m.secured);
            for (size_t i = 0; i < j.size(); ++i)
                if (j[i] == 1) around = std::max(around, sit.hostile[i]);
            if (ours(m.secured) > std::max<int64_t>(10 * kStrengthScale, around)) {
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
    // What the step counts (spec 05 §7.3 "What it counts"): per battle of
    // GameState::combats, per log entry of each empire, and the messages.
    std::vector<uint8_t> battles;
    std::vector<std::vector<uint8_t>> logs;
    PoliticalWindow window;
    bool counts(uint32_t turn) const {
        return window.turn && (turn == *window.turn || (window.andLater && turn > *window.turn));
    }
    bool countsMessage(const DiplomaticMessage& m) const {
        if (window.messagesByDelivery) return m.delivered && m.id.value >= window.firstMessage && m.dated >= window.messagesFrom;
        return counts(m.sentTurn) && m.id.value >= window.firstMessage;
    }
};

// Marks the items of `list` (in order) that the window counts: those dated
// its turn from position `skip` on among them, and later ones when it has them.
template <class List, class TurnOf>
std::vector<uint8_t> countedItems(const List& list, const AngerInputs& in, uint32_t skip, TurnOf&& turnOf) {
    std::vector<uint8_t> out(list.size(), 0);
    uint32_t atTurn = 0;
    for (size_t i = 0; i < list.size(); ++i) {
        const uint32_t turn = turnOf(list[i]);
        if (in.window.turn && turn == *in.window.turn) out[i] = atTurn++ >= skip ? 1 : 0;
        else out[i] = in.counts(turn) ? 1 : 0;
    }
    return out;
}

void updateAngerToward(const Rules& r, const GameState& s, Empire& e, const Empire& x, const AngerInputs& in, const AiProfile& prof) {
    const AngerTable& t = prof.anger;
    Relation& rel = e.relation(x.id);
    int anger = std::clamp(rel.anger, 0, kMaxAnger);
    auto add = [&](int64_t delta) { anger = static_cast<int>(std::clamp<int64_t>(anger + delta, 0, kMaxAnger)); };
    const bool belowNonAggression = treatyIsHostile(rel.treaty);
    const std::vector<SystemId>& ours = in.territory[e.id.index()];

    // 1. Combat. We are Attacking when we were the "current player" when the
    // battle was fought, wherever it was (confirmed: binary): in a turn-based
    // game the player whose turn it was, in a simultaneous one the highest
    // player number. Only the battles the political window counts (since
    // Empire::politicsMark): turn-based games keep two turns' battles.
    if (belowNonAggression)
        for (size_t i = 0; i < s.combats.size(); ++i) {
            const CombatRecord& rec = s.combats[i];
            if (!in.battles[i] || !involves(rec, e.id) || !involves(rec, x.id)) continue;
            const bool attacking = rec.currentPlayer == e.id;  // a record from an old save names nobody (inferred)
            switch (outcomeFor(rec, e.id)) {
                case Outcome::Won: add(attacking ? t.attackingWon : t.defendingWon); break;
                case Outcome::Lost: add(attacking ? t.attackingLost : t.defendingLost); break;
                case Outcome::Stalemate: add(attacking ? t.attackingStalemate : t.defendingStalemate); break;
            }
        }
    // 2. Stellar manipulation: each report in our own log, counted like the
    // others, that X destroyed a planet or a star or made a nebula or black
    // hole (logged to every empire present when it happened, spec 05 §7.3).
    for (size_t i = 0; i < e.log.size(); ++i) {
        const LogEntry& l = e.log[i];
        if (in.logs[e.id.index()][i] && l.category == LogCategory::Events && movement::stellarReportNames(s, l, x.id))
            add(int64_t{2} * t.defendingLost);
    }
    // 3. Successful operations traced to them: the victim's log names the
    // culprit (intel::namesCulprit); blocked attempts and counter-intelligence never do.
    for (size_t i = 0; i < e.log.size(); ++i)
        if (in.logs[e.id.index()][i] && intel::namesCulprit(s, e.log[i], x.id)) add(t.intelligenceAgainstUs);
    // 4. The earliest message from them that arrived this turn.
    const DiplomaticMessage* first = nullptr;
    for (const DiplomaticMessage& m : s.messages)
        if (m.to == e.id && m.from == x.id && m.delivered && in.countsMessage(m) && (!first || m.id < first->id)) first = &m;
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
    // 8. A promise about them to stop hostile actions: -20, one promise a turn.
    if (rel.promises > 0) {
        add(-20);
        --rel.promises;
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
            if (rec.turn != s.turn || !involves(rec, e.id) || !involves(rec, x)) continue;
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
    // Our own designs that fought this turn get a seen date too (spec 05 §8
    // "Design knowledge", §7.5 "Layer fallback"): the record's pieces (a group
    // that mixes designs lists its first). Only the last 20 turns matter.
    std::vector<SeenDesign>& fought = e.aiMemory.designsFought;
    for (const CombatRecord& rec : s.combats) {
        if (rec.turn != s.turn || !involves(rec, e.id)) continue;
        for (const CombatPiece& piece : rec.pieces) {
            if (piece.owner != e.id || !piece.design.valid() || piece.kind == CombatPiece::Kind::Seeker) continue;
            auto it = std::find_if(fought.begin(), fought.end(), [&](const SeenDesign& d) { return d.design == piece.design; });
            if (it == fought.end()) fought.push_back({piece.design, s.turn});
            else it->turn = std::max(it->turn, s.turn);
        }
    }
    std::erase_if(fought, [&](const SeenDesign& d) { return d.turn + 20 < s.turn; });
    std::sort(fought.begin(), fought.end(), [](const SeenDesign& a, const SeenDesign& b) { return a.design < b.design; });
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

void recordAiDecisions(TurnContext& ctx, EmpireId id) {
    GameState& s = ctx.state;
    if (!id.valid() || id.index() >= s.empires.size()) return;
    Empire& e = s.empire(id);
    if (e.aiDifficulty < 0 && e.kind != PlayerKind::Human) {
        const auto& random = s.options.randomAiPlayers;
        const bool randomPlayer = id.index() < random.size() && random[id.index()] != 0;
        e.aiDifficulty = randomPlayer || s.turn == 0 ? difficultyOf(s, id) : rebelDifficulty(s);
    }
    if (!e.alive) return;
    keepCounters(s, e);
    Rng rng = stepRng(s, id);
    recordDecisions(ctx, id, rng);
}

namespace {

void decideState(const Rules& r, GameState& s, EmpireId id) {
    const Empire& e = s.empire(id);
    const AiProfile& prof = profileFor(r, e);
    const Decision d = decide(s, id, assess(r, s, id, prof), prof);
    Empire& me = s.empire(id);
    me.aiMemory = d.memory;
    if (static_cast<int>(d.next) != me.aiState) {
        me.aiState = static_cast<int>(d.next);
        me.aiTurnsInState = 0;
    } else {
        me.aiTurnsInState = std::min(me.aiTurnsInState + 1, kCounterCap);
    }
}

// Territory: every empire whose Politics minister is on (every computer
// player, and a human who turns that minister on) claims its territory anew
// (spec 06 §7 Q47, confirmed: binary). Computer players also make the systems
// they agreed to leave their systems to avoid.
void claimTerritory(const Rules& r, GameState& s, Empire& e) {
    if (!politicsOn(e)) return;
    e.claimedSystems = computeTerritory(s, e.id);
    if (e.kind == PlayerKind::Human) return;
    e.systemsToAvoid = e.aiMemory.avoid;
    std::sort(e.systemsToAvoid.begin(), e.systemsToAvoid.end());
    // The four AI_Settings movement flags become the empire's own Ship
    // Movement and Ship Orders options each turn (spec 05 §7.5).
    const SettingsTable& set = profileFor(r, e).settings;
    e.avoidTaggedMinefields = set.avoidMinefields;
    e.avoidRestrictedSystems = set.avoidRestrictedSystems;
    e.clearOrdersOnEncounter = set.clearOrdersOnAll     ? EncounterClear::Any
                               : set.clearOrdersOnEnemy ? EncounterClear::Enemy
                                                        : EncounterClear::Never;
}

AngerInputs angerInputs(const Rules& r, const GameState& s, PoliticalWindow window) {
    AngerInputs in;
    in.scores = politicalScores(r, s);
    in.territory.resize(s.empires.size());
    in.mee.resize(s.empires.size());
    for (const Empire& e : s.empires) {
        in.territory[e.id.index()] = computeTerritory(s, e.id);
        in.mee[e.id.index()] = megaEvilEmpire(r, in.scores, s, e.id);
    }
    in.window = std::move(window);
    in.battles = countedItems(s.combats, in, in.window.battles, [](const CombatRecord& c) { return c.turn; });
    for (const Empire& e : s.empires) {
        const size_t i = e.id.index();
        in.logs.push_back(countedItems(e.log, in, i < in.window.logs.size() ? in.window.logs[i] : 0, [](const LogEntry& l) { return l.turn; }));
    }
    return in;
}

PoliticalWindow turnWindow(std::optional<uint32_t> eventsTurn) {
    PoliticalWindow w;
    w.turn = eventsTurn;
    return w;
}

// Anger toward every living empire in contact, when the Politics minister is
// on; anger toward an eliminated empire is 0.
void angerOf(const Rules& r, GameState& s, Empire& e, const AngerInputs& in) {
    if (politicsOn(e)) {
        const AiProfile& prof = profileFor(r, e);
        for (const Empire& x : s.empires) {
            if (x.id == e.id || !x.alive || x.id.index() >= e.relations.size() || !e.relation(x.id).contact) continue;
            updateAngerToward(r, s, e, x, in, prof);
        }
    }
    for (const Empire& x : s.empires)
        if (!x.alive && x.id != e.id && x.id.index() < e.relations.size()) e.relation(x.id).anger = 0;
}

} // namespace

void updateAiStates(TurnContext& ctx) {
    GameState& s = ctx.state;
    for (Empire& e : s.empires)
        if (e.alive) forgetDemands(s, e);
    for (Empire& e : s.empires) claimTerritory(ctx.rules, s, e);
    for (const Empire& e : s.empires)
        if (e.alive) decideState(ctx.rules, s, e.id);
}

void updateAiState(TurnContext& ctx, EmpireId id) {
    GameState& s = ctx.state;
    if (!id.valid() || id.index() >= s.empires.size() || !s.empire(id).alive) return;
    forgetDemands(s, s.empire(id));
    claimTerritory(ctx.rules, s, s.empire(id));
    decideState(ctx.rules, s, id);
}

void politicalStep(TurnContext& ctx) {
    GameState& s = ctx.state;
    const AngerInputs in = angerInputs(ctx.rules, s, turnWindow(s.turn));
    for (Empire& e : s.empires) angerOf(ctx.rules, s, e, in);
}

void politicalStep(TurnContext& ctx, EmpireId id, std::optional<uint32_t> eventsTurn) { politicalStep(ctx, id, turnWindow(eventsTurn)); }

PoliticalWindow simultaneousWindow(const GameState& s, EmpireId e) {
    PoliticalWindow w;
    if (s.turn > 0) w.turn = s.turn - 1;
    w.messagesByDelivery = true;
    w.messagesFrom = s.turn;  // the ministers' date − 1
    if (e.valid() && e.index() < s.empires.size()) w.firstMessage = s.empire(e).politicsMark.nextMessage;
    return w;
}

void recordPoliticalStep(GameState& s, EmpireId e) {
    if (e.valid() && e.index() < s.empires.size()) s.empire(e).politicsMark.nextMessage = s.nextMessageId;
}

void politicalStep(TurnContext& ctx, EmpireId id, const PoliticalWindow& window) {
    GameState& s = ctx.state;
    if (!id.valid() || id.index() >= s.empires.size() || !s.empire(id).alive) return;
    const AngerInputs in = angerInputs(ctx.rules, s, window);
    angerOf(ctx.rules, s, s.empire(id), in);
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
