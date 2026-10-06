#pragma once

// Internal to the SDK: the conversions between engine values and
// script::Value that the command codec and the view share.
//
// Codec<T> converts one C++ type: encode builds a value, decode checks the
// value's shape and fills the C++ value, naming the path to the first bad
// value. Structs list their fields once, in a fields() overload below that
// both directions use (as serialize_io.hpp does for the save format): the
// field names are the docs' (docs/sdk/commands.md), lower_snake_case, and
// never a Python keyword, so scripts can read them as attributes (an order's
// `from` is `from_resource`, a message's `from_empire`, Edit Design's `with`
// `new_design`).
//
// Encoding always writes every field, so a script can read any key without
// testing for it; decoding leaves a missing field at its default and refuses
// a field it does not know.

#include "sdk/codec.hpp"
#include "sdk/names.hpp"

#include <array>
#include <concepts>
#include <cstdint>
#include <format>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace opense4::sdk::detail {

using script::Value;
using script::ValueList;
using script::ValueMap;

// ---- Building values ---------------------------------------------------------------------------------

template <class T>
    requires std::integral<T> && (!std::same_as<T, bool>)
Value num(T v) {
    return Value(static_cast<int64_t>(v));
}

// An id, or null for none.
template <class Tag>
Value id(Id<Tag> i) {
    return i.valid() ? Value(static_cast<int64_t>(i.value)) : Value();
}

// A map in insertion order, built without Value::set's search for the key.
class Map {
public:
    explicit Map(size_t reserve = 16) { m_.reserve(reserve); }
    Map& operator()(std::string_view key, Value v) {
        m_.emplace_back(std::string(key), std::move(v));
        return *this;
    }
    Value done() { return Value(std::move(m_)); }

private:
    ValueMap m_;
};

// A list of f(x) for every x of the range.
template <class R, class F>
Value listOf(const R& range, F&& f) {
    ValueList l;
    l.reserve(std::size(range));
    for (const auto& x : range) l.push_back(f(x));
    return Value(std::move(l));
}

// ---- Decoding context ------------------------------------------------------------------------------------

// The path to the value being decoded, and the first error.
class Ctx {
public:
    class Scope {
    public:
        Scope(Ctx& c, size_t keep) : c_(c), keep_(keep) {}
        ~Scope() { c_.path_.resize(keep_); }
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;

    private:
        Ctx& c_;
        size_t keep_;
    };

    Scope key(std::string_view k) {
        const size_t keep = path_.size();
        if (!path_.empty()) path_ += '.';
        path_ += k;
        return {*this, keep};
    }
    Scope index(size_t i) {
        const size_t keep = path_.size();
        path_ += std::format("[{}]", i);
        return {*this, keep};
    }
    // Records the error at the current path (the first one only); returns false.
    bool fail(std::string message) {
        if (!error_) error_ = CodecError{path_, std::move(message)};
        return false;
    }
    const std::optional<CodecError>& error() const { return error_; }
    CodecError takeError() { return error_ ? std::move(*error_) : CodecError{path_, "invalid value"}; }

private:
    std::string path_;
    std::optional<CodecError> error_;
};

// ---- Codec<T> --------------------------------------------------------------------------------------------

template <class T>
struct Codec;

template <class T>
Value enc(const T& x) {
    return Codec<T>::enc(x);
}
template <class T>
bool dec(const Value& v, Ctx& c, T& x) {
    return Codec<T>::dec(v, c, x);
}

template <>
struct Codec<bool> {
    static Value enc(bool b) { return Value(b); }
    static bool dec(const Value& v, Ctx& c, bool& out) {
        if (!v.isBool()) return c.fail("expected true or false");
        out = v.asBool();
        return true;
    }
};

template <class T>
    requires std::integral<T> && (!std::same_as<T, bool>)
struct Codec<T> {
    static Value enc(T x) { return Value(static_cast<int64_t>(x)); }
    static bool dec(const Value& v, Ctx& c, T& out) {
        if (!v.isInt()) return c.fail("expected a whole number");
        const int64_t i = v.asInt();
        if (!std::in_range<T>(i))
            return c.fail(std::format("{} is out of range ({} to {})", i, std::numeric_limits<T>::min(), std::numeric_limits<T>::max()));
        out = static_cast<T>(i);
        return true;
    }
};

