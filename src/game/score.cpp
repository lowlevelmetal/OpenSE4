#include "game/score.hpp"

#include "game/design.hpp"
#include "game/diplomacy.hpp"
#include "game/events.hpp"
#include "game/query.hpp"
#include "game/research.hpp"
#include "game/turn.hpp"
#include "game/xmath.hpp"

#include <algorithm>
#include <format>

namespace opense4::game::score {

namespace {

bool validEmpire(const GameState& s, EmpireId e) { return e.valid() && e.index() < s.empires.size(); }

bool shipOrBase(ruleset::VehicleType t) { return t == ruleset::VehicleType::Ship || t == ruleset::VehicleType::Base; }

// Two empires at Non-Aggression or better (having no contact is worse).
bool atPeace(const GameState& s, EmpireId a, EmpireId b) {
    const Relation& rel = s.empire(a).relation(b);
    return rel.contact && rel.treaty >= Treaty::NonAggression;
}

} // namespace

int64_t scoreOf(const ScoreParts& p) {
    return kTonnageWeight * p.tonnage + p.production + kTechLevelWeight * p.techLevels + (p.everything ? kEverythingBonus : 0);
}

ScoreParts scoreParts(const Rules& r, const GameState& s, EmpireId e) {
    ScoreParts p;
    if (!validEmpire(s, e)) return p;
    for (const Vehicle& v : s.vehicles)
        if (v.owner == e && v.count > 0 && v.status != VehicleStatus::Mothballed && shipOrBase(vehicleType(r, s, v)))
            p.tonnage += r.hull(s.design(v.design).hull).tonnage;
    const diplomacy::Generated g = diplomacy::generated(r, s, e);
    p.production = g.resources.total() + g.research + g.intelligence;
    const Empire& emp = s.empire(e);
    p.techLevels = research::totalLevels(r, emp);
    p.everything = research::researchedEverything(r, s, emp);
    return p;
}

TurnStats currentStats(const Rules& r, const GameState& s, EmpireId e) {
    TurnStats t;
    t.turn = s.turn;
    if (!validEmpire(s, e)) return t;
    const Empire& emp = s.empire(e);
    const diplomacy::Generated g = diplomacy::generated(r, s, e);
    t.production = g.resources;
    t.research = g.research;
    t.intelligence = g.intelligence;
    t.techLevels = research::totalLevels(r, emp);
    std::vector<SystemId> systems;
    for (const auto& c : s.colonies) {
        if (!c || c->owner != e) continue;
        ++t.planets;
        t.population += c->totalPopulation();
        systems.push_back(s.galaxy.object(c->planet).system);
    }
    std::sort(systems.begin(), systems.end());
    t.systems = static_cast<int>(std::unique(systems.begin(), systems.end()) - systems.begin());
    // Ships and bases, mothballed ones excluded (spec 05 §5).
    for (const Vehicle& v : s.vehicles) {
        if (v.owner != e || v.count <= 0 || v.status == VehicleStatus::Mothballed) continue;
        const ruleset::VehicleType type = vehicleType(r, s, v);
        if (type == ruleset::VehicleType::Base) ++t.bases;
        else if (type == ruleset::VehicleType::Ship) ++t.ships;
    }
    t.units = unitCount(r, s, e);
    t.score = empireScore(r, s, e);
    return t;
}

int64_t empireScore(const Rules& r, const GameState& s, EmpireId e) { return scoreOf(scoreParts(r, s, e)); }

std::vector<EmpireId> ranking(const Rules& r, const GameState& s) {
    std::vector<std::pair<EmpireId, int64_t>> rows;
    for (const Empire& e : s.empires)
        if (e.alive) rows.emplace_back(e.id, empireScore(r, s, e.id));
    std::stable_sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    std::vector<EmpireId> out;
    for (const auto& row : rows) out.push_back(row.first);
    return out;
}

bool scoreVisible(const GameState& s, EmpireId viewer, EmpireId other) {
    if (!validEmpire(s, other)) return false;
    if (viewer == other) return true;
    if (!s.empire(other).alive) return false;
    if (s.gameOver) return true;
    switch (s.options.scoreDisplay) {
        case 0: return false;
        case 1: return validEmpire(s, viewer) && atPeace(s, viewer, other);
        default: return true;
    }
}

bool defeated(const Rules& r, const GameState& s, EmpireId e) {
    for (const auto& c : s.colonies)
        if (c && c->owner == e && c->totalPopulation() > 0) return false;
    for (const Vehicle& v : s.vehicles)
        if (v.owner == e && v.count > 0 && !isUnitType(vehicleType(r, s, v))) return false;
    return true;
}

void checkDestruction(TurnContext& ctx, EmpireId id, bool lastTurnPlayed) {
    const Rules& r = ctx.rules;
    GameState& s = ctx.state;
    if (!validEmpire(s, id) || !s.empire(id).alive || !defeated(r, s, id)) return;
    // A human plays one last turn first (spec 06 §7 Q83).
    if (s.empire(id).kind == PlayerKind::Human && !lastTurnPlayed) return;
    Empire& e = s.empire(id);
    e.alive = false;
    e.research.clear();
    e.intel.clear();
    e.researchPool = e.intelPool = 0;
    // Its remaining objects (empty colonies, units) go.
    for (auto& c : s.colonies)
        if (c && c->owner == id) c.reset();
    for (Vehicle& v : s.vehicles)
        if (v.owner == id) v.count = 0;
    s.removeDeadVehicles();
    const std::string text = destroyedText(s, id);
    for (const Empire& x : s.empires)
        if (x.id == id || (x.alive && x.relation(id).contact)) {
            logGoto(ctx.log(x.id, LogCategory::Politics, "Empire Destroyed", text), LogGoto::None);
            addHistory(s, x.id, id, std::format("The {} was destroyed", effects::empireFullName(s.empire(id))));
        }
    // Intelligence projects aimed at it go; every treaty with it returns to "no contact".
    for (Empire& x : s.empires) std::erase_if(x.intel, [&](const IntelProjectOrder& o) { return o.target == id; });
    diplomacy::forgetEmpire(s, id);

    // No victory for the last empire standing; it is told and plays on (spec 05
    // §6). Neutral empires do not count (OpenSE4 choice, inferred).
    std::vector<EmpireId> left;
    for (const Empire& x : s.empires)
        if (x.alive && !isNeutral(x)) left.push_back(x.id);
    if (left.size() == 1) {
        logGoto(ctx.log(left.front(), LogCategory::Politics, "Last Empire Standing", "Every other empire has been destroyed. The game goes on."),
                LogGoto::None);
        addHistory(s, left.front(), {}, "Every other empire has been destroyed");
    }
}

void recordStatistics(TurnContext& ctx, EmpireId e) {
    GameState& s = ctx.state;
    if (!validEmpire(s, e) || !s.empire(e).alive) return;
    TurnStats t = currentStats(ctx.rules, s, e);
    s.empire(e).history.push_back(std::move(t));
    if (s.empire(e).kind == PlayerKind::Human) ctx.records.push_back(playerRecords(ctx.rules, s, e));
}

std::string destroyedText(const GameState& s, EmpireId gone) {
    return std::format("The {} has been destroyed.", effects::empireFullName(s.empire(gone)));
}

std::string dateText(uint32_t turn) { return std::format("{}.{}", 2400 + turn / 10, turn % 10); }

uint32_t fileDate(const GameState& s) { return s.options.simultaneous ? s.turn + 1 : s.turn; }

namespace {

// A date as a whole number of tenths of a year: 24001 for 2400.1.
uint64_t tenths(uint32_t date) { return uint64_t{24000} + date; }

// The date of a log entry as the original dates it: an entry of a
// simultaneous turn processing is made after the date has advanced (the
// engine dates it with the unadvanced turn number); in a turn-based game the
// date never advances during the game turn.
uint32_t entryDate(const GameState& s, const LogEntry& l) { return s.options.simultaneous ? l.turn + 1 : l.turn; }

} // namespace

std::string statisticsLine(EmpireId e, uint32_t date, const TurnStats& t) {
    return std::format("{:>5}{:>8}{:>12}{:>12}{:>12}{:>12}{:>12}{:>12}{:>12}{:>12}{:>12}{:>12}{:>12}", e.value + 1, tenths(date), t.score,
                       t.production.total(), t.research, t.intelligence, t.techLevels, t.systems, t.planets, t.population, t.units, t.ships,
                       t.bases);
}

std::string historyLine(uint32_t date, EmpireId other, std::string_view text) {
    return std::format("{:>8}{:>5}{:>5}{:>5} {}", tenths(date), other.valid() ? other.value + 1 : 0u, 0, 0, text);
}

std::string logLine(uint32_t date, std::string_view title, std::string_view text) {
    // Each line break becomes one space: a CR LF pair, or the single LF our
    // engine's texts break lines with (a lone CR stays as it is).
    std::string flat;
    flat.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\r' && i + 1 < text.size() && text[i + 1] == '\n') {
            flat += ' ';
            ++i;
        } else {
            flat += text[i] == '\n' ? ' ' : text[i];
        }
    }
    // A title longer than 40 is followed by the one space only.
    return std::format("{:<9}{:<40} {}", dateText(date), title, flat);
}

