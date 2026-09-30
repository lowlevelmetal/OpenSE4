// Computer player: the Politics minister (spec 05 §7.4 AI_Politics, §7.5
// AI_Speech, §7.6 Mega Evil Empire). Everything here is (confirmed: binary)
// unless marked otherwise. Anger itself is kept by the AI step (ai_anger.cpp).

#include "datafile/datafile.hpp"
#include "game/ai_planner.hpp"
#include "game/diplomacy.hpp"
#include "game/query.hpp"
#include "game/xmath.hpp"

#include <algorithm>
#include <format>
#include <map>

namespace opense4::game::ai::detail {

namespace {

using datafile::keysEqual;

// Position in the treaty order of spec 05 §7.4 (1 War .. 11 Partnership; 3 is "no contact").
int treatyNumber(Treaty t) { return t <= Treaty::NonIntercourse ? static_cast<int>(t) + 1 : static_cast<int>(t) + 2; }

bool answerable(MessageType t) {
    switch (t) {
        case MessageType::ProposeTreaty:
        case MessageType::CounterTreaty:
        case MessageType::ProposeTrade:
        case MessageType::CounterTrade:
        case MessageType::Gift:
        case MessageType::Tribute: return true;
        default: return isDemand(t);
    }
}

void replaceAll(std::string& text, std::string_view token, std::string_view value) {
    for (size_t pos = text.find(token); pos != std::string::npos; pos = text.find(token, pos + value.size()))
        text.replace(pos, token.size(), value);
}

} // namespace

// A planet's worth (spec 05 §7.4, confirmed: binary): its three resource
// values × 1000 (× 1 in finite-resource games), plus, when colonized, 100
// × its population in millions, 1,000,000 per facility, 10,000 per unit
// of cargo (each million people and each unit) and 1,000,000,000 for a
// capital. A colony the giver no longer holds counts 0.
static int64_t planetWorth(const Planner& p_, ObjectId planet, EmpireId giver) {
    const GameState& s = p_.st;
    if (!planet.valid() || planet.index() >= s.galaxy.objects.size()) return 0;
    const SpaceObject& obj = s.galaxy.object(planet);
    if (obj.kind != ObjectKind::Planet && obj.kind != ObjectKind::Asteroids) return 0;
    const int64_t scale = s.options.finiteResources ? 1 : 1000;
    int64_t worth = (int64_t{obj.value[0]} + obj.value[1] + obj.value[2]) * scale;
    if (const Colony* c = s.colony(planet)) {
        if (c->owner != giver) return 0;
        worth += 100 * c->totalPopulation();
        worth += 1'000'000 * static_cast<int64_t>(c->facilities.size());
        int64_t cargo = c->cargo.totalPopulation();
        for (const UnitStack& u : c->cargo.units) cargo += std::max(0, u.count);
        worth += 10'000 * cargo;
        if (c->homeworld) worth += 1'000'000'000;
    }
    return worth;
}

// Spec 05 §7.4 item values, for the receiving side (confirmed: binary).
int64_t tradeItemValue(const Planner& p_, const PackageItem& item, EmpireId giver, EmpireId receiver) {
    if (diplomacy::isPlaceholder(item)) return 0;
    const GameState& s = p_.st;
    switch (item.kind) {
        case PackageItem::Kind::Resources: return std::max<int64_t>(0, item.resources.total()) / 1000;
        case PackageItem::Kind::Technology: {
            if (item.tech.index() >= p_.r.data().techAreas.size()) return 0;
            const Empire& rcv = s.empire(receiver);
            const int have = rcv.techLevel(item.tech);
            if (s.empire(giver).techLevel(item.tech) <= have || !p_.r.techVisible(s, rcv, item.tech) ||
                have >= p_.r.tech(item.tech).maxLevel)
                return 0;
            return int64_t{have + 1} * p_.r.tech(item.tech).levelCost;
        }
        case PackageItem::Kind::Planet: return planetWorth(p_, item.planet, giver) / 100000;
        case PackageItem::Kind::Vehicle: {
            // A ship: 100 × its scrap value (spec 03 §15, reclamation in its
            // sector included), each resource / 4 without weapon parts; 0
            // when mothballed or without an owner, a design or a position.
            // A unit group: 100 × the units' scrap value at the ship percentage.
            const Vehicle* v = s.vehicle(item.vehicle);
            if (!v || v->count <= 0 || !v->owner.valid() || !v->design.valid() || v->design.index() >= s.designs.size() ||
                !v->location.system.valid() || v->status == VehicleStatus::Mothballed)
                return 0;
            if (isUnitType(vehicleType(p_.r, s, *v))) {
                const int64_t pctShip = p_.r.setting("Scrap Ship Percent Returned", 30);
                int64_t total = 0;
                for (const UnitStack& st : groupStacks(*v)) {
                    const Resources cost = computeDesignStats(p_.r, nullptr, s.design(st.design)).cost;
                    for (Resource k : kResources) total += xmath::pctRound(cost[k], pctShip) * std::max(0, st.count);
                }
                return total * 100;
            }
            const Resources scrap = scrapRefund(p_.r, s, *v);
            if (computeDesignStats(p_.r, nullptr, s.design(v->design)).armed()) return scrap.total() * 100;
            return (scrap[Resource::Minerals] / 4 + scrap[Resource::Organics] / 4 + scrap[Resource::Radioactives] / 4) * 100;
        }
        case PackageItem::Kind::StarChart: return s.empire(receiver).hasExplored(item.system) ? 0 : 20000;
        case PackageItem::Kind::Treaty: return int64_t{treatyNumber(item.treaty)} * 100000;
        case PackageItem::Kind::CommChannel: return 50000;
        case PackageItem::Kind::System: {
            // 100,000 per planet in it (asteroid fields excluded, colonized or
            // not) the receiver could colonize, only when the giver claims it.
            if (!item.system.valid() || item.system.index() >= s.galaxy.systems.size()) return 0;
            const auto& claimed = s.empire(giver).claimedSystems;
            if (std::find(claimed.begin(), claimed.end(), item.system) == claimed.end()) return 0;
            int64_t planets = 0;
            for (ObjectId o : s.galaxy.system(item.system).objects)
                planets += canSettle(p_.r, s, s.empire(receiver), s.galaxy.object(o));
            return planets * 100000;
        }
    }
    return 0;
}

namespace {

class Politician {
public:
    explicit Politician(Planner& p)
        : p_(p), pol_(p.prof.politics), mee_(megaEvilEmpire(p.r, p.scores, p.st, p.id)), ours_(p.scores[p.id.index()]) {}

