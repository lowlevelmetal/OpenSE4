#include "game/diplomacy.hpp"

#include "game/economy.hpp"
#include "game/events.hpp"
#include "game/query.hpp"
#include "game/research.hpp"
#include "game/turn.hpp"

#include <algorithm>
#include <format>
#include <iterator>
#include <numeric>

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

// Acts that take effect whatever happens to the message channel.
bool unilateral(MessageType t) {
    return t == MessageType::DeclareWar || t == MessageType::BreakTreaty || t == MessageType::Surrender;
}

void learnDesign(Empire& e, DesignId d) {
    auto& seen = e.knowledge.seenDesigns;
    auto it = std::lower_bound(seen.begin(), seen.end(), d);
    if (it == seen.end() || *it != d) seen.insert(it, d);
}

void explore(GameState& s, EmpireId e, SystemId sys) {
    if (!validEmpire(s, e) || !sys.valid() || sys.index() >= s.galaxy.systems.size()) return;
    auto& explored = s.empire(e).knowledge.explored;
    if (explored.size() < s.galaxy.systems.size()) explored.resize(s.galaxy.systems.size(), 0);
    explored[sys.index()] = 1;
}

// Everything `from` knows about the galaxy becomes known to `to` too.
void shareMap(GameState& s, EmpireId from, EmpireId to) {
    const Knowledge& a = s.empire(from).knowledge;
    Knowledge& b = s.empire(to).knowledge;
    if (b.explored.size() < a.explored.size()) b.explored.resize(a.explored.size(), 0);
    for (size_t i = 0; i < a.explored.size(); ++i) b.explored[i] |= a.explored[i];
    if (b.knownWarpLink.size() < a.knownWarpLink.size()) b.knownWarpLink.resize(a.knownWarpLink.size(), 0);
    for (size_t i = 0; i < a.knownWarpLink.size(); ++i) b.knownWarpLink[i] |= a.knownWarpLink[i];
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

std::string joinLines(const std::vector<std::string>& lines) {
    std::string out;
    for (const auto& l : lines) {
        if (!out.empty()) out += '\n';
        out += l;
    }
    return out;
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
    setTreaty(ctx, proposal.from, proposal.to, proposal.treaty, master == proposal.from);
}

void acceptPackage(TurnContext& ctx, const DiplomaticMessage& offer) {
    GameState& s = ctx.state;
    const bool gift = offer.type == MessageType::Gift || offer.type == MessageType::Tribute;
    const std::string what = gift ? std::string(displayName(offer.type)) : std::string("Trade");
    if (gift && !s.options.allowGifts) {
        for (EmpireId e : {offer.from, offer.to})
            ctx.log(e, LogCategory::Politics, what + " Cancelled", "Gifts and tributes are not allowed in this game.");
        return;
    }
    const bool placeholders = std::any_of(offer.offer.begin(), offer.offer.end(), isPlaceholder) ||
                              std::any_of(offer.request.begin(), offer.request.end(), isPlaceholder);
    if (placeholders) {
        for (EmpireId e : {offer.from, offer.to})
            ctx.log(e, LogCategory::Politics, what + " Cancelled", "The package still holds unspecified (\"Any\") items; counter with real items.");
        return;
    }
    std::vector<std::string> lines;
    const std::string fromName = nameOf(s, offer.from), toName = nameOf(s, offer.to);
    if (!offer.offer.empty()) {
        lines.push_back(std::format("From the {} to the {}:", fromName, toName));
        executePackage(ctx, offer.from, offer.to, offer.offer);
        for (const auto& item : offer.offer) lines.push_back("  " + describe(s, ctx.rules, item));
    }
    if (!offer.request.empty()) {
        lines.push_back(std::format("From the {} to the {}:", toName, fromName));
        executePackage(ctx, offer.to, offer.from, offer.request);
        for (const auto& item : offer.request) lines.push_back("  " + describe(s, ctx.rules, item));
    }
    for (EmpireId e : {offer.from, offer.to}) ctx.log(e, LogCategory::Politics, what + " Completed", joinLines(lines));
}

void grantIndependence(TurnContext& ctx, const DiplomaticMessage& m) {
    GameState& s = ctx.state;
    const Colony* c = s.colony(m.planet);
    if (!c || c->owner != m.from) return;
    const std::string planet = s.galaxy.object(m.planet).name;
    // The sender evacuates and abandons the planet; the recipient may settle it (inferred).
    s.colonies[m.planet.index()].reset();
    ctx.log(m.from, LogCategory::Politics, "Independence Granted",
            std::format("We have withdrawn from {} so that the {} may settle it.", planet, nameOf(s, m.to)));
    ctx.log(m.to, LogCategory::Politics, "Independence Granted",
            std::format("The {} has abandoned {} for us to settle.", nameOf(s, m.from), planet), locationOf(s.galaxy, m.planet));
}

void receive(TurnContext& ctx, DiplomaticMessage& stored) {
    GameState& s = ctx.state;
    const DiplomaticMessage m = stored;
    const std::string sender = nameOf(s, m.from);
    std::string text = std::format("From the {}.", sender);
    if (m.type == MessageType::ProposeTreaty || m.type == MessageType::CounterTreaty)
        text += std::format(" Proposed treaty: {}.", displayName(m.treaty));
    if (!m.offer.empty() || !m.request.empty()) {
        for (const auto& item : m.offer) text += std::format("\nOffered: {}", describe(s, ctx.rules, item));
        for (const auto& item : m.request) text += std::format("\nRequested: {}", describe(s, ctx.rules, item));
    }
    if (!m.text.empty()) text += "\n" + m.text;
    ctx.log(m.to, LogCategory::Politics, std::string(displayName(m.type)), text);

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
        case MessageType::DeclareWar:
            if (s.empire(m.from).relation(m.to).treaty != Treaty::War) {
                setTreaty(ctx, m.from, m.to, Treaty::War);
                ctx.log(m.from, LogCategory::Politics, "War Declared", std::format("We are now at war with the {}.", nameOf(s, m.to)));
                ctx.log(m.to, LogCategory::Politics, "War Declared", std::format("The {} has declared war on us.", sender));
            }
            break;
        case MessageType::Surrender: surrender(ctx, m.from, m.to); break;
        case MessageType::GrantIndependence: grantIndependence(ctx, m); break;
        default: break;
    }
}

