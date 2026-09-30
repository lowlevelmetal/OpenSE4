// Computer player: diplomacy (spec 05 §7.4 AI_Politics, §7.5 AI_Speech, §7.6).

#include "datafile/datafile.hpp"
#include "game/ai_planner.hpp"
#include "game/query.hpp"
#include "game/research.hpp"

#include <algorithm>
#include <format>
#include <map>

namespace opense4::game::ai::detail {

namespace {

using datafile::keysEqual;

// Politics treats non-hostile empires as friends (inferred).
bool isFriend(const Empire& e, EmpireId other) {
    const Treaty t = e.relation(other).treaty;
    return t != Treaty::War && t != Treaty::NonIntercourse;
}

int warsExcept(const GameState& s, const Empire& e, EmpireId other) {
    int n = 0;
    for (const Empire& o : s.empires)
        if (o.alive && o.id != e.id && o.id != other && e.relation(o.id).treaty == Treaty::War) ++n;
    return n;
}

// Spec 05 §7.4: base + wars + early game + stronger/weaker adjustments.
int threshold(const TreatyRule& rule, int wars, uint32_t turn, int64_t scorePct) {
    int t = rule.baseAnger + rule.perOtherWars * wars;
    if (turn < 50) t += rule.first50Turns;
    if (rule.strongerPercent > 0 && scorePct >= rule.strongerPercent) t += rule.strongerAmount;
    else if (rule.weakerPercent > 0 && scorePct <= rule.weakerPercent) t += rule.weakerAmount;
    return t;
}

int toneFor(const PoliticsTable& pol, int64_t scorePct) {
    if (scorePct <= pol.demandingTonePercent) return 2;
    if (scorePct >= pol.pleadingTonePercent) return 0;
    return 1;
}

// Rough worth of a package item to the receiver (inferred; spec 05 open question 12).
int64_t itemValue(const Rules& r, const GameState& s, const PackageItem& item, EmpireId giver, EmpireId receiver) {
    switch (item.kind) {
        case PackageItem::Kind::Resources: return item.resources.total();
        case PackageItem::Kind::Technology: {
            if (!item.tech.valid() || item.tech.index() >= r.data().techAreas.size()) return 0;
            const int have = s.empire(receiver).techLevel(item.tech);
            const int get = s.empire(giver).techLevel(item.tech);
            int64_t v = 0;
            for (int l = have + 1; l <= get; ++l) v += research::levelCost(r, s, item.tech, l);
            return v;
        }
        case PackageItem::Kind::Planet: {
            const Colony* c = s.colony(item.planet);
            if (!c) return 1000;
            return 3000 + c->totalPopulation() * 5 + static_cast<int64_t>(c->facilities.size()) * 500;
        }
        case PackageItem::Kind::Vehicle: {
            const Vehicle* v = s.vehicle(item.vehicle);
            return v ? computeDesignStats(r, nullptr, s.design(v->design)).cost.total() * std::max(1, v->count) : 0;
        }
        case PackageItem::Kind::StarChart: return 500;
        case PackageItem::Kind::Treaty: return 0;
        case PackageItem::Kind::CommChannel: return 1000;
        case PackageItem::Kind::System: return 1000;
    }
    return 0;
}

int64_t packageValue(const Rules& r, const GameState& s, const std::vector<PackageItem>& items, EmpireId giver, EmpireId receiver) {
    int64_t v = 0;
    for (const auto& i : items) v += itemValue(r, s, i, giver, receiver);
    return v;
}

void replaceAll(std::string& text, std::string_view token, std::string_view value) {
    for (size_t pos = text.find(token); pos != std::string::npos; pos = text.find(token, pos + value.size()))
        text.replace(pos, token.size(), value);
}

bool pendingProposal(const GameState& s, EmpireId from, EmpireId to) {
    for (const auto& m : s.messages)
        if (m.from == from && m.to == to && m.type == MessageType::ProposeTreaty && !m.answered) return true;
    return false;
}

// A binding message we sent recently that may not have taken effect yet.
bool recentlySent(const GameState& s, EmpireId from, EmpireId to, MessageType type) {
    for (const auto& m : s.messages)
        if (m.from == from && m.to == to && m.type == type && m.sentTurn + 2 > s.turn) return true;
    return false;
}

class Diplomat {
public:
    explicit Diplomat(Planner& p) : p_(p), pol_(p.prof.politics), scores_(politicalScores(p.r, p.st)), mee_(megaEvilEmpire(p.r, p.st)) {}