std::string logCopyHeader() { return std::format("{:<9}{:<41}{}", "Date", "Header", "Text"); }

PlayerRecords playerRecords(const Rules& r, const GameState& s, EmpireId e) {
    PlayerRecords out;
    out.empire = e;
    out.turn = s.turn;
    if (!validEmpire(s, e)) return out;
    const uint32_t date = fileDate(s);
    // Statistics: every empire whose score the player may see (spec 05 §5).
    for (const Empire& x : s.empires)
        if (x.alive && scoreVisible(s, e, x.id)) out.statistics.push_back(statisticsLine(x.id, date, currentStats(r, s, x.id)));
    const Empire& me = s.empire(e);
    // The text copy of the whole log, rewritten (spec 05 §3.4).
    if (r.settingFlag("Create Log Text Files for Players", false) && !me.log.empty()) {
        out.log.push_back(logCopyHeader());
        out.log.push_back(std::string(78, '-'));  // a rule of 78 dashes (docs/spec/06 §6.1)
        for (const LogEntry& l : me.log) out.log.push_back(logLine(entryDate(s, l), l.title, l.text));
    }
    if (date == 0) return out;
    const uint32_t before = date - 1;
    // History: the political messages dated the turn before (spec 05 §3.4),
    // by the date of their political entry (DiplomaticMessage::dated).
    auto findMessage = [&](MessageId id) -> const DiplomaticMessage* {
        for (const DiplomaticMessage& m : s.messages)
            if (m.id == id) return &m;
        return nullptr;
    };
    for (const DiplomaticMessage& m : s.messages) {
        if (!m.delivered || m.dated != before || (m.from != e && m.to != e)) continue;
        const EmpireId other = m.from == e ? m.to : m.from;
        const DiplomaticMessage* answered = findMessage(m.inReplyTo);
        const bool acceptsTreaty = m.type == MessageType::AcceptTreaty ||
                                   (m.type == MessageType::AcceptDemand && answered && answered->type == MessageType::CounterTreaty);
        if (acceptsTreaty && answered) out.history.push_back(historyLine(before, other, std::format("{} established", displayName(answered->treaty))));
        else if (m.type == MessageType::BreakTreaty) out.history.push_back(historyLine(before, other, "Treaty broken"));
        else if (m.type == MessageType::DeclareWar) out.history.push_back(historyLine(before, other, "War declared"));
    }
    // ... and the player's own log: empires destroyed, first contacts and
    // contacts lost (the contact check's lines, spec 05 §3.1; a destruction
    // writes only its own line).
    for (const LogEntry& l : me.log) {
        if (entryDate(s, l) != before) continue;
        for (const Empire& x : s.empires) {
            if (x.id == e) continue;
            if (l.title == "Empire Destroyed" && l.text == destroyedText(s, x.id))
                out.history.push_back(historyLine(before, x.id, std::format("The {} was destroyed", effects::empireFullName(x))));
            else if ((l.title == "First Contact" && l.text == diplomacy::firstContactText(s, x.id)) ||
                     diplomacy::treatyEnactedWith(s, l, x.id))
                // A package's "Treaty Enacted" entry is of the first-contact
                // kind, so it writes the contact line too (spec 05 §3.4, spec
                // 06 §7 Q70, confirmed: binary).
                out.history.push_back(historyLine(before, x.id, std::format("First contact with the {}", effects::empireFullName(x))));
            else if (l.title == "Contact Lost" && l.text == diplomacy::contactLostText(s, x.id))
                out.history.push_back(historyLine(before, x.id, std::format("Lost contact with the {}", effects::empireFullName(x))));
        }
    }
    return out;
}