    void run() {
        for (size_t i = 0; i < p_.st.empires.size(); ++i) {
            const EmpireId x{i};
            if (x == p_.id || !p_.st.empire(x).alive || !rel(x).contact) continue;
            turnWith(x);
        }
    }

private:
    Planner& p_;
    const PoliticsTable& pol_;
    EmpireId mee_;
    int64_t ours_;
    std::map<uint32_t, bool> wantsWar_, wantsBreak_;

    const Relation& rel(EmpireId x) const { return p_.emp().relation(x); }
    int anger(EmpireId x) const { return rel(x).anger; }
    // P = their score / our score x 100, truncated; 0 when our score is 0 or less.
    int64_t pct(EmpireId x) const {
        if (ours_ <= 0) return 0;
        return (xmath::Ext(p_.scores[x.index()]) / xmath::Ext(ours_) * xmath::Ext(100)).trunc();
    }
    bool isFriend(EmpireId x) const { return rel(x).treaty >= Treaty::NonAggression; }
    bool human(EmpireId x) const { return p_.st.empire(x).kind == PlayerKind::Human; }
    bool teamMate(EmpireId x) const { return p_.st.options.teamMode && human(x) == human(p_.id); }
    bool teamEnemy(EmpireId x) const { return p_.st.options.teamMode && human(x) != human(p_.id); }
    bool subject() const {
        for (const Empire& o : p_.st.empires)
            if (o.id != p_.id && o.alive && rel(o.id).treaty == Treaty::Subjugation && !rel(o.id).dominant) return true;
        return false;
    }
    // Wars with empires whose score is above a quarter of ours.
    int wars() const {
        int n = 0;
        for (const Empire& o : p_.st.empires)
            if (o.id != p_.id && o.alive && rel(o.id).treaty == Treaty::War && p_.scores[o.id.index()] > ours_ / 4) ++n;
        return n;
    }
    int strongerWeaker(const TreatyRule& percents, const TreatyRule& amounts, int64_t p, bool stronger = true) const {
        int t = 0;
        if (stronger && p >= percents.strongerPercent) t += amounts.strongerAmount;
        if (p < percents.weakerPercent) t += amounts.weakerAmount;
        return t;
    }
    int team(EmpireId x, int t) const {
        if (teamEnemy(x)) return -1;
        if (teamMate(x)) return 101;
        return t;
    }

    // ---- Thresholds (spec 05 §7.4) -------------------------------------------------------------