    void run() {
        answerMessages();
        for (const Empire& other : p_.st.empires) {
            if (!other.alive || other.id == p_.id || !p_.emp().relation(other.id).contact) continue;
            initiate(other.id);
        }
    }

private:
    int64_t scorePct(EmpireId other) const {
        const int64_t ours = scores_[p_.id.index()];
        const int64_t theirs = scores_[other.index()];
        if (ours <= 0) return theirs > 0 ? 1000 : 100;
        return theirs * 100 / ours;
    }
    int anger(EmpireId other) const { return p_.emp().relation(other).anger; }
    int wars(EmpireId other) const { return warsExcept(p_.st, p_.emp(), other); }
    bool team(EmpireId other) const {
        return p_.st.options.teamMode && p_.st.empire(other).kind != PlayerKind::Human && p_.emp().kind != PlayerKind::Human;
    }

    bool send(EmpireId to, MessageType type, std::string_view pool, Treaty treaty = Treaty::None, EmpireId third = {},
              SystemId system = {}, std::vector<PackageItem> offer = {}) {
        DiplomaticMessage m;
        m.to = to;
        m.type = type;
        m.tone = toneFor(pol_, scorePct(to));
        m.treaty = treaty;
        m.thirdEmpire = third;
        m.system = system;
        m.offer = std::move(offer);
        m.text = speechLine(p_, pool, to, third, treaty, system);
        return p_.emit(cmd::SendMessage{std::move(m)});
    }
    bool sendNamed(EmpireId to, MessageType type, Treaty treaty = Treaty::None, EmpireId third = {}, SystemId system = {},
                   std::vector<PackageItem> offer = {}) {
        return send(to, type, std::format("Send {}", angerKeyName(type)), treaty, third, system, std::move(offer));
    }

    // ---- Incoming ----------------------------------------------------------------------------

    void answerMessages() {
        std::vector<MessageId> inbox;
        for (const auto& m : p_.st.messages)
            if (m.to == p_.id && m.delivered && !m.answered) inbox.push_back(m.id);
        for (MessageId id : inbox) {
            auto it = std::find_if(p_.st.messages.begin(), p_.st.messages.end(), [&](const DiplomaticMessage& m) { return m.id == id; });
            if (it == p_.st.messages.end()) continue;
            const DiplomaticMessage msg = *it;  // copy: answering appends to the list
            if (!msg.from.valid() || msg.from.index() >= p_.st.empires.size()) continue;
            answer(msg);
        }
    }

