#include "game/log_picture.hpp"
#include "game/intel.hpp"

#include "game/diplomacy.hpp"
#include "game/events.hpp"
#include "game/research.hpp"
#include "game/turn.hpp"
#include "game/xmath.hpp"

#include <algorithm>
#include <format>

namespace opense4::game::intel {

namespace {

using effects::Effect;

bool validEmpire(const GameState& s, EmpireId e) { return e.valid() && e.index() < s.empires.size(); }
bool living(const GameState& s, EmpireId e) { return validEmpire(s, e) && s.empire(e).alive; }

std::optional<Effect> effectOf(const Rules& r, uint32_t project) {
    if (project >= r.data().intelProjects.size()) return std::nullopt;
    return effects::parseEffect(r.data().intelProjects[project].type);
}

std::string withPrefix(std::string_view text) { return std::format("{}{}", kMinisterPrefix, text); }

int64_t defenseModifier(const Rules& r) { return r.setting("Intelligence Defense Modifier Percent", 100); }

// trunc(Amount × progress × modifier / 100) for one defense project.
int64_t defenseStrength(const Rules& r, const IntelProjectOrder& o) {
    const int64_t amount = r.data().intelProjects[o.project].effectAmount;
    return std::max<int64_t>(0, xmath::pctTrunc(amount * std::max<int64_t>(0, o.progress), defenseModifier(r)));
}

// Goto of a project's outcome (spec 06 §7 Q41, confirmed: binary): its
// location when it has one (a ship, planet or system), else none.
LogGoto outcomeGoto(const std::optional<Location>& where) { return where ? LogGoto::Location : LogGoto::None; }

// Logs a project's source message to `to` and its target message to
// `victim`. `defended`: a defense project stopped the victim's operation, so
// the defender's entry opens Intelligence and the defeated side's has no Goto.
void projectMessages(TurnContext& ctx, const ruleset::IntelProject& p, EmpireId to, EmpireId victim, const effects::Tokens& tokens,
                     std::string_view fallbackSource, std::string_view fallbackTarget, std::string_view suspicion,
                     std::optional<Location> where, Rng& rng, bool defended = false) {
    if (validEmpire(ctx.state, to)) {
        std::string text = p.sourceMessages.empty() ? std::string(fallbackSource)
                                                    : effects::substitute(p.sourceMessages[rng.index(p.sourceMessages.size())], tokens);
        // A counter-intelligence success shows IntelSabotageByUs (spec 06 §4.1).
        logGoto(ctx.log(to, LogCategory::Intelligence, p.name, withPrefix(text), where, defended ? std::string("IntelSabotageByUs") : p.sourcePicture),
                defended ? LogGoto::Intelligence : outcomeGoto(where));
    }
    if (validEmpire(ctx.state, victim)) {
        const ruleset::Message* m = effects::pickMessage(p.targetMessages, rng);
        std::string title = m && !m->title.empty() ? effects::substitute(m->title, tokens) : std::string("Intelligence Report");
        std::string text = m ? effects::substitute(m->text, tokens) : std::string(fallbackTarget);
        if (!suspicion.empty()) text += suspicion;
        logGoto(ctx.log(victim, LogCategory::Intelligence, std::move(title), withPrefix(text), where, p.targetPicture),
                defended ? LogGoto::None : outcomeGoto(where));
    }
}

void failed(TurnContext& ctx, EmpireId source, const ruleset::IntelProject& p, std::string_view why, std::optional<Location> where = {}) {
    // Our project failed: no Goto (spec 06 §7 Q41).
    logGoto(ctx.log(source, LogCategory::Intelligence, p.name, withPrefix(std::format("The operation failed: {}.", why)), where, p.sourcePicture),
            LogGoto::None);
}

// A finished defense project (spec 05 §2.4, confirmed: binary): the other
// living empires are visited in random order, and the first project found
// that is aimed at the owner and needs at most the defense's level is deleted.
void runDefense(TurnContext& ctx, EmpireId owner, const IntelProjectOrder& order, Rng& rng) {
    const Rules& r = ctx.rules;
    GameState& s = ctx.state;
    const ruleset::IntelProject& p = r.data().intelProjects[order.project];
    std::vector<EmpireId> others;
    for (const Empire& e : s.empires)
        if (e.alive && e.id != owner) others.push_back(e.id);
    rng.shuffle(others);
    for (EmpireId x : others) {
        auto& queue = s.empire(x).intel;
        const auto it = std::find_if(queue.begin(), queue.end(), [&](const IntelProjectOrder& o) {
            return o.target == owner && !isDefense(r, o.project) && requirementLevel(r, o.project) <= p.effectAmount;
        });
        if (it == queue.end()) continue;
        const std::string deleted = it->project < r.data().intelProjects.size() ? r.data().intelProjects[it->project].name : std::string{};
        queue.erase(it);
        effects::Tokens tokens;
        effects::setEmpireTokens(tokens, s, owner, x);
        projectMessages(ctx, p, owner, x, tokens, std::format("Our agents shut down the {} operation of the {}.", deleted, tokens.targetEmpireName),
                        std::format("Our {} operation against the {} was uncovered and shut down.", deleted, tokens.sourceEmpireName), {},
                        std::nullopt, rng, true);
        return;
    }
    logGoto(ctx.log(owner, LogCategory::Intelligence, p.name, withPrefix("Our counter-intelligence found no hostile operation to stop."), std::nullopt,
                    p.sourcePicture),
            LogGoto::None);
}

// Runs one finished attack of `source`.
void runAttack(TurnContext& ctx, EmpireId source, const IntelProjectOrder& order, Rng& rng) {
    const Rules& r = ctx.rules;
    GameState& s = ctx.state;
    const ruleset::IntelProject& p = r.data().intelProjects[order.project];
    const auto effect = effects::parseEffect(p.type);
    if (!effect) return failed(ctx, source, p, "our agents do not know how to carry it out");
    if (const std::string problem = orderProblem(r, s, source, order); !problem.empty()) return failed(ctx, source, p, problem);

    // Counter-intelligence: no success roll, only the target's defenses.
    const EmpireId target = order.target;
    const int defender = counterIntelligence(r, s, target, std::max<int64_t>(order.progress, p.cost));
    if (defender >= 0) {
        const uint32_t defense = s.empire(target).intel[static_cast<size_t>(defender)].project;
        const ruleset::IntelProject& d = r.data().intelProjects[defense];
        effects::Tokens tokens;
        // From the defense project's point of view the defender is the source.
        effects::setEmpireTokens(tokens, s, target, source);
        projectMessages(ctx, d, target, source, tokens,
                        std::format("Our counter-intelligence stopped an operation of the {}.", tokens.targetEmpireName),
                        std::format("Our {} operation against the {} was defeated by counter-intelligence.", p.name, tokens.sourceEmpireName),
                        {}, std::nullopt, rng, true);
        return;
    }

    effects::Target request;
    request.empire = target;
    request.source = source;
    request.other = order.thirdEmpire;
    request.object = order.targetPlanet;
    request.vehicle = order.targetVehicle;
    request.tech = order.targetTech;
    const auto picked = effects::pickTarget(r, s, *effect, request, rng);
    if (!picked) return failed(ctx, source, p, "our agents found no valid target");
    const auto where = effects::targetLocation(s, *picked);
    const effects::Outcome out = effects::apply(ctx, *effect, *picked, p.effectAmount, rng);
    if (!out.applied) return failed(ctx, source, p, "the target was not valid", where);

    effects::Tokens full = out.tokens;
    effects::setEmpireTokens(full, s, source, target, picked->other);
    full.actualAmount = out.actual < 0 ? -out.actual : out.actual;
    std::string suspicion;
    if (rng.range(1, kSuspectRoll) == 1) suspicion = suspectLine(full.sourceEmpireName);
    // Every successful effect sends the source and the victim their messages,
    // Planet - Conditions Change included (spec 05 open question 39).
    projectMessages(ctx, p, source, target, full, "The operation succeeded.", "A hostile intelligence operation struck us.",
                    suspicion, where, rng);
    if (!out.report.empty()) {
        std::string text;
        for (const auto& line : out.report) text += (text.empty() ? "" : "\n") + line;
        logGoto(ctx.log(source, LogCategory::Intelligence, std::format("{} Report", p.name), text, where, p.sourcePicture), outcomeGoto(where));
    }
}

} // namespace

std::string suspectLine(std::string_view sourceFullName) { return std::format(" Evidence points to the {}.", sourceFullName); }

bool namesCulprit(const GameState& s, const LogEntry& entry, EmpireId culprit) {
    if (entry.category != LogCategory::Intelligence || !validEmpire(s, culprit)) return false;
    return entry.text.find(suspectLine(effects::empireFullName(s.empire(culprit)))) != std::string::npos;
}

bool isDefense(const Rules& r, uint32_t project) { return effectOf(r, project) == Effect::IntelligenceDefense; }

int requirementLevel(const Rules& r, uint32_t project) {
    if (project >= r.data().intelProjects.size()) return 0;
    // The sum of the levels of the project's tech block (spec 05 §2.4, confirmed: binary).
    int level = 0;
    for (const auto& q : r.data().intelProjects[project].requirements) level += q.level;
    return level;
}

int64_t defensePoints(const Rules& r, const GameState& s, EmpireId e) {
    if (!validEmpire(s, e)) return 0;
    int64_t total = 0;
    for (const IntelProjectOrder& o : s.empire(e).intel)
        if (isDefense(r, o.project)) total = std::min(kDefenseCap, total + defenseStrength(r, o));
    return total;
}

int counterIntelligence(const Rules& r, GameState& s, EmpireId target, int64_t attack) {
    if (!living(s, target)) return -1;
    auto& queue = s.empire(target).intel;
    const int64_t modifier = defenseModifier(r);
    int64_t total = 0;
    for (size_t k = queue.size(); k-- > 0;) {
        IntelProjectOrder& o = queue[k];
        if (!isDefense(r, o.project)) continue;
        const int64_t had = std::max<int64_t>(0, o.progress);
        total = std::min(kDefenseCap, total + defenseStrength(r, o));
        o.progress = 0;
        if (total < attack) continue;
        // Defeated: the project that tipped the balance keeps the surplus,
        // converted back to progress, but never more than it had.
        const int64_t amount = r.data().intelProjects[o.project].effectAmount;
        const int64_t surplus = total - attack;
        const int64_t keep = amount > 0 && modifier > 0 ? (xmath::Ext(surplus) / xmath::Ext(amount) / xmath::percent(modifier)).trunc() : surplus;
        o.progress = std::clamp<int64_t>(keep, 0, had);
        return static_cast<int>(k);
    }
    return -1;
}

std::string orderProblem(const Rules& r, const GameState& s, EmpireId source, const IntelProjectOrder& order) {
    if (!s.options.allowIntel) return "Intelligence is disabled in this game";
    const auto effect = effectOf(r, order.project);
    if (!effect) return "Unknown project type";
    if (*effect == Effect::IntelligenceDefense) return {};
    if (!validEmpire(s, order.target) || order.target == source || !s.empire(order.target).alive) return "No target empire";
    if (!diplomacy::inContact(s, source, order.target)) return "No contact with the target empire";
    return {};
}

void intelStep(TurnContext& ctx, EmpireId id) {
    const Rules& r = ctx.rules;
    GameState& s = ctx.state;
    // "Allow Intelligence Projects" off: the step is skipped entirely.
    if (!s.options.allowIntel || !living(s, id)) return;
    Rng rng = s.rng.fork();
    const int64_t pool = std::max<int64_t>(0, s.empire(id).intelPool);
    s.empire(id).intelPool = 0;

    // 1. No contact with any living empire: the whole queue is lost. Projects
    //    aimed at a destroyed empire (and unknown projects) go.
    {
        Empire& e = s.empire(id);
        bool contact = false;
        for (const Empire& other : s.empires)
            if (other.alive && other.id != id && e.relation(other.id).contact) contact = true;
        if (!contact) e.intel.clear();
        std::erase_if(e.intel, [&](const IntelProjectOrder& o) {
            return o.project >= r.data().intelProjects.size() || (o.target.valid() && !living(s, o.target));
        });
        if (e.intel.size() > research::kMaxProjects) e.intel.resize(research::kMaxProjects);
    }

    // 2. Shares of the pool, as for research.
    {
        Empire& e = s.empire(id);
        std::vector<int64_t> need;
        need.reserve(e.intel.size());
        for (const IntelProjectOrder& o : e.intel) need.push_back(std::max<int64_t>(0, r.data().intelProjects[o.project].cost - o.progress));
        const std::vector<int64_t> give = research::allocate(pool, need, e.intelEvenly);
        for (size_t i = 0; i < e.intel.size(); ++i) e.intel[i].progress += give[i];
    }

    // 3. Every project whose progress reached its Cost runs, in queue order.
    //    Only this empire's step changes its own queue, so the indices hold.
    std::vector<size_t> ran;
    for (size_t i = 0; i < s.empire(id).intel.size(); ++i) {
        const IntelProjectOrder order = s.empire(id).intel[i];
        if (order.progress < r.data().intelProjects[order.project].cost) continue;
        ran.push_back(i);
        if (isDefense(r, order.project)) runDefense(ctx, id, order, rng);
        else runAttack(ctx, id, order, rng);
    }

    // 4. What ran leaves the queue, or restarts in place with Repeat.
    Empire& e = s.empire(id);
    for (size_t k = ran.size(); k-- > 0;) {
        if (ran[k] >= e.intel.size()) continue;
        if (e.repeatIntel) e.intel[ran[k]].progress = 0;
        else e.intel.erase(e.intel.begin() + static_cast<std::ptrdiff_t>(ran[k]));
    }
    s.removeDeadVehicles();
}

} // namespace opense4::game::intel
