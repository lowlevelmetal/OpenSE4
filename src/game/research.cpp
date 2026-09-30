#include "game/research.hpp"

#include "game/diplomacy.hpp"
#include "game/turn.hpp"

#include <algorithm>
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

// Drops entries for unknown areas and entries beyond the area's maximum.
void pruneQueue(const Rules& r, Empire& e) {
    std::vector<int> pending(r.data().techAreas.size(), 0);
    std::erase_if(e.research, [&](const ResearchProject& p) {
        if (!validArea(r, p.area)) return true;
        const int target = e.techLevel(p.area) + 1 + pending[p.area.index()];
        if (target > r.tech(p.area).maxLevel) return true;
        ++pending[p.area.index()];
        return false;
    });
    if (e.research.size() > kMaxProjects) e.research.resize(kMaxProjects);
}

// Research points still needed by each entry of a queue.
std::vector<int64_t> remaining(const Rules& r, const GameState& s, const Empire& e) {
    const std::vector<int> targets = targetLevels(e);
    std::vector<int64_t> need(e.research.size(), 0);
    for (size_t i = 0; i < e.research.size(); ++i)
        need[i] = std::max<int64_t>(0, levelCost(r, s, e.research[i].area, targets[i]) - e.research[i].progress);
    return need;
}

} // namespace

int64_t levelCost(const Rules& r, const GameState& s, ruleset::TechAreaId area, int level) {
    return r.techLevelCost(area, level, s.options.techCostGrowth);
}

std::vector<ruleset::TechAreaId> researchable(const Rules& r, const GameState& s, const Empire& e) {
    std::vector<TechAreaId> out;
    for (uint32_t i = 0; i < r.data().techAreas.size(); ++i) {
        const TechAreaId a{i};
        if (e.techLevel(a) < r.tech(a).maxLevel && r.techVisible(s, e, a)) out.push_back(a);
    }
    return out;
}

std::vector<int> targetLevels(const Empire& e) {
    std::vector<int> out;
    out.reserve(e.research.size());
    for (size_t i = 0; i < e.research.size(); ++i) {
        int earlier = 0;
        for (size_t j = 0; j < i; ++j)
            if (e.research[j].area == e.research[i].area) ++earlier;
        out.push_back(e.techLevel(e.research[i].area) + 1 + earlier);
    }
    return out;
}

std::vector<int64_t> allocate(int64_t points, std::span<const int64_t> need, bool evenly) {
    std::vector<int64_t> give(need.size(), 0);
    if (points <= 0 || need.empty()) return give;
    if (evenly) {
        const int64_t share = points / static_cast<int64_t>(need.size());
        for (size_t i = 0; i < need.size(); ++i) give[i] = std::clamp<int64_t>(need[i], 0, share);
    } else {
        int64_t left = points;
        for (size_t i = 0; i < need.size() && left > 0; ++i) {
            give[i] = std::clamp<int64_t>(need[i], 0, left);
            left -= give[i];
        }
    }
    return give;
}

int etaTurns(const Rules& r, const GameState& s, const Empire& e, size_t queueIndex) {
    if (queueIndex >= e.research.size()) return -1;
    const int64_t rp = e.economy.research;
    if (rp <= 0) return -1;
    // Simulate on a copy: allocation, completion, removal (no repeats).
    Empire sim;
    sim.techLevels = e.techLevels;
    sim.research = e.research;
    std::vector<uint8_t> tracked(sim.research.size(), 0);
    tracked[queueIndex] = 1;
    constexpr int kHorizon = 1000;
    for (int turn = 1; turn <= kHorizon; ++turn) {
        const std::vector<int> targets = targetLevels(sim);
        const std::vector<int64_t> need = remaining(r, s, sim);
        const std::vector<int64_t> give = allocate(rp, need, e.researchEvenly);
        std::vector<ResearchProject> next;
        std::vector<uint8_t> nextTracked;
        for (size_t i = 0; i < sim.research.size(); ++i) {
            if (give[i] >= need[i]) {
                if (tracked[i]) return turn;
                if (sim.techLevels.size() <= sim.research[i].area.index()) sim.techLevels.resize(sim.research[i].area.index() + 1, 0);
                sim.techLevels[sim.research[i].area.index()] = targets[i];
                continue;
            }
            ResearchProject p = sim.research[i];
            p.progress += give[i];
            next.push_back(p);
            nextTracked.push_back(tracked[i]);
        }
        sim.research = std::move(next);
        tracked = std::move(nextTracked);
        if (std::none_of(tracked.begin(), tracked.end(), [](uint8_t t) { return t != 0; })) return -1;
    }
    return -1;
}

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

