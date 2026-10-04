#include "game/log_picture.hpp"

#include "datafile/datafile.hpp"
#include "game/query.hpp"

#include <charconv>
#include <format>

namespace opense4::game::logpicture {

namespace {

bool number(std::string_view text, uint32_t& out) {
    const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), out);
    return ec == std::errc{} && end == text.data() + text.size() && !text.empty();
}

// The word of the file name for a surface or an atmosphere (spec 06 §4.1).
std::string_view surfaceWord(std::string_view surface) {
    if (datafile::keysEqual(surface, "Ice")) return "Ice";
    if (datafile::keysEqual(surface, "Gas Giant") || datafile::keysEqual(surface, "Gas")) return "Gas";
    return "Rock";
}

std::string_view atmosphereWord(std::string_view atmosphere) {
    for (std::string_view a : {"Oxygen", "Methane", "Hydrogen", "Argon", "None"})
        if (datafile::keysEqual(atmosphere, a)) return a;
    if (datafile::keysEqual(atmosphere, "Carbon Dioxide") || datafile::keysEqual(atmosphere, "CarbDiox")) return "CarbDiox";
    return "None";
}

} // namespace

std::string race(EmpireId e) { return std::format("race:{}", e.value); }
std::string hull(DesignId d) { return std::format("hull:{}", d.value); }
std::string planet(ObjectId p) { return std::format("planet:{}", p.value); }
std::string facility(uint32_t f) { return std::format("facility:{}", f); }
std::string fleet(EmpireId e) { return std::format("fleet:{}", e.value); }

std::string colonyFounded(const SpaceObject& planet) {
    return std::format("Colony{}{}", surfaceWord(planet.surface), atmosphereWord(planet.atmosphere));
}

std::string unitGroup(ruleset::VehicleType type, EmpireId owner, DesignId design) {
    switch (type) {
        case ruleset::VehicleType::Fighter: return std::format("group:fighter:{}", owner.value);
        case ruleset::VehicleType::Mine: return std::format("group:mine:{}", owner.value);
        case ruleset::VehicleType::Satellite: return std::format("group:satellite:{}", owner.value);
        default: return hull(design);
    }
}

std::string vehicle(const Rules& r, const GameState& s, const Vehicle& v) {
    if (v.fleet.valid()) return fleet(v.owner);
    const ruleset::VehicleType type = vehicleType(r, s, v);
    if (isUnitType(type)) return unitGroup(type, v.owner, v.design);
    return hull(v.design);
}

std::string developed(const Rules& r, size_t index) {
    const auto& d = r.data();
    if (index < d.components.size()) return std::format("developed:component:{}", index);
    index -= d.components.size();
    if (index < d.facilities.size()) return std::format("developed:facility:{}", index);
    index -= d.facilities.size();
    if (index < d.vehicleSizes.size()) return std::format("developed:hull:{}", index);
    index -= d.vehicleSizes.size();
    return std::format("developed:intel:{}", index);
}

Parsed parse(std::string_view picture) {
    Parsed out;
    if (picture.empty()) return out;
    const size_t colon = picture.find(':');
    if (colon == std::string_view::npos) {
        out.kind = Parsed::Kind::Event;
        out.name = std::string(picture);
        return out;
    }
    const std::string_view kind = picture.substr(0, colon);
    std::string_view rest = picture.substr(colon + 1);
    uint32_t n = 0;
    auto twoParts = [&](Parsed::Kind k) {
        const size_t second = rest.find(':');
        if (second == std::string_view::npos || !number(rest.substr(second + 1), n)) return;
        out.kind = k;
        out.name = std::string(rest.substr(0, second));
        out.id = n;
        out.empire = EmpireId{n};
    };
    if (kind == "race" && number(rest, n)) out = Parsed{Parsed::Kind::Race, {}, 0, EmpireId{n}};
    else if (kind == "fleet" && number(rest, n)) out = Parsed{Parsed::Kind::Fleet, {}, 0, EmpireId{n}};
    else if (kind == "hull" && number(rest, n)) out = Parsed{Parsed::Kind::Hull, {}, n, {}};
    else if (kind == "planet" && number(rest, n)) out = Parsed{Parsed::Kind::Planet, {}, n, {}};
    else if (kind == "facility" && number(rest, n)) out = Parsed{Parsed::Kind::Facility, {}, n, {}};
    else if (kind == "group") twoParts(Parsed::Kind::Group);
    else if (kind == "developed") twoParts(Parsed::Kind::Developed);
    return out;
}

} // namespace opense4::game::logpicture
