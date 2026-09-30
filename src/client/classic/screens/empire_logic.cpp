#include "client/classic/screens/empire_logic.hpp"

#include "game/ai.hpp"
#include "game/query.hpp"
#include "game/research.hpp"
#include "game/score.hpp"

#include <algorithm>
#include <cctype>
#include <format>

namespace opense4::client::classic {

namespace {

using game::MessageType;
using game::Treaty;

std::string grouped(int64_t v) {
    const bool neg = v < 0;
    const std::string digits = std::to_string(neg ? -v : v);
    std::string out;
    for (size_t i = 0; i < digits.size(); ++i) {
        if (i > 0 && (digits.size() - i) % 3 == 0) out += ',';
        out += digits[i];
    }
    return neg ? "-" + out : out;
}

bool startsWith(std::string_view s, std::string_view prefix) {
    if (s.size() < prefix.size()) return false;
    for (size_t i = 0; i < prefix.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(s[i])) != std::tolower(static_cast<unsigned char>(prefix[i]))) return false;
    return true;
}

bool sameText(std::string_view a, std::string_view b) { return a.size() == b.size() && startsWith(a, b); }

bool areaAllowed(const game::GameState& s, ruleset::TechAreaId a) {
    const auto& allowed = s.options.techAreasAllowed;
    return allowed.empty() || a.index() >= allowed.size() || allowed[a.index()];
}

} // namespace

// ---- Empires -------------------------------------------------------------------------------

std::vector<game::EmpireId> contactedEmpires(const game::GameState& s, game::EmpireId me) {
    std::vector<game::EmpireId> out;
    if (!me.valid() || me.index() >= s.empires.size()) return out;
    const game::Empire& e = s.empire(me);
    for (const game::Empire& other : s.empires)
        if (other.id != me && other.alive && other.id.index() < e.relations.size() && e.relation(other.id).contact) out.push_back(other.id);
    return out;
}

std::string_view treatyCode(Treaty t) {
    switch (t) {
        case Treaty::War: return "WR";
        case Treaty::NonIntercourse: return "NI";
        case Treaty::None: return "--";
        case Treaty::NonAggression: return "NA";
        case Treaty::Subjugation: return "SB";
        case Treaty::Protectorate: return "PR";
        case Treaty::TradeAlliance: return "TA";
        case Treaty::TradeResearchAlliance: return "TR";
        case Treaty::MilitaryAlliance: return "MA";
        case Treaty::Partnership: return "PS";
        case Treaty::Count: break;
    }
    return "??";
}

std::string_view moodWord(int anger) { return game::ai::moodLabel(anger); }  // spec 05 §7.3

bool treatyVisibleTo(const game::GameState& s, game::EmpireId viewer, game::EmpireId a, game::EmpireId b) {
    if (a == viewer || b == viewer) return true;
    return game::allied(s, viewer, a) || game::allied(s, viewer, b);
}

// ---- Messages ---------------------------------------------------------------------------------

MessageNeeds messageNeeds(MessageType t) {
    MessageNeeds n;
    switch (t) {
        case MessageType::ProposeTreaty:
        case MessageType::CounterTreaty: n.treaty = true; break;
        case MessageType::ProposeTrade:
        case MessageType::CounterTrade: n.offer = n.request = true; break;
        case MessageType::Gift:
        case MessageType::Tribute: n.offer = true; break;
        case MessageType::GrantIndependence: n.ownPlanet = true; break;
        case MessageType::DemandRemoveShips:
        case MessageType::DemandRemoveColonies:
        case MessageType::DemandStopAttacks: n.system = true; break;
        case MessageType::DemandLeavePlanet:
        case MessageType::RequestAttackPlanet: n.planet = true; break;
        case MessageType::RequestAttackEmpire: n.thirdEmpire = n.system = true; break;
        case MessageType::RequestStopHostilities:
        case MessageType::RequestBreakTreaty:
        case MessageType::RequestDeclareWar:
        case MessageType::RequestMakePeace:
        case MessageType::RequestSupport: n.thirdEmpire = true; break;
        default: break;
    }
    return n;
}

