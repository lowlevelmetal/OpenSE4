#include "game/intel.hpp"

#include "game/diplomacy.hpp"
#include "game/events.hpp"
#include "game/research.hpp"
#include "game/turn.hpp"

#include <algorithm>
#include <format>

namespace opense4::game::intel {

namespace {

using effects::Effect;

bool validEmpire(const GameState& s, EmpireId e) { return e.valid() && e.index() < s.empires.size(); }

std::optional<Effect> effectOf(const Rules& r, uint32_t project) {
    if (project >= r.data().intelProjects.size()) return std::nullopt;
    return effects::parseEffect(r.data().intelProjects[project].type);
}

bool anyTarget(const IntelProjectOrder& o) { return !o.targetPlanet.valid() && !o.targetVehicle.valid() && !o.thirdEmpire.valid(); }

std::string withPrefix(std::string_view text) { return std::format("{}{}", kMinisterPrefix, text); }

// Logs a project's source message to `to` and its target message to `victim`.
void projectMessages(TurnContext& ctx, const ruleset::IntelProject& p, EmpireId to, EmpireId victim, const effects::Tokens& tokens,
                     std::string_view fallbackSource, std::string_view fallbackTarget, std::string_view suspicion,
                     std::optional<Location> where, Rng& rng) {
    if (validEmpire(ctx.state, to)) {
        std::string text = p.sourceMessages.empty() ? std::string(fallbackSource)
                                                    : effects::substitute(p.sourceMessages[rng.below(p.sourceMessages.size())], tokens);
        ctx.log(to, LogCategory::Intelligence, p.name, withPrefix(text), where, p.sourcePicture);
    }
    if (validEmpire(ctx.state, victim)) {
        const ruleset::Message* m = effects::pickMessage(p.targetMessages, rng);
        std::string title = m && !m->title.empty() ? effects::substitute(m->title, tokens) : std::string("Intelligence Report");
        std::string text = m ? effects::substitute(m->text, tokens) : std::string(fallbackTarget);
        if (!suspicion.empty()) text += suspicion;
        ctx.log(victim, LogCategory::Intelligence, std::move(title), withPrefix(text), where, p.targetPicture);
    }
}

// Removes `amount` points from the target's defense projects in list order.
void drainDefense(const Rules& r, Empire& e, int64_t amount) {
    for (IntelProjectOrder& o : e.intel) {
        if (amount <= 0) break;
        if (!isDefense(r, o.project)) continue;
        const int64_t take = std::min(o.progress, amount);
        o.progress -= take;
        amount -= take;
    }
}

// Tests one attack against the target's counter-intelligence (spec 05 §2.4
// placeholder model). On a block, logs both sides and drains the defense.
bool blocked(TurnContext& ctx, EmpireId source, EmpireId target, const IntelProjectOrder& order, Rng& rng) {
    const Rules& r = ctx.rules;
    GameState& s = ctx.state;
    const int64_t d = defensePoints(r, s, target);
    if (d <= 0) return false;
    const int64_t a = std::max<int64_t>(1, attackStrength(r, order));
    if (static_cast<int64_t>(rng.below(static_cast<uint64_t>(d + a))) >= d) return false;

    Empire& victim = s.empire(target);
    // The first defense project with progress speaks for the defense.
    const IntelProjectOrder* defender = nullptr;
    for (const IntelProjectOrder& o : victim.intel)
        if (isDefense(r, o.project) && o.progress > 0) {
            defender = &o;
            break;
        }
    const uint32_t defenseProject = defender ? defender->project : 0;
    drainDefense(r, victim, a);

    const ruleset::IntelProject& attack = r.data().intelProjects[order.project];
    effects::Tokens tokens;
    // From the defense project's point of view the defender is the source.
    effects::setEmpireTokens(tokens, s, target, source);
    if (defenseProject < r.data().intelProjects.size()) {
        const ruleset::IntelProject& p = r.data().intelProjects[defenseProject];
        projectMessages(ctx, p, target, source, tokens,
                        std::format("Our counter-intelligence stopped an operation of the {}.", tokens.targetEmpireName),
                        std::format("Our {} operation against the {} was stopped by counter-intelligence.", attack.name, tokens.sourceEmpireName),
                        {}, std::nullopt, rng);
    }
    return true;
}

} // namespace

bool isDefense(const Rules& r, uint32_t project) { return effectOf(r, project) == Effect::IntelligenceDefense; }

int64_t defensePoints(const Rules& r, const GameState& s, EmpireId e) {
    if (!validEmpire(s, e)) return 0;
    int64_t total = 0;
    for (const IntelProjectOrder& o : s.empire(e).intel)
        if (isDefense(r, o.project)) total += o.progress * std::max(1, r.data().intelProjects[o.project].effectAmount);
    return total * r.setting("Intelligence Defense Modifier Percent", 100) / 100;
}

int64_t attackStrength(const Rules& r, const IntelProjectOrder& order) {
    if (order.project >= r.data().intelProjects.size()) return 0;
    const int64_t cost = r.data().intelProjects[order.project].cost;
    return anyTarget(order) ? cost * (100 + kAnyTargetBonusPercent) / 100 : cost;
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

void runIntel(TurnContext& ctx) {
    const Rules& r = ctx.rules;
    GameState& s = ctx.state;
    if (!s.options.allowIntel) return;
    Rng rng = s.rng.fork();

    // 1. Every empire spends its points (so defenses are current before any attack).
    for (Empire& e : s.empires) {
        if (!e.alive) continue;
        std::erase_if(e.intel, [&](const IntelProjectOrder& o) { return o.project >= r.data().intelProjects.size(); });
        std::vector<int64_t> need;
        need.reserve(e.intel.size());
        for (const IntelProjectOrder& o : e.intel) need.push_back(std::max<int64_t>(0, r.data().intelProjects[o.project].cost - o.progress));
        const std::vector<int64_t> give = research::allocate(std::max<int64_t>(0, e.economy.intelligence), need, e.intelEvenly);
        for (size_t i = 0; i < e.intel.size(); ++i) e.intel[i].progress += give[i];
    }

    // 2. Finished projects run, empire by empire, in queue order. Defense
    //    projects never finish: they protect while in progress (spec 05 §2.4).
    for (size_t ei = 0; ei < s.empires.size(); ++ei) {
        if (!s.empires[ei].alive) continue;
        const EmpireId source = s.empires[ei].id;
        for (size_t i = 0; i < s.empire(source).intel.size();) {
            const IntelProjectOrder order = s.empire(source).intel[i];
            const ruleset::IntelProject& p = r.data().intelProjects[order.project];
            const auto effect = effects::parseEffect(p.type);
            if (!effect || *effect == Effect::IntelligenceDefense || order.progress < p.cost) {
                ++i;
                continue;
            }

            const std::string problem = orderProblem(r, s, source, order);
            if (!problem.empty()) {
                ctx.log(source, LogCategory::Intelligence, p.name, withPrefix(std::format("The operation could not proceed: {}.", problem)));
            } else if (!blocked(ctx, source, order.target, order, rng)) {
                effects::Target request;
                request.empire = order.target;
                request.source = source;
                request.other = order.thirdEmpire;
                request.object = order.targetPlanet;
                request.vehicle = order.targetVehicle;
                const auto target = effects::pickTarget(r, s, *effect, request, rng);
                const auto where = target ? effects::targetLocation(s, *target) : std::nullopt;
                // `Change Bad Intelligence Chance - System` where the target is.
                const int64_t modifier =
                    where ? effects::systemChanceModifier(r, s, order.target, where->system, AbilityKind::ChangeBadIntelChanceSystem) : 0;
                if (!target) {
                    ctx.log(source, LogCategory::Intelligence, p.name, withPrefix("Our operatives found no suitable target."));
                } else if (!rng.percent(static_cast<int>(std::clamp<int64_t>(100 + modifier, 0, 100)))) {
                    ctx.log(source, LogCategory::Intelligence, p.name, withPrefix("The operation failed."), where);
                    ctx.log(order.target, LogCategory::Intelligence, "Operation Foiled",
                            withPrefix("Local security foiled a hostile intelligence operation."), where);
                } else {
                    const effects::Outcome out = effects::apply(ctx, *effect, *target, p.effectAmount, rng);
                    if (!out.applied) {
                        ctx.log(source, LogCategory::Intelligence, p.name, withPrefix("The operation achieved nothing."), where);
                    } else {
                        effects::Tokens full = out.tokens;
                        effects::setEmpireTokens(full, s, source, order.target, target->other);
                        full.actualAmount = out.actual < 0 ? -out.actual : out.actual;
                        std::string suspicion;
                        if (rng.percent(kDetectionPercent))
                            suspicion = std::format(" Evidence points to the {}.", full.sourceEmpireName);
                        projectMessages(ctx, p, source, order.target, full, "The operation succeeded.",
                                        "A hostile intelligence operation struck us.", suspicion, where, rng);
                        if (!out.report.empty()) {
                            std::string text;
                            for (const auto& line : out.report) text += (text.empty() ? "" : "\n") + line;
                            ctx.log(source, LogCategory::Intelligence, std::format("{} Report", p.name), text, where, p.sourcePicture);
                        }
                    }
                }
            }

            // It leaves the queue unless Repeat is on (then it starts over in place).
            Empire& e = s.empire(source);
            if (i < e.intel.size() && e.intel[i].project == order.project && e.intel[i].target == order.target && !e.repeatIntel) {
                e.intel.erase(e.intel.begin() + static_cast<std::ptrdiff_t>(i));
            } else {
                if (i < e.intel.size() && e.repeatIntel) e.intel[i].progress = 0;
                ++i;
            }
        }
    }
    s.removeDeadVehicles();
}

} // namespace opense4::game::intel