// Union-find over systems joined by warp points (either direction).
struct Components {
    std::vector<uint32_t> parent;
    explicit Components(const Galaxy& g) : parent(g.systems.size()) {
        std::iota(parent.begin(), parent.end(), 0u);
        for (const SpaceObject& o : g.objects)
            if (o.kind == ObjectKind::WarpPoint && o.destination.valid() && o.destination.index() < g.objects.size()) {
                const auto& sysObjs = g.system(o.system).objects;
                if (std::find(sysObjs.begin(), sysObjs.end(), o.id) == sysObjs.end()) continue;  // closed
                join(o.system.value, g.object(o.destination).system.value);
            }
    }
    uint32_t find(uint32_t x) {
        while (parent[x] != x) x = parent[x] = parent[parent[x]];
        return x;
    }
    void join(uint32_t a, uint32_t b) {
        a = find(a);
        b = find(b);
        if (a != b) parent[std::max(a, b)] = std::min(a, b);
    }
};

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

void setTreaty(TurnContext& ctx, EmpireId a, EmpireId b, Treaty t, bool aDominant) {
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
                ctx.log(party, LogCategory::Politics, "Treaty Not Possible",
                        std::format("As a subject of the {} we cannot sign a treaty with the {}.", nameOf(s, master), nameOf(s, other)));
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
    if (t == Treaty::War) ra.lastWarTurn = rb.lastWarTurn = static_cast<int32_t>(s.turn);
    // Trade continues only between two trade-level treaties (spec 05 §3.3).
    if (!(treatyTradesResources(old) && treatyTradesResources(t))) ra.tradePercent = rb.tradePercent = 0;

    ctx.mood(a, std::string(treatyTrigger(t, aDom)));
    ctx.mood(b, std::string(treatyTrigger(t, bDom)));
    auto role = [&](bool dom) { return !dominance ? std::string{} : dom ? std::string(" We are the dominant partner.") : std::string(" We are the subordinate partner."); };
    ctx.log(a, LogCategory::Politics, "New Treaty", std::format("Our treaty with the {} is now {}.{}", nameOf(s, b), displayName(t), role(aDom)));
    ctx.log(b, LogCategory::Politics, "New Treaty", std::format("Our treaty with the {} is now {}.{}", nameOf(s, a), displayName(t), role(bDom)));

    // A subject may hold no other treaty (spec 05 §3.2).
    if (t == Treaty::Subjugation) {
        const EmpireId subject = aDom ? b : a;
        const EmpireId master = aDom ? a : b;
        for (size_t i = 0; i < s.empires.size(); ++i) {
            const EmpireId x{i};
            if (x == subject || x == master) continue;
            if (s.empire(subject).relation(x).treaty <= Treaty::None) continue;
            setTreaty(ctx, subject, x, Treaty::None);
            ctx.log(x, LogCategory::Politics, "Treaty Broken",
                    std::format("The {} ended its treaty with us on becoming a subject of the {}.", nameOf(s, subject), nameOf(s, master)));
        }
    }
}

