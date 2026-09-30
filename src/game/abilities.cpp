#include "game/abilities.hpp"

#include "datafile/datafile.hpp"

#include <array>
#include <unordered_map>

namespace opense4::game {

namespace {

constexpr std::array<std::string_view, static_cast<size_t>(AbilityKind::Count)> kIdentifiers{
#define OPENSE4_ABILITY_TEXT(name, text) text,
    OPENSE4_ABILITIES(OPENSE4_ABILITY_TEXT)
#undef OPENSE4_ABILITY_TEXT
    "Unknown",
};

const std::unordered_map<std::string, AbilityKind>& table() {
    static const std::unordered_map<std::string, AbilityKind> t = [] {
        std::unordered_map<std::string, AbilityKind> m;
        for (size_t i = 0; i < static_cast<size_t>(AbilityKind::Unknown); ++i)
            m.emplace(datafile::normalizeKey(kIdentifiers[i]), static_cast<AbilityKind>(i));
        return m;
    }();
    return t;
}

} // namespace

std::string_view identifier(AbilityKind k) { return kIdentifiers[static_cast<size_t>(k)]; }

std::optional<AbilityKind> parseAbilityKind(std::string_view text) {
    const std::string key = datafile::normalizeKey(text);
    if (key.empty() || key == "none") return std::nullopt;
    if (key.starts_with("ai tag")) return AbilityKind::AITag;
    const auto& t = table();
    if (auto it = t.find(key); it != t.end()) return it->second;
    return AbilityKind::Unknown;
}

ParsedAbility parseAbility(const ruleset::Ability& a) {
    ParsedAbility p;
    p.kind = parseAbilityKind(a.type).value_or(AbilityKind::Unknown);
    p.value1 = a.number1();
    p.value2 = a.number2();
    p.text1 = a.value1;
    p.raw = a.type;
    return p;
}

std::vector<ParsedAbility> parseAbilities(std::span<const ruleset::Ability> list) {
    std::vector<ParsedAbility> out;
    out.reserve(list.size());
    for (const auto& a : list)
        if (parseAbilityKind(a.type)) out.push_back(parseAbility(a));
    return out;
}

int64_t sumValue1(std::span<const ParsedAbility> list, AbilityKind k) {
    int64_t total = 0;
    for (const auto& a : list)
        if (a.kind == k) total += a.value1;
    return total;
}

int64_t bestValue1(std::span<const ParsedAbility> list, AbilityKind k) {
    int64_t best = 0;
    bool found = false;
    for (const auto& a : list)
        if (a.kind == k && (!found || a.value1 > best)) {
            best = a.value1;
            found = true;
        }
    return best;
}

bool hasAbility(std::span<const ParsedAbility> list, AbilityKind k) {
    for (const auto& a : list)
        if (a.kind == k) return true;
    return false;
}

Resources spaceYardRates(std::span<const ParsedAbility> list) {
    Resources r;
    for (const auto& a : list)
        if (a.kind == AbilityKind::SpaceYard && a.value1 >= 1 && a.value1 <= 3) r.v[static_cast<size_t>(a.value1 - 1)] += a.value2;
    return r;
}

} // namespace opense4::game
