#include "game/score.hpp"

#include "game/design.hpp"
#include "game/events.hpp"
#include "game/query.hpp"
#include "game/research.hpp"
#include "game/turn.hpp"

#include <algorithm>
#include <format>

namespace opense4::game::score {

namespace {

bool validEmpire(const GameState& s, EmpireId e) { return e.valid() && e.index() < s.empires.size(); }

// Empires that take part in victory (neutral empires do not, inferred).
bool contender(const Empire& e) { return e.alive && e.kind != PlayerKind::Neutral; }

void announce(TurnContext& ctx, const std::string& title, const std::string& text) {
    for (const Empire& e : ctx.state.empires) ctx.log(e.id, LogCategory::Misc, title, text);
}

void declareWinner(TurnContext& ctx, EmpireId winner, std::string_view reason) {
    GameState& s = ctx.state;
    s.gameOver = true;
    s.winner = winner;
    if (validEmpire(s, winner))
        announce(ctx, "Game Over", std::format("The {} has won the game: {}.", effects::empireFullName(s.empire(winner)), reason));
    else
        announce(ctx, "Game Over", std::format("The game has ended without a winner: {}.", reason));
}

// Best score among contenders; ties go to the lower empire id.
EmpireId leader(const std::vector<std::pair<EmpireId, int64_t>>& scores) {
    EmpireId best;
    int64_t bestScore = 0;
    for (const auto& [id, sc] : scores)
        if (!best.valid() || sc > bestScore) {
            best = id;
            bestScore = sc;
        }
    return best;
}

} // namespace

Weights weights(const Rules& r) {
    Weights w;
    w.resources = r.setting("Score Weight Resources", w.resources);
    w.research = r.setting("Score Weight Research", w.research);
    w.intelligence = r.setting("Score Weight Intelligence", w.intelligence);
    w.techLevels = r.setting("Score Weight Tech Levels", w.techLevels);
    w.systems = r.setting("Score Weight Systems", w.systems);
    w.planets = r.setting("Score Weight Planets", w.planets);
    w.population = r.setting("Score Weight Population", w.population);
    w.units = r.setting("Score Weight Units", w.units);
    w.ships = r.setting("Score Weight Ships", w.ships);
    w.bases = r.setting("Score Weight Bases", w.bases);
    return w;
}

int64_t scoreOf(const TurnStats& t, const Weights& w) {
    const int64_t sum = std::max<int64_t>(0, t.production.total()) * w.resources + std::max<int64_t>(0, t.research) * w.research +
                        std::max<int64_t>(0, t.intelligence) * w.intelligence + int64_t{t.techLevels} * w.techLevels +
                        int64_t{t.systems} * w.systems + int64_t{t.planets} * w.planets + t.population * w.population +
                        int64_t{t.units} * w.units + int64_t{t.ships} * w.ships + int64_t{t.bases} * w.bases;
    return sum / 1000;
}

TurnStats currentStats(const Rules& r, const GameState& s, EmpireId e) {
    TurnStats t;
    t.turn = s.turn;
    if (!validEmpire(s, e)) return t;
    const Empire& emp = s.empire(e);
    const EconomyReport& eco = emp.economy;
    t.production = eco.colonies + eco.trade + eco.tariffsIn + eco.remoteMining + eco.otherIncome;
    t.research = eco.research;
    t.intelligence = eco.intelligence;
    t.techLevels = research::totalLevels(emp);
    std::vector<SystemId> systems;
    for (const auto& c : s.colonies) {
        if (!c || c->owner != e) continue;
        ++t.planets;
        t.population += c->totalPopulation();
        systems.push_back(s.galaxy.object(c->planet).system);
    }
    std::sort(systems.begin(), systems.end());
    t.systems = static_cast<int>(std::unique(systems.begin(), systems.end()) - systems.begin());
    for (const Vehicle& v : s.vehicles) {
        if (v.owner != e) continue;
        const ruleset::VehicleType type = vehicleType(r, s, v);
        if (type == ruleset::VehicleType::Base) ++t.bases;
        else if (type == ruleset::VehicleType::Ship && v.status != VehicleStatus::Mothballed) ++t.ships;
    }
    t.units = unitCount(r, s, e);
    t.score = scoreOf(t, weights(r));
    return t;
}

int64_t empireScore(const Rules& r, const GameState& s, EmpireId e) { return currentStats(r, s, e).score; }

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
    if (viewer == other || s.options.showAllScores) return true;
    return allied(s, viewer, other);
}

