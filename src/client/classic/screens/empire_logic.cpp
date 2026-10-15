#include "client/classic/screens/empire_logic.hpp"

#include "datafile/datafile.hpp"
#include "game/ai.hpp"
#include "game/ai_data.hpp"
#include "game/diplomacy.hpp"
#include "game/design.hpp"
#include "game/events.hpp"
#include "game/query.hpp"
#include "game/research.hpp"
#include "game/score.hpp"
#include "game/sight.hpp"

#include <algorithm>
#include <cctype>
#include <format>
#include <optional>

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
        case Treaty::War: return "War";
        case Treaty::NonIntercourse: return "NI";
        case Treaty::None: return "";
        case Treaty::NonAggression: return "NA";
        case Treaty::Subjugation: return "Sub";
        case Treaty::Protectorate: return "Pro";
        case Treaty::TradeAlliance: return "TA";
        case Treaty::TradeResearchAlliance: return "TR";
        case Treaty::MilitaryAlliance: return "MA";
        case Treaty::Partnership: return "P";
        case Treaty::Count: break;
    }
    return "";
}

std::string_view moodWord(int anger) { return game::ai::moodLabel(anger); }  // spec 05 §7.3

bool treatyVisibleTo(const game::GameState& s, game::EmpireId viewer, game::EmpireId a, game::EmpireId b) {
    return game::diplomacy::treatyVisible(s, viewer, a, b);
}

std::string treatyGridCell(const game::GameState& s, game::EmpireId viewer, game::EmpireId a, game::EmpireId b) {
    if (a == b || !a.valid() || !b.valid() || a.index() >= s.empires.size() || b.index() >= s.empires.size()) return {};
    if (!treatyVisibleTo(s, viewer, a, b)) return {};
    const game::Relation& rel = s.empire(a).relation(b);
    if (!rel.contact || rel.treaty == Treaty::None || rel.treaty >= Treaty::Count) return {};
    std::string out(treatyCode(rel.treaty));
    if (rel.treaty == Treaty::Subjugation || rel.treaty == Treaty::Protectorate) out += rel.dominant ? " (D, S)" : " (S, D)";
    return out;
}

std::string treatyRoles(const game::Relation& ours) {
    if (ours.treaty == Treaty::Subjugation) return ours.dominant ? "(We Dominate Them)" : "(They Dominate Us)";
    if (ours.treaty == Treaty::Protectorate) return ours.dominant ? "(We Protect Them)" : "(They Protect Us)";
    return {};
}