    void answer(const DiplomaticMessage& msg) {
        const EmpireId from = msg.from;
        const int64_t pct = scorePct(from);
        // An AI bent on war with the sender ignores it (spec 05 §7.4).
        if (p_.atWarWith(from) && anger(from) > threshold(pol_.declareWar, wars(from), p_.st.turn, pct) && !team(from)) return;
        bool accept = false;
        std::string pool;
        const bool friendly = isFriend(p_.emp(), from);
        switch (msg.type) {
            case MessageType::ProposeTreaty:
            case MessageType::CounterTreaty:  // accepting a counter-proposal is an Accept Demand reply
                accept = acceptTreaty(msg, pct);
                pool = accept ? "Send Accept Treaty" : "Send Refuse Treaty";
                break;
            case MessageType::ProposeTrade:
            case MessageType::CounterTrade: {
                const int64_t received = packageValue(p_.r, p_.st, msg.offer, from, p_.id);
                const int64_t given = packageValue(p_.r, p_.st, msg.request, p_.id, from);
                const int need = friendly ? pol_.acceptTradeFriendPercent : pol_.acceptTradeEnemyPercent;
                accept = given <= 0 ? anger(from) <= pol_.maxAngerAcceptGift : received * 100 >= given * need;
                pool = accept ? "Send Accept Trade" : "Send Refuse Trade";
                break;
            }
            case MessageType::Gift:
                accept = anger(from) <= pol_.maxAngerAcceptGift;
                pool = accept ? "Send Accept Gift" : "Send Refuse Gift";
                break;
            case MessageType::Tribute:
                accept = anger(from) <= pol_.maxAngerAcceptTribute;
                pool = accept ? "Send Accept Tribute" : "Send Refuse Tribute";
                break;
            default:
                if (!isDemand(msg.type)) return;  // nothing to answer
                accept = acceptDemand(msg, pct, friendly);
                pool = std::format("Response {} {} {}", friendly ? "Friend" : "Enemy", accept ? "YES" : "NO", angerKeyName(msg.type));
                if (!p_.prof.speech.pool(pool)) pool = accept ? "Send Accept Demand/Request" : "Send Refuse Demand/Request";
                break;
        }
        std::string text = speechLine(p_, pool, from, msg.thirdEmpire, msg.treaty, msg.system, msg.planet);
        if (!p_.emit(cmd::AnswerMessage{msg.id, accept, std::move(text)})) return;
        if (accept && isDemand(msg.type)) comply(msg);
    }

    bool acceptTreaty(const DiplomaticMessage& msg, int64_t pct) {
        const EmpireId from = msg.from;
        if (team(from)) return true;
        if (p_.st.options.teamMode && p_.st.empire(from).kind == PlayerKind::Human) return false;
        const Relation& rel = p_.emp().relation(from);
        const Treaty t = msg.treaty;
        if (t > pol_.highestAllowedTreaty || t == Treaty::War) return false;
        if (t == Treaty::Subjugation && pct < pol_.acceptSubjugationPercent) return false;
        if (t == Treaty::Protectorate && pct < pol_.acceptProtectoratePercent) return false;
        if (rel.treatyTurn > 0 && p_.st.turn < rel.treatyTurn + static_cast<uint32_t>(std::max(0, pol_.acceptMinimumTurnsSinceTreaty)))
            return false;
        if (t > Treaty::NonAggression && rel.lastWarTurn >= 0 &&
            static_cast<int64_t>(p_.st.turn) - rel.lastWarTurn < pol_.turnsSinceWarBeforeFriendly)
            return false;
        int limit = threshold(pol_.accept, wars(from), p_.st.turn, pct);
        const int steps = static_cast<int>(t) - static_cast<int>(rel.treaty);
        if (steps > 0) limit += pol_.acceptPerHigherLevel * steps;
        if (anger(from) < limit) return true;
        return p_.rng.percent(pol_.acceptMinimumChance);  // (inferred reading of "Minimum Anger Chance")
    }

    bool acceptDemand(const DiplomaticMessage& msg, int64_t pct, bool friendly) {
        const DemandRule& rule = pol_.demands[static_cast<size_t>(msg.type)];
        if (msg.type == MessageType::DemandSurrender && p_.mode == Mode::Minister) return false;  // ministers never surrender
        if (!(friendly ? rule.acceptFromFriend : rule.acceptFromEnemy)) return false;
        return pct >= rule.acceptScorePercent;
    }