    int acceptThreshold(EmpireId x, Treaty t) const {
        const int64_t p = pct(x);
        int T = pol_.accept.baseAnger;
        if (t >= Treaty::TradeAlliance) T += pol_.acceptPerHigherLevel * (treatyNumber(t) - treatyNumber(Treaty::TradeAlliance));
        T = std::max(T, pol_.acceptMinimumChance);
        T += pol_.accept.perOtherWars * wars();
        if (t >= Treaty::TradeResearchAlliance && rel(x).treatyAge < pol_.acceptMinimumTurnsSinceTreaty) T = -1;
        if (p_.date < 50) T += pol_.accept.first50Turns;  // the first 50 turns, by the date the ministers see
        T += strongerWeaker(pol_.accept, pol_.accept, p);
        if (mee_.valid()) T = x == mee_ ? -1 : T + 60;
        if (t == Treaty::Subjugation && p < pol_.acceptSubjugationPercent) T = -1;
        if (t == Treaty::Protectorate && p < pol_.acceptProtectoratePercent) T = -1;
        return team(x, T);
    }
    int proposeThreshold(EmpireId x) const {
        int T = pol_.propose.baseAnger + pol_.propose.perOtherWars * wars() + (p_.date < 50 ? pol_.propose.first50Turns : 0) +
                strongerWeaker(pol_.propose, pol_.propose, pct(x));
        if (mee_.valid()) T = x == mee_ ? -1 : T + 70;
        return team(x, T);
    }
    // Break uses its own percents with the Declare War amounts (a quirk of the original).
    int breakThreshold(EmpireId x) const {
        int T = pol_.breakTreaty.baseAnger + pol_.breakTreaty.perOtherWars * wars() + strongerWeaker(pol_.breakTreaty, pol_.declareWar, pct(x));
        if (mee_.valid()) T += x == mee_ ? -20 : 60;
        return team(x, T);
    }
    int warThreshold(EmpireId x) const {
        int T = pol_.declareWar.baseAnger + pol_.declareWar.perOtherWars * wars() +
                strongerWeaker(pol_.declareWar, pol_.declareWar, pct(x), x != mee_);
        if (mee_.valid()) T += x == mee_ ? -40 : 60;
        return team(x, T);
    }

    // The whole acceptance test for a treaty offered by x.
    bool acceptsTreaty(EmpireId x, Treaty t) const {
        if (teamMate(x)) return true;
        if (teamEnemy(x)) return false;
        if (t > pol_.highestAllowedTreaty || t == Treaty::War) return false;
        if (rel(x).turnsSinceWar < pol_.turnsSinceWarBeforeFriendly) return false;
        if (x == mee_) return false;
        return acceptThreshold(x, t) > anger(x);
    }

    // ---- Decisions -----------------------------------------------------------------------------

    bool wantsWar(EmpireId x) {
        auto it = wantsWar_.find(x.value);
        if (it != wantsWar_.end()) return it->second;
        bool want = false;
        if (rel(x).treaty == Treaty::War || teamMate(x)) want = false;
        else if (teamEnemy(x) || rel(x).queuedWar) want = true;
        else if (p_.st.options.teamMode || p_.rng.percent(50)) want = anger(x) >= warThreshold(x);
        return wantsWar_[x.value] = want;
    }
    bool wantsBreak(EmpireId x) {
        auto it = wantsBreak_.find(x.value);
        if (it != wantsBreak_.end()) return it->second;
        bool want = false;
        if (rel(x).treaty < Treaty::NonAggression || teamMate(x)) want = false;
        else if (teamEnemy(x) || rel(x).queuedBreak) want = true;
        else if (p_.st.options.teamMode || p_.rng.percent(50)) want = anger(x) >= breakThreshold(x);
        return wantsBreak_[x.value] = want;
    }

    void turnWith(EmpireId x) {
        if (p_.rng.percent(50) && (wantsWar(x) || wantsBreak(x))) {
            initiative(x);
            return;
        }
        if (answerNewest(x)) return;  // it sent x something
        // In a simultaneous game nothing is started while a message from x waits
        // (dated this turn or the turn before, inferred).
        if (p_.st.options.simultaneous)
            for (const DiplomaticMessage& m : p_.st.messages)
                if (m.from == x && m.to == p_.id && m.delivered && m.sentTurn + 1 >= p_.st.turn && !m.answered && answerable(m.type)) return;
        initiative(x);
    }

    void initiative(EmpireId x) {
        if (wantsWar(x)) {
            // Against the MEE the text is a `Mega Evil Declarations` line.
            if (x == mee_) send(x, MessageType::DeclareWar, "Mega Evil Declarations");
            else sendNamed(x, MessageType::DeclareWar);
            return;
        }
        if (wantsBreak(x)) {
            sendNamed(x, MessageType::BreakTreaty, rel(x).treaty);
            return;
        }
        if (propose(x)) return;
        if (p_.rng.percent(25)) demand(x);
    }

    bool propose(EmpireId x) {
        if (subject()) return false;
        const bool mate = teamMate(x);
        const bool forced = mate || rel(x).queuedPeace;
        int T = 50;  // when a forced proposal computed no threshold
        if (!forced) {
            const int chance = pol_.proposeChancePercent * (mee_.valid() && x != mee_ ? 3 : 1);
            if (!p_.st.options.teamMode && !p_.rng.percent(chance)) return false;
            const bool gated = (human(p_.id) || human(x)) && !p_.st.options.teamMode && !mee_.valid();
            if (gated && rel(x).turnsSinceWar < pol_.turnsSinceWarBeforeFriendly) return false;
            T = proposeThreshold(x);
            if (anger(x) > T) return false;
            if (anger(x) > 70 && p_.rng.percent(50)) return false;
        }
        Treaty pick = Treaty::Count;
        if (mate) {
            if (rel(x).treaty != Treaty::Partnership) pick = Treaty::Partnership;
        } else {
            for (const auto& [treaty, below] : pol_.proposeTypes)
                if (treaty <= pol_.highestAllowedTreaty && treaty > rel(x).treaty && anger(x) <= T - below) pick = treaty;  // the last one
        }
        if (pick == Treaty::Count) return false;
        return sendNamed(x, MessageType::ProposeTreaty, pick);
    }