std::string empireStatusLine(const game::GameState& s, game::EmpireId me, game::EmpireId them) {
    if (!me.valid() || me.index() >= s.empires.size() || !them.valid() || them.index() >= s.empires.size()) return {};
    const game::Relation& rel = s.empire(me).relation(them);
    if (rel.messageSentThisTurn) return "Message Sent";
    const game::DiplomaticMessage* m = replyableMessage(s, me, them);
    if (!m) return {};
    switch (m->type) {
        case MessageType::ProposeTrade:
        case MessageType::CounterTrade: return "Offered Trade";
        case MessageType::Gift: return "Offered Gift";
        case MessageType::Tribute: return "Offered Tribute";
        case MessageType::ProposeTreaty:
        case MessageType::CounterTreaty:
            if (m->treaty != rel.treaty) return std::format("Proposed Treaty: {}", game::displayName(m->treaty));
            return {};
        default: return {};
    }
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
        // A request for a gift or tribute names what it wants (spec 06 §4.2,
        // confirmed: binary): its package is the asked-for side.
        case MessageType::DemandGift:
        case MessageType::DemandTribute: n.request = true; break;
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

bool tradePackage(MessageType t) { return t == MessageType::ProposeTrade || t == MessageType::CounterTrade; }

std::vector<MessageType> sendableMessageTypes(Treaty t, const game::GameOptions& o, bool subjugated) {
    // The original's list (spec 06 §4.2, confirmed: binary). Propose Trade and
    // every request are offered whatever the treaty, war included; Break
    // Treaty only from Non-Aggression up; Surrender and the demand for it only
    // with Allow Surrender; Gift and Tribute only with gifts allowed, while
    // the requests for them do not depend on that option. Grant Independence
    // is OpenSE4's (the original never lists it).
    std::vector<MessageType> out{MessageType::General};
    if (!subjugated) out.push_back(MessageType::ProposeTreaty);
    if (t >= Treaty::NonAggression) out.push_back(MessageType::BreakTreaty);
    if (t != Treaty::War) out.push_back(MessageType::DeclareWar);
    out.push_back(MessageType::ProposeTrade);
    if (o.allowGifts) {
        out.push_back(MessageType::Gift);
        out.push_back(MessageType::Tribute);
    }
    if (o.allowSurrender) out.push_back(MessageType::Surrender);
    out.push_back(MessageType::GrantIndependence);
    out.push_back(MessageType::DemandGift);
    out.push_back(MessageType::DemandTribute);
    if (o.allowSurrender) out.push_back(MessageType::DemandSurrender);
    for (MessageType m : {MessageType::DemandRemoveShips, MessageType::DemandRemoveColonies, MessageType::DemandLeavePlanet,
                          MessageType::DemandStopEspionage, MessageType::DemandStopSabotage, MessageType::DemandStopAttacks,
                          MessageType::RequestStopHostilities, MessageType::RequestBreakTreaty, MessageType::RequestDeclareWar,
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

const game::DiplomaticMessage* replyableMessage(const game::GameState& s, game::EmpireId me, game::EmpireId from) {
    if (!me.valid() || !from.valid() || me == from || me.index() >= s.empires.size() || from.index() >= s.empires.size()) return nullptr;
    // Dated after the cut: the date − 2, or in a turn-based game the date − 1
    // when the sender's number is lower than ours (the engine's window).
    return game::diplomacy::newestMessage(s, me, from, s.turn);
}

std::vector<MessageType> replyTypes(MessageType received) {
    switch (received) {
        case MessageType::ProposeTreaty:
        case MessageType::CounterTreaty: return {MessageType::AcceptTreaty, MessageType::RefuseTreaty, MessageType::CounterTreaty};
        case MessageType::ProposeTrade:
        case MessageType::CounterTrade: return {MessageType::AcceptTrade, MessageType::RefuseTrade, MessageType::CounterTrade};
        case MessageType::Gift:
        case MessageType::Tribute: return {MessageType::AcceptGift, MessageType::RefuseGift};
        default:
            if (received >= MessageType::DemandGift && received <= MessageType::DemandStopAttacks) return {MessageType::AcceptDemand, MessageType::RefuseDemand};
            return {};
    }
}

std::string replyTypeName(MessageType reply, MessageType received) {
    if (received == MessageType::Tribute && reply == MessageType::AcceptGift) return "Accept Tribute";
    if (received == MessageType::Tribute && reply == MessageType::RefuseGift) return "Refuse Tribute";
    return std::string(game::displayName(reply));
}

std::optional<bool> replyAccepts(MessageType reply) {
    switch (reply) {
        case MessageType::AcceptTreaty:
        case MessageType::AcceptTrade:
        case MessageType::AcceptGift:
        case MessageType::AcceptDemand: return true;
        case MessageType::RefuseTreaty:
        case MessageType::RefuseTrade:
        case MessageType::RefuseGift:
        case MessageType::RefuseDemand: return false;
        default: return std::nullopt;
    }
}

OfferView offerView(const game::DiplomaticMessage* newest) {
    if (!newest) return OfferView::None;
    switch (newest->type) {
        case MessageType::ProposeTrade:
        case MessageType::CounterTrade: return OfferView::Trade;
        case MessageType::Gift: return OfferView::Gift;
        case MessageType::Tribute: return OfferView::Tribute;
        default: return OfferView::None;
    }
}

std::vector<Treaty> proposableTreaties(Treaty current) {
    (void)current;   // the current treaty stays listed
    std::vector<Treaty> out;
    for (Treaty t : {Treaty::NonIntercourse, Treaty::NonAggression, Treaty::Subjugation, Treaty::Protectorate, Treaty::TradeAlliance,
                     Treaty::TradeResearchAlliance, Treaty::MilitaryAlliance, Treaty::Partnership})
        out.push_back(t);
    return out;
}

std::vector<Treaty> packageTreaties(Treaty current) {
    std::vector<Treaty> out;
    for (int i = 0; i < static_cast<int>(Treaty::Count); ++i) {
        const auto t = static_cast<Treaty>(i);
        if (t != Treaty::None && t != current) out.push_back(t);
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
        case MessageType::AcceptTreaty: return std::format("We accept the {}.", treatyName);
        case MessageType::RefuseTreaty: return "We cannot accept your proposal.";
        case MessageType::AcceptTrade: return "The exchange is acceptable.";
        case MessageType::RefuseTrade: return "The exchange is not acceptable.";
        case MessageType::AcceptGift: return "We accept your offer.";
        case MessageType::RefuseGift: return "We decline your offer.";
        case MessageType::AcceptDemand: return "We will do as you ask.";
        case MessageType::RefuseDemand: return "We will not do that.";
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


std::string messageSpeechPool(const game::GameState& s, game::EmpireId me, game::EmpireId to, MessageType t, MessageType received) {
    if (t == MessageType::AcceptDemand || t == MessageType::RefuseDemand) {
        const bool friendly = me.valid() && me.index() < s.empires.size() && to.valid() && to.index() < s.empires.size() &&
                              s.empire(me).relation(to).treaty >= Treaty::NonAggression;
        return std::format("Response {} {} {}", friendly ? "Friend" : "Enemy", t == MessageType::AcceptDemand ? "YES" : "NO",
                           game::ai::angerKeyName(received));
    }
    if (received == MessageType::Tribute && (t == MessageType::AcceptGift || t == MessageType::RefuseGift))
        return t == MessageType::AcceptGift ? "Send Accept Tribute" : "Send Refuse Tribute";
    return std::format("Send {}", game::ai::angerKeyName(t));
}

std::string messageText(const game::Rules& r, const game::GameState& s, game::EmpireId me, const game::DiplomaticMessage& draft,
                        MessageType received, uint64_t pick) {
    if (me.valid() && me.index() < s.empires.size()) {
        const game::ai::AiProfile& profile = game::ai::profileFor(r, s.empire(me).race.style);
        if (const std::vector<std::string>* lines = profile.speech.pool(messageSpeechPool(s, me, draft.to, draft.type, received)))
            return game::ai::fillSpeech(s, (*lines)[static_cast<size_t>(pick % lines->size())], me, draft.to, draft.thirdEmpire, draft.treaty,
                                        draft.system, draft.planet);
    }
    return defaultMessageText(draft.type, draft.treaty);
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

std::string_view packageTabName(PackageTab t) {
    switch (t) {
        case PackageTab::Systems: return "Systems";
        case PackageTab::Planets: return "Planets";
        case PackageTab::Resources: return "Resources";
        case PackageTab::Technology: return "Technology";
        case PackageTab::Ships: return "Ships";
        case PackageTab::Units: return "Units";
        case PackageTab::StarCharts: return "Star Charts";
        case PackageTab::Treaty: return "Treaty";
        case PackageTab::CommChannels: return "Comm Channels";
        case PackageTab::Count: break;
    }
    return {};
}

bool packageTabEnabled(const game::GameOptions& o, PackageTab t) { return t != PackageTab::Technology || o.allowTechTrades; }

std::vector<game::PackageItem> packageChoices(const game::Rules& r, const game::GameState& s, game::EmpireId me, game::EmpireId them,
                                              PackageTab tab, bool give) {
    using K = game::PackageItem::Kind;
    std::vector<game::PackageItem> out;
    if (!me.valid() || me.index() >= s.empires.size() || !them.valid() || them.index() >= s.empires.size() || me == them) return out;
    const game::Empire& us = s.empire(me);
    const game::EmpireId owner = give ? me : them;
    auto item = [](K kind) {
        game::PackageItem i;
        i.kind = kind;
        return i;
    };
    // The "Any" placeholder, on the side we ask for only.
    auto any = [&](K kind) {
        if (!give) out.push_back(item(kind));
    };
    // A vehicle of `owner` we may list: ours, or one of theirs that we see.
    auto listable = [&](const game::Vehicle& v) {
        if (v.owner != owner || v.count <= 0) return false;
        return give || std::find(us.knowledge.visibleVehicles.begin(), us.knowledge.visibleVehicles.end(), v.id) != us.knowledge.visibleVehicles.end();
    };
    // The named rows from `first` on in alphabetical order, the letter case
    // ignored (ours), after the "Any" row.
    auto sortFrom = [&](size_t first, auto&& name) {
        std::stable_sort(out.begin() + static_cast<std::ptrdiff_t>(first), out.end(), [&](const game::PackageItem& a, const game::PackageItem& b) {
            const std::string x = name(a), y = name(b);
            return std::lexicographical_compare(x.begin(), x.end(), y.begin(), y.end(), [](char p, char q) {
                return std::tolower(static_cast<unsigned char>(p)) < std::tolower(static_cast<unsigned char>(q));
            });
        });
    };
    switch (tab) {
        case PackageTab::Systems:
        case PackageTab::StarCharts: {
            const bool charts = tab == PackageTab::StarCharts;
            if (charts) any(K::StarChart);
            for (const game::StarSystem& sys : s.galaxy.systems) {
                if (!us.hasExplored(sys.id)) continue;
                game::PackageItem i = item(charts ? K::StarChart : K::System);
                i.system = sys.id;
                out.push_back(i);
            }
            break;
        }
        case PackageTab::Planets: {
            // Ours; or the recipient's colonies we see now, a colony whose
            // cloak beats our detection left out, sorted (spec 05 §3.4,
            // confirmed: binary).
            any(K::Planet);
            const size_t first = out.size();
            for (const auto& c : s.colonies) {
                if (!c || c->owner != owner) continue;
                if (!give && (!us.hasExplored(s.galaxy.object(c->planet).system) || !game::sight::canSeeColony(r, s, me, c->planet))) continue;
                game::PackageItem i = item(K::Planet);
                i.planet = c->planet;
                out.push_back(i);
            }
            if (!give) sortFrom(first, [&](const game::PackageItem& i) { return s.galaxy.object(i.planet).name; });
            break;
        }
        case PackageTab::Resources: break;
        case PackageTab::Technology:
            if (!s.options.allowTechTrades) break;
            any(K::Technology);
            // Ours with a level of at least 1; or every area we can see
            // (requirements met, allowed, racial and unique checks), maxed
            // ones included; both alphabetical (spec 05 §3.4, confirmed: binary).
            for (uint32_t a = 0; a < r.data().techAreas.size(); ++a) {
                const ruleset::TechAreaId id{a};
                if (give ? us.techLevel(id) <= 0 : !r.techVisible(s, us, id)) continue;
                game::PackageItem i = item(K::Technology);
                i.tech = id;
                out.push_back(i);
            }
            sortFrom(give ? 0 : 1, [&](const game::PackageItem& i) { return r.tech(i.tech).name; });
            break;
        case PackageTab::Ships:
        case PackageTab::Units: {
            const bool units = tab == PackageTab::Units;
            any(K::Vehicle);
            for (const game::Vehicle& v : s.vehicles) {
                if (!listable(v) || game::isUnitType(game::vehicleType(r, s, v))) continue;
                // Units: our ships and bases that carry units; on the side
                // asked for every one of theirs we see, loaded or not, as we
                // cannot see their cargo (spec 05 §3.4, confirmed: binary).
                if (units && give && v.cargo.units.empty()) continue;
                game::PackageItem i = item(K::Vehicle);
                i.vehicle = v.id;
                out.push_back(i);
            }
            break;
        }
        case PackageTab::Treaty:
            for (Treaty t : packageTreaties(us.relation(them).treaty)) {
                game::PackageItem i = item(K::Treaty);
                i.treaty = t;
                out.push_back(i);
            }
            break;
        case PackageTab::CommChannels:
            // Both sides list the living empires we are in contact with, other
            // than us and the recipient, after "Any Comm Channel" on the side
            // asked for (spec 05 §3.4, confirmed: binary).
            any(K::CommChannel);
            for (const game::Empire& x : s.empires) {
                if (x.id == me || x.id == them || !x.alive || !us.relation(x.id).contact) continue;
                game::PackageItem i = item(K::CommChannel);
                i.empire = x.id;
                out.push_back(i);
            }
            break;
        case PackageTab::Count: break;
    }
    return out;
}

namespace {

bool sameLineValue(const game::PackageItem& a, const game::PackageItem& b) {
    using K = game::PackageItem::Kind;
    if (a.kind != b.kind) return false;
    switch (a.kind) {
        case K::Resources:
            for (game::Resource res : game::kResources)
                if ((a.resources[res] > 0) != (b.resources[res] > 0)) return false;
            return true;
        case K::Technology: return a.tech == b.tech;
        case K::Planet: return a.planet == b.planet;
        case K::Vehicle: return a.vehicle == b.vehicle;
        case K::StarChart:
        case K::System: return a.system == b.system;
        case K::Treaty: return true;  // one treaty per side
        case K::CommChannel: return a.empire == b.empire;
    }
    return false;
}

bool counted(const game::PackageItem& item) { return item.kind == game::PackageItem::Kind::Resources || isAnyItem(item); }

// The resource a resource line holds.
std::optional<game::Resource> lineResource(const game::PackageItem& item) {
    for (game::Resource res : game::kResources)
        if (item.resources[res] > 0) return res;
    return std::nullopt;
}

void setSteps(PackageLine& line, int steps) {
    line.steps = steps;
    if (line.item.kind != game::PackageItem::Kind::Resources) return;
    if (const auto res = lineResource(line.item)) line.item.resources[*res] = int64_t{steps} * kPackageStepAmount;
}

} // namespace

int packageClickSteps(bool x10000, bool x100000, bool shift) {
    if (x10000) return 10;
    if (x100000) return 100;
    return shift ? 10 : 1;
}

game::PackageItem resourceRow(game::Resource res) {
    game::PackageItem i;
    i.kind = game::PackageItem::Kind::Resources;
    i.resources[res] = kPackageStepAmount;
    return i;
}

void addPackageClick(std::vector<PackageLine>& lines, const game::PackageItem& row, int steps) {
    if (steps <= 0) return;
    if (!counted(row)) {
        for (PackageLine& l : lines)
            if (sameLineValue(l.item, row)) {
                if (row.kind == game::PackageItem::Kind::Treaty) l.item.treaty = row.treaty;
                return;
            }
        lines.push_back({row, 1});
        return;
    }
    bool placed = false;
    std::optional<int> rest;
    const size_t n = lines.size();
    for (size_t i = 0; i < n; ++i) {
        PackageLine& l = lines[i];
        if (!sameLineValue(l.item, row) || l.steps >= kMaxPackageSteps) continue;
        if (l.steps + steps <= kMaxPackageSteps) {
            setSteps(l, l.steps + steps);
        } else {
            rest = l.steps + steps - kMaxPackageSteps;
            setSteps(l, kMaxPackageSteps);
        }
        placed = true;
        if (rest) break;
    }
    if (placed && !rest) return;
    PackageLine fresh{row, 0};
    setSteps(fresh, rest.value_or(steps));
    lines.push_back(fresh);
}

void takePackageClick(std::vector<PackageLine>& lines, size_t line, int steps) {
    if (line >= lines.size()) return;
    PackageLine& l = lines[line];
    if (counted(l.item) && l.steps > steps) {
        setSteps(l, l.steps - steps);
        return;
    }
    lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(line));
}

std::vector<PackageLine> packageLines(const std::vector<game::PackageItem>& items) {
    std::vector<PackageLine> out;
    for (const game::PackageItem& item : items) {
        if (item.kind == game::PackageItem::Kind::Resources) {
            for (game::Resource res : game::kResources) {
                int64_t left = item.resources[res] / kPackageStepAmount;
                while (left > 0) {
                    const int steps = static_cast<int>(std::min<int64_t>(left, kMaxPackageSteps));
                    PackageLine l{resourceRow(res), 0};
                    setSteps(l, steps);
                    out.push_back(l);
                    left -= steps;
                }
            }
            continue;
        }
        if (isAnyItem(item) && !out.empty() && isAnyItem(out.back().item) && sameLineValue(out.back().item, item) &&
            out.back().steps < kMaxPackageSteps) {
            ++out.back().steps;
            continue;
        }
        out.push_back({item, 1});
    }
    return out;
}

std::vector<game::PackageItem> packageItems(const std::vector<PackageLine>& lines) {
    std::vector<game::PackageItem> out;
    for (const PackageLine& l : lines) {
        const int copies = isAnyItem(l.item) ? std::max(1, l.steps) : 1;
        for (int k = 0; k < copies; ++k) out.push_back(l.item);
    }
    return out;
}

bool samePackage(const std::vector<game::PackageItem>& a, const std::vector<game::PackageItem>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        const game::PackageItem& x = a[i];
        const game::PackageItem& y = b[i];
        if (x.kind != y.kind || x.tech != y.tech || x.planet != y.planet || x.vehicle != y.vehicle || x.system != y.system || x.treaty != y.treaty ||
            x.empire != y.empire)
            return false;
        for (game::Resource res : game::kResources)
            if (x.resources[res] != y.resources[res]) return false;
    }
    return true;
}

std::string packageLineText(const game::Rules& r, const game::GameState& s, const PackageLine& line) {
    if (line.item.kind == game::PackageItem::Kind::Resources)
        if (const auto res = lineResource(line.item)) return std::format("{} - {}000", game::displayName(*res), line.steps);
    if (isAnyItem(line.item)) {
        using K = game::PackageItem::Kind;
        std::string_view name = "Any";
        switch (line.item.kind) {
            case K::Planet: name = "Any Planet"; break;
            case K::Technology: name = "Any Tech Area"; break;
            case K::Vehicle: name = "Any Ship"; break;
            case K::StarChart: name = "Any Star Chart"; break;
            case K::System: name = "Any System"; break;
            case K::CommChannel: name = "Any Comm Channel"; break;
            case K::Resources:
            case K::Treaty: break;
        }
        return line.steps > 1 ? std::format("{} - {}", name, line.steps) : std::string(name);
    }
    return packageItemText(r, s, line.item);
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

std::vector<ResearchListArea> researchListAreas(const game::Rules& r, const game::GameState& s, const game::Empire& e) {
    const std::vector<ruleset::TechAreaId> open = researchableAreas(r, s, e);
    const auto& allowed = s.options.techAreasAllowed;
    std::vector<ResearchListArea> out;
    for (uint32_t i = 0; i < r.data().techAreas.size(); ++i) {
        const ruleset::TechAreaId a{i};
        if (std::find(open.begin(), open.end(), a) != open.end()) {
            out.push_back({a, false});
            continue;
        }
        if (!allowed.empty() && i < allowed.size() && !allowed[i]) continue;
        const int max = r.tech(a).maxLevel;
        if (max > 0 && e.techLevel(a) >= max) out.push_back({a, true});
    }
    return out;
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

std::vector<LevelUnlocks> levelUnlocksFrom(const game::Rules& r, ruleset::TechAreaId area, int first) {
    std::vector<LevelUnlocks> out;
    for (int level = std::max(1, first); level <= r.tech(area).maxLevel; ++level)
        if (std::vector<TechUnlock> items = techUnlocks(r, area, level); !items.empty()) out.push_back({level, std::move(items)});
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

std::vector<std::pair<std::string, std::vector<ruleset::TechAreaId>>> techAreasByGroup(const game::Rules& r,
                                                                                       const std::vector<ruleset::TechAreaId>& areas) {
    std::vector<std::pair<std::string, std::vector<ruleset::TechAreaId>>> out;
    for (ruleset::TechAreaId a : areas) {
        const std::string& g = r.tech(a).group;
        auto it = std::find_if(out.begin(), out.end(), [&](const auto& p) { return p.first == g; });
        if (it == out.end()) {
            out.push_back({g, {}});
            it = out.end() - 1;
        }
        it->second.push_back(a);
    }
    std::stable_sort(out.begin(), out.end(), [](const auto& x, const auto& y) {
        return std::lexicographical_compare(x.first.begin(), x.first.end(), y.first.begin(), y.first.end(), [](char p, char q) {
            return std::tolower(static_cast<unsigned char>(p)) < std::tolower(static_cast<unsigned char>(q));
        });
    });
    return out;
}

namespace {

// The racial trait that opens a racial area: a trait of type "Tech Area"
// whose first value is the area's Racial Area number (spec 05 §1.2).
std::string racialTraitName(const game::Rules& r, int racialArea) {
    for (const ruleset::RacialTrait& t : r.data().racialTraits)
        if (datafile::keysEqual(t.traitType, "Tech Area") && !t.values.empty() && datafile::parseInteger(t.values.front()).value_or(-1) == racialArea)
            return t.name;
    return {};
}

} // namespace

std::vector<TechTreeRow> techAreaRows(const game::Rules& r, ruleset::TechAreaId area) {
    std::vector<TechTreeRow> out;
    const ruleset::TechArea& t = r.tech(area);
    for (const ruleset::TechRequirement& q : t.requirements)
        if (q.area.valid() && q.area.index() < r.data().techAreas.size()) out.push_back({area, {t.name, r.tech(q.area).name, std::to_string(q.level)}});
    if (t.racialArea > 0) out.push_back({area, {t.name, racialTraitName(r, t.racialArea), "Racial Trait"}});
    if (t.uniqueArea > 0) out.push_back({area, {t.name, "Unique Technology", "(Must Be Found)"}});
    if (out.empty()) out.push_back({area, {t.name, {}, {}}});
    return out;
}

std::vector<TechTreeRow> techLevelRows(const game::Rules& r, ruleset::TechAreaId area) {
    std::vector<TechTreeRow> out;
    const ruleset::TechArea& t = r.tech(area);
    const ruleset::Ruleset& d = r.data();
    for (int level = 1; level <= t.maxLevel; ++level) {
        const size_t before = out.size();
        auto items = [&](const auto& list) {
            for (const auto& item : list) {
                const auto& reqs = item.requirements;
                if (std::none_of(reqs.begin(), reqs.end(), [&](const ruleset::TechRequirement& q) { return q.area == area && q.level == level; }))
                    continue;
                std::string name = item.name;
                if (reqs.size() > 1) name += std::format(" [{}]", reqs.size());
                out.push_back({area, {t.name, std::to_string(level), std::move(name)}});
            }
        };
        items(d.vehicleSizes);
        items(d.components);
        items(d.facilities);
        items(d.intelProjects);
        if (out.size() == before) out.push_back({area, {t.name, std::to_string(level), {}}});
    }
    return out;
}

std::vector<TechTreeRow> techTreeRows(const game::Rules& r, bool levels) {
    std::vector<ruleset::TechAreaId> all;
    for (uint32_t i = 0; i < r.data().techAreas.size(); ++i) all.push_back(ruleset::TechAreaId{i});
    std::vector<TechTreeRow> out;
    for (const auto& [group, areas] : techAreasByGroup(r, all))
        for (ruleset::TechAreaId a : areas)
            for (TechTreeRow& row : levels ? techLevelRows(r, a) : techAreaRows(r, a)) out.push_back(std::move(row));
    return out;
}

std::string techTreeExport(const std::vector<TechTreeRow>& rows) {
    std::string out;
    for (const TechTreeRow& row : rows) out += std::format("{},{},{}\n", row.cells[0], row.cells[1], row.cells[2]);
    return out;
}

// ---- Intelligence ---------------------------------------------------------------------------

IntelTarget intelTargetKind(const ruleset::IntelProject& p) {
    // The second list window a project asks for after "Select Empire" (spec
    // 05 §2.1, confirmed: binary), by its Type.
    using game::effects::Effect;
    const std::optional<Effect> effect = game::effects::parseEffect(p.type);
    if (!effect) return IntelTarget::Empire;   // a mod's project: the empire only
    switch (*effect) {
        case Effect::IntelligenceDefense: return IntelTarget::None;
        case Effect::ShipDamage:
        case Effect::ShipLoseMovement:
        case Effect::ShipLoseSupply:
        case Effect::ShipRebel:
        case Effect::ShipExperienceChange:
        case Effect::ShipCargoDamage:
        case Effect::ShipOrdersChange: return IntelTarget::Vehicle;
        case Effect::PlanetConditionsChange:
        case Effect::PlanetValueChange:
        case Effect::PlanetPopulationChange:
        case Effect::PlanetPopulationAngerChange:
        case Effect::PlanetPopulationRiot:
        case Effect::PlanetPopulationRebel:
        case Effect::PlanetCargoDamage:
        case Effect::PlanetFacilityDamage:
        case Effect::PlanetInfo: return IntelTarget::Planet;
        case Effect::ResearchSteal: return IntelTarget::Tech;
        case Effect::PoliticsDisruptTrade:
        case Effect::PoliticsInterceptMessages:
        case Effect::PoliticsFakeMessages:
        case Effect::PoliticsPreventMessages:
        case Effect::PoliticsTreatyInfo: return IntelTarget::ThirdEmpire;
        default: return IntelTarget::Empire;
    }
}

std::vector<uint32_t> availableIntelProjects(const game::Rules& r, const game::Empire& e) {
    std::vector<uint32_t> out;
    const auto& projects = r.data().intelProjects;
    for (uint32_t i = 0; i < projects.size(); ++i)
        if (r.meets(e, projects[i].requirements)) out.push_back(i);
    return out;
}

namespace {

// Rows sorted by their labels, the letter case ignored (ours).
void sortRowsByName(std::vector<IntelTargetRow>& rows) {
    std::stable_sort(rows.begin(), rows.end(), [](const IntelTargetRow& a, const IntelTargetRow& b) {
        return std::lexicographical_compare(a.label.begin(), a.label.end(), b.label.begin(), b.label.end(), [](char p, char q) {
            return std::tolower(static_cast<unsigned char>(p)) < std::tolower(static_cast<unsigned char>(q));
        });
    });
}

} // namespace

IntelTargetList intelTargetList(const game::Rules& r, const game::GameState& s, game::EmpireId me, const game::IntelProjectOrder& order,
                                int stage) {
    IntelTargetList out;
    out.title = "Select Empire";
    if (order.project >= r.data().intelProjects.size() || !me.valid() || me.index() >= s.empires.size()) return out;
    const game::Empire& e = s.empire(me);
    const IntelTarget kind = intelTargetKind(r.data().intelProjects[order.project]);
    auto row = [&](std::string label, auto set) {
        IntelTargetRow x{std::move(label), order};
        set(x.order);
        return x;
    };
    if (stage == 0) {
        for (game::EmpireId k : contactedEmpires(s, me)) out.rows.push_back(row(s.empire(k).name, [k](game::IntelProjectOrder& o) { o.target = k; }));
        out.last = kind == IntelTarget::Empire || kind == IntelTarget::None;
        return out;
    }
    std::vector<IntelTargetRow> named;
    switch (kind) {
        case IntelTarget::Vehicle:
            out.title = "Select Ship";
            out.rows.push_back(row("Any Ship", [](game::IntelProjectOrder& o) { o.targetVehicle = {}; }));
            for (game::VehicleId id : e.knowledge.visibleVehicles)
                if (const game::Vehicle* v = s.vehicle(id); v && v->owner == order.target)
                    named.push_back(row(v->name, [id](game::IntelProjectOrder& o) { o.targetVehicle = id; }));
            break;
        case IntelTarget::Planet:
            // The colonies `me` sees now, by the detection rule (spec 01
            // §6.9); "Any" can still strike the others.
            out.title = "Select Planet";
            out.rows.push_back(row("Any Planet", [](game::IntelProjectOrder& o) { o.targetPlanet = {}; }));
            for (const auto& c : s.colonies)
                if (c && c->owner == order.target && e.hasExplored(s.galaxy.object(c->planet).system) &&
                    game::sight::canSeeColony(r, s, me, c->planet)) {
                    const game::ObjectId p = c->planet;
                    named.push_back(row(s.galaxy.object(p).name, [p](game::IntelProjectOrder& o) { o.targetPlanet = p; }));
                }
            break;
        case IntelTarget::Tech:
            // The areas the spying empire can research now. "Any" keeps the
            // original's draw, which finds only areas where the spy already
            // leads (spec 05 §2.3).
            out.title = "Select Technology";
            out.rows.push_back(row("Any Technology", [](game::IntelProjectOrder& o) { o.targetTech = {}; }));
            for (ruleset::TechAreaId a : researchableAreas(r, s, e))
                named.push_back(row(r.tech(a).name, [a](game::IntelProjectOrder& o) { o.targetTech = a; }));
            break;
        case IntelTarget::ThirdEmpire:
            out.rows.push_back(row("Any Empire", [](game::IntelProjectOrder& o) { o.thirdEmpire = {}; }));
            for (game::EmpireId k : contactedEmpires(s, me))
                if (k != order.target) out.rows.push_back(row(s.empire(k).name, [k](game::IntelProjectOrder& o) { o.thirdEmpire = k; }));
            break;
        case IntelTarget::None:
        case IntelTarget::Empire: break;
    }
    sortRowsByName(named);
    for (IntelTargetRow& x : named) out.rows.push_back(std::move(x));
    return out;
}

std::string intelTargetLine(const game::Rules& r, const game::GameState& s, const game::IntelProjectOrder& o) {
    if (o.project >= r.data().intelProjects.size()) return {};
    switch (intelTargetKind(r.data().intelProjects[o.project])) {
        case IntelTarget::Vehicle: {
            if (!o.targetVehicle.valid()) return "Any Ship";
            const game::Vehicle* v = s.vehicle(o.targetVehicle);
            return v ? v->name : std::string("Unknown Ship");
        }
        case IntelTarget::Planet:
            if (!o.targetPlanet.valid()) return "Any Planet";
            return o.targetPlanet.index() < s.galaxy.objects.size() ? s.galaxy.object(o.targetPlanet).name : std::string("Unknown Planet");
        case IntelTarget::Tech:
            if (!o.targetTech.valid()) return "Any Technology";
            return o.targetTech.index() < r.data().techAreas.size() ? r.tech(o.targetTech).name : std::string("Unknown Technology");
        case IntelTarget::ThirdEmpire:
            if (!o.thirdEmpire.valid()) return "Any Empire";
            return o.thirdEmpire.index() < s.empires.size() ? s.empire(o.thirdEmpire).name : std::string("Unknown Empire");
        case IntelTarget::None:
        case IntelTarget::Empire: break;
    }
    return {};
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

std::vector<game::TurnStats> comparisonRows(const game::GameState& s, game::EmpireId viewer, game::EmpireId empire) {
    std::vector<game::TurnStats> out;
    if (!viewer.valid() || viewer.index() >= s.empires.size() || !empire.valid() || empire.index() >= s.empires.size()) return out;
    const game::Empire& v = s.empire(viewer);
    if (v.statistics.empty() && !v.history.empty()) {
        // A game saved before the record existed (save format 9).
        if (game::score::scoreVisible(s, viewer, empire)) out = s.empire(empire).history;
    } else {
        for (const game::StatisticsRow& row : v.statistics)
            if (row.empire == empire) out.push_back(row.stats);
    }
    std::stable_sort(out.begin(), out.end(), [](const game::TurnStats& a, const game::TurnStats& b) { return a.turn < b.turn; });
    return out;
}

std::vector<game::EmpireId> comparisonEmpires(const game::GameState& s, game::EmpireId viewer) {
    std::vector<game::EmpireId> out;
    for (const game::Empire& e : s.empires)
        if (!comparisonRows(s, viewer, e.id).empty()) out.push_back(e.id);
    return out;
}

namespace {

// v / 1000 with one decimal, rounded half away from zero ("12.3"; the
// rounding is inferred).
std::string tenthsOfThousand(int64_t v) {
    const bool negative = v < 0;
    const int64_t a = negative ? -v : v;
    const int64_t tenths = (a + 50) / 100;
    return std::format("{}{}.{}", negative && tenths > 0 ? "-" : "", tenths / 10, tenths % 10);
}

} // namespace

std::string scoreFigure(int64_t v) {
    if (v >= 1'000'000 || v <= -1'000'000) return tenthsOfThousand(v / 1000) + "M";
    return tenthsOfThousand(v) + "K";
}

std::string populationFigure(int64_t millions) {
    if (millions >= 1'000'000 || millions <= -1'000'000) return tenthsOfThousand(millions / 1000) + "T";
    return tenthsOfThousand(millions) + "B";
}

std::string rankText(int rank) {
    switch (rank) {
        case 1: return "1st";
        case 2: return "2nd";
        case 3: return "3rd";
        default: return std::format("{}th", rank);
    }
}

std::vector<int> denseRanks(const std::vector<int64_t>& scores) {
    std::vector<int64_t> distinct = scores;
    std::sort(distinct.begin(), distinct.end(), std::greater<>());
    distinct.erase(std::unique(distinct.begin(), distinct.end()), distinct.end());
    std::vector<int> out;
    out.reserve(scores.size());
    for (int64_t v : scores)
        out.push_back(int(std::find(distinct.begin(), distinct.end(), v) - distinct.begin()) + 1);
    return out;
}

bool victoryColumnVisible(const game::GameState& s, game::EmpireId viewer, game::EmpireId other) {
    if (!other.valid() || other.index() >= s.empires.size()) return false;
    if (viewer == other) return true;
    if (!s.empire(other).alive) return false;
    if (s.gameOver) return true;
    if (s.options.scoreDisplay > 0) return true;
    if (!viewer.valid() || viewer.index() >= s.empires.size()) return false;
    const game::Relation& rel = s.empire(viewer).relation(other);
    return rel.contact && rel.treaty >= Treaty::NonAggression;
}

bool victoryRowOn(const game::VictoryConditions& v, VictoryRow row) {
    switch (row) {
        case VictoryRow::Score: return v.score;
        case VictoryRow::Years: return v.years;
        case VictoryRow::PercentOfSecond: return v.percentOfSecond;
        case VictoryRow::Tech: return v.techPercent;
        case VictoryRow::Peace: return v.peace;
        case VictoryRow::Delay: return v.delay;
        case VictoryRow::Count: break;
    }
    return false;
}

bool victoryMark(const game::Rules& r, const game::GameState& s, VictoryRow row, game::EmpireId e) {
    const game::VictoryConditions& v = s.options.victory;
    if (!victoryRowOn(v, row) || !e.valid() || e.index() >= s.empires.size()) return false;
    const int64_t now = s.turn;
    switch (row) {
        case VictoryRow::Score: return game::score::empireScore(r, s, e) >= v.scoreValue;
        case VictoryRow::Years: return now >= int64_t{v.yearsTurns};
        case VictoryRow::PercentOfSecond: {
            const int64_t mine = game::score::empireScore(r, s, e);
            for (const game::Empire& other : s.empires)
                if (other.alive && other.id != e && !game::score::leadsBy(mine, game::score::empireScore(r, s, other.id), v.percentOfSecondValue))
                    return false;
            return true;
        }
        case VictoryRow::Tech: {
            const game::Empire& emp = s.empire(e);
            return game::score::techShareMet(game::research::totalLevels(r, emp), game::research::maxLevels(r, s, emp), v.techPercentValue);
        }
        case VictoryRow::Peace: return s.peacefulTurns >= static_cast<uint32_t>(std::max(0, v.peaceTurns));
        case VictoryRow::Delay: return now >= int64_t{v.delayTurns};
        case VictoryRow::Count: break;
    }
    return false;
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
    // Oldest first, as the record is (in order of events).
    std::stable_sort(out.begin(), out.end(), [](const HistoryLine& a, const HistoryLine& b) { return a.turn < b.turn; });
    return out;
}

std::string decadeHeading(uint32_t turn) {
    const uint32_t year = 2400 + turn / 10;
    const uint32_t decade = year - year % 10;
    return std::format("{} - {}", decade, decade + 10);
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

// ---- Log ------------------------------------------------------------------------------------------

bool logListsEntry(const game::GameState& s, const game::LogEntry& l) { return l.turn + 1 >= s.turn; }

int messagesAwaitingReply(const game::GameState& s, game::EmpireId me, game::EmpireId from) {
    if (!me.valid() || me.index() >= s.empires.size()) return 0;
    // Only the newest recent message from that empire can be answered (spec
    // 05 §3.4, confirmed: binary).
    const game::DiplomaticMessage* m = replyableMessage(s, me, from);
    if (!m || m->answered || !answerable(m->type)) return 0;
    const std::vector<game::LogEntry>& log = s.empire(me).log;
    return std::any_of(log.begin(), log.end(), [&](const game::LogEntry& l) { return l.message == m->id && logListsEntry(s, l); }) ? 1 : 0;
}

uint8_t logOpeningFilter(uint8_t stored, const std::vector<int>& counts) {
    if (stored == 0 || stored > counts.size()) return 0;
    return counts[stored - 1] > 0 ? stored : uint8_t{0};
}

int logOpeningRow(int32_t stored, const std::vector<int32_t>& shown) {
    if (shown.empty()) return -1;
    const auto it = std::find(shown.begin(), shown.end(), stored);
    return it == shown.end() ? 0 : int(it - shown.begin());
}

int logOpeningScroll(int32_t storedEntry, int32_t storedScroll, const std::vector<int32_t>& shown) {
    if (std::find(shown.begin(), shown.end(), storedEntry) == shown.end()) return 0;
    return std::max(0, storedScroll);
}

int logCombatRecord(const game::GameState& s, const game::LogEntry& l) {
    if (l.category != game::LogCategory::Combat || !l.location) return -1;
    for (size_t i = 0; i < s.combats.size(); ++i)
        if (s.combats[i].location == *l.location) return int(i);
    return -1;
}

std::optional<LogWindow> logWindowTarget(game::LogGoto target) {
    switch (target) {
        case game::LogGoto::ConstructionQueues: return LogWindow::ConstructionQueues;
        case game::LogGoto::Research: return LogWindow::Research;
        case game::LogGoto::Intelligence: return LogWindow::Intelligence;
        case game::LogGoto::EmpireOptions: return LogWindow::EmpireOptions;
        case game::LogGoto::Designs: return LogWindow::Designs;
        case game::LogGoto::Empires: return LogWindow::Empires;
        case game::LogGoto::None:
        case game::LogGoto::Location: break;
    }
    return std::nullopt;
}

std::vector<CombatDamageRow> combatDamageRows(const game::Rules& r, const game::GameState& s, const game::CombatRecord& c) {
    using Kind = game::CombatPiece::Kind;
    const size_t n = c.pieces.size();
    // Survivors by name: does `empire` (or, with `other`, any other empire) hold a survivor of this name?
    auto survives = [&](const std::string& name, game::EmpireId empire, bool other) {
        return std::any_of(c.pieces.begin(), c.pieces.end(), [&](const game::CombatPiece& q) {
            return q.survivor.valid() && q.name == name && (other ? q.survivor != empire : q.survivor == empire);
        });
    };
    std::vector<CombatDamageRow> out;
    for (const game::EmpireId empire : c.participants) {
        CombatDamageRow head;
        head.empire = empire;
        head.header = true;
        head.name = empire.valid() && empire.index() < s.empires.size() ? s.empire(empire).name : std::string("Unknown");
        out.push_back(std::move(head));
        for (uint32_t i = 0; i < n; ++i) {
            const game::CombatPiece& p = c.pieces[i];
            if (p.owner != empire || p.damage < 0 || p.kind == Kind::Seeker || p.kind == Kind::Obstacle) continue;
            CombatDamageRow row;
            row.empire = empire;
            row.piece = i;
            row.name = p.name;
            // Ships and bases add their hull's Code (spec 06 §7 Q18).
            if (p.kind == Kind::Vehicle && p.design.valid() && p.design.index() < s.designs.size()) {
                const uint32_t hull = s.design(p.design).hull;
                if (hull < r.data().vehicleSizes.size() && !r.hull(hull).code.empty()) row.name += std::format(" ({})", r.hull(hull).code);
            }
            if (survives(p.name, empire, false)) row.damage = std::format("{}%", p.damage);
            else row.damage = survives(p.name, empire, true) ? "Taken" : "Dead";
            out.push_back(std::move(row));
        }
    }
    return out;
}

} // namespace opense4::client::classic