bool inContact(const GameState& s, EmpireId a, EmpireId b) {
    return validEmpire(s, a) && validEmpire(s, b) && a != b && s.empire(a).relation(b).contact;
}

void makeContact(TurnContext& ctx, EmpireId a, EmpireId b) {
    GameState& s = ctx.state;
    if (!validEmpire(s, a) || !validEmpire(s, b) || a == b || inContact(s, a, b)) return;
    s.empire(a).relation(b).contact = true;
    s.empire(b).relation(a).contact = true;
    ctx.log(a, LogCategory::Politics, "First Contact", std::format("We have made contact with the {}.", nameOf(s, b)));
    ctx.log(b, LogCategory::Politics, "First Contact", std::format("We have made contact with the {}.", nameOf(s, a)));
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
    v->targetVehicle = {};
    v->targetObject = {};
    if (s.design(v->design).owner != to) learnDesign(s.empire(to), v->design);
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
    std::vector<std::string> unavailable;
    for (const PackageItem& item : items) {
        Empire& g = s.empire(giver);
        Empire& rcv = s.empire(receiver);
        switch (item.kind) {
            case PackageItem::Kind::Resources: {
                // Only what the giver actually holds moves.
                const Resources moved = min(max(item.resources, Resources{}), max(g.stockpile, Resources{}));
                g.stockpile -= moved;
                rcv.stockpile += moved;
                break;
            }
            case PackageItem::Kind::Technology: {
                if (!s.options.allowTechTrades) {
                    unavailable.push_back(describe(s, r, item) + " (technology transfers are not allowed in this game)");
                    break;
                }
                if (!item.tech.valid() || item.tech.index() >= r.data().techAreas.size()) {
                    unavailable.push_back(describe(s, r, item));
                    break;
                }
                // The giver's level; useful only if it is higher than ours.
                research::grantLevel(ctx, receiver, item.tech, g.techLevel(item.tech), "trade");
                break;
            }
            case PackageItem::Kind::Planet: {
                const Colony* c = s.colony(item.planet);
                if (!c || c->owner != giver) unavailable.push_back(describe(s, r, item));
                else transferColony(s, item.planet, receiver);
                break;
            }
            case PackageItem::Kind::Vehicle: {
                const Vehicle* v = s.vehicle(item.vehicle);
                if (!v || v->owner != giver) unavailable.push_back(describe(s, r, item));
                else transferVehicle(s, item.vehicle, receiver);
                break;
            }
            case PackageItem::Kind::StarChart: {
                if (!item.system.valid() || !g.hasExplored(item.system)) {
                    unavailable.push_back(describe(s, r, item));
                    break;
                }
                explore(s, receiver, item.system);
                for (ObjectId wp : s.galaxy.warpPoints(item.system))
                    if (wp.index() < g.knowledge.knownWarpLink.size() && g.knowledge.knownWarpLink[wp.index()] &&
                        wp.index() < rcv.knowledge.knownWarpLink.size())
                        rcv.knowledge.knownWarpLink[wp.index()] = 1;
                break;
            }
            case PackageItem::Kind::Treaty: {
                // The package names the dominant side of a Subjugation or
                // Protectorate in `empire`; by default the receiver (inferred).
                const EmpireId master = item.empire == giver ? giver : receiver;
                setTreaty(ctx, giver, receiver, item.treaty, master == giver);
                break;
            }
            case PackageItem::Kind::CommChannel: {
                if (!living(s, item.empire) || item.empire == receiver || !inContact(s, giver, item.empire)) {
                    unavailable.push_back(describe(s, r, item));
                    break;
                }
                makeContact(ctx, receiver, item.empire);
                break;
            }
            case PackageItem::Kind::System:
                std::erase(g.claimedSystems, item.system);
                break;
        }
    }
    if (!unavailable.empty()) {
        const std::string text = std::format("These items were no longer available:\n{}", joinLines(unavailable));
        ctx.log(giver, LogCategory::Politics, "Items Unavailable", text);
        ctx.log(receiver, LogCategory::Politics, "Items Unavailable", text);
    }
}