    // ---- Demands the AI starts (spec 05 §7.4, always in the neutral tone) --------------------------

    bool mayDemand(MessageType t, EmpireId x) const {
        const DemandRule& rule = pol_.demands[static_cast<size_t>(t)];
        return isFriend(x) ? rule.sendToFriend : rule.sendToEnemy;
    }

    bool demand(EmpireId x) {
        const Relation& r = rel(x);
        auto sendDemand = [&](MessageType t, EmpireId third = {}, SystemId sys = {}) {
            return send(x, t, std::format("Send {}", angerKeyName(t)), Treaty::None, third, sys, {}, {}, 1);
        };
        // 1. Stop what they did to us.
        if (r.attackedUs && mayDemand(MessageType::DemandStopAttacks, x)) return sendDemand(MessageType::DemandStopAttacks, {}, r.attackedIn);
        if (r.spiedOnUs && mayDemand(MessageType::DemandStopEspionage, x)) return sendDemand(MessageType::DemandStopEspionage);
        // 2. Their ships or colonies where we have a colony.
        if (p_.rng.percent(20)) {
            std::vector<uint8_t> ownColony(p_.st.galaxy.systems.size(), 0);
            for (const auto& c : p_.st.colonies)
                if (c && c->owner == p_.id) ownColony[p_.st.galaxy.object(c->planet).system.index()] = 1;
            SystemId colonies, ships;
            for (const auto& c : p_.st.colonies)
                if (c && c->owner == x && !colonies.valid() && ownColony[p_.st.galaxy.object(c->planet).system.index()])
                    colonies = p_.st.galaxy.object(c->planet).system;
            for (VehicleId vid : p_.emp().knowledge.visibleVehicles)
                if (const Vehicle* v = p_.st.vehicle(vid); v && v->owner == x && !ships.valid() && ownColony[v->location.system.index()])
                    ships = v->location.system;
            if (colonies.valid() && mayDemand(MessageType::DemandRemoveColonies, x)) return sendDemand(MessageType::DemandRemoveColonies, {}, colonies);
            if (ships.valid() && mayDemand(MessageType::DemandRemoveShips, x)) return sendDemand(MessageType::DemandRemoveShips, {}, ships);
        }
        // 3. Break with an empire we are hostile to (or join our war).
        if (r.treaty >= Treaty::TradeAlliance && p_.rng.percent(20)) {
            for (const Empire& y : p_.st.empires) {
                if (y.id == x || y.id == p_.id || !y.alive || !rel(y.id).contact || !hostileTo(p_.emp(), y.id)) continue;
                if (r.treaty >= Treaty::MilitaryAlliance && p_.atWarWith(y.id)) {
                    if (mayDemand(MessageType::RequestDeclareWar, x)) return sendDemand(MessageType::RequestDeclareWar, y.id);
                } else if (mayDemand(MessageType::RequestBreakTreaty, x)) {
                    return sendDemand(MessageType::RequestBreakTreaty, y.id);
                }
                break;
            }
        }
        // 4. Make peace with one of our allies.
        if (r.treaty >= Treaty::MilitaryAlliance && p_.rng.percent(20) && mayDemand(MessageType::RequestMakePeace, x))
            for (const Empire& y : p_.st.empires)
                if (y.id != x && y.id != p_.id && y.alive && rel(y.id).treaty >= Treaty::MilitaryAlliance &&
                    p_.st.empire(x).relation(y.id).treaty == Treaty::War)
                    return sendDemand(MessageType::RequestMakePeace, y.id);
        // 5. Attack an empire in a system.
        if (r.treaty >= Treaty::MilitaryAlliance && p_.rng.percent(20) && mayDemand(MessageType::RequestAttackEmpire, x))
            for (const Candidate& c : p_.sit.candidates)
                if (c.owner != x && hostileTo(p_.emp(), c.owner)) return sendDemand(MessageType::RequestAttackEmpire, c.owner, c.system);
        // 6. Surrender, after more than one combat report in the last two turns. No flag needed.
        if (r.treaty == Treaty::War && p_.rng.percent(33) && r.combatsThisTurn + r.combatsLastTurn > 1)
            return sendDemand(MessageType::DemandSurrender);
        // 7. Chatter, from `Send General Message` (the miscellaneous pools are never read).
        if (r.treaty >= Treaty::TradeAlliance && p_.rng.percent(10))
            return send(x, MessageType::General, "Send General Message", Treaty::None, {}, {}, {}, {}, 1);
        return false;
    }

    // ---- Answers ---------------------------------------------------------------------------------