void grantLevel(TurnContext& ctx, EmpireId e, ruleset::TechAreaId area, int newLevel, std::string_view source) {
    const Rules& r = ctx.rules;
    GameState& s = ctx.state;
    if (!e.valid() || e.index() >= s.empires.size() || !validArea(r, area)) return;
    Empire& emp = s.empire(e);
    newLevel = std::min(newLevel, r.tech(area).maxLevel);
    if (newLevel <= emp.techLevel(area)) return;
    if (emp.techLevels.size() < r.data().techAreas.size()) emp.techLevels.resize(r.data().techAreas.size(), 0);

    const std::vector<uint8_t> before = availability(r, emp);
    emp.techLevels[area.index()] = newLevel;
    const std::vector<uint8_t> after = availability(r, emp);

    const std::string& name = r.tech(area).name;
    ctx.log(e, LogCategory::Research, "New Tech Level", std::format("{} is now at level {} ({}).", name, newLevel, source));
    for (size_t i = 0; i < after.size(); ++i)
        if (after[i] && !before[i]) {
            const std::string item = itemName(r, i);
            ctx.log(e, LogCategory::Research, std::format("{} Discovered", item),
                    std::format("{} is now available thanks to {} level {}.", item, name, newLevel));
        }
    pruneQueue(r, emp);

    // A subject's discoveries pass to its master (spec 05 §3.2).
    const EmpireId master = diplomacy::masterOf(s, e);
    if (master.valid() && s.empire(master).relation(e).treaty == Treaty::Subjugation)
        grantLevel(ctx, master, area, newLevel, "subject empire");
}

void grantRandomAdvances(TurnContext& ctx, EmpireId e, int count, Rng& rng, std::string_view source) {
    for (int n = 0; n < count; ++n) {
        const std::vector<TechAreaId> options = researchable(ctx.rules, ctx.state, ctx.state.empire(e));
        if (options.empty()) return;
        const TechAreaId a = options[rng.below(options.size())];
        grantLevel(ctx, e, a, ctx.state.empire(e).techLevel(a) + 1, source);
    }
}

int totalLevels(const Empire& e) {
    int n = 0;
    for (int l : e.techLevels) n += l;
    return n;
}

int techPercent(const Rules& r, const GameState& s, const Empire& e) {
    int64_t owned = 0, total = 0;
    for (uint32_t i = 0; i < r.data().techAreas.size(); ++i) {
        const ruleset::TechArea& t = r.data().techAreas[i];
        if (t.racialArea > 0 || t.uniqueArea > 0) continue;
        if (!s.options.techAreasAllowed.empty() && i < s.options.techAreasAllowed.size() && !s.options.techAreasAllowed[i]) continue;
        total += t.maxLevel;
        owned += std::min(e.techLevel(TechAreaId{i}), t.maxLevel);
    }
    return total > 0 ? static_cast<int>(owned * 100 / total) : 0;
}

void runResearch(TurnContext& ctx) {
    const Rules& r = ctx.rules;
    GameState& s = ctx.state;
    for (size_t ei = 0; ei < s.empires.size(); ++ei) {
        if (!s.empires[ei].alive) continue;
        const EmpireId id = s.empires[ei].id;
        pruneQueue(r, s.empires[ei]);
        Empire& e = s.empires[ei];
        if (e.research.empty()) continue;

        const std::vector<int> targets = targetLevels(e);
        const std::vector<int64_t> need = remaining(r, s, e);
        const std::vector<int64_t> give = allocate(std::max<int64_t>(0, e.economy.research), need, e.researchEvenly);

        std::vector<ResearchProject> kept;
        std::vector<std::pair<TechAreaId, int>> done;
        for (size_t i = 0; i < e.research.size(); ++i) {
            ResearchProject p = e.research[i];
            p.progress += give[i];
            if (give[i] >= need[i]) done.emplace_back(p.area, targets[i]);
            else kept.push_back(p);
        }
        e.research = std::move(kept);
        const bool repeat = e.repeatResearch;
        for (const auto& [area, level] : done) {
            grantLevel(ctx, id, area, level, "research");
            Empire& now = s.empire(id);
            if (repeat && now.research.size() < kMaxProjects) {
                int queued = 0;
                for (const auto& p : now.research)
                    if (p.area == area) ++queued;
                if (now.techLevel(area) + queued < r.tech(area).maxLevel) now.research.push_back({area, 0});
            }
        }
        if (!done.empty() && s.empire(id).research.empty())
            ctx.log(id, LogCategory::Research, "All Projects Completed", "The research queue is empty.");
    }
}

} // namespace opense4::game::research