void surrender(TurnContext& ctx, EmpireId from, EmpireId to) {
    const Rules& r = ctx.rules;
    GameState& s = ctx.state;
    if (!living(s, from) || !living(s, to) || from == to) return;
    for (auto& c : s.colonies)
        if (c && c->owner == from) transferColony(s, c->planet, to);
    for (Fleet& f : s.fleets)
        if (f.owner == from) {
            f.owner = to;
            f.orders.clear();
        }
    for (Vehicle& v : s.vehicles)
        if (v.owner == from) {
            v.owner = to;
            v.orders.clear();
            v.repeatOrders = false;
        }
    Empire& loser = s.empire(from);
    s.empire(to).stockpile += max(loser.stockpile, Resources{});
    loser.stockpile = {};
    for (DesignId d : loser.designs) learnDesign(s.empire(to), d);
    for (DesignId d : loser.knowledge.seenDesigns)
        if (s.design(d).owner != to) learnDesign(s.empire(to), d);
    shareMap(s, from, to);
    // The surrendered empire's knowledge passes on as well (inferred).
    for (uint32_t i = 0; i < r.data().techAreas.size(); ++i)
        research::grantLevel(ctx, to, ruleset::TechAreaId{i}, s.empire(from).techLevel(ruleset::TechAreaId{i}), "surrender");
    Empire& gone = s.empire(from);
    gone.alive = false;
    gone.research.clear();
    gone.intel.clear();
    const std::string text = std::format("The {} has surrendered to the {}.", nameOf(s, from), nameOf(s, to));
    for (const Empire& e : s.empires)
        if (e.alive || e.id == from) ctx.log(e.id, LogCategory::Politics, "Surrender", text);
}

// ---- Turn phases ------------------------------------------------------------------------------------

void deliverMessages(TurnContext& ctx) {
    GameState& s = ctx.state;
    std::vector<MessageId> lost;
    const size_t n = s.messages.size();
    for (size_t i = 0; i < n; ++i) {
        if (s.messages[i].delivered) continue;
        s.messages[i].delivered = true;
        const DiplomaticMessage& m = s.messages[i];
        if (!living(s, m.from) || !living(s, m.to) || m.from == m.to) {
            lost.push_back(m.id);
            continue;
        }
        // A channel cut by `Politics - Prevent Messages` swallows the message.
        if (!unilateral(m.type) && s.turn < s.empire(m.from).relation(m.to).messagesBlockedUntil) {
            lost.push_back(m.id);
            continue;
        }
        if (!answerable(m.type)) s.messages[i].answered = true;
        receive(ctx, s.messages[i]);
    }
    std::erase_if(s.messages, [&](const DiplomaticMessage& m) {
        if (std::find(lost.begin(), lost.end(), m.id) != lost.end()) return true;
        if (!living(s, m.from) || !living(s, m.to)) return true;
        return m.delivered && m.sentTurn + kMessageLifetime < s.turn;
    });
}