    // Spec 05 §7.4 "Which messages get an answer" (confirmed: binary): each
    // turn at most the newest unanswered political message from x, whatever
    // its type. A message counts while it is dated this turn or the turn
    // before, as the log the original reads (inferred). True when a reply
    // went out.
    bool repliedByUs(const DiplomaticMessage& m) const {
        for (const DiplomaticMessage& o : p_.st.messages)
            if (o.from == p_.id && o.inReplyTo == m.id) return true;
        return false;
    }
    bool unanswered(const DiplomaticMessage& m) const {
        if (answerable(m.type) && m.answered) return false;  // the rest are marked answered when delivered
        return !repliedByUs(m);
    }
    bool answerNewest(EmpireId x) {
        const DiplomaticMessage* newest = nullptr;
        for (const DiplomaticMessage& m : p_.st.messages)
            if (m.from == x && m.to == p_.id && m.delivered && m.sentTurn + 1 >= p_.st.turn && unanswered(m) && (!newest || m.id > newest->id))
                newest = &m;
        if (!newest) return false;
        const DiplomaticMessage msg = *newest;  // copy: answering appends to the list
        switch (msg.type) {
            case MessageType::ProposeTreaty:
            case MessageType::CounterTreaty: return answerTreaty(msg);
            case MessageType::ProposeTrade:
            case MessageType::CounterTrade: return answerTrade(msg);
            case MessageType::Gift:
            case MessageType::Tribute: return answerGift(msg);
            case MessageType::DemandGift:
            case MessageType::DemandTribute: return answerRequest(msg);
            case MessageType::DemandSurrender: return answerSurrender(msg);
            case MessageType::General: return false;  // no answer
            default: return isDemand(msg.type) ? answerDemand(msg) : acknowledge(msg);
        }
    }

    // Friend or Enemy by the treaty at answer time: Non-Aggression or better is Friend.
    std::string_view side(EmpireId x) const { return isFriend(x) ? "Friend" : "Enemy"; }

    // A General message in reply to `msg`, from a response pool; nothing when the pool is empty.
    bool generalReply(const DiplomaticMessage& msg, std::string_view pool) {
        auto text = speechLine(p_, pool, msg.from, msg.thirdEmpire, msg.treaty, msg.system, msg.planet);
        if (!text) return false;
        DiplomaticMessage chat;
        chat.to = msg.from;
        chat.type = MessageType::General;
        chat.inReplyTo = msg.id;
        chat.text = std::move(*text);
        return p_.emit(cmd::SendMessage{std::move(chat)});
    }

    // An acknowledgement (an answer to one of ours, a declaration, a surrender,
    // a granted independence) gets a General message from the `Response
    // Friend/Enemy <type>` pool; Accept and Refuse Demand have no such pool and
    // get nothing, so two computer players never chatter back and forth.
    bool acknowledge(const DiplomaticMessage& msg) {
        const std::string_view type = ackPool(msg);
        if (type.empty()) return false;
        return generalReply(msg, std::format("Response {} {}", side(msg.from), type));
    }
    // The type part of an acknowledgement's response pool, empty for anything else.
    std::string_view ackPool(const DiplomaticMessage& m) const {
        switch (m.type) {
            case MessageType::AcceptGift:
            case MessageType::RefuseGift: {
                bool tribute = false;
                for (const DiplomaticMessage& o : p_.st.messages)
                    if (o.id == m.inReplyTo) tribute = o.type == MessageType::Tribute;
                if (tribute) return m.type == MessageType::AcceptGift ? "Accept Tribute" : "Refuse Tribute";
                return angerKeyName(m.type);
            }
            case MessageType::AcceptTreaty:
            case MessageType::RefuseTreaty:
            case MessageType::BreakTreaty:
            case MessageType::DeclareWar:
            case MessageType::AcceptTrade:
            case MessageType::RefuseTrade:
            case MessageType::Surrender:
            case MessageType::GrantIndependence: return angerKeyName(m.type);
            default: return {};
        }
    }

    bool reply(const DiplomaticMessage& msg, bool accept, std::string_view pool) {
        auto text = speechLine(p_, pool, msg.from, msg.thirdEmpire, msg.treaty, msg.system, msg.planet);
        if (!text) return false;  // an empty pool: the reply is not sent
        return p_.emit(cmd::AnswerMessage{msg.id, accept, std::move(*text)});
    }

    bool answerTreaty(const DiplomaticMessage& msg) {
        if (subject()) return false;  // no answer at all
        const EmpireId x = msg.from;
        const bool accept = acceptsTreaty(x, msg.treaty);
        if (!reply(msg, accept, accept ? "Send Accept Treaty" : "Send Refuse Treaty")) return false;
        // A counter-proposal one step lower, never below Non-Aggression.
        if (!accept && msg.treaty > Treaty::NonAggression && msg.treaty < Treaty::Count && p_.rng.percent(25)) {
            const Treaty lower = static_cast<Treaty>(static_cast<int>(msg.treaty) - 1);
            if (lower > rel(x).treaty && acceptsTreaty(x, lower)) {
                DiplomaticMessage m;
                m.inReplyTo = msg.id;
                sendNamed(x, MessageType::CounterTreaty, lower, {}, {}, {}, {}, &m);
            }
        }
        return true;
    }