std::vector<MessageType> sendableMessageTypes(Treaty t, const game::GameOptions& o) {
    std::vector<MessageType> out{MessageType::General, MessageType::ProposeTreaty};
    const bool war = t == Treaty::War;
    if (t != Treaty::War && t != Treaty::None) out.push_back(MessageType::BreakTreaty);
    if (!war) out.push_back(MessageType::DeclareWar);
    if (!war) out.push_back(MessageType::ProposeTrade);
    if (o.allowGifts) {
        out.push_back(MessageType::Gift);
        out.push_back(MessageType::Tribute);
    }
    out.push_back(MessageType::Surrender);
    out.push_back(MessageType::GrantIndependence);
    if (o.allowGifts) {
        out.push_back(MessageType::DemandGift);
        out.push_back(MessageType::DemandTribute);
    }
    for (MessageType m : {MessageType::DemandSurrender, MessageType::DemandRemoveShips, MessageType::DemandRemoveColonies,
                          MessageType::DemandLeavePlanet, MessageType::DemandStopEspionage, MessageType::DemandStopSabotage,
                          MessageType::DemandStopAttacks})
        out.push_back(m);
    // Asking for help makes little sense at war with the recipient.
    if (!war)
        for (MessageType m : {MessageType::RequestStopHostilities, MessageType::RequestBreakTreaty, MessageType::RequestDeclareWar,
                              MessageType::RequestMakePeace, MessageType::RequestSupport, MessageType::RequestAttackEmpire,
                              MessageType::RequestAttackPlanet})
            out.push_back(m);
    return out;
}

bool answerable(MessageType t) {
    switch (t) {
        case MessageType::ProposeTreaty:
        case MessageType::CounterTreaty:
        case MessageType::ProposeTrade:
        case MessageType::CounterTrade:
        case MessageType::Gift:
        case MessageType::Tribute: return true;
        default: return t >= MessageType::DemandGift && t <= MessageType::DemandStopAttacks;
    }
}

MessageType counterType(MessageType received) {
    switch (received) {
        case MessageType::ProposeTreaty:
        case MessageType::CounterTreaty: return MessageType::CounterTreaty;
        case MessageType::ProposeTrade:
        case MessageType::CounterTrade: return MessageType::CounterTrade;
        default: return MessageType::General;
    }
}

std::vector<Treaty> proposableTreaties(Treaty current) {
    std::vector<Treaty> out;
    for (int i = static_cast<int>(Treaty::NonIntercourse); i < static_cast<int>(Treaty::Count); ++i) {
        const auto t = static_cast<Treaty>(i);
        if (t != current) out.push_back(t);
    }
    return out;
}

std::string_view toneName(int tone) {
    switch (tone) {
        case 0: return "Pleading";
        case 2: return "Demanding";
        default: return "Neutral";
    }
}

std::string defaultMessageText(MessageType t, Treaty treaty) {
    const std::string treatyName(game::displayName(treaty));
    switch (t) {
        case MessageType::General: return "Greetings from our people.";
        case MessageType::ProposeTreaty: return std::format("We propose a {} between our empires.", treatyName);
        case MessageType::CounterTreaty: return std::format("We cannot agree to that, but would accept a {}.", treatyName);
        case MessageType::BreakTreaty: return "Our agreement with you is at an end.";
        case MessageType::DeclareWar: return "From this day our empires are at war.";
        case MessageType::ProposeTrade: return "We offer the following exchange.";
        case MessageType::CounterTrade: return "Your terms are unacceptable. Consider this exchange instead.";
        case MessageType::Gift: return "Please accept this gift as a sign of our goodwill.";
        case MessageType::Tribute: return "We offer this tribute in recognition of your strength.";
        case MessageType::Surrender: return "We can no longer resist. Our empire is yours.";
        case MessageType::GrantIndependence: return "We release this colony from our rule.";
        case MessageType::DemandGift: return "A gift from you would be welcome.";
        case MessageType::DemandTribute: return "You will pay tribute to us.";
        case MessageType::DemandSurrender: return "Surrender now and you will be spared.";
        case MessageType::DemandRemoveShips: return "Remove your ships from this system.";
        case MessageType::DemandRemoveColonies: return "Remove your colonies from this system.";
        case MessageType::DemandLeavePlanet: return "Leave this planet.";
        case MessageType::RequestStopHostilities: return "We ask you to stop your hostilities against this empire.";
        case MessageType::RequestBreakTreaty: return "We ask you to break your treaty with this empire.";
        case MessageType::RequestDeclareWar: return "We ask you to declare war on this empire.";
        case MessageType::RequestMakePeace: return "We ask you to make peace with this empire.";
        case MessageType::RequestSupport: return "We ask for your support against this empire.";
        case MessageType::RequestAttackEmpire: return "We ask you to attack this empire.";
        case MessageType::RequestAttackPlanet: return "We ask you to attack this planet.";
        case MessageType::DemandStopEspionage: return "Stop spying on us.";
        case MessageType::DemandStopSabotage: return "Stop sabotaging our empire.";
        case MessageType::DemandStopAttacks: return "Stop your attacks in this system.";
        default: return {};
    }
}

