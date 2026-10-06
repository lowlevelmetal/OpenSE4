#include "game/log_picture.hpp"
#include "game/diplomacy.hpp"

#include "game/economy.hpp"
#include "game/events.hpp"
#include "game/hooks.hpp"
#include "game/query.hpp"
#include "game/research.hpp"
#include "game/sight.hpp"
#include "game/turn.hpp"
#include "game/xmath.hpp"

#include <algorithm>
#include <format>
#include <optional>

namespace opense4::game::diplomacy {

namespace {

bool validEmpire(const GameState& s, EmpireId e) { return e.valid() && e.index() < s.empires.size(); }
bool living(const GameState& s, EmpireId e) { return validEmpire(s, e) && s.empire(e).alive; }

std::string nameOf(const GameState& s, EmpireId e) {
    return validEmpire(s, e) ? effects::empireFullName(s.empire(e)) : std::string("an unknown empire");
}

DiplomaticMessage* findMessage(GameState& s, MessageId id) {
    if (!id.valid()) return nullptr;
    auto it = std::lower_bound(s.messages.begin(), s.messages.end(), id, [](const DiplomaticMessage& m, MessageId x) { return m.id < x; });
    if (it != s.messages.end() && it->id == id) return &*it;
    // Not sorted for some reason (e.g. loaded data): fall back to a scan.
    auto scan = std::find_if(s.messages.begin(), s.messages.end(), [&](const DiplomaticMessage& m) { return m.id == id; });
    return scan != s.messages.end() ? &*scan : nullptr;
}

// Messages the recipient may answer (the rest are marked answered on delivery).
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

// The empire sees design `d` now (spec 05 §8 step 12 counts from this turn).
void learnDesign(const GameState& s, Empire& e, DesignId d) { seeDesign(e.knowledge, d, s.turn); }

// The same for a design that may be the empire's own (then nothing changes).
void learnForeign(GameState& s, EmpireId e, DesignId d) {
    if (d.valid() && d.index() < s.designs.size() && s.design(d).owner != e) learnDesign(s, s.empire(e), d);
}

// Designs another empire has seen pass on with the turn that empire last
// saw them (a design we already know keeps the later date), so shared
// knowledge ages from the real sighting and two partners cannot keep a
// design alive by teaching it back and forth (inferred, spec 05 open
// question 31).
void shareSeenDesigns(GameState& s, EmpireId from, EmpireId to) {
    const std::vector<SeenDesign> seen = s.empire(from).knowledge.seenDesigns;
    for (const SeenDesign& x : seen)
        if (x.design.index() < s.designs.size() && s.design(x.design).owner != to) seeDesign(s.empire(to).knowledge, x.design, x.turn);
}

void explore(GameState& s, EmpireId e, SystemId sys) {
    if (!validEmpire(s, e) || !sys.valid() || sys.index() >= s.galaxy.systems.size()) return;
    auto& explored = s.empire(e).knowledge.explored;
    if (explored.size() < s.galaxy.systems.size()) explored.resize(s.galaxy.systems.size(), 0);
    explored[sys.index()] = 1;
}

// Everything `from` knows about the galaxy becomes known to `to` too.
// Returns the number of systems that became explored for `to`.
int shareMap(GameState& s, EmpireId from, EmpireId to) {
    const Knowledge& a = s.empire(from).knowledge;
    Knowledge& b = s.empire(to).knowledge;
    int added = 0;
    if (b.explored.size() < a.explored.size()) b.explored.resize(a.explored.size(), 0);
    for (size_t i = 0; i < a.explored.size(); ++i)
        if (a.explored[i] && !b.explored[i]) {
            b.explored[i] = 1;
            ++added;
        }
    if (b.knownWarpLink.size() < a.knownWarpLink.size()) b.knownWarpLink.resize(a.knownWarpLink.size(), 0);
    for (size_t i = 0; i < a.knownWarpLink.size(); ++i) b.knownWarpLink[i] |= a.knownWarpLink[i];
    return added;
}

std::string describe(const GameState& s, const Rules& r, const PackageItem& item) {
    switch (item.kind) {
        case PackageItem::Kind::Resources:
            return std::format("{} minerals, {} organics, {} radioactives", item.resources[Resource::Minerals],
                               item.resources[Resource::Organics], item.resources[Resource::Radioactives]);
        case PackageItem::Kind::Technology:
            return item.tech.valid() && item.tech.index() < r.data().techAreas.size() ? std::format("technology: {}", r.tech(item.tech).name)
                                                                                         : std::string("technology");
        case PackageItem::Kind::Planet:
            return item.planet.valid() && item.planet.index() < s.galaxy.objects.size()
                       ? std::format("planet {}", s.galaxy.object(item.planet).name)
                       : std::string("a planet");
        case PackageItem::Kind::Vehicle:
            if (const Vehicle* v = s.vehicle(item.vehicle)) return std::format("vehicle {}", v->name);
            return "a vehicle";
        case PackageItem::Kind::StarChart:
            return item.system.valid() && item.system.index() < s.galaxy.systems.size()
                       ? std::format("star charts of {}", s.galaxy.system(item.system).name)
                       : std::string("star charts");
        case PackageItem::Kind::Treaty: return std::format("a treaty of {}", displayName(item.treaty));
        case PackageItem::Kind::CommChannel: return std::format("communication channels with the {}", nameOf(s, item.empire));
        case PackageItem::Kind::System:
            return item.system.valid() && item.system.index() < s.galaxy.systems.size()
                       ? std::format("our claim to {}", s.galaxy.system(item.system).name)
                       : std::string("a system claim");
    }
    return "?";
}

// The message a reply answers, if it is really one of `kinds` sent by the
// reply's recipient to its sender. Marks it answered.
DiplomaticMessage* repliedTo(GameState& s, const DiplomaticMessage& reply, std::initializer_list<MessageType> kinds) {
    DiplomaticMessage* o = findMessage(s, reply.inReplyTo);
    if (!o || o->from != reply.to || o->to != reply.from || !o->delivered) return nullptr;
    if (std::find(kinds.begin(), kinds.end(), o->type) == kinds.end()) return nullptr;
    o->answered = true;
    return o;
}

void acceptTreaty(TurnContext& ctx, const DiplomaticMessage& proposal) {
    // The proposer is the master of a Subjugation/Protectorate unless the
    // proposal names the dominant side in `thirdEmpire` (inferred).
    EmpireId master = proposal.from;
    if (proposal.thirdEmpire == proposal.from || proposal.thirdEmpire == proposal.to) master = proposal.thirdEmpire;
    // An accepted proposal makes no treaty entry: its "Message" entry is all
    // (spec 06 §7 Q70, confirmed: binary).
    setTreaty(ctx, proposal.from, proposal.to, proposal.treaty, master == proposal.from, TreatyEntry::None);
}

// Carries out an accepted trade, gift or tribute (spec 06 §7 Q70, spec 05
// §3.4, confirmed: binary): a gift's or tribute's items from giver to
// receiver; for a trade, the proposer's offered items first (proposer to
// accepter), then the requested ones (accepter to proposer). Each item makes
// its own entries; nothing is logged for the package as a whole. A package
// that still holds "Any" items cannot be carried out (OpenSE4's guard: the
// original never lets one be accepted).
void acceptPackage(TurnContext& ctx, const DiplomaticMessage& offer) {
    // The game option for gifts and tributes only limits the message types a
    // human can pick; an accepted gift's items move whatever it says (spec 05
    // §7.4, confirmed: binary).
    const bool gift = offer.type == MessageType::Gift || offer.type == MessageType::Tribute;
    const std::string what = gift ? std::string(displayName(offer.type)) : std::string("Trade");
    const bool placeholders = std::any_of(offer.offer.begin(), offer.offer.end(), isPlaceholder) ||
                              std::any_of(offer.request.begin(), offer.request.end(), isPlaceholder);
    if (placeholders) {
        for (EmpireId e : {offer.from, offer.to})
            logGoto(ctx.log(e, LogCategory::Politics, what + " Cancelled", "The package still holds unspecified (\"Any\") items; counter with real items.",
                            std::nullopt, logpicture::race(e == offer.from ? offer.to : offer.from)),
                    LogGoto::Empires);
        return;
    }
    executePackage(ctx, offer.from, offer.to, offer.offer);
    executePackage(ctx, offer.to, offer.from, offer.request);
}

void grantIndependence(TurnContext& ctx, const DiplomaticMessage& m) {
    GameState& s = ctx.state;
    const Colony* c = s.colony(m.planet);
    if (!c || c->owner != m.from) return;
    const std::string planet = s.galaxy.object(m.planet).name;
    // The sender evacuates and abandons the planet; the recipient may settle it (inferred).
    s.colonies[m.planet.index()].reset();
    ctx.log(m.from, LogCategory::Politics, "Independence Granted",
            std::format("We have withdrawn from {} so that the {} may settle it.", planet, nameOf(s, m.to)), locationOf(s.galaxy, m.planet), "GiftGiven");
    ctx.log(m.to, LogCategory::Politics, "Independence Granted",
            std::format("The {} has abandoned {} for us to settle.", nameOf(s, m.from), planet), locationOf(s.galaxy, m.planet), "GiftReceived");
    addHistory(s, m.from, m.to, std::format("Granted {} its independence for the {}", planet, nameOf(s, m.to)), locationOf(s.galaxy, m.planet));
    addHistory(s, m.to, m.from, std::format("The {} granted {} its independence", nameOf(s, m.from), planet), locationOf(s.galaxy, m.planet));
}

void receive(TurnContext& ctx, DiplomaticMessage& stored) {
    GameState& s = ctx.state;
    const DiplomaticMessage m = stored;
    const std::string sender = nameOf(s, m.from);
    std::string text = std::format("The {} sends us a message: {}.", sender, displayName(m.type));
    if (m.type == MessageType::ProposeTreaty || m.type == MessageType::CounterTreaty)
        text += std::format(" Proposed treaty: {}.", displayName(m.treaty));
    if (!m.offer.empty() || !m.request.empty()) {
        for (const auto& item : m.offer) text += std::format("\nOffered: {}", describe(s, ctx.rules, item));
        for (const auto& item : m.request) text += std::format("\nRequested: {}", describe(s, ctx.rules, item));
    }
    if (!m.text.empty()) text += std::format("\n\"{}\"", m.text);
    // Every delivered message is an ordinary Politics entry titled "Message"
    // whose Goto opens Empires (spec 06 §4.1, §7 Q42, confirmed: binary); it
    // names the message for the Log's details and Send Reply. The acceptance
    // of a trade, gift or tribute becomes it after the package's items have
    // made their entries (spec 06 §7 Q70).
    auto messageEntry = [&] {
        if (LogEntry* entry = logGoto(ctx.log(m.to, LogCategory::Politics, "Message", text, std::nullopt, logpicture::race(m.from)), LogGoto::Empires))
            entry->message = m.id;
    };
    const DiplomaticMessage* answered = findMessage(s, m.inReplyTo);
    const bool package = m.type == MessageType::AcceptTrade || m.type == MessageType::AcceptGift ||
                         (m.type == MessageType::AcceptDemand && answered && answered->type == MessageType::CounterTrade);
    if (!package) messageEntry();

    // A counter-proposal answers the message it counters.
    if (m.type == MessageType::CounterTreaty) repliedTo(s, m, {MessageType::ProposeTreaty, MessageType::CounterTreaty});
    if (m.type == MessageType::CounterTrade) repliedTo(s, m, {MessageType::ProposeTrade, MessageType::CounterTrade});

    switch (m.type) {
        case MessageType::AcceptTreaty:
            if (const DiplomaticMessage* o = repliedTo(s, m, {MessageType::ProposeTreaty, MessageType::CounterTreaty})) acceptTreaty(ctx, *o);
            break;
        case MessageType::AcceptTrade:
            if (const DiplomaticMessage* o = repliedTo(s, m, {MessageType::ProposeTrade, MessageType::CounterTrade})) {
                const DiplomaticMessage copy = *o;
                acceptPackage(ctx, copy);
            }
            break;
        case MessageType::AcceptGift:
            if (const DiplomaticMessage* o = repliedTo(s, m, {MessageType::Gift, MessageType::Tribute})) {
                const DiplomaticMessage copy = *o;
                acceptPackage(ctx, copy);
            }
            break;
        case MessageType::AcceptDemand:
            // Answering a counter-proposal produces an Accept Demand reply.
            if (DiplomaticMessage* o = repliedTo(s, m, {MessageType::CounterTreaty, MessageType::CounterTrade})) {
                const DiplomaticMessage copy = *o;
                if (copy.type == MessageType::CounterTreaty) acceptTreaty(ctx, copy);
                else acceptPackage(ctx, copy);
            } else {
                repliedTo(s, m, {MessageType::DemandGift, MessageType::DemandTribute, MessageType::DemandSurrender,
                                 MessageType::DemandRemoveShips, MessageType::DemandRemoveColonies, MessageType::DemandLeavePlanet,
                                 MessageType::RequestStopHostilities, MessageType::RequestBreakTreaty, MessageType::RequestDeclareWar,
                                 MessageType::RequestMakePeace, MessageType::RequestSupport, MessageType::RequestAttackEmpire,
                                 MessageType::RequestAttackPlanet, MessageType::DemandStopEspionage, MessageType::DemandStopSabotage,
                                 MessageType::DemandStopAttacks, MessageType::General});
            }
            break;
        case MessageType::RefuseTreaty: repliedTo(s, m, {MessageType::ProposeTreaty, MessageType::CounterTreaty}); break;
        case MessageType::RefuseTrade: repliedTo(s, m, {MessageType::ProposeTrade, MessageType::CounterTrade}); break;
        case MessageType::RefuseGift: repliedTo(s, m, {MessageType::Gift, MessageType::Tribute}); break;
        case MessageType::BreakTreaty: {
            const Treaty current = s.empire(m.from).relation(m.to).treaty;
            if (current != Treaty::War && current != Treaty::None) setTreaty(ctx, m.from, m.to, Treaty::None);
            break;
        }
        case MessageType::DeclareWar: declareWar(ctx, m.from, m.to); break;
        case MessageType::Surrender:
            // With Allow Surrender off a Surrender message does nothing at all (spec 05 §7.4, confirmed: binary).
            if (s.options.allowSurrender) surrender(ctx, m.from, m.to);
            break;
        case MessageType::GrantIndependence: grantIndependence(ctx, m); break;
        default: break;
    }
    if (package) messageEntry();
}

} // namespace

// ---- Treaties and contact -------------------------------------------------------------------------

std::string_view treatyTrigger(Treaty t, bool dominant) {
    switch (t) {
        case Treaty::War: return "New Treaty War";
        case Treaty::NonIntercourse: return "New Treaty Non Intercourse";
        case Treaty::None: return "New Treaty None";
        case Treaty::NonAggression: return "New Treaty Non Aggression";
        case Treaty::Subjugation: return dominant ? "New Treaty Subjugated (Dom)" : "New Treaty Subjugated (Sub)";
        case Treaty::Protectorate: return dominant ? "New Treaty Protectorate (Dom)" : "New Treaty Protectorate (Sub)";
        case Treaty::TradeAlliance: return "New Treaty Trade";
        case Treaty::TradeResearchAlliance: return "New Treaty Trade and Research";
        case Treaty::MilitaryAlliance: return "New Treaty Military Alliance";
        case Treaty::Partnership: return "New Treaty Partnership";
        case Treaty::Count: break;
    }
    return {};
}

std::string treatyEnactedText(const GameState& s, EmpireId other, Treaty t, std::string_view role) {
    return std::format("Our treaty with the {} is now {}.{}", nameOf(s, other), displayName(t), role);
}

bool treatyEnactedWith(const GameState& s, const LogEntry& l, EmpireId other) {
    return l.title == "Treaty Enacted" && l.text.starts_with(std::format("Our treaty with the {} is now ", nameOf(s, other)));
}

void setTreaty(TurnContext& ctx, EmpireId a, EmpireId b, Treaty t, bool aDominant, TreatyEntry entry) {
    GameState& s = ctx.state;
    if (!validEmpire(s, a) || !validEmpire(s, b) || a == b || t >= Treaty::Count) return;
    const bool dominance = t == Treaty::Subjugation || t == Treaty::Protectorate;
    const bool aDom = dominance && aDominant;
    const bool bDom = dominance && !aDominant;
    // A subject may hold no treaty but the one with its master (spec 05 §3.2).
    if (t > Treaty::None)
        for (auto [party, other] : {std::pair{a, b}, std::pair{b, a}}) {
            const EmpireId master = masterOf(s, party);
            if (master.valid() && master != other && s.empire(party).relation(master).treaty == Treaty::Subjugation) {
                logGoto(ctx.log(party, LogCategory::Politics, "Treaty Not Possible",
                                std::format("As a subject of the {} we cannot sign a treaty with the {}.", nameOf(s, master), nameOf(s, other)),
                                std::nullopt, logpicture::race(other)),
                        LogGoto::Empires);
                return;
            }
        }
    Relation& ra = s.empire(a).relation(b);
    Relation& rb = s.empire(b).relation(a);
    if (ra.treaty == t && ra.dominant == aDom && rb.dominant == bDom) return;
    const Treaty old = ra.treaty;
    ra.treaty = rb.treaty = t;
    ra.dominant = aDom;
    rb.dominant = bDom;
    ra.treatyTurn = rb.treatyTurn = s.turn;
    if (ctx.hooks && old != t) {
        // A mod's event (hooks.hpp).
        HookArgs h;
        h.empire = a;
        h.other = b;
        h.treaty = t;
        h.oldTreaty = old;
        runHook(ctx, Hook::TreatyChanged, h);
    }
    if (t == Treaty::War) ra.lastWarTurn = rb.lastWarTurn = static_cast<int32_t>(s.turn);
    // The trade counter restarts unless the old and the new treaty both
    // trade (spec 05 §3.3, confirmed: binary).
    if (!(treatyTradesResources(old) && treatyTradesResources(t))) ra.tradeTurns = rb.tradeTurns = 0;

    ctx.mood(a, std::string(treatyTrigger(t, aDom)));
    ctx.mood(b, std::string(treatyTrigger(t, bDom)));
    auto role = [&](bool dom) { return !dominance ? std::string{} : dom ? std::string(" We are the dominant partner.") : std::string(" We are the subordinate partner."); };
    // Treaties enacted, lost or subjugations: Goto opens Empires (spec 06 §7 Q41).
    // A package's treaty is "Treaty Enacted" (spec 06 §7 Q70); an accepted
    // proposal logs nothing here.
    if (entry != TreatyEntry::None) {
        const std::string title = entry == TreatyEntry::Enacted ? "Treaty Enacted" : "New Treaty";
        logGoto(ctx.log(a, LogCategory::Politics, title, treatyEnactedText(s, b, t, role(aDom)), std::nullopt, logpicture::race(b)), LogGoto::Empires);
        logGoto(ctx.log(b, LogCategory::Politics, title, treatyEnactedText(s, a, t, role(bDom)), std::nullopt, logpicture::race(a)), LogGoto::Empires);
    }
    // The History window lists every treaty change under the other empire.
    auto history = [&](EmpireId other, bool dom) {
        const std::string them = nameOf(s, other);
        if (t == Treaty::War) return std::format("War with the {}", them);
        if (t == Treaty::None) return old == Treaty::War ? std::format("Peace with the {}", them) : std::format("Treaty with the {} ended", them);
        return std::format("{} with the {}{}", displayName(t), them, !dominance ? "" : dom ? " (we are the master)" : " (we are the subject)");
    };
    addHistory(s, a, b, history(b, aDom));
    addHistory(s, b, a, history(a, bDom));

    // A subject may hold no other treaty (spec 05 §3.2).
    if (t == Treaty::Subjugation) {
        const EmpireId subject = aDom ? b : a;
        const EmpireId master = aDom ? a : b;
        for (size_t i = 0; i < s.empires.size(); ++i) {
            const EmpireId x{i};
            if (x == subject || x == master) continue;
            if (s.empire(subject).relation(x).treaty <= Treaty::None) continue;
            setTreaty(ctx, subject, x, Treaty::None);
            logGoto(ctx.log(x, LogCategory::Politics, "Treaty Broken",
                            std::format("The {} ended its treaty with us on becoming a subject of the {}.", nameOf(s, subject), nameOf(s, master)),
                            std::nullopt, logpicture::race(subject)),
                    LogGoto::Empires);
        }
    }
}

bool inContact(const GameState& s, EmpireId a, EmpireId b) {
    return validEmpire(s, a) && validEmpire(s, b) && a != b && s.empire(a).relation(b).contact;
}

namespace {

// One side of a first contact: `a` has met `b` (its mark, its log entry and
// its history line).
void meetOneSide(TurnContext& ctx, EmpireId a, EmpireId b) {
    GameState& s = ctx.state;
    if (inContact(s, a, b)) return;
    s.empire(a).relation(b).contact = true;
    ctx.log(a, LogCategory::Politics, "First Contact", firstContactText(s, b), std::nullopt, logpicture::race(b));
    addHistory(s, a, b, std::format("First contact with the {}", nameOf(s, b)));
}

} // namespace

void makeContact(TurnContext& ctx, EmpireId a, EmpireId b) {
    GameState& s = ctx.state;
    if (!validEmpire(s, a) || !validEmpire(s, b) || a == b) return;
    meetOneSide(ctx, a, b);
    meetOneSide(ctx, b, a);
}

std::string firstContactText(const GameState& s, EmpireId other) { return std::format("We have made contact with the {}.", nameOf(s, other)); }

std::string contactLostText(const GameState& s, EmpireId other) {
    return std::format("No warp route links our colonies to the {} any more: we have lost contact with it.", nameOf(s, other));
}

std::vector<uint8_t> warpReach(const GameState& s, EmpireId e) {
    const size_t n = s.galaxy.systems.size();
    std::vector<uint8_t> reached(n, 0);
    std::vector<SystemId> frontier;
    for (const auto& c : s.colonies) {
        if (!c || c->owner != e) continue;
        const SystemId sys = s.galaxy.object(c->planet).system;
        if (sys.index() < n && !reached[sys.index()]) {
            reached[sys.index()] = 1;
            frontier.push_back(sys);
        }
    }
    while (!frontier.empty()) {
        const SystemId at = frontier.back();
        frontier.pop_back();
        for (SystemId next : s.galaxy.neighbors(at))
            if (next.index() < n && !reached[next.index()]) {
                reached[next.index()] = 1;
                frontier.push_back(next);
            }
    }
    return reached;
}

namespace {

bool colonyIn(const GameState& s, const std::vector<uint8_t>& reached, EmpireId owner) {
    for (const auto& c : s.colonies) {
        if (!c || c->owner != owner) continue;
        const SystemId sys = s.galaxy.object(c->planet).system;
        if (sys.index() < reached.size() && reached[sys.index()]) return true;
    }
    return false;
}

} // namespace

bool warpLinked(const GameState& s, EmpireId from, EmpireId to) {
    if (!validEmpire(s, from) || !validEmpire(s, to)) return false;
    return colonyIn(s, warpReach(s, from), to);
}

void declareWar(TurnContext& ctx, EmpireId from, EmpireId to) {
    GameState& s = ctx.state;
    if (!living(s, from) || !living(s, to) || from == to || s.empire(from).relation(to).treaty == Treaty::War) return;
    setTreaty(ctx, from, to, Treaty::War);
    // A treaty lost: Goto opens Empires (spec 06 §7 Q41).
    logGoto(ctx.log(from, LogCategory::Politics, "War Declared", std::format("We are now at war with the {}.", nameOf(s, to)), std::nullopt,
                    logpicture::race(to)),
            LogGoto::Empires);
    logGoto(ctx.log(to, LogCategory::Politics, "War Declared", std::format("The {} has declared war on us.", nameOf(s, from)), std::nullopt,
                    logpicture::race(from)),
            LogGoto::Empires);
}

void forgetEmpire(GameState& s, EmpireId gone) {
    if (!validEmpire(s, gone)) return;
    for (Empire& e : s.empires) {
        if (e.id == gone) continue;
        for (Relation* rel : {&e.relation(gone), &s.empire(gone).relation(e.id)}) {
            rel->contact = false;
            rel->treaty = Treaty::None;
            rel->dominant = false;
            rel->tradeTurns = 0;
        }
    }
}

EmpireId masterOf(const GameState& s, EmpireId e) {
    if (!validEmpire(s, e)) return {};
    const Empire& emp = s.empire(e);
    for (size_t i = 0; i < emp.relations.size() && i < s.empires.size(); ++i) {
        const Relation& rel = emp.relations[i];
        if ((rel.treaty == Treaty::Subjugation || rel.treaty == Treaty::Protectorate) && !rel.dominant &&
            s.empires[i].relation(e).dominant)
            return EmpireId{i};
    }
    return {};
}

bool treatyVisible(const GameState& s, EmpireId viewer, EmpireId a, EmpireId b) {
    if (!validEmpire(s, viewer) || !validEmpire(s, a) || !validEmpire(s, b)) return false;
    if (viewer == a || viewer == b) return true;
    return treatyAllowsResupply(s.empire(viewer).relation(a).treaty) || treatyAllowsResupply(s.empire(viewer).relation(b).treaty);
}

// ---- Ownership transfers ----------------------------------------------------------------------------

void transferColony(GameState& s, ObjectId planet, EmpireId to) {
    Colony* c = s.colony(planet);
    if (!c || !validEmpire(s, to) || c->owner == to) return;
    c->owner = to;
    c->homeworld = false;
    c->queue = ConstructionQueue{};  // the old owner's orders and designs do not carry over
    c->minister = false;
    explore(s, to, s.galaxy.object(planet).system);
}

void transferVehicle(GameState& s, VehicleId id, EmpireId to) {
    Vehicle* v = s.vehicle(id);
    if (!v || !validEmpire(s, to) || v->owner == to) return;
    effects::detachFromFleet(s, *v);
    v->owner = to;
    v->orders.clear();
    v->repeatOrders = false;
    v->minister = false;
    explore(s, to, v->location.system);
}

bool isPlaceholder(const PackageItem& item) {
    switch (item.kind) {
        case PackageItem::Kind::Resources:
        case PackageItem::Kind::Treaty: return false;
        case PackageItem::Kind::Technology: return !item.tech.valid();
        case PackageItem::Kind::Planet: return !item.planet.valid();
        case PackageItem::Kind::Vehicle: return !item.vehicle.valid();
        case PackageItem::Kind::StarChart:
        case PackageItem::Kind::System: return !item.system.valid();
        case PackageItem::Kind::CommChannel: return !item.empire.valid();
    }
    return true;
}

void executePackage(TurnContext& ctx, EmpireId giver, EmpireId receiver, std::span<const PackageItem> items) {
    const Rules& r = ctx.rules;
    GameState& s = ctx.state;
    if (!living(s, giver) || !living(s, receiver) || giver == receiver) return;
    const std::string giverName = nameOf(s, giver), receiverName = nameOf(s, receiver);
    // Every entry is category Politics; the receiver's comes first (spec 06 §7 Q70).
    // GiftReceived for the receiver, GiftGiven for the giver (spec 06 §4.1).
    auto entry = [&](EmpireId e, std::string title, std::string text, LogGoto target, std::optional<Location> where = std::nullopt) {
        if (LogEntry* l = ctx.log(e, LogCategory::Politics, std::move(title), std::move(text), where, e == receiver ? "GiftReceived" : "GiftGiven"))
            l->target = target;
    };
    for (const PackageItem& item : items) {
        switch (item.kind) {
            case PackageItem::Kind::Resources: {
                // The package's amount, at most what the giver holds; both
                // entries are made even when nothing moves.
                Empire& g = s.empire(giver);
                const Resources moved = min(max(item.resources, Resources{}), max(g.stockpile, Resources{}));
                g.stockpile -= moved;
                s.empire(receiver).stockpile += moved;
                std::string amounts;
                for (Resource k : kResources) {
                    if (item.resources[k] <= 0) continue;
                    if (!amounts.empty()) amounts += ", ";
                    amounts += std::format("{} {}", moved[k], displayName(k));
                }
                if (amounts.empty()) amounts = "no resources";
                entry(receiver, "Resources Received", std::format("The {} has sent us {}.", giverName, amounts), LogGoto::Empires);
                entry(giver, "Resources Transfered", std::format("We have sent the {} {}.", receiverName, amounts), LogGoto::Empires);
                break;
            }
            case PackageItem::Kind::Technology: {
                if (!item.tech.valid() || item.tech.index() >= r.data().techAreas.size()) break;
                // No game option is tested here (Allow Technology Gifts
                // included). The data is of no use when the receiver cannot
                // gain a level in the area (its racial and unique checks) or
                // the giver is not ahead of it.
                const std::string& area = r.tech(item.tech).name;
                const int level = std::min(s.empire(giver).techLevel(item.tech), r.tech(item.tech).maxLevel);
                const bool useful = research::canGainLevel(r, s, s.empire(receiver), item.tech) && level > s.empire(receiver).techLevel(item.tech);
                entry(receiver, "Technology Received",
                      std::format("The {} has sent us its data on {}.{}", giverName, area, useful ? "" : " It teaches us nothing we can use."),
                      LogGoto::Research);
                entry(giver, "Technology Transfered", std::format("We have sent the {} our data on {}.", receiverName, area), LogGoto::Research);
                // A level gained then makes its usual Research entries.
                research::grantLevel(ctx, receiver, item.tech, level, "trade");
                break;
            }
            case PackageItem::Kind::Planet: {
                // Only a colony the giver still holds; otherwise nothing at all.
                const Colony* c = s.colony(item.planet);
                if (!c || c->owner != giver) break;
                // The receiver learns the designs of the units in the planet's
                // cargo (spec 05 §8 "Design knowledge", confirmed: binary).
                for (const UnitStack& u : c->cargo.units) learnForeign(s, receiver, u.design);
                transferColony(s, item.planet, receiver);
                const Location where = locationOf(s.galaxy, item.planet);
                const std::string& planet = s.galaxy.object(item.planet).name;
                entry(receiver, "Planet Received", std::format("The {} has handed {} over to us.", giverName, planet), LogGoto::Location, where);
                entry(giver, "Planet Transfered", std::format("We have handed {} over to the {}.", planet, receiverName), LogGoto::Location, where);
                // The system is now explored for the receiver: the first-contact check runs there (spec 05 §3.1).
                firstContactIn(ctx, where.system);
                break;
            }
            case PackageItem::Kind::Vehicle: {
                const Vehicle* v = s.vehicle(item.vehicle);
                if (!v || v->count <= 0 || v->owner != giver) break;
                // The ship's design (every design of a unit group) and the
                // designs of the units in its cargo.
                for (const UnitStack& st : groupStacks(*v)) learnForeign(s, receiver, st.design);
                for (const UnitStack& u : v->cargo.units) learnForeign(s, receiver, u.design);
                transferVehicle(s, item.vehicle, receiver);
                const Vehicle& moved = *s.vehicle(item.vehicle);
                entry(receiver, "Vehicle Received", std::format("The {} has handed the {} over to us.", giverName, moved.name), LogGoto::Location,
                      moved.location);
                entry(giver, "Vehicle Transfered", std::format("We have handed the {} over to the {}.", moved.name, receiverName), LogGoto::Location,
                      moved.location);
                firstContactIn(ctx, moved.location.system);  // as for a planet (spec 05 §3.1)
                break;
            }
            case PackageItem::Kind::StarChart: {
                // The system becomes explored for the receiver; nothing tests the
                // giver's own exploration and no warp link passes. Only the
                // receiver is told; Goto shows the system, no sector.
                if (!item.system.valid() || item.system.index() >= s.galaxy.systems.size()) break;
                explore(s, receiver, item.system);
                entry(receiver, "Starcharts Received",
                      std::format("The {} has sent us its star charts of the {} system.", giverName, s.galaxy.system(item.system).name),
                      LogGoto::Location, Location{item.system, Sector{-1, -1}});
                break;
            }
            case PackageItem::Kind::Treaty: {
                // The package names the dominant side of a Subjugation or
                // Protectorate in `empire`; by default the receiver (inferred).
                // Both parties log "Treaty Enacted" (spec 06 §7 Q70).
                const EmpireId master = item.empire == giver ? giver : receiver;
                setTreaty(ctx, receiver, giver, item.treaty, master == receiver, TreatyEntry::Enacted);
                break;
            }
            case PackageItem::Kind::CommChannel: {
                // Only when the receiver has no contact with C: both are set to
                // treaty None, without a first-contact check or entry. Nothing
                // tests the giver's contact with C or that C is alive.
                const EmpireId c = item.empire;
                if (!validEmpire(s, c) || c == receiver || inContact(s, receiver, c)) break;
                for (auto [x, y] : {std::pair{receiver, c}, std::pair{c, receiver}}) {
                    Relation& rel = s.empire(x).relation(y);
                    rel.contact = true;
                    rel.treaty = Treaty::None;
                    rel.dominant = false;
                }
                entry(receiver, "Comm Channels Received",
                      std::format("The {} has opened communication channels between us and the {}.", giverName, nameOf(s, c)), LogGoto::Empires);
                entry(c, "Comm Channels Opened",
                      std::format("The {} has opened communication channels between us and the {}.", giverName, receiverName), LogGoto::Empires);
                break;
            }
            case PackageItem::Kind::System: {
                // A border claim: the giver's claim goes and the receiver claims
                // the system; no entry.
                if (!item.system.valid() || item.system.index() >= s.galaxy.systems.size()) break;
                std::erase(s.empire(giver).claimedSystems, item.system);
                std::vector<SystemId>& claims = s.empire(receiver).claimedSystems;
                if (!std::binary_search(claims.begin(), claims.end(), item.system))
                    claims.insert(std::upper_bound(claims.begin(), claims.end(), item.system), item.system);
                break;
            }
        }
    }
}

void surrender(TurnContext& ctx, EmpireId from, EmpireId to) {
    const Rules& r = ctx.rules;
    GameState& s = ctx.state;
    if (!living(s, from) || !living(s, to) || from == to) return;
    // With Allow Surrender off the message does nothing (spec 05 §3.4, §7.4, confirmed: binary).
    if (!s.options.allowSurrender) return;
    // Every object passes, and every system where one lies becomes explored
    // for the recipient; no other map knowledge passes.
    for (auto& c : s.colonies)
        if (c && c->owner == from) transferColony(s, c->planet, to);
    for (Fleet& f : s.fleets)
        if (f.owner == from) f.owner = to;  // its members' copies of its orders are cleared below
    for (Vehicle& v : s.vehicles)
        if (v.owner == from) {
            v.owner = to;
            v.orders.clear();
            v.repeatOrders = false;
            explore(s, to, v.location.system);
        }
    // The minerals, organics and radioactives pass; research and intelligence points do not.
    Empire& loser = s.empire(from);
    s.empire(to).stockpile += max(loser.stockpile, Resources{});
    loser.stockpile = {};
    // Every design of the surrendering empire, and every design it knew whose
    // owner can still build it, dated with the surrender turn (spec 05 §8).
    for (DesignId d : s.empire(from).designs) learnForeign(s, to, d);
    for (DesignId d : seenDesignIds(s.empire(from).knowledge)) {
        if (d.index() >= s.designs.size()) continue;
        const Design& design = s.design(d);
        if (design.owner.valid() && design.owner.index() < s.empires.size() && r.designTechnology(s.empire(design.owner), design))
            learnForeign(s, to, d);
    }
    // Exactly one level in every area where the recipient is behind, that is
    // allowed and passes the racial and unique checks for both empires, and
    // whose requirements the surrendering empire meets.
    for (uint32_t i = 0; i < r.data().techAreas.size(); ++i) {
        const ruleset::TechAreaId a{i};
        const Empire& giver = s.empire(from);
        const Empire& taker = s.empire(to);
        if (taker.techLevel(a) >= giver.techLevel(a)) continue;
        if (!research::canGainLevel(r, s, giver, a) || !research::canGainLevel(r, s, taker, a) || !r.meets(giver, r.tech(a).requirements))
            continue;
        research::grantLevel(ctx, to, a, taker.techLevel(a) + 1, "surrender");
    }
    // Both sides and the living third empires in contact with either side are
    // told. Nothing else happens: the sender, owning nothing, is destroyed at
    // its next destruction check (spec 05 §6).
    const std::string text = std::format("The {} has surrendered to the {}.", nameOf(s, from), nameOf(s, to));
    for (const Empire& e : s.empires) {
        const bool party = e.id == from || e.id == to;
        if (!party && !(e.alive && (inContact(s, e.id, from) || inContact(s, e.id, to)))) continue;
        // The other empire's race portrait; for a witness, the one that surrendered (inferred).
        logGoto(ctx.log(e.id, LogCategory::Politics, "Surrender", text, std::nullopt, logpicture::race(e.id == from ? to : from)), LogGoto::Empires);
        addHistory(s, e.id, e.id == from ? to : from, std::format("The {} surrendered to the {}", nameOf(s, from), nameOf(s, to)));
    }
    // A surrender runs the first-contact check in every system (spec 05 §3.1, confirmed: binary).
    firstContactEverywhere(ctx);
}

// ---- Turn phases ------------------------------------------------------------------------------------

void deliverMessages(TurnContext& ctx, std::optional<uint32_t> date) {
    GameState& s = ctx.state;
    std::vector<MessageId> lost;
    const size_t n = s.messages.size();
    for (size_t i = 0; i < n; ++i) {
        if (s.messages[i].delivered) continue;
        s.messages[i].delivered = true;
        s.messages[i].dated = date.value_or(s.turn);
        const DiplomaticMessage& m = s.messages[i];
        if (!living(s, m.from) || !living(s, m.to) || m.from == m.to) {
            lost.push_back(m.id);
            continue;
        }
        if (!answerable(m.type)) s.messages[i].answered = true;
        receive(ctx, s.messages[i]);
        if (ctx.hooks) {
            // A mod's event (hooks.hpp): the message reached its empire.
            HookArgs a;
            a.empire = s.messages[i].from;
            a.other = s.messages[i].to;
            a.message = s.messages[i].id;
            runHook(ctx, Hook::MessageSent, a);
        }
    }
    std::erase_if(s.messages, [&](const DiplomaticMessage& m) {
        if (std::find(lost.begin(), lost.end(), m.id) != lost.end()) return true;
        if (!living(s, m.from) || !living(s, m.to)) return true;
        return m.delivered && m.sentTurn + kMessageLifetime < s.turn;
    });
}

namespace {

// Whether `viewer` detects some object of `owner` in `sys` now: a vehicle
// there, or a colony there, that passes the detection rule of spec 01 §6.3
// by the current positions and sensors (a cloaked colony's cloak levels
// included, spec 01 §6.9).
bool detectsIn(const Rules& r, const GameState& s, EmpireId viewer, EmpireId owner, SystemId sys) {
    for (const Vehicle& v : s.vehicles)
        if (v.count > 0 && v.owner == owner && v.location.system == sys && sight::canSeeVehicle(r, s, viewer, v)) return true;
    for (ObjectId o : s.galaxy.system(sys).objects)
        if (const Colony* c = s.colony(o); c && c->owner == owner && sight::canSeeColony(r, s, viewer, o)) return true;
    return false;
}

} // namespace

void firstContactIn(TurnContext& ctx, SystemId sys, EmpireId onlySide) {
    GameState& s = ctx.state;
    if (!sys.valid() || sys.index() >= s.galaxy.systems.size()) return;
    const size_t n = s.empires.size();
    // The living empires with an object in the system: only they can detect
    // each other there.
    std::vector<uint8_t> here(n, 0);
    for (const Vehicle& v : s.vehicles)
        if (v.count > 0 && v.location.system == sys && living(s, v.owner)) here[v.owner.index()] = 1;
    for (ObjectId o : s.galaxy.system(sys).objects)
        if (const Colony* c = s.colony(o); c && living(s, c->owner)) here[c->owner.index()] = 1;
    // Contact needs mutual detection in the system and a warp path from each
    // side's colonies to a colony of the other (spec 05 §3.1, confirmed:
    // binary), the test the contact check repeats every turn. Every warp link
    // works both ways (spec 01 §3.5), so the two directions agree; checking
    // both keeps a new contact from being lost at the next check.
    std::vector<std::optional<std::vector<uint8_t>>> reach(n);
    auto linked = [&](size_t from, size_t to) {
        if (!reach[from]) reach[from] = warpReach(s, EmpireId{from});
        return colonyIn(s, *reach[from], EmpireId{to});
    };
    // A pair where one side has met the other already (a one-sided contact,
    // below) is completed by the next check in a system where both detect
    // each other.
    for (size_t a = 0; a < n; ++a) {
        if (!here[a]) continue;
        for (size_t b = a + 1; b < n; ++b) {
            if (!here[b] || !s.empires[a].alive || !s.empires[b].alive) continue;
            if (onlySide.valid() && onlySide.index() != a && onlySide.index() != b) continue;
            if (s.empires[a].relations[b].contact && s.empires[b].relations[a].contact) continue;
            const EmpireId ea{a}, eb{b};
            if (!detectsIn(ctx.rules, s, ea, eb, sys) || !detectsIn(ctx.rules, s, eb, ea, sys)) continue;
            if (!linked(a, b) || !linked(b, a)) continue;
            if (onlySide.valid()) meetOneSide(ctx, onlySide, onlySide == ea ? eb : ea);
            else makeContact(ctx, ea, eb);
        }
    }
}

void firstContactEverywhere(TurnContext& ctx) {
    for (size_t i = 0; i < ctx.state.galaxy.systems.size(); ++i) firstContactIn(ctx, SystemId{i});
}

void afterDecloak(TurnContext& ctx, SystemId sys, EmpireId onlySide) {
    sight::updateKnowledge(ctx.rules, ctx.state);
    firstContactIn(ctx, sys, onlySide);
}

bool recalculateColony(TurnContext& ctx, Colony& c, EmpireId onlySide) {
    if (!sight::recalculateColony(ctx.rules, c)) return false;
    // The automatic decloak is the Decloak order's own step (spec 01 §6.9, §14 Q44, confirmed: binary).
    afterDecloak(ctx, ctx.state.galaxy.object(c.planet).system, onlySide);
    return true;
}

void recalculateColonies(const Rules& r, GameState& s) {
    // Every colony's levels first; then each automatic decloak's step: sight
    // once, and the first-contact check in each system where a colony
    // decloaked, in the order the colonies decloaked (inferred).
    std::vector<SystemId> decloakedIn;
    for (auto& c : s.colonies)
        if (c && sight::recalculateColony(r, *c)) {
            const SystemId sys = s.galaxy.object(c->planet).system;
            if (std::find(decloakedIn.begin(), decloakedIn.end(), sys) == decloakedIn.end()) decloakedIn.push_back(sys);
        }
    if (decloakedIn.empty()) return;
    sight::updateKnowledge(r, s);
    TurnContext ctx{r, s, {}, {}, {}};
    for (SystemId sys : decloakedIn) firstContactIn(ctx, sys);
}

void checkContacts(TurnContext& ctx) {
    GameState& s = ctx.state;
    const size_t n = s.empires.size();
    for (size_t a = 0; a < n; ++a) {
        if (!s.empires[a].alive) continue;
        const EmpireId id{a};
        std::optional<std::vector<uint8_t>> reached;
        for (size_t b = 0; b < n; ++b) {
            const EmpireId other{b};
            if (b == a || !s.empires[a].relations[b].contact) continue;
            if (!reached) reached = warpReach(s, id);
            if (colonyIn(s, *reached, other)) continue;
            // Our side only returns to "no contact"; the anger stays.
            Relation& rel = s.empires[a].relations[b];
            rel.contact = false;
            rel.treaty = Treaty::None;
            rel.dominant = false;
            rel.treatyTurn = s.turn;
            std::erase_if(s.empires[a].intel, [&](const IntelProjectOrder& o) { return o.target == other; });
            logGoto(ctx.log(id, LogCategory::Politics, "Contact Lost", contactLostText(s, other), std::nullopt, logpicture::race(other)), LogGoto::Empires);
            addHistory(s, id, other, std::format("Lost contact with the {}", nameOf(s, other)));
        }
    }
}

void treatyStep(TurnContext& ctx, EmpireId id) {
    GameState& s = ctx.state;
    if (!living(s, id)) return;
    const size_t n = s.empires.size();
    auto others = [&](auto&& fn) {
        for (size_t i = 0; i < n; ++i)
            if (const EmpireId other{i}; other != id && s.empire(other).alive) fn(other);
    };
    // 1. Consistency check (confirmed: binary): mismatched records fall to None.
    others([&](EmpireId other) {
        Relation& ours = s.empire(id).relation(other);
        Relation& theirs = s.empire(other).relation(id);
        if (ours.treaty == theirs.treaty) return;
        ours.treaty = theirs.treaty = Treaty::None;
        ours.dominant = theirs.dominant = false;
        ours.tradeTurns = theirs.tradeTurns = 0;
    });
    // 2. A master learns every design of its subject that it does not know
    // and the subject can build, dated with the design's creation date (spec
    // 05 §8 "Design knowledge", confirmed: binary): one made more than 50
    // turns ago is forgotten again at step 12.
    others([&](EmpireId other) {
        const Relation& rel = s.empire(id).relation(other);
        if (rel.treaty != Treaty::Subjugation || !rel.dominant) return;
        const Empire& subject = s.empire(other);
        Knowledge& known = s.empire(id).knowledge;
        for (DesignId d : subject.designs) {
            if (d.index() >= s.designs.size() || knowsDesign(known, d)) continue;
            const Design& design = s.design(d);
            if (ctx.rules.designTechnology(subject, design)) seeDesign(known, d, design.createdTurn);
        }
    });
    // 3. Trade income from every partner, at the trade percentage the counters
    // give before they grow (spec 05 §3.3).
    economy::collectTrade(ctx, id);
    // 4. Partnership: the partner's explored systems and seen designs.
    others([&](EmpireId other) {
        if (s.empire(id).relation(other).treaty != Treaty::Partnership) return;
        if (shareMap(s, other, id) > 0)
            ctx.log(id, LogCategory::Misc, "New System Maps Available", std::format("The {} has shared its star charts with us.", nameOf(s, other)),
                    std::nullopt, "GiftReceived");
        shareSeenDesigns(s, other, id);
    });
    // 5. The trade counter grows toward every other living empire, whatever the treaty.
    others([&](EmpireId other) { ++s.empire(id).relation(other).tradeTurns; });
}

// ---- Trade and tariffs ------------------------------------------------------------------------------

Generated generated(const Rules& r, const GameState& s, EmpireId e) {
    // What the colonies deliver, system by system (spec 02 §5.5): the figure
    // the empire's own income starts from.
    const economy::Production p = economy::empireProduction(r, s, e);
    return {p.resources, p.research, p.intelligence};
}

int tradePercent(const Rules& r, const GameState& s, EmpireId e, EmpireId partner) {
    if (!validEmpire(s, e) || !validEmpire(s, partner) || e == partner) return 0;
    const Relation& rel = s.empire(e).relation(partner);
    if (!treatyTradesResources(rel.treaty)) return 0;
    return static_cast<int>(std::clamp<int64_t>(rel.tradeTurns, 0, r.setting("Maximum Trade Percentage", 20)));
}

int64_t tradeFactor(const Rules& r, const Empire& receiver) {
    const ruleset::Culture* c = r.culture(receiver.race);
    return int64_t{100} + (receiver.race.characteristic(Characteristic::PoliticalSavvy) - 100) + r.traitValue(receiver.race, "Trade") +
           (c ? c->trade : 0);
}

int64_t tradeShare(int64_t partnerBase, int tradePercent, int64_t factor) {
    if (partnerBase <= 0 || tradePercent <= 0 || factor <= 0) return 0;
    return std::max<int64_t>(0, xmath::pctTrunc(xmath::pctRound(partnerBase, tradePercent), factor));
}

namespace {

// Trade partners of `e` whose treaty `qualifies`, with the trade percentage.
template <class Fn>
void forTradePartners(const Rules& r, const GameState& s, EmpireId e, bool (*qualifies)(Treaty), Fn&& fn) {
    if (!living(s, e)) return;
    const Empire& emp = s.empire(e);
    for (size_t i = 0; i < s.empires.size() && i < emp.relations.size(); ++i) {
        const EmpireId partner{i};
        if (partner == e || !s.empires[i].alive || !qualifies(emp.relations[i].treaty)) continue;
        const int pct = tradePercent(r, s, e, partner);
        if (pct > 0) fn(partner, pct);
    }
}

bool partnership(Treaty t) { return t == Treaty::Partnership; }
bool tradesResources(Treaty t) { return treatyTradesResources(t); }
bool tradesResearch(Treaty t) { return treatyTradesResearch(t); }

} // namespace

Resources tradeIncome(const Rules& r, const GameState& s, EmpireId e) {
    Resources total = tariffsReceived(r, s, e);
    if (!living(s, e)) return total;
    const int64_t f = tradeFactor(r, s.empire(e));
    forTradePartners(r, s, e, tradesResources, [&](EmpireId partner, int pct) {
        const Generated g = generated(r, s, partner);
        for (Resource res : kResources) total[res] += tradeShare(g.resources[res], pct, f);
    });
    return total;
}

Resources tariffsReceived(const Rules& r, const GameState& s, EmpireId e) {
    Resources total;
    if (!living(s, e)) return total;
    for (const Empire& x : s.empires)
        if (x.alive && x.id != e && masterOf(s, x.id) == e) total += tariffsPaid(r, s, x.id);
    return total;
}

int64_t researchTradeIncome(const Rules& r, const GameState& s, EmpireId e) {
    int64_t total = 0;
    if (!living(s, e)) return total;
    const int64_t f = tradeFactor(r, s.empire(e));
    forTradePartners(r, s, e, tradesResearch, [&](EmpireId partner, int pct) { total += tradeShare(generated(r, s, partner).research, pct, f); });
    return total;
}

int64_t intelTradeIncome(const Rules& r, const GameState& s, EmpireId e) {
    int64_t total = 0;
    if (!living(s, e)) return total;
    const int64_t f = tradeFactor(r, s.empire(e));
    forTradePartners(r, s, e, partnership, [&](EmpireId partner, int pct) { total += tradeShare(generated(r, s, partner).intelligence, pct, f); });
    return total;
}

Generated tariffOn(const Rules& r, const GameState& s, EmpireId e, const Generated& income) {
    Generated due;
    const EmpireId master = masterOf(s, e);
    if (!master.valid() || !s.empire(master).alive) return due;
    const Treaty t = s.empire(e).relation(master).treaty;
    const int64_t pct = t == Treaty::Subjugation ? r.setting("Treaty Subjugated Resource Percentage", 40)
                                                 : r.setting("Treaty Protectorate Resource Percentage", 20);
    auto cut = [&](int64_t amount) { return amount <= 0 ? int64_t{0} : std::clamp<int64_t>(xmath::pctRound(amount, pct), 0, amount); };
    for (Resource res : kResources) due.resources[res] = cut(income.resources[res]);
    due.research = cut(income.research);
    due.intelligence = cut(income.intelligence);
    return due;
}

Generated tariffDue(const Rules& r, const GameState& s, EmpireId e) {
    if (!masterOf(s, e).valid()) return {};
    const economy::Production income = economy::nonTradeIncome(r, s, e);
    return tariffOn(r, s, e, {income.resources, income.research, income.intelligence});
}

Resources tariffsPaid(const Rules& r, const GameState& s, EmpireId e) { return tariffDue(r, s, e).resources; }

} // namespace opense4::game::diplomacy