    int64_t itemValue(const PackageItem& item, EmpireId giver, EmpireId receiver) const { return tradeItemValue(p_, item, giver, receiver); }
    int64_t packageValue(const std::vector<PackageItem>& items, EmpireId giver, EmpireId receiver) const {
        int64_t v = 0;
        for (const PackageItem& i : items) v += itemValue(i, giver, receiver);
        return v;
    }
    bool treatyItemsPass(EmpireId x, const std::vector<PackageItem>& a, const std::vector<PackageItem>& b) const {
        for (const auto* list : {&a, &b})
            for (const PackageItem& i : *list)
                if (i.kind == PackageItem::Kind::Treaty && !acceptsTreaty(x, i.treaty)) return false;
        return true;
    }
    // Can we hand this item over?
    bool canGive(const PackageItem& i, EmpireId to) const {
        const Empire& us = p_.emp();
        switch (i.kind) {
            case PackageItem::Kind::Resources: return us.stockpile.covers(max(i.resources, Resources{}));
            case PackageItem::Kind::Technology: return i.tech.index() < p_.r.data().techAreas.size() && us.techLevel(i.tech) > 0;
            case PackageItem::Kind::Planet: {
                const Colony* c = p_.st.colony(i.planet);
                return c && c->owner == p_.id;
            }
            case PackageItem::Kind::Vehicle: {
                const Vehicle* v = p_.st.vehicle(i.vehicle);
                return v && v->owner == p_.id;
            }
            case PackageItem::Kind::StarChart: return us.hasExplored(i.system);
            case PackageItem::Kind::CommChannel:
                return i.empire.valid() && i.empire.index() < p_.st.empires.size() && i.empire != to && rel(i.empire).contact;
            case PackageItem::Kind::Treaty:
            case PackageItem::Kind::System: return true;
        }
        return false;
    }
    // A random concrete item for an "any" placeholder (inferred choice of item).
    std::optional<PackageItem> concrete(const PackageItem& any, EmpireId to) {
        PackageItem out = any;
        const GameState& s = p_.st;
        switch (any.kind) {
            case PackageItem::Kind::Technology: {
                std::vector<ruleset::TechAreaId> ahead;
                for (uint32_t a = 0; a < p_.r.data().techAreas.size(); ++a)
                    if (p_.emp().techLevel(ruleset::TechAreaId{a}) > s.empire(to).techLevel(ruleset::TechAreaId{a}))
                        ahead.push_back(ruleset::TechAreaId{a});
                if (ahead.empty()) return std::nullopt;
                out.tech = ahead[static_cast<size_t>(p_.rng.below(ahead.size()))];
                return out;
            }
            case PackageItem::Kind::Planet: {
                std::vector<ObjectId> planets;
                for (const auto& c : s.colonies)
                    if (c && c->owner == p_.id && !c->homeworld) planets.push_back(c->planet);
                if (planets.empty()) return std::nullopt;
                out.planet = planets[static_cast<size_t>(p_.rng.below(planets.size()))];
                return out;
            }
            case PackageItem::Kind::Vehicle: {
                std::vector<VehicleId> ships;
                for (const Vehicle& v : s.vehicles)
                    if (v.owner == p_.id) ships.push_back(v.id);
                if (ships.empty()) return std::nullopt;
                out.vehicle = ships[static_cast<size_t>(p_.rng.below(ships.size()))];
                return out;
            }
            case PackageItem::Kind::StarChart:
            case PackageItem::Kind::System: {
                std::vector<SystemId> known;
                for (size_t i = 0; i < s.galaxy.systems.size(); ++i)
                    if (p_.explored(SystemId{i})) known.push_back(SystemId{i});
                if (known.empty()) return std::nullopt;
                out.system = known[static_cast<size_t>(p_.rng.below(known.size()))];
                return out;
            }
            case PackageItem::Kind::CommChannel: {
                std::vector<EmpireId> met;
                for (const Empire& o : s.empires)
                    if (o.id != p_.id && o.id != to && o.alive && rel(o.id).contact) met.push_back(o.id);
                if (met.empty()) return std::nullopt;
                out.empire = met[static_cast<size_t>(p_.rng.below(met.size()))];
                return out;
            }
            default: return out;
        }
    }