template <>
struct Codec<std::string> {
    static Value enc(const std::string& s) { return Value(s); }
    static bool dec(const Value& v, Ctx& c, std::string& out) {
        if (!v.isString()) return c.fail("expected text");
        out = v.asString();
        return true;
    }
};

template <class I>
constexpr std::string_view idWhat() {
    if constexpr (std::is_same_v<I, game::SystemId>) return "a system id";
    else if constexpr (std::is_same_v<I, game::ObjectId>) return "an object id";
    else if constexpr (std::is_same_v<I, game::EmpireId>) return "an empire id";
    else if constexpr (std::is_same_v<I, game::VehicleId>) return "a vehicle id";
    else if constexpr (std::is_same_v<I, game::FleetId>) return "a fleet id";
    else if constexpr (std::is_same_v<I, game::DesignId>) return "a design id";
    else if constexpr (std::is_same_v<I, game::MessageId>) return "a message id";
    else if constexpr (std::is_same_v<I, ruleset::TechAreaId>) return "a tech area id";
    else return "an id";
}

// Ids: a whole number, or null for none.
template <class Tag>
struct Codec<Id<Tag>> {
    static Value enc(Id<Tag> i) { return id(i); }
    static bool dec(const Value& v, Ctx& c, Id<Tag>& out) {
        if (v.isNull()) {
            out = {};
            return true;
        }
        if (!v.isInt() || v.asInt() < 0 || v.asInt() >= int64_t{Id<Tag>::kInvalid})
            return c.fail(std::format("expected {} (or null)", idWhat<Id<Tag>>()));
        out = Id<Tag>{v.asInt()};
        return true;
    }
};

// "a" or "an" before a word.
constexpr std::string_view article(std::string_view word) {
    return !word.empty() && std::string_view("aeiou").find(word.front()) != std::string_view::npos ? "an" : "a";
}

// Enumerations: their names.
template <NamedEnum E>
struct Codec<E> {
    static Value enc(E e) {
        const std::string_view n = enumName(e);
        return n.empty() ? Value(static_cast<int64_t>(e)) : Value(n);   // a value no name covers (never made by the engine)
    }
    static bool dec(const Value& v, Ctx& c, E& out) {
        constexpr std::string_view what = EnumNames<E>::kWhat;
        if (!v.isString()) return c.fail(std::format("expected {} {} name", article(what), what));
        const auto e = parseEnum<E>(v.asString());
        if (!e) return c.fail(std::format("'{}' is not {} {}", v.asString(), article(what), what));
        out = *e;
        return true;
    }
};

template <class T>
struct Codec<std::vector<T>> {
    static Value enc(const std::vector<T>& v) {
        ValueList l;
        l.reserve(v.size());
        for (const T& x : v) l.push_back(detail::enc(x));
        return Value(std::move(l));
    }
    static bool dec(const Value& v, Ctx& c, std::vector<T>& out) {
        if (!v.isList()) return c.fail("expected a list");
        const ValueList& l = v.asList();
        out.clear();
        out.reserve(l.size());
        for (size_t i = 0; i < l.size(); ++i) {
            const Ctx::Scope at = c.index(i);
            T x{};
            if (!detail::dec(l[i], c, x)) return false;
            out.push_back(std::move(x));
        }
        return true;
    }
};

template <class T, size_t N>
struct Codec<std::array<T, N>> {
    static Value enc(const std::array<T, N>& a) {
        ValueList l;
        l.reserve(N);
        for (const T& x : a) l.push_back(detail::enc(x));
        return Value(std::move(l));
    }
    static bool dec(const Value& v, Ctx& c, std::array<T, N>& out) {
        if (!v.isList() || v.asList().size() != N) return c.fail(std::format("expected a list of {}", N));
        for (size_t i = 0; i < N; ++i) {
            const Ctx::Scope at = c.index(i);
            if (!detail::dec(v.asList()[i], c, out[i])) return false;
        }
        return true;
    }
};

// Optional values: null for none.
template <class T>
struct Codec<std::optional<T>> {
    static Value enc(const std::optional<T>& o) { return o ? detail::enc(*o) : Value(); }
    static bool dec(const Value& v, Ctx& c, std::optional<T>& out) {
        if (v.isNull()) {
            out.reset();
            return true;
        }
        T x{};
        if (!detail::dec(v, c, x)) return false;
        out = std::move(x);
        return true;
    }
};

// ---- Structs: their fields --------------------------------------------------------------------------------