bool isAnyItem(const game::PackageItem& item) {
    using K = game::PackageItem::Kind;
    switch (item.kind) {
        case K::Resources: return false;
        case K::Technology: return !item.tech.valid();
        case K::Planet: return !item.planet.valid();
        case K::Vehicle: return !item.vehicle.valid();
        case K::StarChart:
        case K::System: return !item.system.valid();
        case K::Treaty: return false;
        case K::CommChannel: return !item.empire.valid();
    }
    return false;
}

bool packageHasAny(const std::vector<game::PackageItem>& items) {
    return std::any_of(items.begin(), items.end(), [](const game::PackageItem& i) { return isAnyItem(i); });
}

std::string_view packageKindName(game::PackageItem::Kind k) {
    using K = game::PackageItem::Kind;
    switch (k) {
        case K::Resources: return "Resources";
        case K::Technology: return "Technology";
        case K::Planet: return "Planet";
        case K::Vehicle: return "Vehicle";
        case K::StarChart: return "Star Chart";
        case K::Treaty: return "Treaty";
        case K::CommChannel: return "Comm Channel";
        case K::System: return "System";
    }
    return "?";
}

std::string packageItemText(const game::Rules& r, const game::GameState& s, const game::PackageItem& item) {
    using K = game::PackageItem::Kind;
    switch (item.kind) {
        case K::Resources: {
            std::string out;
            for (game::Resource res : game::kResources) {
                if (item.resources[res] == 0) continue;
                if (!out.empty()) out += ", ";
                out += std::format("{} {}", grouped(item.resources[res]), game::displayName(res));
            }
            return out.empty() ? "No resources" : out;
        }
        case K::Technology:
            if (!item.tech.valid() || item.tech.index() >= r.data().techAreas.size()) return "Any technology";
            return std::format("Technology: {}", r.tech(item.tech).name);
        case K::Planet:
            if (!item.planet.valid() || item.planet.index() >= s.galaxy.objects.size()) return "Any planet";
            return std::format("Planet: {}", s.galaxy.object(item.planet).name);
        case K::Vehicle:
            if (const game::Vehicle* v = item.vehicle.valid() ? s.vehicle(item.vehicle) : nullptr)
                return std::format("Vehicle: {} ({})", v->name, s.design(v->design).name);
            return item.vehicle.valid() ? "Vehicle (no longer exists)" : "Any vehicle";
        case K::StarChart:
            if (!item.system.valid() || item.system.index() >= s.galaxy.systems.size()) return "Any star chart";
            return std::format("Star chart: {}", s.galaxy.system(item.system).name);
        case K::System:
            if (!item.system.valid() || item.system.index() >= s.galaxy.systems.size()) return "Any system claim";
            return std::format("System claim: {}", s.galaxy.system(item.system).name);
        case K::Treaty: return std::format("Treaty: {}", game::displayName(item.treaty));
        case K::CommChannel:
            if (!item.empire.valid() || item.empire.index() >= s.empires.size()) return "Any comm channel";
            return std::format("Comm channel: {}", s.empire(item.empire).name);
    }
    return "?";
}

// ---- Research -------------------------------------------------------------------------------

std::vector<ruleset::TechAreaId> researchableAreas(const game::Rules& r, const game::GameState& s, const game::Empire& e) {
    auto list = game::research::researchable(r, s, e);
    if (!list.empty()) return list;
    for (uint32_t i = 0; i < r.data().techAreas.size(); ++i) {
        const ruleset::TechAreaId a{i};
        if (r.techVisible(s, e, a) && e.techLevel(a) < r.tech(a).maxLevel) list.push_back(a);
    }
    return list;
}

