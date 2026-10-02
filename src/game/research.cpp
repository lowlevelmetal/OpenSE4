#include "game/research.hpp"

#include "game/economy.hpp"
#include "game/turn.hpp"
#include "game/xmath.hpp"

#include <algorithm>
#include <array>
#include <format>

namespace opense4::game::research {

namespace {

using ruleset::TechAreaId;

// Availability of every item that carries a tech requirement block, in a
// fixed order: components, facilities, hulls, intel projects.
std::vector<uint8_t> availability(const Rules& r, const Empire& e) {
    const auto& d = r.data();
    std::vector<uint8_t> out;
    out.reserve(d.components.size() + d.facilities.size() + d.vehicleSizes.size() + d.intelProjects.size());
    for (const auto& c : d.components) out.push_back(r.meets(e, c.requirements));
    for (const auto& f : d.facilities) out.push_back(r.meets(e, f.requirements));
    for (const auto& h : d.vehicleSizes) out.push_back(r.meets(e, h.requirements));
    for (const auto& p : d.intelProjects) out.push_back(r.meets(e, p.requirements));
    return out;
}

// Name of item `i` in availability() order.
std::string itemName(const Rules& r, size_t i) {
    const auto& d = r.data();
    if (i < d.components.size()) return d.components[i].name;
    i -= d.components.size();
    if (i < d.facilities.size()) return d.facilities[i].name;
    i -= d.facilities.size();
    if (i < d.vehicleSizes.size()) return d.vehicleSizes[i].name;
    i -= d.vehicleSizes.size();
    return d.intelProjects[i].name;
}

bool needsLevel(std::span<const ruleset::TechRequirement> reqs, TechAreaId area, int level) {
    return std::any_of(reqs.begin(), reqs.end(), [&](const ruleset::TechRequirement& q) { return q.area == area && q.level == level; });
}

bool validArea(const Rules& r, TechAreaId a) { return a.valid() && a.index() < r.data().techAreas.size(); }

// Step 1 of the research step: entries of unknown areas and of areas at
// their maximum leave the queue (confirmed: binary). An area can be queued
// only once (confirmed: binary); the queue command still accepts repeats, so
// later entries of an area are dropped here (inferred), and the queue is cut
// to its 12 slots.
void pruneQueue(const Rules& r, Empire& e) {
    std::vector<uint8_t> seen(r.data().techAreas.size(), 0);
    std::erase_if(e.research, [&](const ResearchProject& p) {
        if (!validArea(r, p.area) || e.techLevel(p.area) >= r.tech(p.area).maxLevel || seen[p.area.index()]) return true;
        seen[p.area.index()] = 1;
        return false;
    });
    if (e.research.size() > kMaxProjects) e.research.resize(kMaxProjects);
}

// What each entry still needs to finish the area's next level (at least 0).
std::vector<int64_t> remaining(const Rules& r, const GameState& s, const Empire& e, std::span<const ResearchProject> queue) {
    std::vector<int64_t> need;
    need.reserve(queue.size());
    for (const ResearchProject& p : queue)
        need.push_back(std::max<int64_t>(0, levelCost(r, s, p.area, e.techLevel(p.area) + 1) - p.progress));
    return need;
}

int64_t addCapped(int64_t pool, int64_t amount) {
    const int64_t sum = std::max<int64_t>(0, pool) + std::max<int64_t>(0, amount);
    return std::min(sum, kPoolCap);
}

} // namespace

int64_t levelCost(const Rules& r, const GameState& s, ruleset::TechAreaId area, int level) {
    return r.techLevelCost(area, level, s.options.techCost);
}

bool canGainLevel(const Rules& r, const GameState& s, const Empire& e, ruleset::TechAreaId area) {
    return validArea(r, area) && r.techAreaOpen(s, e, area);
}

bool isResearchable(const Rules& r, const GameState& s, const Empire& e, ruleset::TechAreaId area) {
    return validArea(r, area) && e.techLevel(area) < r.tech(area).maxLevel && r.techVisible(s, e, area);
}

std::vector<ruleset::TechAreaId> researchable(const Rules& r, const GameState& s, const Empire& e) {
    std::vector<TechAreaId> out;
    for (uint32_t i = 0; i < r.data().techAreas.size(); ++i)
        if (isResearchable(r, s, e, TechAreaId{i})) out.push_back(TechAreaId{i});
    return out;
}

std::vector<int64_t> allocate(int64_t pool, std::span<const int64_t> need, bool evenly) {
    std::vector<int64_t> give(need.size(), 0);
    if (pool <= 0 || need.empty()) return give;
    if (evenly) {
        // round(pool / N), ties to even, uncapped (confirmed: binary).
        const int64_t share = xmath::divRoundHalfEven(pool, static_cast<int64_t>(need.size()));
        std::fill(give.begin(), give.end(), share);
    } else {
        int64_t left = pool;
        for (size_t i = 0; i < need.size() && left > 0; ++i) {
            give[i] = std::clamp<int64_t>(need[i], 0, left);
            left -= give[i];
        }
    }
    return give;
}

int etaTurns(const Rules& r, const GameState& s, const Empire& e, size_t queueIndex) {
    if (queueIndex >= e.research.size()) return -1;
    const int64_t first = availablePoints(s, e);
    const int64_t later = e.economy.research;
    if (first <= 0 && later <= 0) return -1;
    // Simulate on a copy: shares, completion, removal (no repeats).
    Empire sim;
    sim.techLevels = e.techLevels;
    std::vector<ResearchProject> queue = e.research;
    std::vector<uint8_t> tracked(queue.size(), 0);
    tracked[queueIndex] = 1;
    constexpr int kHorizon = 1000;
    for (int turn = 1; turn <= kHorizon; ++turn) {
        const std::vector<int64_t> need = remaining(r, s, sim, queue);
        const std::vector<int64_t> give = allocate(turn == 1 ? first : later, need, e.researchEvenly);
        std::vector<ResearchProject> next;
        std::vector<uint8_t> nextTracked;
        for (size_t i = 0; i < queue.size(); ++i) {
            if (give[i] >= need[i]) {
                if (tracked[i]) return turn;
                const size_t a = queue[i].area.index();
                if (sim.techLevels.size() <= a) sim.techLevels.resize(a + 1, 0);
                ++sim.techLevels[a];
                continue;
            }
            ResearchProject p = queue[i];
            p.progress += give[i];
            next.push_back(p);
            nextTracked.push_back(tracked[i]);
        }
        queue = std::move(next);
        tracked = std::move(nextTracked);
        if (std::none_of(tracked.begin(), tracked.end(), [](uint8_t t) { return t != 0; })) return -1;
        if (later <= 0) return -1;
    }
    return -1;
}

// ---- Pools ----------------------------------------------------------------------------------------

int64_t availablePoints(const GameState&, const Empire& e) { return std::max<int64_t>(0, e.researchPool); }

void addToPools(Empire& e, int64_t research, int64_t intelligence) {
    e.researchPool = addCapped(e.researchPool, research);
    e.intelPool = addCapped(e.intelPool, intelligence);
}

void openingPools(const Rules& r, GameState& s) {
    // Starting Resources plus one turn of the empire's own production of each
    // kind: its colonies' output as delivered, with the `Minimum Empire X
    // Generation` rule for a resource delivered at exactly 0 (spec 02 §5.6,
    // §9). No remote mining, `Generate Points`, trade, tariffs or computer
    // bonus, and nothing is drawn from finite stocks (empireProduction only
    // reads them). Intelligence starts at 0 (spec 05 §1.1, spec 02 §13 Q37,
    // confirmed: binary). Starting Resources is one amount for every kind.
    static constexpr std::array<std::string_view, 3> kMinimum{"Minimum Empire Minerals Generation", "Minimum Empire Organics Generation",
                                                              "Minimum Empire Radioactives Generation"};
    const int64_t starting = std::max<int64_t>(0, s.options.startingResources[Resource::Minerals]);
    for (Empire& e : s.empires) {
        const economy::Production made = economy::empireProduction(r, s, e.id);
        Resources opening = made.resources;
        for (size_t k = 0; k < 3; ++k)
            if (opening.v[k] == 0) opening.v[k] = r.setting(kMinimum[k], 200);
        for (size_t k = 0; k < 3; ++k)
            e.stockpile.v[k] = std::clamp<int64_t>(std::max<int64_t>(0, s.options.startingResources.v[k]) + opening.v[k], 0, kPoolCap);
        e.researchPool = std::min(starting + std::max<int64_t>(0, made.research), kPoolCap);
        e.intelPool = 0;
    }
}

// ---- Levels ------------------------------------------------------------------------------------------

std::vector<std::string> unlockedBy(const Rules& r, ruleset::TechAreaId area, int level) {
    std::vector<std::string> out;
    const auto& d = r.data();
    for (const auto& c : d.components)
        if (needsLevel(c.requirements, area, level)) out.push_back(c.name);
    for (const auto& f : d.facilities)
        if (needsLevel(f.requirements, area, level)) out.push_back(f.name);
    for (const auto& h : d.vehicleSizes)
        if (needsLevel(h.requirements, area, level)) out.push_back(h.name);
    for (const auto& p : d.intelProjects)
        if (needsLevel(p.requirements, area, level)) out.push_back(p.name);
    return out;
}

std::vector<std::string> availableItems(const Rules& r, const Empire& e) {
    std::vector<std::string> out;
    const std::vector<uint8_t> avail = availability(r, e);
    for (size_t i = 0; i < avail.size(); ++i)
        if (avail[i]) out.push_back(itemName(r, i));
    return out;
}

namespace {

// The areas whose own requirements the empire meets (allowed in this game,
// racial and unique checks included).
std::vector<uint8_t> openAreas(const Rules& r, const GameState& s, const Empire& e) {
    std::vector<uint8_t> out;
    out.reserve(r.data().techAreas.size());
    for (uint32_t i = 0; i < r.data().techAreas.size(); ++i) out.push_back(r.techVisible(s, e, TechAreaId{i}));
    return out;
}

// Sets the level and writes the entries of a level gained (grantLevel).
void setLevel(TurnContext& ctx, EmpireId e, TechAreaId area, int newLevel, std::string_view source) {
    const Rules& r = ctx.rules;
    GameState& s = ctx.state;
    Empire& emp = s.empire(e);
    if (emp.techLevels.size() < r.data().techAreas.size()) emp.techLevels.resize(r.data().techAreas.size(), 0);
    const std::vector<uint8_t> before = availability(r, emp);
    const std::vector<uint8_t> areasBefore = openAreas(r, s, emp);
    emp.techLevels[area.index()] = newLevel;
    const std::vector<uint8_t> after = availability(r, emp);
    const std::vector<uint8_t> areasAfter = openAreas(r, s, emp);

    const std::string& name = r.tech(area).name;
    ctx.log(e, LogCategory::Research, "New Tech Level", std::format("{} is now at level {} ({}).", name, newLevel, source));
    const size_t projects = after.size() - r.data().intelProjects.size();
    for (size_t i = 0; i < after.size(); ++i)
        if (after[i] && !before[i]) {
            const std::string item = itemName(r, i);
            if (i < projects)
                ctx.log(e, LogCategory::Research, std::format("{} Discovered", item),
                        std::format("{} is now available thanks to {} level {}.", item, name, newLevel));
            else
                ctx.log(e, LogCategory::Research, std::format("{} Developed", item),
                        std::format("Our agents can now carry out {}, thanks to {} level {}.", item, name, newLevel));
        }
    for (size_t i = 0; i < areasAfter.size(); ++i)
        if (areasAfter[i] && !areasBefore[i])
            ctx.log(e, LogCategory::Research, "New Tech Area Discovered",
                    std::format("{} can now be researched, thanks to {} level {}.", r.data().techAreas[i].name, name, newLevel));
    // Subjugation passes no technology to the master (spec 05 §1.5, confirmed: binary).
}

} // namespace

void grantLevel(TurnContext& ctx, EmpireId e, ruleset::TechAreaId area, int newLevel, std::string_view source) {
    const Rules& r = ctx.rules;
    GameState& s = ctx.state;
    if (!e.valid() || e.index() >= s.empires.size() || !validArea(r, area)) return;
    const Empire& emp = s.empire(e);
    newLevel = std::min(newLevel, r.tech(area).maxLevel);
    if (newLevel <= emp.techLevel(area) || !canGainLevel(r, s, emp, area)) return;
    setLevel(ctx, e, area, newLevel, source);
}

bool analyzeLevel(TurnContext& ctx, EmpireId e, ruleset::TechAreaId area) {
    const Rules& r = ctx.rules;
    GameState& s = ctx.state;
    if (!e.valid() || e.index() >= s.empires.size() || !validArea(r, area)) return false;
    if (!canGainLevel(r, s, s.empire(e), area)) return false;
    setLevel(ctx, e, area, s.empire(e).techLevel(area) + 1, "analysis");
    return true;
}

void grantRandomAdvances(TurnContext& ctx, EmpireId e, int count, Rng& rng, std::string_view source) {
    const Rules& r = ctx.rules;
    const size_t areas = r.data().techAreas.size();
    if (areas == 0 || !e.valid() || e.index() >= ctx.state.empires.size()) return;
    for (int n = 0; n < count; ++n) {
        // Up to 1,000 uniform draws among all areas until one is researchable
        // (spec 05 §1.2, confirmed: binary).
        for (int draw = 0; draw < kRuinsDraws; ++draw) {
            const TechAreaId a{static_cast<uint32_t>(rng.below(areas))};
            const Empire& emp = ctx.state.empire(e);
            if (!isResearchable(r, ctx.state, emp, a)) continue;
            grantLevel(ctx, e, a, emp.techLevel(a) + 1, source);
            break;
        }
    }
}

// ---- Totals ------------------------------------------------------------------------------------------

int totalLevels(const Rules& r, const Empire& e) {
    int n = 0;
    for (uint32_t i = 0; i < r.data().techAreas.size(); ++i) n += std::clamp(e.techLevel(TechAreaId{i}), 0, r.data().techAreas[i].maxLevel);
    return n;
}

int maxLevels(const Rules& r, const GameState& s, const Empire& e) {
    int n = 0;
    for (uint32_t i = 0; i < r.data().techAreas.size(); ++i)
        if (canGainLevel(r, s, e, TechAreaId{i})) n += std::max(0, r.data().techAreas[i].maxLevel);
    return n;
}

int techPercent(const Rules& r, const GameState& s, const Empire& e) {
    const int64_t total = maxLevels(r, s, e);
    return total > 0 ? static_cast<int>(int64_t{totalLevels(r, e)} * 100 / total) : 0;
}

bool researchedEverything(const Rules& r, const GameState& s, const Empire& e) { return totalLevels(r, e) >= maxLevels(r, s, e); }

// ---- Turn steps ----------------------------------------------------------------------------------------

void researchStep(TurnContext& ctx, EmpireId id) {
    const Rules& r = ctx.rules;
    GameState& s = ctx.state;
    if (!id.valid() || id.index() >= s.empires.size() || !s.empire(id).alive) return;
    Empire& e = s.empire(id);
    const int64_t pool = std::max<int64_t>(0, e.researchPool);
    e.researchPool = 0;

    // 1. Areas at their maximum leave the queue.
    pruneQueue(r, e);
    if (e.research.empty()) return;

    // 2. Every share comes from the pool before any progress changes.
    const std::vector<int64_t> need = remaining(r, s, e, e.research);
    const std::vector<int64_t> give = allocate(pool, need, e.researchEvenly);

    // 3. Progress; a project that reaches the cost completes one level and
    //    leaves the queue with its excess.
    std::vector<ResearchProject> kept;
    std::vector<TechAreaId> done;
    for (size_t i = 0; i < e.research.size(); ++i) {
        ResearchProject p = e.research[i];
        p.progress += give[i];
        if (give[i] >= need[i]) done.push_back(p.area);
        else kept.push_back(p);
    }
    e.research = std::move(kept);
    const bool repeat = e.repeatResearch;
    for (TechAreaId area : done) grantLevel(ctx, id, area, s.empire(id).techLevel(area) + 1, "research");

    // 4. Repeat Projects: completed areas below their maximum go to the end.
    Empire& now = s.empire(id);
    if (repeat)
        for (TechAreaId area : done)
            if (now.techLevel(area) < r.tech(area).maxLevel && now.research.size() < kMaxProjects &&
                std::none_of(now.research.begin(), now.research.end(), [&](const ResearchProject& p) { return p.area == area; }))
                now.research.push_back({area, 0});
    if (!done.empty() && now.research.empty())
        ctx.log(id, LogCategory::Research, "All Projects Completed", "The research queue is empty.");
}

} // namespace opense4::game::research