// Encodes the fields fields() lists, in that order.
struct Writer {
    ValueMap map;
    template <class T>
    void operator()(std::string_view key, T&& value) {
        map.emplace_back(std::string(key), detail::enc(std::as_const(value)));
    }
};

// Decodes the fields fields() lists from a map; missing ones keep their
// value, unknown ones are refused by finish().
class Reader {
public:
    Reader(const ValueMap& map, Ctx& ctx, std::vector<std::string_view> known = {}) : map_(map), ctx_(ctx), known_(std::move(known)) {}

    template <class T>
    void operator()(std::string_view key, T&& out) {
        known_.push_back(key);
        if (!ok_) return;
        for (const auto& [k, v] : map_) {
            if (k != key) continue;
            const Ctx::Scope at = ctx_.key(key);
            ok_ = detail::dec(v, ctx_, out);
            return;
        }
    }
    bool finish() {
        if (!ok_) return false;
        for (const auto& [k, v] : map_) {
            bool known = false;
            for (std::string_view n : known_) known = known || n == k;
            if (known) continue;
            const Ctx::Scope at = ctx_.key(k);
            return ctx_.fail("unknown field");
        }
        return true;
    }

private:
    const ValueMap& map_;
    Ctx& ctx_;
    std::vector<std::string_view> known_;
    bool ok_ = true;
};

template <class T>
concept HasFields = requires(Writer& w, T& x) { fields(w, x); };

template <class T>
    requires HasFields<T>
struct Codec<T> {
    static Value enc(const T& x) {
        Writer w;
        fields(w, const_cast<T&>(x));   // reads only: fields() hands the writer each member
        return Value(std::move(w.map));
    }
    static bool dec(const Value& v, Ctx& c, T& out) {
        if (!v.isMap()) return c.fail("expected a map");
        Reader r(v.asMap(), c);
        fields(r, out);
        return r.finish();
    }
};

// A Convert Resources order's `from` and `to`: Resource values held in a
// byte, any other value being an order that does nothing (Order::from).
struct ResourceCode {
    uint8_t* v;
};
template <>
struct Codec<ResourceCode> {
    static Value enc(const ResourceCode& p) {
        return *p.v < game::kResources.size() ? Value(enumName(static_cast<game::Resource>(*p.v))) : num(*p.v);
    }
    static bool dec(const Value& v, Ctx& c, ResourceCode& p) {
        if (v.isInt()) return Codec<uint8_t>::dec(v, c, *p.v);
        game::Resource r{};
        if (!Codec<game::Resource>::dec(v, c, r)) return false;
        *p.v = static_cast<uint8_t>(r);
        return true;
    }
};

// Empire::ministers and cmd::SetMinisters::areas: the minister areas switched
// on, as a list of minister names (bit i is Minister i).
struct MinisterAreas {
    uint32_t* v;
};
template <>
struct Codec<MinisterAreas> {
    static Value enc(const MinisterAreas& p) {
        ValueList l;
        for (size_t i = 0; i < game::kMinisters; ++i)
            if (*p.v & (uint32_t{1} << i)) l.push_back(Value(enumName(static_cast<game::Minister>(i))));
        return Value(std::move(l));
    }
    static bool dec(const Value& v, Ctx& c, MinisterAreas& p) {
        std::vector<game::Minister> list;
        if (!Codec<std::vector<game::Minister>>::dec(v, c, list)) return false;
        *p.v = 0;
        for (game::Minister m : list) *p.v |= game::ministerBit(m);
        return true;
    }
};
struct OptionalMinisterAreas {
    std::optional<uint32_t>* v;
};
template <>
struct Codec<OptionalMinisterAreas> {
    static Value enc(const OptionalMinisterAreas& p) {
        if (!*p.v) return Value();
        uint32_t bits = **p.v;
        return Codec<MinisterAreas>::enc(MinisterAreas{&bits});
    }
    static bool dec(const Value& v, Ctx& c, OptionalMinisterAreas& p) {
        if (v.isNull()) {
            p.v->reset();
            return true;
        }
        uint32_t bits = 0;
        MinisterAreas m{&bits};
        if (!Codec<MinisterAreas>::dec(v, c, m)) return false;
        *p.v = bits;
        return true;
    }
};

// Commands inside lists (EmpireOrders): codec.cpp.
template <>
struct Codec<game::Command> {
    static Value enc(const game::Command& c);
    static bool dec(const Value& v, Ctx& c, game::Command& out);
};

