// Computer player: research and intelligence queues (spec 05 §7.5 AI_Research, §2).

#include "datafile/datafile.hpp"
#include "game/ai_planner.hpp"
#include "game/research.hpp"

#include <algorithm>

namespace opense4::game::ai::detail {

namespace {

using datafile::keysEqual;
using ruleset::TechAreaId;

constexpr StateMask kWarStates = maskOf(AiState::PrepareForAttack) | maskOf(AiState::Attack) | maskOf(AiState::SecureHoldings) |
                                 maskOf(AiState::Incursion) | maskOf(AiState::PrepareForDefense) | maskOf(AiState::DefendShortTerm) |
                                 maskOf(AiState::DefendLongTerm);

bool mentions(std::span<const ruleset::TechRequirement> reqs, TechAreaId a, int level) {
    for (const auto& q : reqs)
        if (q.area == a && q.level == level) return true;
    return false;
}

bool othersMet(const Empire& e, std::span<const ruleset::TechRequirement> reqs, TechAreaId a) {
    for (const auto& q : reqs)
        if (q.area != a && e.techLevel(q.area) < q.level) return false;
    return true;
}

// Heuristic value of the next level of an area when the AI tables have
// nothing for this data set: cheap levels that unlock things first.
int64_t areaPriority(Planner& p, TechAreaId a) {
    const Empire& e = p.emp();
    const int next = e.techLevel(a) + 1;
    const int64_t cost = std::max<int64_t>(1, research::levelCost(p.r, p.st, a, next));
    const auto& data = p.r.data();
    int64_t bonus = 50;
    for (const auto& c : data.components) {
        if (!mentions(c.requirements, a, next) || !othersMet(e, c.requirements, a)) continue;
        bonus += 100;
        for (const auto& ab : c.abilities) {
            const auto k = parseAbilityKind(ab.type);
            if ((k == AbilityKind::ColonizeRock || k == AbilityKind::ColonizeIce || k == AbilityKind::ColonizeGas) &&
                (p.state == AiState::Exploration || p.state == AiState::Infrastructure))
                bonus += 300;
        }
        if (c.isWeapon() && (maskOf(p.state) & kWarStates)) bonus += 150;
    }
    for (const auto& f : data.facilities)
        if (mentions(f.requirements, a, next) && othersMet(e, f.requirements, a)) bonus += 100;
    for (const auto& h : data.vehicleSizes)
        if (mentions(h.requirements, a, next) && othersMet(e, h.requirements, a)) bonus += 120;
    // Areas that open other areas.
    for (uint32_t i = 0; i < data.techAreas.size(); ++i)
        if (mentions(data.techAreas[i].requirements, a, next)) bonus += 80;
    int64_t weight = 100;
    const std::string group = datafile::normalizeKey(p.r.tech(a).group);
    if ((maskOf(p.state) & kWarStates) && group.find("weapon") != std::string::npos) weight = 200;
    return bonus * weight * 1000 / (cost / 100 + 1);
}

} // namespace

void planResearch(Planner& p) {
    const Empire& e = p.emp();
    const auto& areas = p.r.data().techAreas;
    auto open = [&](TechAreaId a) {
        return a.valid() && a.index() < areas.size() && p.r.techVisible(p.st, e, a) && e.techLevel(a) < p.r.tech(a).maxLevel;
    };
    auto count = [](const std::vector<ResearchProject>& q, TechAreaId a) {
        return static_cast<int>(std::count_if(q.begin(), q.end(), [&](const ResearchProject& x) { return x.area == a; }));
    };
    // Room for one more level of `a` behind what is already queued.
    auto roomFor = [&](const std::vector<ResearchProject>& q, TechAreaId a) {
        return open(a) && e.techLevel(a) + count(q, a) < p.r.tech(a).maxLevel;
    };
    std::vector<ResearchProject> queue;
    for (const ResearchProject& proj : e.research)
        if (roomFor(queue, proj.area)) queue.push_back(proj);
    if (p.mode == Mode::Minimal && !queue.empty()) return;

    auto queued = [&](TechAreaId a) { return count(queue, a) > 0; };
    // The minimum share a queued project asked for (the table row that added it).
    auto minShare = [&](TechAreaId a) {
        for (const ResearchRow& row : p.prof.research) {
            if (!(row.states & maskOf(p.state)) || !keysEqual(row.area, p.r.tech(a).name)) continue;
            const int target = row.level >= 9999 ? p.r.tech(a).maxLevel : row.level;
            if (e.techLevel(a) < target) return row.minPercent;
        }
        return 0;
    };
    // Spec 05 §7.5: a new project may not break an existing project's share
    // (100 = alone, 50 = one other, 33 = two others, 25 = three others).
    auto roomForAnother = [&]() {
        std::vector<TechAreaId> distinct;
        for (const ResearchProject& q : queue)
            if (std::find(distinct.begin(), distinct.end(), q.area) == distinct.end()) distinct.push_back(q.area);
        const size_t areasQueued = distinct.size();
        for (const ResearchProject& q : queue) {
            const int share = minShare(q.area);
            if (share <= 0) continue;
            const size_t others = static_cast<size_t>(std::max(0, 100 / share - 1));
            if (areasQueued > others) return false;
        }
        return true;
    };

    for (const ResearchRow& row : p.prof.research) {
        if (queue.size() >= 12) break;
        if (!(row.states & maskOf(p.state))) continue;
        const auto area = p.r.data().findTechArea(row.area);
        if (!area || !open(*area) || queued(*area)) continue;
        const int target = row.level >= 9999 ? p.r.tech(*area).maxLevel : std::min(row.level, p.r.tech(*area).maxLevel);
        if (e.techLevel(*area) >= target) continue;
        if (!roomForAnother()) break;
        queue.push_back({*area, 0});
    }

    // Keep a few projects going when the tables offer nothing (other data sets,
    // late game): the most useful levels per research point (inferred).
    std::vector<std::pair<int64_t, uint32_t>> scored;
    for (uint32_t i = 0; i < areas.size(); ++i)
        if (open(TechAreaId{i})) scored.emplace_back(areaPriority(p, TechAreaId{i}), i);
    std::sort(scored.begin(), scored.end(),
              [](const auto& x, const auto& y) { return x.first != y.first ? x.first > y.first : x.second < y.second; });
    const size_t want = static_cast<size_t>(2 + std::min(p.difficulty, 2));
    if (queue.size() < want && roomForAnother())
        for (const auto& [score, i] : scored) {
            if (queue.size() >= want) break;
            if (!queued(TechAreaId{i})) queue.push_back({TechAreaId{i}, 0});
        }

    // Research points do not carry over (spec 05 §1.1): queue further levels
    // until the queue can absorb a turn's points, first of what is already
    // queued, in order (inferred).
    auto need = [&]() {
        int64_t total = 0;
        std::vector<ResearchProject> seen;
        for (const ResearchProject& q : queue) {
            const int level = e.techLevel(q.area) + 1 + count(seen, q.area);
            total += std::max<int64_t>(0, research::levelCost(p.r, p.st, q.area, level) - (count(seen, q.area) == 0 ? q.progress : 0));
            seen.push_back(q);
        }
        return total;
    };
    const int64_t points = e.economy.research;
    while (points > 0 && queue.size() < 12 && need() < points) {
        std::optional<TechAreaId> more;
        for (const ResearchProject& q : queue)
            if (!more && roomFor(queue, q.area)) more = q.area;
        for (const auto& [score, i] : scored)
            if (!more && roomFor(queue, TechAreaId{i})) more = TechAreaId{i};
        if (!more) break;
        queue.push_back({*more, 0});
    }
    if (queue.size() > 12) queue.resize(12);

    // Funded in list order, so nothing is spread thin and lost (inferred).
    bool same = queue.size() == e.research.size() && !e.researchEvenly && !e.repeatResearch;
    for (size_t i = 0; same && i < queue.size(); ++i) same = queue[i].area == e.research[i].area;
    if (same) return;
    p.emit(cmd::SetResearch{std::move(queue), false, false});
}

void planIntel(Planner& p) {
    if (!p.st.options.allowIntel) return;
    const Empire& e = p.emp();
    const auto& projects = p.r.data().intelProjects;
    if (projects.empty()) return;
    auto available = [&](uint32_t i) { return p.r.meets(e, projects[i].requirements); };
    auto isDefense = [&](uint32_t i) { return keysEqual(projects[i].type, "Intelligence Defense"); };

    std::vector<IntelProjectOrder> queue;
    std::optional<uint32_t> defense;
    for (uint32_t i = 0; i < projects.size(); ++i) {
        if (!available(i) || !isDefense(i)) continue;
        if (!defense || projects[i].effectAmount > projects[*defense].effectAmount) defense = i;
    }
    if (defense) {
        IntelProjectOrder o;
        o.project = *defense;
        queue.push_back(o);
    }

    // One operation against the nearest war enemy we are in contact with (inferred).
    if (p.difficulty >= 1) {
        EmpireId target;
        for (const Empire& other : p.st.empires)
            if (other.alive && other.id != p.id && p.atWarWith(other.id) && e.relation(other.id).contact) {
                target = other.id;
                break;
            }
        if (target.valid()) {
            // Prefer information, then sabotage of points and facilities; skip
            // political operations, which need a third empire.
            static constexpr std::array<std::string_view, 8> kPreferred{
                "Planet - Info", "Empire - Info", "Ship - Locations", "Tech Level - Info",
                "Points - Steal", "Planet - Facility Damage", "Ship - Damage", "Research - Steal"};
            std::optional<uint32_t> pick;
            for (std::string_view type : kPreferred) {
                for (uint32_t i = 0; i < projects.size() && !pick; ++i)
                    if (available(i) && keysEqual(projects[i].type, type)) pick = i;
                if (pick) break;
            }
            if (pick) {
                IntelProjectOrder o;
                o.project = *pick;
                o.target = target;
                queue.push_back(o);
            }
        }
    }

    bool same = queue.size() == e.intel.size() && e.intelEvenly && !e.repeatIntel;
    for (size_t i = 0; same && i < queue.size(); ++i) same = queue[i].project == e.intel[i].project && queue[i].target == e.intel[i].target;
    if (same) return;
    p.emit(cmd::SetIntel{std::move(queue), true, false});
}

} // namespace opense4::game::ai::detail