    bool answerTrade(const DiplomaticMessage& msg) {
        const EmpireId x = msg.from;
        if (teamMate(x) || teamEnemy(x)) return reply(msg, teamMate(x), teamMate(x) ? "Send Accept Trade" : "Send Refuse Trade");
        for (const PackageItem& i : msg.request)
            if (!diplomacy::isPlaceholder(i) && !canGive(i, x)) return reply(msg, false, "Send Refuse Trade");
        const bool anyItems = std::any_of(msg.request.begin(), msg.request.end(), [](const PackageItem& i) { return diplomacy::isPlaceholder(i); });
        const int need = isFriend(x) ? pol_.acceptTradeFriendPercent : pol_.acceptTradeEnemyPercent;
        auto passes = [&](const std::vector<PackageItem>& give, const std::vector<PackageItem>& get) {
            return packageValue(get, x, p_.id) >= xmath::pctTrunc(packageValue(give, p_.id, x), need) && treatyItemsPass(x, give, get);
        };
        if (anyItems) {
            if (!reply(msg, false, "Send Refuse Trade")) return false;
            // Counter with concrete items, half of the time, if the value test passes.
            std::vector<PackageItem> give;
            for (const PackageItem& i : msg.request) {
                if (!diplomacy::isPlaceholder(i)) {
                    give.push_back(i);
                    continue;
                }
                const auto c = concrete(i, x);
                if (!c) return true;
                give.push_back(*c);
            }
            if (p_.rng.percent(50) && passes(give, msg.offer)) {
                DiplomaticMessage m;
                m.inReplyTo = msg.id;
                sendNamed(x, MessageType::CounterTrade, Treaty::None, {}, {}, std::move(give), msg.offer, &m);
            }
            return true;
        }
        const bool accept = passes(msg.request, msg.offer);
        return reply(msg, accept, accept ? "Send Accept Trade" : "Send Refuse Trade");
    }

    bool answerGift(const DiplomaticMessage& msg) {
        const EmpireId x = msg.from;
        const bool gift = msg.type == MessageType::Gift;
        bool accept = false;
        if (teamMate(x)) accept = true;
        else if (!teamEnemy(x))
            accept = anger(x) <= (gift ? pol_.maxAngerAcceptGift : pol_.maxAngerAcceptTribute) && treatyItemsPass(x, msg.offer, {});
        return reply(msg, accept, gift ? (accept ? "Send Accept Gift" : "Send Refuse Gift") : (accept ? "Send Accept Tribute" : "Send Refuse Tribute"));
    }

    // The 13 demands from "remove ships" to "stop attacks": Accept or Refuse
    // Demand, the text from `Response Friend/Enemy YES/NO <demand>`.
    bool answerDemand(const DiplomaticMessage& msg) {
        const EmpireId x = msg.from;
        const DemandRule& rule = pol_.demands[static_cast<size_t>(msg.type)];
        bool accept = false;
        if (teamEnemy(x)) accept = false;
        else if (teamMate(x)) accept = true;
        else accept = pct(x) >= rule.acceptScorePercent && (isFriend(x) ? rule.acceptFromFriend : rule.acceptFromEnemy);
        return reply(msg, accept, std::format("Response {} {} {}", side(x), accept ? "YES" : "NO", angerKeyName(msg.type)));
    }

    // A request for a gift or tribute: the gift or tribute itself when
    // accepted, otherwise a General message from `Response Friend/Enemy Want
    // a gift/tribute`.
    bool answerRequest(const DiplomaticMessage& msg) {
        const EmpireId x = msg.from;
        const bool friendly = isFriend(x);
        const bool gift = msg.type == MessageType::DemandGift;
        const DemandRule& rule = pol_.demands[static_cast<size_t>(msg.type)];
        const int64_t p = pct(x);
        bool accept = false;
        if (teamEnemy(x)) accept = false;
        else if (teamMate(x)) accept = true;
        else
            accept = p >= rule.acceptScorePercent && (friendly ? rule.acceptFromFriend : rule.acceptFromEnemy) &&
                     anger(x) <= (gift ? (friendly ? pol_.giftMaxAngerFriend : pol_.giftMaxAngerEnemy)
                                       : (friendly ? pol_.tributeMaxAngerFriend : pol_.tributeMaxAngerEnemy));
        if (accept) return payUp(msg, friendly, p);
        return generalReply(msg, std::format("Response {} {}", side(x), angerKeyName(msg.type)));
    }

    // A surrender demand, only with Allow Surrender (otherwise no answer at
    // all): considered only by a computer-controlled empire (a human's
    // ministers never surrender); Surrender when accepted, otherwise a General
    // message from `Response Friend/Enemy Demand your surrender`.
    bool answerSurrender(const DiplomaticMessage& msg) {
        if (!p_.st.options.allowSurrender) return false;
        const EmpireId x = msg.from;
        const DemandRule& rule = pol_.demands[static_cast<size_t>(msg.type)];
        bool accept = false;
        if (p_.emp().kind != PlayerKind::Human && !teamEnemy(x))
            accept = teamMate(x) || (pct(x) >= rule.acceptScorePercent && (isFriend(x) ? rule.acceptFromFriend : rule.acceptFromEnemy));
        if (accept) {
            DiplomaticMessage m;
            m.inReplyTo = msg.id;
            return sendNamed(x, MessageType::Surrender, Treaty::None, {}, {}, {}, {}, &m);
        }
        return generalReply(msg, std::format("Response {} {}", side(x), angerKeyName(msg.type)));
    }