    // What an accepted demand makes the computer do this turn.
    void comply(const DiplomaticMessage& msg) {
        const EmpireId from = msg.from;
        switch (msg.type) {
            case MessageType::DemandRemoveShips:
            case MessageType::DemandStopAttacks: withdrawFrom(msg.system); break;
            case MessageType::DemandRemoveColonies:
                if (msg.system.valid() && msg.system.index() < p_.st.galaxy.systems.size())
                    for (ObjectId o : p_.st.galaxy.system(msg.system).objects)
                        if (const Colony* c = p_.st.colony(o); c && c->owner == p_.id && !c->homeworld) p_.emit(cmd::AbandonPlanet{o});
                break;
            case MessageType::DemandLeavePlanet:
                if (const Colony* c = p_.st.colony(msg.planet); c && c->owner == p_.id && !c->homeworld) p_.emit(cmd::AbandonPlanet{msg.planet});
                break;
            case MessageType::RequestStopHostilities:
            case MessageType::RequestMakePeace:
                if (validThird(msg.thirdEmpire) && p_.atWarWith(msg.thirdEmpire))
                    sendNamed(msg.thirdEmpire, MessageType::ProposeTreaty, Treaty::NonAggression);
                break;
            case MessageType::RequestBreakTreaty:
                if (validThird(msg.thirdEmpire) && p_.emp().relation(msg.thirdEmpire).treaty >= Treaty::NonAggression)
                    sendNamed(msg.thirdEmpire, MessageType::BreakTreaty, p_.emp().relation(msg.thirdEmpire).treaty);
                break;
            case MessageType::RequestDeclareWar:
            case MessageType::RequestAttackEmpire:
            case MessageType::RequestSupport:
                if (validThird(msg.thirdEmpire) && !p_.atWarWith(msg.thirdEmpire)) sendNamed(msg.thirdEmpire, MessageType::DeclareWar);
                break;
            case MessageType::DemandStopEspionage:
            case MessageType::DemandStopSabotage: {
                std::vector<IntelProjectOrder> keep;
                for (const auto& o : p_.emp().intel)
                    if (o.target != from) keep.push_back(o);
                if (keep.size() != p_.emp().intel.size()) p_.emit(cmd::SetIntel{keep, p_.emp().intelEvenly, p_.emp().repeatIntel});
                break;
            }
            case MessageType::DemandSurrender: sendNamed(from, MessageType::Surrender); break;
            case MessageType::DemandGift:
            case MessageType::DemandTribute: {
                // Accepting a demand moves nothing by itself: pay up (inferred amount).
                const bool gift = msg.type == MessageType::DemandGift;
                const bool friendly = isFriend(p_.emp(), from);
                const int64_t base = gift ? (friendly ? pol_.giftBaseFriend : pol_.giftBaseEnemy)
                                          : (friendly ? pol_.tributeBaseFriend : pol_.tributeBaseEnemy);
                auto offer = resourcesWorth(base);
                if (!offer.empty() && p_.st.options.allowGifts)
                    sendNamed(from, gift ? MessageType::Gift : MessageType::Tribute, Treaty::None, {}, {}, std::move(offer));
                break;
            }
            default: break;
        }
    }

    bool validThird(EmpireId e) const {
        return e.valid() && e.index() < p_.st.empires.size() && e != p_.id && p_.st.empire(e).alive && p_.emp().relation(e).contact;
    }

    void withdrawFrom(SystemId sys) {
        if (!sys.valid() || !p_.home.valid() || sys == p_.home) return;
        for (const Fleet& f : p_.st.fleets) {
            const Vehicle* leader = p_.st.vehicle(f.leader);
            if (p_.controlsFleet(f) && leader && leader->location.system == sys) {
                Order o;
                o.kind = OrderKind::MoveTo;
                o.location = p_.homeLocation;
                p_.setFleetOrders(f.id, {o});
            }
        }
        for (VehicleId id : p_.ownVehicles()) {
            const Vehicle* v = p_.st.vehicle(id);
            if (!v || v->fleet.valid() || v->location.system != sys || p_.info(v->design).stats.movement <= 0) continue;
            Order o;
            o.kind = OrderKind::MoveTo;
            o.location = p_.homeLocation;
            p_.setOrders(id, {o});
        }
    }