// ---- The fields of each struct ------------------------------------------------------------------------------

template <class A>
void fields(A& a, game::Sector& x) {
    a("x", x.x);
    a("y", x.y);
}
template <class A>
void fields(A& a, game::Location& x) {
    a("system", x.system);
    a("x", x.sector.x);
    a("y", x.sector.y);
}
template <class A>
void fields(A& a, game::Resources& x) {
    a("minerals", x.v[0]);
    a("organics", x.v[1]);
    a("radioactives", x.v[2]);
}
template <class A>
void fields(A& a, game::Order& x) {
    a("kind", x.kind);
    a("location", x.location);
    a("object", x.object);
    a("vehicle", x.vehicle);
    a("design", x.design);
    a("amount", x.amount);
    a("from_resource", ResourceCode{&x.from});
    a("to_resource", ResourceCode{&x.to});
}
template <class A>
void fields(A& a, game::PopulationGroup& x) {
    a("race", x.race);
    a("millions", x.millions);
}
template <class A>
void fields(A& a, game::UnitStack& x) {
    a("design", x.design);
    a("count", x.count);
}
template <class A>
void fields(A& a, game::Cargo& x) {
    a("population", x.population);
    a("units", x.units);
}
template <class A>
void fields(A& a, game::QueueItem& x) {
    a("kind", x.kind);
    a("design", x.design);
    a("facility", x.facility);
    a("count", x.count);
    a("spent", x.spent);
}
template <class A>
void fields(A& a, game::cmd::QueueTarget& x) {
    a("planet", x.planet);
    a("vehicle", x.vehicle);
}
template <class A>
void fields(A& a, game::DesignEntry& x) {
    a("component", x.component);
    a("mount", x.mount);
}
template <class A>
void fields(A& a, game::Design& x) {
    a("id", x.id);
    a("owner", x.owner);
    a("name", x.name);
    a("design_type", x.designType);
    a("hull", x.hull);
    a("entries", x.entries);
    a("strategy", x.strategy);
    a("obsolete", x.obsolete);
    a("created_turn", x.createdTurn);
    a("template_name", x.templateName);
    a("retrofitted", x.retrofitted);
    a("ever_built", x.everBuilt);
    a("built", x.built);
    a("lost", x.lost);
    a("enemy_tonnage_destroyed", x.enemyTonnageDestroyed);
    a("scrapped", x.scrapped);
    a("picture", x.picture);
}
template <class A>
void fields(A& a, game::ResearchProject& x) {
    a("area", x.area);
    a("progress", x.progress);
}
template <class A>
void fields(A& a, game::IntelProjectOrder& x) {
    a("project", x.project);
    a("target", x.target);
    a("target_planet", x.targetPlanet);
    a("target_vehicle", x.targetVehicle);
    a("third_empire", x.thirdEmpire);
    a("target_tech", x.targetTech);
    a("progress", x.progress);
}
template <class A>
void fields(A& a, game::PackageItem& x) {
    a("kind", x.kind);
    a("resources", x.resources);
    a("tech", x.tech);
    a("planet", x.planet);
    a("vehicle", x.vehicle);
    a("system", x.system);
    a("treaty", x.treaty);
    a("empire", x.empire);
}
template <class A>
void fields(A& a, game::DiplomaticMessage& x) {
    a("id", x.id);
    a("from_empire", x.from);
    a("to_empire", x.to);
    a("sent_turn", x.sentTurn);
    a("type", x.type);
    a("tone", x.tone);
    a("text", x.text);
    a("treaty", x.treaty);
    a("offer", x.offer);
    a("request", x.request);
    a("third_empire", x.thirdEmpire);
    a("system", x.system);
    a("planet", x.planet);
    a("in_reply_to", x.inReplyTo);
    a("delivered", x.delivered);
    a("answered", x.answered);
    a("dated", x.dated);
}
template <class A>
void fields(A& a, game::Waypoint& x) {
    a("name", x.name);
    a("location", x.location);
    a("set", x.set);
}
template <class A>
void fields(A& a, std::pair<std::string, std::string>& x) {   // a combat strategy's setting
    a("name", x.first);
    a("value", x.second);
}
template <class A>
void fields(A& a, ruleset::CombatStrategy& x) {
    a("name", x.name);
    a("settings", x.settings);
}
template <class A>
void fields(A& a, game::InterfaceOptions& x) {
    a("show_log_at_turn_start", x.showLogAtTurnStart);
    a("confirm_end_turn", x.confirmEndTurn);
    a("confirm_scrap", x.confirmScrap);
    a("confirm_stellar_manipulation", x.confirmStellarManipulation);
    a("confirm_delete_research", x.confirmDeleteResearch);
    a("confirm_delete_intel", x.confirmDeleteIntel);
    a("confirm_delete_first_queue_item", x.confirmDeleteFirstQueueItem);
    a("note_similar_abilities", x.noteSimilarAbilities);
    a("skip_under_construction", x.skipUnderConstruction);
    a("skip_damaged", x.skipDamaged);
    a("stop_once_per_location", x.stopOncePerLocation);
    a("skip_in_fleets", x.skipInFleets);
    a("warp_point_names", x.warpPointNames);
    a("planet_names", x.planetNames);
    a("colonizable_markers", x.colonizableMarkers);
    a("system_grid", x.systemGrid);
    a("coordinate_location", x.coordinateLocation);
    a("facility_markers", x.facilityMarkers);
    a("galaxy_grid_lines", x.galaxyGridLines);
    a("galaxy_warp_lines", x.galaxyWarpLines);
    a("latest_construction_only", x.latestConstructionOnly);
    a("latest_components_only", x.latestComponentsOnly);
    a("auto_claim_colonized", x.autoClaimColonized);
    a("log_filter", x.logFilter);
    a("log_position", x.logPosition);
    a("log_scroll", x.logScroll);
    a("planets_tab", x.planetsTab);
    a("planets_no_sys_to_avoid", x.planetsNoSysToAvoid);
    a("queues_tab", x.queuesTab);
    a("queues_shown", x.queuesShown);
    a("simulator_no_obsolete", x.simulatorNoObsolete);
    a("ships_tab", x.shipsTab);
    a("ships_shown", x.shipsShown);
    a("planets_sort", x.planetsSort);
    a("colonies_sort", x.coloniesSort);
    a("ships_sort", x.shipsSort);
    a("queues_sort", x.queuesSort);
    a("replay_animate", x.replayAnimate);
    a("replay_fast", x.replayFast);
    a("replay_view_rect", x.replayViewRect);
    a("replay_grid", x.replayGrid);
    a("design_to_hit", x.designToHit);
    a("design_condensed", x.designCondensed);
    a("designs_hide_obsolete", x.designsHideObsolete);
    a("designs_stats_view", x.designsStatsView);
}
template <class A>
void fields(A& a, game::combat::Square& x) {
    a("x", x.x);
    a("y", x.y);
}
template <class A>
void fields(A& a, game::combat::TacticalOrder& x) {
    a("kind", x.kind);
    a("empire", x.empire);
    a("piece", x.piece);
    a("target", x.target);
    a("weapon", x.weapon);
    a("instance", x.instance);
    a("x", x.x);
    a("y", x.y);
    a("path", x.path);
    a("design", x.design);
    a("count", x.count);
    a("group", x.group);
    a("formation", x.formation);
    a("on", x.on);
    a("alone", x.alone);
}
template <class A>
void fields(A& a, game::EmpireOrders& x) {
    a("empire", x.empire);
    a("turn", x.turn);
    a("commands", x.commands);
}
template <class A>
void fields(A& a, game::VictoryConditions& x) {
    a("score", x.score);
    a("score_value", x.scoreValue);
    a("years", x.years);
    a("years_value", x.yearsValue);
    a("percent_of_second", x.percentOfSecond);
    a("percent_of_second_value", x.percentOfSecondValue);
    a("tech_percent", x.techPercent);
    a("tech_percent_value", x.techPercentValue);
    a("peace", x.peace);
    a("peace_years", x.peaceYears);
    a("delay", x.delay);
    a("delay_years", x.delayYears);
}