bool leadsBy(int64_t score, int64_t other, int percent) { return xmath::Ext(score) >= xmath::percent(percent) * xmath::Ext(other); }

bool techShareMet(int64_t levels, int64_t maxLevels, int percent) {
    return xmath::Ext(levels) >= xmath::Ext(maxLevels * percent) / xmath::Ext(100);
}

bool galaxyAtPeace(const GameState& s) {
    for (const Empire& a : s.empires)
        for (const Empire& b : s.empires)
            if (a.id < b.id && a.alive && b.alive && !atPeace(s, a.id, b.id)) return false;
    return true;
}

void checkVictory(TurnContext& ctx, uint32_t date) {
    const Rules& r = ctx.rules;
    GameState& s = ctx.state;
    if (s.gameOver) return;
    const VictoryConditions& v = s.options.victory;
    const int64_t now = date;
    // "After X years": nothing is checked before then, and the peace counter stands still.
    if (v.delay && now < int64_t{v.delayYears} * 10) return;

    // The peace counter rises each turn and restarts whenever two living
    // empires hold a treaty worse than Non-Aggression (no contact included).
    s.peacefulTurns = galaxyAtPeace(s) ? s.peacefulTurns + 1 : 0;

    std::vector<std::pair<EmpireId, int64_t>> scores;
    for (const Empire& e : s.empires)
        if (e.alive) scores.emplace_back(e.id, empireScore(r, s, e.id));
    if (scores.empty()) return;

    std::string reason;
    bool scoreMet = false, yearsMet = false, secondMet = false, techMet = false, peaceMet = false;
    if (v.score)
        scoreMet = std::any_of(scores.begin(), scores.end(), [&](const auto& row) { return row.second >= v.scoreValue; });
    if (v.years) yearsMet = now >= int64_t{v.yearsValue} * 10;
    if (v.percentOfSecond) {
        // Some living empire has score >= (X / 100) × the score of every other
        // living empire, compared in floating point; with one living empire
        // the test passes (confirmed: binary).
        for (const auto& [id, sc] : scores) {
            const bool leads = std::all_of(scores.begin(), scores.end(), [&](const auto& other) {
                return other.first == id || leadsBy(sc, other.second, v.percentOfSecondValue);
            });
            if (leads) secondMet = true;
        }
    }
    if (v.techPercent)
        // The capped level sum >= the sum of the maximum levels × X / 100, in
        // floating point (confirmed: binary).
        for (const auto& [id, sc] : scores) {
            const Empire& e = s.empire(id);
            if (techShareMet(research::totalLevels(r, e), research::maxLevels(r, s, e), v.techPercentValue)) techMet = true;
        }
    if (v.peace) peaceMet = s.peacefulTurns >= static_cast<uint32_t>(std::max(0, v.peaceYears)) * 10;

    // Quirk (confirmed: binary): with "% of second place" on, its result
    // replaces those of the Score and Years tests.
    bool over = v.percentOfSecond ? secondMet : (scoreMet || yearsMet);
    over = over || techMet || peaceMet;
    if (!over) return;
    if (v.percentOfSecond && secondMet) reason = std::format("an empire has {}% of every other empire's score", v.percentOfSecondValue);
    else if (scoreMet) reason = std::format("an empire's score has reached {}", v.scoreValue);
    else if (yearsMet) reason = std::format("{} years have passed", v.yearsValue);
    else if (techMet) reason = std::format("an empire has researched {}% of its technology", v.techPercentValue);
    else reason = std::format("the quadrant has been at peace for {} years", v.peaceYears);

    // The game ends; the original names no winner. The best score (neutral
    // empires aside, ties to the lower number) is kept for the game-over
    // screen (OpenSE4 choice, inferred).
    s.gameOver = true;
    s.winner = {};
    int64_t best = 0;
    for (const auto& [id, sc] : scores)
        if (!isNeutral(s.empire(id)) && (!s.winner.valid() || sc > best)) {
            s.winner = id;
            best = sc;
        }
    for (const Empire& e : s.empires) {
        logGoto(ctx.log(e.id, LogCategory::Misc, "Game Over",
                        std::format("This is the last turn: {}. The Scores window shows the final ranking.", reason)),
                LogGoto::None);
        addHistory(s, e.id, {}, std::format("The game ended: {}", reason));
    }
}

} // namespace opense4::game::score