    // ---- Outgoing ----------------------------------------------------------------------------

    void initiate(EmpireId other) {
        const Relation& rel = p_.emp().relation(other);
        const int64_t pct = scorePct(other);
        const int a = anger(other);
        const int w = wars(other);

        if (p_.st.options.teamMode) {
            // Team mode (spec 05 §7.1): computers band together against humans.
            if (team(other)) {
                if (rel.treaty != Treaty::Partnership && !pendingProposal(p_.st, p_.id, other))
                    sendNamed(other, MessageType::ProposeTreaty, Treaty::Partnership);
            } else if (p_.st.empire(other).kind == PlayerKind::Human && rel.treaty != Treaty::War &&
                       !recentlySent(p_.st, p_.id, other, MessageType::DeclareWar)) {
                sendNamed(other, MessageType::DeclareWar);
            }
            return;
        }

        if (rel.treaty != Treaty::War && !p_.neutral && a > threshold(pol_.declareWar, w, p_.st.turn, pct)) {
            if (!recentlySent(p_.st, p_.id, other, MessageType::DeclareWar)) sendNamed(other, MessageType::DeclareWar);
            return;
        }
        if (rel.treaty >= Treaty::NonAggression && a > threshold(pol_.breakTreaty, w, p_.st.turn, pct)) {
            if (!recentlySent(p_.st, p_.id, other, MessageType::BreakTreaty)) sendNamed(other, MessageType::BreakTreaty, rel.treaty);
            return;
        }
        if (other == mee_ && p_.rng.percent(10)) {
            if (send(other, MessageType::General, "Mega Evil Declarations")) return;
        }
        if (!p_.neutral && p_.rng.percent(pol_.proposeChancePercent) && !pendingProposal(p_.st, p_.id, other)) {
            const int limit = threshold(pol_.propose, w, p_.st.turn, pct);
            if (rel.treaty == Treaty::War) {
                // Peace after a long enough war (inferred).
                if (a < limit && rel.lastWarTurn >= 0 && static_cast<int64_t>(p_.st.turn) - rel.lastWarTurn >= pol_.turnsSinceWarBeforeFriendly &&
                    sendNamed(other, MessageType::ProposeTreaty, Treaty::NonAggression))
                    return;
            } else {
                for (const auto& [treaty, below] : pol_.proposeTypes) {
                    if (treaty <= rel.treaty || treaty > pol_.highestAllowedTreaty) continue;  // never propose the current one
                    if (a <= limit - below) {
                        if (sendNamed(other, MessageType::ProposeTreaty, treaty)) return;
                        break;
                    }
                }
            }
        }
        if (!p_.neutral && demands(other, pct, a)) return;
        if (p_.st.options.allowGifts) payments(other, pct, a);
    }

    bool mayDemand(MessageType t, EmpireId other, int64_t pct) const {
        const DemandRule& rule = pol_.demands[static_cast<size_t>(t)];
        const bool friendly = isFriend(p_.emp(), other);
        return pct <= rule.sendScorePercent && (friendly ? rule.sendToFriend : rule.sendToEnemy);
    }