std::string_view unlockKindName(TechUnlock::Kind k) {
    switch (k) {
        case TechUnlock::Kind::Component: return "Component";
        case TechUnlock::Kind::Facility: return "Facility";
        case TechUnlock::Kind::Hull: return "Vehicle Size";
        case TechUnlock::Kind::IntelProject: return "Intel Project";
        case TechUnlock::Kind::TechArea: return "Tech Area";
    }
    return "?";
}

std::vector<TechUnlock> techUnlocks(const game::Rules& r, ruleset::TechAreaId area, int level) {
    std::vector<TechUnlock> out;
    auto names = [&](const std::vector<ruleset::TechRequirement>& reqs) {
        return std::any_of(reqs.begin(), reqs.end(), [&](const ruleset::TechRequirement& q) { return q.area == area && q.level == level; });
    };
    const ruleset::Ruleset& d = r.data();
    for (uint32_t i = 0; i < d.vehicleSizes.size(); ++i)
        if (names(d.vehicleSizes[i].requirements)) out.push_back({TechUnlock::Kind::Hull, i, d.vehicleSizes[i].name});
    for (uint32_t i = 0; i < d.components.size(); ++i)
        if (names(d.components[i].requirements)) out.push_back({TechUnlock::Kind::Component, i, d.components[i].name});
    for (uint32_t i = 0; i < d.facilities.size(); ++i)
        if (names(d.facilities[i].requirements)) out.push_back({TechUnlock::Kind::Facility, i, d.facilities[i].name});
    for (uint32_t i = 0; i < d.intelProjects.size(); ++i)
        if (names(d.intelProjects[i].requirements)) out.push_back({TechUnlock::Kind::IntelProject, i, d.intelProjects[i].name});
    for (uint32_t i = 0; i < d.techAreas.size(); ++i)
        if (names(d.techAreas[i].requirements)) out.push_back({TechUnlock::Kind::TechArea, i, d.techAreas[i].name});
    return out;
}

std::vector<std::string> unlockNames(const game::Rules& r, ruleset::TechAreaId area, int level) {
    auto names = game::research::unlockedBy(r, area, level);
    if (!names.empty()) return names;
    for (const TechUnlock& u : techUnlocks(r, area, level))
        names.push_back(u.kind == TechUnlock::Kind::TechArea ? std::format("{} (tech area)", u.name) : u.name);
    return names;
}

std::string requirementText(const game::Rules& r, const std::vector<ruleset::TechRequirement>& reqs) {
    std::string out;
    for (const auto& q : reqs) {
        if (!q.area.valid() || q.area.index() >= r.data().techAreas.size()) continue;
        if (!out.empty()) out += ", ";
        out += std::format("{} {}", r.tech(q.area).name, q.level);
    }
    return out.empty() ? "None" : out;
}

std::vector<ruleset::TechAreaId> dependentAreas(const game::Rules& r, ruleset::TechAreaId area) {
    std::vector<ruleset::TechAreaId> out;
    const auto& areas = r.data().techAreas;
    for (uint32_t i = 0; i < areas.size(); ++i)
        for (const auto& q : areas[i].requirements)
            if (q.area == area) {
                out.push_back(ruleset::TechAreaId{i});
                break;
            }
    return out;
}

std::pair<int, int> techProgress(const game::Rules& r, const game::GameState& s, const game::Empire& e) {
    // The measure of the research-share victory (spec 05 §6): every level,
    // each capped, against the areas allowed in the game that the race can see.
    return {game::research::totalLevels(r, e), game::research::maxLevels(r, s, e)};
}

std::string etaText(int turns) {
    if (turns < 0) return "Never";
    if (turns == 0) return "This turn";
    return std::format("{}.{} years", turns / 10, turns % 10);
}