// ---- Commands -----------------------------------------------------------------------------------------------

namespace cmd = game::cmd;

template <class A>
void fields(A& a, cmd::SetOrders& x) {
    a("vehicle", x.vehicle);
    a("fleet", x.fleet);
    a("planet", x.planet);
    a("orders", x.orders);
    a("repeat", x.repeat);
}
template <class A>
void fields(A& a, cmd::CreateFleet& x) {
    a("name", x.name);
    a("members", x.members);
}
template <class A>
void fields(A& a, cmd::OrderTagged& x) {
    a("vehicles", x.vehicles);
    a("orders", x.orders);
    a("repeat", x.repeat);
}
template <class A>
void fields(A& a, cmd::JoinFleet& x) {
    a("fleet", x.fleet);
    a("vehicle", x.vehicle);
}
template <class A>
void fields(A& a, cmd::LeaveFleet& x) {
    a("vehicle", x.vehicle);
}
template <class A>
void fields(A& a, cmd::DisbandFleet& x) {
    a("fleet", x.fleet);
}
template <class A>
void fields(A& a, cmd::SetFleetOptions& x) {
    a("fleet", x.fleet);
    a("formation", x.formation);
    a("strategy", x.strategy);
}
template <class A>
void fields(A& a, cmd::SetFleetLeader& x) {
    a("fleet", x.fleet);
    a("vehicle", x.vehicle);
}
template <class A>
void fields(A& a, cmd::SetVehicleStrategy& x) {
    a("design", x.design);
    a("strategy", x.strategy);
}
template <class A>
void fields(A& a, cmd::Rename& x) {
    a("vehicle", x.vehicle);
    a("fleet", x.fleet);
    a("design", x.design);
    a("planet", x.planet);
    a("name", x.name);
}
template <class A>
void fields(A& a, cmd::Scrap& x) {
    a("vehicle", x.vehicle);
    a("facility_planet", x.facilityPlanet);
    a("facility_slot", x.facilitySlot);
    a("move_first", x.moveFirst);
}
template <class A>
void fields(A& a, cmd::Mothball& x) {
    a("vehicle", x.vehicle);
    a("mothball", x.mothball);
}
template <class A>
void fields(A& a, cmd::Analyze& x) {
    a("vehicle", x.vehicle);
}
template <class A>
void fields(A& a, cmd::SelfDestruct& x) {
    a("vehicle", x.vehicle);
}
template <class A>
void fields(A& a, cmd::FireOn& x) {
    a("vehicle", x.vehicle);
}
template <class A>
void fields(A& a, cmd::SetMinister& x) {
    a("vehicle", x.vehicle);
    a("planet", x.planet);
    a("empire_wide", x.empireWide);
    a("on", x.on);
}
template <class A>
void fields(A& a, cmd::EnterSector& x) {
    a("vehicle", x.vehicle);
    a("fleet", x.fleet);
    a("where", x.where);
    a("enter", x.enter);
    a("tagged", x.tagged);
}
template <class A>
void fields(A& a, cmd::QueueAdd& x) {
    a("target", x.target);
    a("item", x.item);
    a("position", x.position);
}
template <class A>
void fields(A& a, cmd::QueueRemove& x) {
    a("target", x.target);
    a("index", x.index);
}
template <class A>
void fields(A& a, cmd::QueueMove& x) {
    a("target", x.target);
    a("from_index", x.from);
    a("to_index", x.to);
}
template <class A>
void fields(A& a, cmd::QueueSetCount& x) {
    a("target", x.target);
    a("index", x.index);
    a("count", x.count);
}
template <class A>
void fields(A& a, cmd::QueueFlags& x) {
    a("target", x.target);
    a("on_hold", x.onHold);
    a("repeat", x.repeat);
    a("emergency", x.emergency);
    a("auto_waypoint", x.autoWaypoint);
}
template <class A>
void fields(A& a, cmd::QueueReplaceFacility& x) {
    a("target", x.target);
    a("index", x.index);
    a("facility", x.facility);
}
template <class A>
void fields(A& a, cmd::Retrofit& x) {
    a("vehicle", x.vehicle);
    a("design", x.design);
}
template <class A>
void fields(A& a, cmd::SetColonyType& x) {
    a("planet", x.planet);
    a("colony_type", x.colonyType);
}
template <class A>
void fields(A& a, cmd::AbandonPlanet& x) {
    a("planet", x.planet);
}
template <class A>
void fields(A& a, cmd::CloakColony& x) {
    a("planet", x.planet);
    a("cloak", x.cloak);
}
template <class A>
void fields(A& a, cmd::TransferCargo& x) {
    a("from_vehicle", x.fromVehicle);
    a("from_planet", x.fromPlanet);
    a("to_vehicle", x.toVehicle);
    a("to_planet", x.toPlanet);
    a("unit_design", x.unitDesign);
    a("population_race", x.populationRace);
    a("amount", x.amount);
}
template <class A>
void fields(A& a, cmd::JettisonCargo& x) {
    a("vehicle", x.vehicle);
    a("planet", x.planet);
    a("population", x.population);
    a("units", x.units);
}
template <class A>
void fields(A& a, cmd::CreateDesign& x) {
    a("design", x.design);
}
template <class A>
void fields(A& a, cmd::EditDesign& x) {
    a("design", x.design);
    a("new_design", x.with);
}
template <class A>
void fields(A& a, cmd::SetDesignObsolete& x) {
    a("design", x.design);
    a("obsolete", x.obsolete);
}
template <class A>
void fields(A& a, cmd::DeleteDesign& x) {
    a("design", x.design);
}
template <class A>
void fields(A& a, cmd::SetResearch& x) {
    a("queue", x.queue);
    a("evenly", x.evenly);
    a("repeat", x.repeat);
}
template <class A>
void fields(A& a, cmd::SetIntel& x) {
    a("queue", x.queue);
    a("evenly", x.evenly);
    a("repeat", x.repeat);
}
template <class A>
void fields(A& a, cmd::SendMessage& x) {
    a("message", x.message);
    a("minister", x.minister);
}
template <class A>
void fields(A& a, cmd::AnswerMessage& x) {
    a("message", x.message);
    a("accept", x.accept);
    a("text", x.text);
}
template <class A>
void fields(A& a, cmd::DecideWar& x) {
    a("target", x.target);
}
template <class A>
void fields(A& a, cmd::CarryOutDemand& x) {
    a("demand", x.demand);
}
template <class A>
void fields(A& a, cmd::UseDemandEntry& x) {
    a("list", x.list);
    a("about", x.about);
}
template <class A>
void fields(A& a, cmd::SetWaypoint& x) {
    a("slot", x.slot);
    a("waypoint", x.waypoint);
}
template <class A>
void fields(A& a, cmd::SetSystemFlags& x) {
    a("system", x.system);
    a("avoid", x.avoid);
    a("claim", x.claim);
}
template <class A>
void fields(A& a, cmd::SetSystemNote& x) {
    a("system", x.system);
    a("note", x.note);
}
template <class A>
void fields(A& a, cmd::TagMinefield& x) {
    a("location", x.location);
    a("tagged", x.tagged);
}
template <class A>
void fields(A& a, cmd::SetStrategy& x) {
    a("index", x.index);
    a("strategy", x.strategy);
    a("remove", x.remove);
}
template <class A>
void fields(A& a, cmd::SetRepairPriorities& x) {
    a("priorities", x.priorities);
}
template <class A>
void fields(A& a, cmd::SetDesignTypes& x) {
    a("design_types", x.designTypes);
}
template <class A>
void fields(A& a, cmd::SetColonyTypes& x) {
    a("colony_types", x.colonyTypes);
}
template <class A>
void fields(A& a, cmd::SetEmpireOptions& x) {
    a("ai_minimal_changes", x.aiMinimalChanges);
    a("password_hash", x.passwordHash);
    a("choose_colony_type", x.chooseColonyType);
}
template <class A>
void fields(A& a, cmd::SetEmail& x) {
    a("email", x.email);
}
template <class A>
void fields(A& a, cmd::SetMinisters& x) {
    a("areas", OptionalMinisterAreas{&x.areas});
    a("style", x.style);
    a("use_race_style", x.useRaceStyle);
    a("new_vehicles", x.newVehicles);
    a("individual", x.individual);
    a("fleets", x.fleets);
    a("complete_ai", x.completeAi);
}
template <class A>
void fields(A& a, cmd::SetEncounterOptions& x) {
    a("clear_orders_on_encounter", x.clearOrdersOnEncounter);
    a("avoid_tagged_minefields", x.avoidTaggedMinefields);
    a("avoid_restricted_systems", x.avoidRestrictedSystems);
}
template <class A>
void fields(A& a, cmd::SetInterfaceOptions& x) {
    a("options", x.options);
}
template <class A>
void fields(A& a, cmd::OpenVehicleReport& x) {
    a("vehicle", x.vehicle);
}

// The number of fields fields() lists for T (tests compare it with the struct's members).
struct FieldCounter {
    size_t count = 0;
    template <class T>
    void operator()(std::string_view, T&&) {
        ++count;
    }
};

} // namespace opense4::sdk::detail