void updateContacts(TurnContext& ctx) {
    GameState& s = ctx.state;
    const size_t n = s.empires.size();
    // Who sees whom this turn: a visible vehicle, or presence where they have a colony.
    std::vector<uint8_t> sees(n * n, 0);
    for (const Empire& e : s.empires) {
        if (!e.alive) continue;
        for (VehicleId id : e.knowledge.visibleVehicles)
            if (const Vehicle* v = s.vehicle(id); v && validEmpire(s, v->owner) && v->owner != e.id)
                sees[e.id.index() * n + v->owner.index()] = 1;
    }
    for (const auto& c : s.colonies) {
        if (!c || !validEmpire(s, c->owner)) continue;
        const SystemId sys = s.galaxy.object(c->planet).system;
        for (const Empire& e : s.empires)
            if (e.alive && e.id != c->owner && sys.index() < e.knowledge.present.size() && e.knowledge.present[sys.index()])
                sees[e.id.index() * n + c->owner.index()] = 1;
    }
    auto seen = [&](size_t a, size_t b) { return sees[a * n + b] || sees[b * n + a]; };
    for (size_t a = 0; a < n; ++a)
        for (size_t b = a + 1; b < n; ++b)
            if (s.empires[a].alive && s.empires[b].alive && seen(a, b)) makeContact(ctx, EmpireId{a}, EmpireId{b});

    // Contact is lost when no warp path links any of our planets to any of
    // theirs (spec 05 §3.1); the treaty falls back to None (inferred).
    if (!s.options.noWarpPoints && !s.galaxy.systems.empty()) {
        Components comp(s.galaxy);
        std::vector<std::vector<uint32_t>> regions(n);
        for (const auto& c : s.colonies)
            if (c && validEmpire(s, c->owner)) regions[c->owner.index()].push_back(comp.find(s.galaxy.object(c->planet).system.value));
        for (auto& list : regions) {
            std::sort(list.begin(), list.end());
            list.erase(std::unique(list.begin(), list.end()), list.end());
        }
        for (size_t a = 0; a < n; ++a)
            for (size_t b = a + 1; b < n; ++b) {
                if (!s.empires[a].alive || !s.empires[b].alive || !s.empires[a].relations[b].contact || seen(a, b)) continue;
                if (regions[a].empty() || regions[b].empty()) continue;
                std::vector<uint32_t> shared;
                std::set_intersection(regions[a].begin(), regions[a].end(), regions[b].begin(), regions[b].end(), std::back_inserter(shared));
                if (!shared.empty()) continue;
                const EmpireId ea{a}, eb{b};
                if (s.empires[a].relations[b].treaty != Treaty::None) setTreaty(ctx, ea, eb, Treaty::None);
                s.empires[a].relations[b].contact = s.empires[b].relations[a].contact = false;
                ctx.log(ea, LogCategory::Politics, "Contact Lost", std::format("We have lost contact with the {}.", nameOf(s, eb)));
                ctx.log(eb, LogCategory::Politics, "Contact Lost", std::format("We have lost contact with the {}.", nameOf(s, ea)));
            }
    }

    // Partnership shares explored space and scanned designs; a master sees
    // its subject's designs (spec 05 §3.2).
    for (size_t a = 0; a < n; ++a)
        for (size_t b = 0; b < n; ++b) {
            if (a == b || !s.empires[a].alive || !s.empires[b].alive) continue;
            const Relation& rel = s.empires[a].relations[b];
            if (rel.treaty == Treaty::Partnership) {
                shareMap(s, EmpireId{a}, EmpireId{b});
                const std::vector<DesignId> seenByA = s.empires[a].knowledge.seenDesigns;
                for (DesignId d : seenByA)
                    if (d.index() < s.designs.size() && s.design(d).owner != EmpireId{b}) learnDesign(s.empires[b], d);
            }
            if (rel.treaty == Treaty::Subjugation && rel.dominant) {
                const std::vector<DesignId> own = s.empires[b].designs;
                for (DesignId d : own) learnDesign(s.empires[a], d);
            }
        }
}