std::string techAreasExport(const game::Rules& r, const game::GameState& s, const game::Empire& e) {
    std::string out = std::format("Technology areas for {} ({})\n\n", e.name, std::format("{}.{}", 2400 + s.turn / 10, s.turn % 10));
    const auto& areas = r.data().techAreas;
    for (uint32_t i = 0; i < areas.size(); ++i) {
        const ruleset::TechAreaId a{i};
        if (!areaAllowed(s, a)) continue;
        const ruleset::TechArea& t = areas[i];
        out += std::format("{}\n", t.name);
        out += std::format("  Group:     {}\n", t.group);
        out += std::format("  Level:     {} of {}\n", e.techLevel(a), t.maxLevel);
        out += std::format("  Requires:  {}\n", requirementText(r, t.requirements));
        std::string leads;
        for (ruleset::TechAreaId d : dependentAreas(r, a)) leads += (leads.empty() ? "" : ", ") + r.tech(d).name;
        out += std::format("  Leads to:  {}\n\n", leads.empty() ? "Nothing" : leads);
    }
    return out;
}

std::string techLevelsExport(const game::Rules& r, const game::GameState& s, const game::Empire& e) {
    std::string out = std::format("Technology levels for {} ({})\n\n", e.name, std::format("{}.{}", 2400 + s.turn / 10, s.turn % 10));
    const auto& areas = r.data().techAreas;
    for (uint32_t i = 0; i < areas.size(); ++i) {
        const ruleset::TechAreaId a{i};
        if (!areaAllowed(s, a)) continue;
        const ruleset::TechArea& t = areas[i];
        out += std::format("{} (level {} of {})\n", t.name, e.techLevel(a), t.maxLevel);
        for (int level = 1; level <= t.maxLevel; ++level) {
            const auto unlocks = techUnlocks(r, a, level);
            out += std::format("  Level {:>2}  {:>9} RP", level, grouped(r.techLevelCost(a, level, s.options.techCost)));
            if (unlocks.empty()) {
                out += "\n";
                continue;
            }
            out += "  ";
            for (size_t k = 0; k < unlocks.size(); ++k)
                out += std::format("{}{} [{}]", k ? ", " : "", unlocks[k].name, unlockKindName(unlocks[k].kind));
            out += "\n";
        }
        out += "\n";
    }
    return out;
}

// ---- Intelligence ---------------------------------------------------------------------------

IntelTarget intelTargetKind(const ruleset::IntelProject& p) {
    const std::string_view t = p.type;
    if (sameText(t, "Intelligence Defense")) return IntelTarget::None;
    if (sameText(t, "Politics - Fake Messages") || sameText(t, "Politics - Prevent Messages")) return IntelTarget::ThirdEmpire;
    if (sameText(t, "Planet - Locations")) return IntelTarget::Empire;
    if (startsWith(t, "Planet -")) return IntelTarget::Planet;
    if (sameText(t, "Ship - Locations") || sameText(t, "Ship - Concentrations") || sameText(t, "Ship - Construction Info"))
        return IntelTarget::Empire;
    if (startsWith(t, "Ship -")) return IntelTarget::Vehicle;
    return IntelTarget::Empire;
}

std::vector<uint32_t> availableIntelProjects(const game::Rules& r, const game::Empire& e) {
    std::vector<uint32_t> out;
    const auto& projects = r.data().intelProjects;
    for (uint32_t i = 0; i < projects.size(); ++i)
        if (r.meets(e, projects[i].requirements)) out.push_back(i);
    return out;
}

// ---- Statistics -------------------------------------------------------------------------------

std::string_view metricName(Metric m) {
    switch (m) {
        case Metric::Score: return "Score";
        case Metric::Resources: return "Resources";
        case Metric::Research: return "Research";
        case Metric::Intelligence: return "Intelligence";
        case Metric::TechLevels: return "Tech Levels";
        case Metric::Systems: return "Systems";
        case Metric::Planets: return "Planets";
        case Metric::Population: return "Population";
        case Metric::Units: return "Units";
        case Metric::Ships: return "Ships";
        case Metric::Bases: return "Bases";
        case Metric::Count: break;
    }
    return "?";
}

int64_t metricValue(const game::TurnStats& t, Metric m) {
    switch (m) {
        case Metric::Score: return t.score;
        case Metric::Resources: return t.production.total();
        case Metric::Research: return t.research;
        case Metric::Intelligence: return t.intelligence;
        case Metric::TechLevels: return t.techLevels;
        case Metric::Systems: return t.systems;
        case Metric::Planets: return t.planets;
        case Metric::Population: return t.population;
        case Metric::Units: return t.units;
        case Metric::Ships: return t.ships;
        case Metric::Bases: return t.bases;
        case Metric::Count: break;
    }
    return 0;
}

