#include "client/classic/screens/design_tools.hpp"

#include "datafile/datafile.hpp"
#include "game/design.hpp"

#include <algorithm>
#include <array>
#include <format>

namespace opense4::client::classic {

using ruleset::VehicleType;
using ruleset::WeaponKind;

bool isUnitHull(VehicleType t) { return t != VehicleType::Ship && t != VehicleType::Base; }

bool isLatestComponent(const game::Rules& r, const game::Empire& e, uint32_t component) {
    const auto target = r.componentUpgradeTarget(e, r.component(component).family);
    return !target || *target == component;
}

bool upgradeEntries(const game::Rules& r, const game::Empire& e, std::vector<game::DesignEntry>& entries) {
    bool changed = false;
    for (game::DesignEntry& entry : entries) {
        // Each entry on its own, its mount kept; none of the family researched: it stays.
        const auto target = r.componentUpgradeTarget(e, r.component(entry.component).family);
        if (!target || *target == entry.component) continue;
        entry.component = *target;
        changed = true;
    }
    return changed;
}

std::vector<EntryGroup> groupEntries(std::span<const game::DesignEntry> entries) {
    std::vector<EntryGroup> out;
    for (size_t i = 0; i < entries.size(); ++i) {
        auto it = std::find_if(out.begin(), out.end(), [&](const EntryGroup& g) { return g.entry == entries[i]; });
        if (it == out.end()) out.push_back({entries[i], 1, i});
        else {
            ++it->count;
            it->last = i;
        }
    }
    return out;
}

namespace {

// Available components passing `keep`, in data order; with `onlyLatest`, of
// each run of neighbouring components of one family the last (spec 02 §6.4,
// Rules::onlyLatestComponents).
template <class Keep>
std::vector<uint32_t> filterComponents(const game::Rules& r, const game::Empire& e, bool onlyLatest, Keep keep) {
    std::vector<uint32_t> out;
    for (uint32_t i = 0; i < r.data().components.size(); ++i)
        if (keep(r.component(i)) && r.componentAvailable(e, i)) out.push_back(i);
    return onlyLatest ? r.onlyLatestComponents(out) : out;
}

} // namespace

std::vector<ruleset::VehicleType> designableTypes(const game::Rules& r, const game::Empire& e) {
    std::vector<ruleset::VehicleType> out;
    for (size_t t = 0; t < static_cast<size_t>(ruleset::VehicleType::Count); ++t) {
        const auto type = static_cast<ruleset::VehicleType>(t);
        if (!hullsOfType(r, e, type).empty()) out.push_back(type);
    }
    return out;
}

std::vector<uint32_t> hullsOfType(const game::Rules& r, const game::Empire& e, ruleset::VehicleType t, std::optional<uint32_t> keep) {
    std::vector<uint32_t> out;
    for (uint32_t i = 0; i < r.data().vehicleSizes.size(); ++i)
        if (r.hull(i).type == t && (r.hullAvailable(e, i) || keep == i)) out.push_back(i);
    return out;
}

std::vector<std::string> hullRuleLines(const ruleset::VehicleSize& h) {
    std::vector<std::string> out;
    if (h.mustHaveBridge) out.emplace_back("Needs a bridge");
    if (h.canHaveAuxControl) out.emplace_back("Auxiliary control: at most 1");
    if (h.minLifeSupport > 0) out.push_back(std::format("Life support: at least {}", h.minLifeSupport));
    if (h.minCrewQuarters > 0) out.push_back(std::format("Crew quarters: at least {}", h.minCrewQuarters));
    if (!h.usesEngines) {
        out.emplace_back("Cannot carry engines");
    } else {
        out.push_back(h.maxEngines > 0 ? std::format("Engines: at most {}", h.maxEngines) : std::string("Engines: no limit"));
        if (h.enginesPerMove > 1) out.push_back(std::format("{} engines per movement point", h.enginesPerMove));
    }
    if (h.minPercentFighterBays > 0) out.push_back(std::format("Fighter bays: at least {}% of the hull", h.minPercentFighterBays));
    if (h.minPercentColonyModules > 0) out.push_back(std::format("Colony modules: at least {}% of the hull", h.minPercentColonyModules));
    if (h.minPercentCargo > 0) out.push_back(std::format("Cargo space: at least {}% of the hull", h.minPercentCargo));
    return out;
}

std::string designWindowTitle(ruleset::VehicleType t) { return std::string(ruleset::displayName(t)) + " Design"; }

std::vector<uint32_t> designerComponents(const game::Rules& r, const game::Empire& e, uint32_t hull, std::string_view group, bool onlyLatest) {
    const auto mask = ruleset::maskOf(r.hull(hull).type);
    return filterComponents(r, e, onlyLatest, [&](const ruleset::Component& c) {
        return (c.vehicles & mask) && (group.empty() || datafile::keysEqual(c.generalGroup, group));
    });
}

std::vector<std::string> componentGroups(const game::Rules& r, const game::Empire& e, uint32_t hull) {
    std::vector<std::string> out;
    for (uint32_t i : designerComponents(r, e, hull, {}, false)) {
        const std::string& g = r.component(i).generalGroup;
        if (g.empty()) continue;
        if (std::none_of(out.begin(), out.end(), [&](const std::string& x) { return datafile::keysEqual(x, g); })) out.push_back(g);
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<uint32_t> hullMounts(const game::Rules& r, const game::Empire& e, uint32_t hull) {
    std::vector<uint32_t> out;
    const auto comps = designerComponents(r, e, hull, {}, false);
    for (uint32_t m = 0; m < r.data().weaponMounts.size(); ++m) {
        if (!r.mountAvailable(e, m)) continue;
        if (std::any_of(comps.begin(), comps.end(), [&](uint32_t c) { return game::mountAllowed(r, hull, c, m); })) out.push_back(m);
    }
    return out;
}

int32_t mountFor(const game::Rules& r, uint32_t hull, uint32_t component, int32_t mount) {
    if (mount < 0) return -1;
    return game::mountAllowed(r, hull, component, static_cast<uint32_t>(mount)) ? mount : -1;
}

bool mountFitsWeapon(const game::Rules& r, uint32_t mount, uint32_t component) {
    if (mount >= r.data().weaponMounts.size()) return false;
    const std::string& req = r.data().weaponMounts[mount].weaponTypeRequirement;
    const ruleset::Component& c = r.component(component);
    if (req.empty() || datafile::keysEqual(req, "Any")) return true;
    if (datafile::keysEqual(req, "None")) return !c.isWeapon();
    switch (c.weapon.kind) {
        case WeaponKind::DirectFire: return datafile::keysEqual(req, "Direct Fire");
        case WeaponKind::Seeking: return datafile::keysEqual(req, "Seeking");
        case WeaponKind::Warhead: return datafile::keysEqual(req, "Warhead");
        case WeaponKind::PointDefense: return datafile::keysEqual(req, "Point-Defense");
        case WeaponKind::None: break;
    }
    return false;
}

std::vector<uint32_t> knownWeapons(const game::Rules& r, const game::Empire& e, WeaponKind kind, bool onlyLatest) {
    return filterComponents(r, e, onlyLatest,
                            [&](const ruleset::Component& c) { return c.isWeapon() && (kind == WeaponKind::None || c.weapon.kind == kind); });
}

bool designNameTaken(const game::GameState& s, const game::Empire& e, std::string_view name) {
    // Names differ from every design in the game, other empires' included,
    // exactly (spec 03 §4.1); a client sees the foreign designs it knows.
    return std::any_of(e.designs.begin(), e.designs.end(), [&](game::DesignId id) { return s.design(id).name == name; }) ||
           game::designNameInUse(s, name);
}

std::string romanNumeral(int n) {
    if (n <= 0 || n >= 40) return {};
    static constexpr std::array<const char*, 10> kOnes{"", "I", "II", "III", "IV", "V", "VI", "VII", "VIII", "IX"};
    std::string out(static_cast<size_t>(n / 10), 'X');
    return out + kOnes[static_cast<size_t>(n % 10)];
}

std::string nextVersionName(const game::GameState& s, const game::Empire& e, std::string_view name) {
    std::string base(name);
    int version = 1;
    if (const size_t space = base.rfind(' '); space != std::string::npos) {
        const std::string_view tail = std::string_view(base).substr(space + 1);
        for (int n = 1; n < 40; ++n)
            if (tail == romanNumeral(n)) {
                version = n;
                base.resize(space);
                break;
            }
    }
    for (int n = version + 1; n < 40; ++n) {
        std::string candidate = std::format("{} {}", base, romanNumeral(n));
        if (!designNameTaken(s, e, candidate)) return candidate;
    }
    for (int n = 40;; ++n) {
        std::string candidate = std::format("{} {}", base, n);
        if (!designNameTaken(s, e, candidate)) return candidate;
    }
}

std::string suggestDesignName(const game::GameState& s, const game::Empire& e, std::span<const std::string> list, size_t& start) {
    for (size_t k = 0; k < list.size(); ++k) {
        const size_t i = (start + k) % list.size();
        if (!list[i].empty() && !designNameTaken(s, e, list[i])) {
            start = i + 1;
            return list[i];
        }
    }
    return {};
}

namespace {

// Windows-1252 bytes 0x80..0x9F (0 = undefined).
constexpr std::array<char32_t, 32> kCp1252High{0x20AC, 0,      0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160,
                                               0x2039, 0x0152, 0,      0x017D, 0,      0,      0x2018, 0x2019, 0x201C, 0x201D, 0x2022,
                                               0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0,      0x017E, 0x0178};

void appendUtf8(std::string& out, char32_t cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

} // namespace

std::vector<std::string> parseNameList(std::string_view text) {
    std::vector<std::string> out;
    std::string line;
    auto flush = [&] {
        while (!line.empty() && (line.back() == ' ' || line.back() == '\t')) line.pop_back();
        const size_t first = line.find_first_not_of(" \t");
        if (first != std::string::npos) out.push_back(line.substr(first));
        line.clear();
    };
    for (char ch : text) {
        const auto b = static_cast<unsigned char>(ch);
        if (b == '\n') flush();
        else if (b == '\r') continue;
        else if (b < 0x80) line += ch;
        else if (b < 0xA0) {
            const char32_t cp = kCp1252High[b - 0x80];
            appendUtf8(line, cp ? cp : U'?');
        } else {
            appendUtf8(line, b);
        }
    }
    flush();
    return out;
}

} // namespace opense4::client::classic