bool defeated(const Rules& r, const GameState& s, EmpireId e) {
    for (const auto& c : s.colonies)
        if (c && c->owner == e && c->totalPopulation() > 0) return false;
    for (const Vehicle& v : s.vehicles)
        if (v.owner == e && v.count > 0 && !isUnitType(vehicleType(r, s, v))) return false;
    return true;
}

void endOfTurn(TurnContext& ctx) {
    const Rules& r = ctx.rules;
    GameState& s = ctx.state;

    // ---- Statistics and history.
    for (Empire& e : s.empires)
        if (e.alive) e.history.push_back(currentStats(r, s, e.id));

    // ---- Eliminations: no populated colony and no ship or base left
    // (spec 02 §2, inferred). Leftovers (empty colonies, units) go with it.
    for (Empire& e : s.empires) {
        if (!e.alive || !defeated(r, s, e.id)) continue;
        e.alive = false;
        e.research.clear();
        e.intel.clear();
        for (auto& c : s.colonies)
            if (c && c->owner == e.id) c.reset();
        for (Vehicle& v : s.vehicles)
            if (v.owner == e.id) v.count = 0;
        announce(ctx, "Empire Destroyed", std::format("The {} has been eliminated.", effects::empireFullName(e)));
    }
    s.removeDeadVehicles();

    // ---- Peace: consecutive turns without a war between living empires.
    bool war = false;
    for (const Empire& a : s.empires)
        for (const Empire& b : s.empires)
            if (a.id < b.id && a.alive && b.alive && a.relation(b.id).treaty == Treaty::War) war = true;
    s.peacefulTurns = war ? 0 : s.peacefulTurns + 1;

    if (s.gameOver) return;

    // ---- Victory. This turn is the (turn + 1)-th one played.
    std::vector<std::pair<EmpireId, int64_t>> scores;
    for (const Empire& e : s.empires)
        if (contender(e)) scores.emplace_back(e.id, empireScore(r, s, e.id));
    size_t everyone = 0;
    for (const Empire& e : s.empires)
        if (e.kind != PlayerKind::Neutral) ++everyone;

    // Last empire standing ends any game that started with several.
    if (everyone >= 2 && scores.size() <= 1) {
        declareWinner(ctx, scores.empty() ? EmpireId{} : scores.front().first,
                      scores.empty() ? "no empire survived" : "it is the last empire standing");
        return;
    }
    const VictoryConditions& v = s.options.victory;
    const int64_t played = int64_t{s.turn} + 1;
    if (v.delay && played < int64_t{v.delayYears} * 10) return;
    if (scores.empty()) return;

    if (v.score) {
        std::vector<std::pair<EmpireId, int64_t>> reached;
        for (const auto& row : scores)
            if (row.second >= v.scoreValue) reached.push_back(row);
        if (!reached.empty()) {
            declareWinner(ctx, leader(reached), std::format("its score reached {}", v.scoreValue));
            return;
        }
    }
    if (v.percentOfSecond && scores.size() >= 2) {
        std::vector<std::pair<EmpireId, int64_t>> sorted = scores;
        std::stable_sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
        const int64_t pct = std::max<int64_t>(100, v.percentOfSecondValue);
        if (sorted[0].second > 0 && sorted[0].second * 100 >= pct * sorted[1].second) {
            declareWinner(ctx, sorted[0].first, std::format("its score is {}% of the runner-up's", pct));
            return;
        }
    }
    if (v.techPercent) {
        for (const auto& row : scores)
            if (research::techPercent(r, s, s.empire(row.first)) >= v.techPercentValue) {
                declareWinner(ctx, row.first, std::format("it has discovered {}% of all technology", v.techPercentValue));
                return;
            }
    }
    if (v.peace && s.peacefulTurns >= static_cast<uint32_t>(std::max(0, v.peaceYears)) * 10) {
        // Who wins a peace victory is open (spec 05 §11): the best score (inferred).
        declareWinner(ctx, leader(scores), std::format("the galaxy has been at peace for {} years", v.peaceYears));
        return;
    }
    if (v.years && played >= int64_t{v.yearsValue} * 10) {
        declareWinner(ctx, leader(scores), std::format("it had the best score after {} years", v.yearsValue));
        return;
    }
}

} // namespace opense4::game::score