std::vector<game::TurnStats> statsSeries(const game::Rules& r, const game::GameState& s, game::EmpireId e) {
    std::vector<game::TurnStats> out;
    if (!e.valid() || e.index() >= s.empires.size()) return out;
    out = s.empire(e).history;
    std::sort(out.begin(), out.end(), [](const game::TurnStats& a, const game::TurnStats& b) { return a.turn < b.turn; });
    if (out.empty() || out.back().turn < s.turn) {
        game::TurnStats now = game::score::currentStats(r, s, e);
        now.turn = s.turn;
        out.push_back(now);
    }
    return out;
}

std::vector<game::EmpireId> historyEmpires(const game::GameState& s, game::EmpireId viewer) {
    std::vector<game::EmpireId> out;
    if (!viewer.valid() || viewer.index() >= s.empires.size()) return out;
    std::vector<uint8_t> listed(s.empires.size(), 0);
    listed[viewer.index()] = 1;
    for (game::EmpireId e : contactedEmpires(s, viewer)) listed[e.index()] = 1;
    for (const game::HistoryEntry& h : s.empire(viewer).historyEvents)
        if (h.empire.valid() && h.empire.index() < listed.size()) listed[h.empire.index()] = 1;
    for (size_t i = 0; i < listed.size(); ++i)
        if (listed[i]) out.push_back(game::EmpireId{i});
    return out;
}

std::vector<HistoryLine> historyLines(const game::Rules& r, const game::GameState& s, game::EmpireId viewer, game::EmpireId empire,
                                      bool stats) {
    std::vector<HistoryLine> out;
    if (!viewer.valid() || viewer.index() >= s.empires.size()) return out;
    if (empire.valid()) {
        if (empire.index() >= s.empires.size()) return out;
        const game::Empire& e = s.empire(empire);
        out.push_back({0, std::format("The {} {} is founded", e.name, e.empireType), std::nullopt});
        if (stats)
            for (HistoryEvent& h : statsEvents(statsSeries(r, s, empire))) out.push_back({h.turn, std::move(h.text), std::nullopt});
    }
    for (const game::HistoryEntry& h : s.empire(viewer).historyEvents)
        if (h.empire == empire) out.push_back({h.turn, h.text, h.location});
    // Oldest first (the record is in order of events), then reversed.
    std::stable_sort(out.begin(), out.end(), [](const HistoryLine& a, const HistoryLine& b) { return a.turn < b.turn; });
    std::reverse(out.begin(), out.end());
    return out;
}

std::vector<HistoryEvent> statsEvents(const std::vector<game::TurnStats>& series) {
    std::vector<HistoryEvent> out;
    auto plural = [](int64_t n, std::string_view one, std::string_view many) { return std::format("{} {}", n, n == 1 ? one : many); };
    for (size_t i = 1; i < series.size(); ++i) {
        const game::TurnStats& a = series[i - 1];
        const game::TurnStats& b = series[i];
        if (b.planets > a.planets) out.push_back({b.turn, std::format("Gained {} (now {})", plural(b.planets - a.planets, "planet", "planets"), b.planets)});
        if (b.planets < a.planets) out.push_back({b.turn, std::format("Lost {} (now {})", plural(a.planets - b.planets, "planet", "planets"), b.planets)});
        if (b.systems > a.systems) out.push_back({b.turn, std::format("Spread to {} (now {})", plural(b.systems - a.systems, "new system", "new systems"), b.systems)});
        if (b.systems < a.systems) out.push_back({b.turn, std::format("Withdrew from {} (now {})", plural(a.systems - b.systems, "system", "systems"), b.systems)});
        if (b.techLevels > a.techLevels)
            out.push_back({b.turn, std::format("Learned {} (now {})", plural(b.techLevels - a.techLevels, "tech level", "tech levels"), b.techLevels)});
        if (b.ships < a.ships) out.push_back({b.turn, std::format("Lost {} (now {})", plural(a.ships - b.ships, "ship", "ships"), b.ships)});
        if (b.bases < a.bases) out.push_back({b.turn, std::format("Lost {} (now {})", plural(a.bases - b.bases, "base", "bases"), b.bases)});
    }
    return out;
}

} // namespace opense4::client::classic
