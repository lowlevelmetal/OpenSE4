// Computer player: the Research and Intelligence ministers (spec 05 §7.5,
// confirmed: binary). Difficulty changes nothing here.

#include "datafile/datafile.hpp"
#include "game/ai_planner.hpp"

#include <algorithm>

namespace opense4::game::ai::detail {

namespace {

using datafile::keysEqual;
using ruleset::TechAreaId;

bool researchEventLastTurn(const Empire& e, uint32_t turn) {
    for (const LogEntry& l : e.log)
        if (l.category == LogCategory::Research && l.turn + 1 == turn) return true;
    return false;
}

} // namespace

void planResearch(Planner& p) {
    const Empire& e = p.emp();
    const auto& areas = p.r.data().techAreas;
    auto researchable = [&](TechAreaId a) {
        return a.valid() && a.index() < areas.size() && p.r.techVisible(p.st, e, a) && e.techLevel(a) < p.r.tech(a).maxLevel;
    };
    auto queued = [](const std::vector<ResearchProject>& q, TechAreaId a) {
        return std::any_of(q.begin(), q.end(), [&](const ResearchProject& x) { return x.area == a; });
    };

    // Each run: fund in queue order, no "keep researching", drop areas at their maximum.
    std::vector<ResearchProject> queue;
    for (const ResearchProject& proj : e.research)
        if (proj.area.index() < areas.size() && e.techLevel(proj.area) < p.r.tech(proj.area).maxLevel) queue.push_back(proj);

    const bool choose = (researchEventLastTurn(e, p.st.turn) || queue.size() < 4) && e.economy.research > 0 && queue.size() < 12;
    if (choose) {
        // Mine sweeping: every fifth turn, once our ships have met a mine field.
        if (p.st.turn % 5 == 0 && e.aiMemory.metMinefield) {
            for (uint32_t c = 0; c < p.r.data().components.size(); ++c) {
                if (!hasAbility(p.r.componentAbilities(c), AbilityKind::MineSweeping)) continue;
                for (const ruleset::TechRequirement& req : p.r.component(c).requirements)
                    if (researchable(req.area) && e.techLevel(req.area) < req.level && !queued(queue, req.area) && queue.size() < 12)
                        queue.push_back({req.area, 0});
                break;  // the first component in the list with Mine Sweeping
            }
        }
        // The share total: each queued project's Min Percent, from the first matching
        // row for its area whose level is still above the area's level.
        auto share = [&]() {
            int total = 0;
            for (const ResearchProject& q : queue)
                for (const ResearchRow& row : p.prof.research) {
                    if (!(row.states & maskOf(p.state)) || !keysEqual(row.area, p.r.tech(q.area).name)) continue;
                    const int level = row.level >= 9999 ? p.r.tech(q.area).maxLevel : row.level;
                    if (level > e.techLevel(q.area)) {
                        total += row.minPercent;
                        break;
                    }
                }
            return total;
        };
        while (queue.size() < 12 && share() < 100) {
            std::optional<TechAreaId> pick;
            for (const ResearchRow& row : p.prof.research) {
                if (!(row.states & maskOf(p.state))) continue;
                const auto area = p.r.data().findTechArea(row.area);
                if (!area || !researchable(*area) || queued(queue, *area)) continue;
                const int level = row.level >= 9999 ? p.r.tech(*area).maxLevel : row.level;
                if (e.techLevel(*area) >= level) continue;
                pick = *area;
                break;
            }
            if (!pick) break;
            queue.push_back({*pick, 0});
        }
        // Still nothing: one researchable area at random.
        if (queue.empty()) {
            std::vector<TechAreaId> open;
            for (uint32_t i = 0; i < areas.size(); ++i)
                if (researchable(TechAreaId{i})) open.push_back(TechAreaId{i});
            if (!open.empty()) queue.push_back({open[static_cast<size_t>(p.rng.below(open.size()))], 0});
        }
    }

    bool same = queue.size() == e.research.size() && !e.researchEvenly && !e.repeatResearch;
    for (size_t i = 0; same && i < queue.size(); ++i) same = queue[i].area == e.research[i].area;
    if (same) return;
    p.emit(cmd::SetResearch{std::move(queue), false, false});
}

void planIntel(Planner& p) {
    if (!p.st.options.allowIntel) return;
    const Empire& e = p.emp();
    const auto& projects = p.r.data().intelProjects;

    // Projects against empires we now hold Non-Aggression or better with are dropped.
    std::vector<IntelProjectOrder> queue;
    for (const IntelProjectOrder& o : e.intel)
        if (!o.target.valid() || o.target.index() >= e.relations.size() || e.relation(o.target).treaty < Treaty::NonAggression) queue.push_back(o);

    // The target: the empire in contact, below Non-Aggression, we are angriest at.
    EmpireId target;
    for (const Empire& x : p.st.empires) {
        if (x.id == p.id || !x.alive) continue;
        const Relation& rel = e.relation(x.id);
        if (!rel.contact || rel.treaty >= Treaty::NonAggression || rel.anger <= 0) continue;
        if (!target.valid() || rel.anger > e.relation(target).anger) target = x.id;
    }
    if (target.valid() && e.economy.intelligence > 0) {
        std::vector<uint32_t> available;
        for (uint32_t i = 0; i < projects.size(); ++i)
            if (p.r.meets(e, projects[i].requirements)) available.push_back(i);
        int64_t added = 0;
        for (int tries = 0; tries < 10 && !available.empty(); ++tries) {
            if (queue.size() >= 12 || added > e.economy.intelligence) break;
            const uint32_t pick = available[static_cast<size_t>(p.rng.below(available.size()))];
            IntelProjectOrder o;
            o.project = pick;
            if (!keysEqual(projects[pick].type, "Intelligence Defense")) o.target = target;
            queue.push_back(o);
            added += projects[pick].cost;
        }
    }

    bool same = queue.size() == e.intel.size();
    for (size_t i = 0; same && i < queue.size(); ++i) same = queue[i].project == e.intel[i].project && queue[i].target == e.intel[i].target;
    if (same) return;
    p.emit(cmd::SetIntel{std::move(queue), e.intelEvenly, e.repeatIntel});
}

} // namespace opense4::game::ai::detail