    bool demands(EmpireId other, int64_t pct, int a) {
        // Their ships inside our claimed systems.
        if (!allied(p_.st, p_.id, other) && mayDemand(MessageType::DemandRemoveShips, other, pct) && p_.rng.percent(25)) {
            std::map<SystemId, int> count;
            for (VehicleId vid : p_.emp().knowledge.visibleVehicles)
                if (const Vehicle* v = p_.st.vehicle(vid); v && v->owner == other &&
                                                             std::binary_search(p_.emp().claimedSystems.begin(), p_.emp().claimedSystems.end(),
                                                                                v->location.system))
                    ++count[v->location.system];
            SystemId worst;
            int most = 0;
            for (const auto& [sys, n] : count)
                if (n > most) {
                    most = n;
                    worst = sys;
                }
            if (worst.valid() && sendNamed(other, MessageType::DemandRemoveShips, Treaty::None, {}, worst)) return true;
        }
        // Ask a friend to join a war.
        if (isFriend(p_.emp(), other) && p_.emp().relation(other).treaty >= Treaty::TradeAlliance &&
            mayDemand(MessageType::RequestDeclareWar, other, pct) && p_.rng.percent(10)) {
            for (const Empire& enemy : p_.st.empires)
                if (enemy.alive && enemy.id != other && p_.atWarWith(enemy.id) && p_.st.empire(other).relation(enemy.id).treaty != Treaty::War &&
                    sendNamed(other, MessageType::RequestDeclareWar, Treaty::None, enemy.id))
                    return true;
        }
        // A stronger empire leans on a weaker one now and then.
        if (a > 20 && mayDemand(MessageType::DemandGift, other, pct) && p_.rng.percent(3))
            return sendNamed(other, MessageType::DemandGift);
        return false;
    }

    std::vector<PackageItem> resourcesWorth(int64_t value) const {
        const Resources& have = p_.emp().stockpile;
        value = std::min(value, have.total() / 10);
        value = value / 1000 * 1000;  // packages move resources in steps of 1000
        if (value <= 0) return {};
        PackageItem item;
        item.kind = PackageItem::Kind::Resources;
        for (size_t i = 0; i < 3 && value > 0; ++i) {
            const int64_t part = std::min(value, have.v[i] / 1000 * 1000);
            item.resources.v[i] = part;
            value -= part;
        }
        if (item.resources.isZero()) return {};
        return {item};
    }

    void payments(EmpireId other, int64_t pct, int a) {
        const bool friendly = isFriend(p_.emp(), other);
        // Tribute to a much stronger empire that is not a friend (inferred trigger).
        if (!friendly || p_.emp().relation(other).treaty <= Treaty::None) {
            if (pct >= pol_.pleadingTonePercent && a <= (friendly ? pol_.tributeMaxAngerFriend : pol_.tributeMaxAngerEnemy) &&
                p_.rng.percent(5)) {
                const int64_t base = friendly ? pol_.tributeBaseFriend : pol_.tributeBaseEnemy;
                const int64_t per = friendly ? pol_.tributePerPercentFriend : pol_.tributePerPercentEnemy;
                auto offer = resourcesWorth(base + per * std::max<int64_t>(0, pct - 100));
                if (!offer.empty()) sendNamed(other, MessageType::Tribute, Treaty::None, {}, {}, std::move(offer));
            }
            return;
        }
        // Gifts to good friends when rich (inferred trigger).
        if (p_.emp().relation(other).treaty >= Treaty::TradeAlliance && a <= pol_.giftMaxAngerFriend && p_.emp().stockpile.total() > 30000 &&
            p_.rng.percent(3)) {
            auto offer = resourcesWorth(pol_.giftBaseFriend + pol_.giftPerPercentFriend * std::max<int64_t>(0, pct - 100));
            if (!offer.empty()) sendNamed(other, MessageType::Gift, Treaty::None, {}, {}, std::move(offer));
        }
    }

    Planner& p_;
    const PoliticsTable& pol_;
    std::vector<int64_t> scores_;
    EmpireId mee_;
};

} // namespace

std::string speechLine(Planner& p, std::string_view pool, EmpireId target, EmpireId other, Treaty proposed, SystemId system,
                       ObjectId planet) {
    const std::vector<std::string>* lines = p.prof.speech.pool(pool);
    if (!lines) lines = builtinProfile().speech.pool(pool);
    if (!lines) return {};
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

void planDiplomacy(Planner& p) { Diplomat(p).run(); }

} // namespace opense4::game::ai::detail