    // An accepted request for a gift or tribute: the requested items in order
    // (a random concrete one for each "any") until the package is worth V.
    bool payUp(const DiplomaticMessage& msg, bool friendly, int64_t p) {
        if (!p_.st.options.allowGifts) return false;
        const bool gift = msg.type == MessageType::DemandGift;
        const int64_t base = gift ? (friendly ? pol_.giftBaseFriend : pol_.giftBaseEnemy) : (friendly ? pol_.tributeBaseFriend : pol_.tributeBaseEnemy);
        const int64_t per = gift ? (friendly ? pol_.giftPerPercentFriend : pol_.giftPerPercentEnemy)
                                 : (friendly ? pol_.tributePerPercentFriend : pol_.tributePerPercentEnemy);
        const int64_t target = base + (p > 100 ? p * per - 100 : 0);
        std::vector<PackageItem> package;
        int64_t value = 0;
        for (const PackageItem& want : msg.request) {
            if (value >= target) break;
            std::optional<PackageItem> item = diplomacy::isPlaceholder(want) ? concrete(want, msg.from) : std::optional<PackageItem>(want);
            if (!item || !canGive(*item, msg.from)) continue;
            value += itemValue(*item, p_.id, msg.from);
            package.push_back(*item);
        }
        if (package.empty()) return false;
        DiplomaticMessage m;
        m.inReplyTo = msg.id;
        return sendNamed(msg.from, gift ? MessageType::Gift : MessageType::Tribute, Treaty::None, {}, {}, std::move(package), {}, &m);
    }

    // ---- Sending -----------------------------------------------------------------------------

    int toneFor(EmpireId x) const {
        const int64_t p = pct(x);
        if (p <= pol_.demandingTonePercent) return 2;
        if (p >= pol_.pleadingTonePercent) return 0;
        return 1;
    }

    bool send(EmpireId to, MessageType type, std::string_view pool, Treaty treaty = Treaty::None, EmpireId third = {}, SystemId system = {},
              std::vector<PackageItem> offer = {}, std::vector<PackageItem> request = {}, int tone = -1,
              const DiplomaticMessage* base = nullptr) {
        DiplomaticMessage m = base ? *base : DiplomaticMessage{};
        m.to = to;
        m.type = type;
        m.tone = tone >= 0 ? tone : toneFor(to);
        m.treaty = treaty;
        m.thirdEmpire = third;
        m.system = system;
        m.offer = std::move(offer);
        m.request = std::move(request);
        auto text = speechLine(p_, pool, to, third, treaty, system);
        if (!text) return false;  // a message whose pool is empty is not sent at all
        m.text = std::move(*text);
        return p_.emit(cmd::SendMessage{std::move(m)});
    }
    bool sendNamed(EmpireId to, MessageType type, Treaty treaty = Treaty::None, EmpireId third = {}, SystemId system = {},
                   std::vector<PackageItem> offer = {}, std::vector<PackageItem> request = {}, const DiplomaticMessage* base = nullptr) {
        return send(to, type, std::format("Send {}", angerKeyName(type)), treaty, third, system, std::move(offer), std::move(request), -1, base);
    }
};

} // namespace

std::optional<std::string> speechLine(Planner& p, std::string_view pool, EmpireId target, EmpireId other, Treaty proposed, SystemId system,
                                      ObjectId planet) {
    const std::vector<std::string>* lines = p.prof.speech.pool(pool);
    if (!lines) return std::nullopt;
    std::string text = (*lines)[static_cast<size_t>(p.rng.below(lines->size()))];
    const Empire& us = p.emp();
    auto who = [&](EmpireId e) -> const Empire* {
        return e.valid() && e.index() < p.st.empires.size() ? &p.st.empire(e) : nullptr;
    };
    const Empire* them = who(target);
    const Empire* third = who(other);
    replaceAll(text, "[%OurEmperorTitle]", us.leaderTitle);
    replaceAll(text, "[%OurEmperorName]", us.leaderName);
    replaceAll(text, "[%OurEmpireName]", us.name);
    replaceAll(text, "[%TargetEmperorTitle]", them ? them->leaderTitle : std::string{});
    replaceAll(text, "[%TargetEmperorName]", them ? them->leaderName : std::string{});
    replaceAll(text, "[%TargetEmpireName]", them ? them->name : std::string{});
    replaceAll(text, "[%OtherEmperorTitle]", third ? third->leaderTitle : std::string{});
    replaceAll(text, "[%OtherEmperorName]", third ? third->leaderName : std::string{});
    replaceAll(text, "[%OtherEmpireName]", third ? third->name : std::string{});
    replaceAll(text, "[%TreatyName]", them ? displayName(us.relation(them->id).treaty) : std::string_view{});
    replaceAll(text, "[%ProposedTreatyName]", displayName(proposed));
    replaceAll(text, "[%SystemName]",
               system.valid() && system.index() < p.st.galaxy.systems.size() ? p.st.galaxy.system(system).name : std::string{});
    replaceAll(text, "[%PlanetName]",
               planet.valid() && planet.index() < p.st.galaxy.objects.size() ? p.st.galaxy.object(planet).name : std::string{});
    return text;
}

void planPolitics(Planner& p) { Politician(p).run(); }

} // namespace opense4::game::ai::detail