void advanceTrade(TurnContext& ctx) {
    GameState& s = ctx.state;
    const int maxPct = static_cast<int>(ctx.rules.setting("Maximum Trade Percentage", 20));
    for (size_t a = 0; a < s.empires.size(); ++a)
        for (size_t b = a + 1; b < s.empires.size(); ++b) {
            Relation& ra = s.empires[a].relations[b];
            Relation& rb = s.empires[b].relations[a];
            if (s.empires[a].alive && s.empires[b].alive && ra.contact && treatyTradesResources(ra.treaty))
                ra.tradePercent = rb.tradePercent = std::min(maxPct, std::max(ra.tradePercent, rb.tradePercent) + 1);
            else
                ra.tradePercent = rb.tradePercent = 0;
        }
}

// ---- Trade and tariffs ------------------------------------------------------------------------------

Generated generated(const Rules& r, const GameState& s, EmpireId e) {
    Generated g;
    for (const auto& c : s.colonies) {
        if (!c || c->owner != e) continue;
        const economy::ColonyOutput out = economy::colonyOutput(r, s, *c);
        if (!out.connected || out.blockaded) continue;
        g.resources += out.production;
        g.research += out.research;
        g.intelligence += out.intelligence;
    }
    return g;
}

int64_t tradeShare(int64_t partnerBase, int tradePercent, int politicalSavvy, int cultureTrade) {
    if (partnerBase <= 0 || tradePercent <= 0) return 0;
    const int64_t multiplier = std::max<int64_t>(0, int64_t{100} + (politicalSavvy - 100) + cultureTrade);
    return partnerBase * tradePercent * multiplier / 10000;
}

namespace {

template <class Pick>
void forTradePartners(const GameState& s, EmpireId e, bool (*qualifies)(Treaty), Pick&& fn) {
    if (!living(s, e)) return;
    const Empire& emp = s.empire(e);
    for (size_t i = 0; i < s.empires.size() && i < emp.relations.size(); ++i) {
        const Relation& rel = emp.relations[i];
        if (i == e.index() || !s.empires[i].alive || !rel.contact || !qualifies(rel.treaty) || rel.tradePercent <= 0) continue;
        fn(EmpireId{i}, rel.tradePercent);
    }
}

int savvy(const Empire& e) { return e.race.characteristic(Characteristic::PoliticalSavvy); }
int cultureTrade(const Rules& r, const Empire& e) {
    const ruleset::Culture* c = r.culture(e.race);
    return c ? c->trade : 0;
}
bool partnership(Treaty t) { return t == Treaty::Partnership; }
bool tradesResources(Treaty t) { return treatyTradesResources(t); }
bool tradesResearch(Treaty t) { return treatyTradesResearch(t); }

} // namespace

Resources tradeIncome(const Rules& r, const GameState& s, EmpireId e) {
    Resources total = tariffsReceived(r, s, e);
    if (!living(s, e)) return total;
    const Empire& emp = s.empire(e);
    forTradePartners(s, e, tradesResources, [&](EmpireId partner, int pct) {
        const Generated g = generated(r, s, partner);
        for (Resource res : kResources) total[res] += tradeShare(g.resources[res], pct, savvy(emp), cultureTrade(r, emp));
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
    const Empire& emp = s.empire(e);
    forTradePartners(s, e, tradesResearch,
                     [&](EmpireId partner, int pct) { total += tradeShare(generated(r, s, partner).research, pct, savvy(emp), cultureTrade(r, emp)); });
    return total;
}

int64_t intelTradeIncome(const Rules& r, const GameState& s, EmpireId e) {
    int64_t total = 0;
    if (!living(s, e)) return total;
    const Empire& emp = s.empire(e);
    forTradePartners(s, e, partnership, [&](EmpireId partner, int pct) {
        total += tradeShare(generated(r, s, partner).intelligence, pct, savvy(emp), cultureTrade(r, emp));
    });
    return total;
}

Resources tariffsPaid(const Rules& r, const GameState& s, EmpireId e) {
    const EmpireId master = masterOf(s, e);
    if (!master.valid() || !s.empire(master).alive) return {};
    const Treaty t = s.empire(e).relation(master).treaty;
    const int64_t pct = t == Treaty::Subjugation ? r.setting("Treaty Subjugated Resource Percentage", 40)
                                                 : r.setting("Treaty Protectorate Resource Percentage", 20);
    return max(generated(r, s, e).resources, Resources{}).percent(pct);
}

} // namespace opense4::game::diplomacy
