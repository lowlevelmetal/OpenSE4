#pragma once

// The ability system (docs/spec/03 §3). Data files name abilities by string;
// the engine works with this closed enumeration. Unknown identifiers (mods)
// are kept as AbilityKind::Unknown with their raw text.

#include "game/types.hpp"
#include "ruleset/ability_names.hpp"
#include "ruleset/ruleset.hpp"

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace opense4::game {

// The list of types is OPENSE4_ABILITIES in ruleset/ability_names.hpp.

enum class AbilityKind : uint16_t {
#define OPENSE4_ABILITY_ENUM(name, text) name,
    OPENSE4_ABILITIES(OPENSE4_ABILITY_ENUM)
#undef OPENSE4_ABILITY_ENUM
    Unknown,
    Count
};

std::string_view identifier(AbilityKind k);
// "AI Tag 07" maps to AITag; "None"/empty maps to nullopt.
std::optional<AbilityKind> parseAbilityKind(std::string_view text);

// An ability with its values pre-parsed.
struct ParsedAbility {
    AbilityKind kind = AbilityKind::Unknown;
    int64_t value1 = 0;      // numeric Val 1 (0 if blank or text)
    int64_t value2 = 0;
    std::string text1;       // raw Val 1 (sight-type names etc.)
    std::string raw;         // original identifier (for Unknown and AI tags)
    // Where the entry came from: a component (with its `Family`) or the hull and
    // other whole-vehicle sources. Only the per-family mode looks at it.
    bool fromComponent = false;
    int family = 0;
};

ParsedAbility parseAbility(const ruleset::Ability& a);
std::vector<ParsedAbility> parseAbilities(std::span<const ruleset::Ability> list);

// Helpers over a list (kept for the callers that predate the modes below).
int64_t sumValue1(std::span<const ParsedAbility> list, AbilityKind k);
int64_t bestValue1(std::span<const ParsedAbility> list, AbilityKind k);  // max, 0 if absent
bool hasAbility(std::span<const ParsedAbility> list, AbilityKind k);

// ---- Aggregation modes (spec 03 §3.2, confirmed: binary) ----------------------------------
// The original reads each ability type through one fixed mode, whatever list
// the rule looks at (a vehicle's abilities, a planet's facilities, a system).
enum class Aggregation : uint8_t {
    Sum,           // Σ V1 (or V2), at most kAbilitySumCap
    Largest,       // largest value, starting from 0 (negative entries never count)
    Smallest,      // smallest V1, 0 when there is none
    Count,         // number of entries
    Present,       // at least one entry
    PerSightType,  // per sight type: largest V2 among entries whose V1 names that type
    FirstPerId,    // entries grouped by V2 (16 bits); each group's first V1; groups summed
    PerFamily,     // whole-vehicle entries in full + each component family's largest; summed
    Unspecified,   // not traced: rules read these with an explicit helper
};
Aggregation aggregationOf(AbilityKind k);

inline constexpr int64_t kAbilitySumCap = 2'000'000'000;

int64_t abilitySum(std::span<const ParsedAbility> list, AbilityKind k, bool value2 = false);
int64_t abilityLargest(std::span<const ParsedAbility> list, AbilityKind k, bool value2 = false);
int64_t abilitySmallest(std::span<const ParsedAbility> list, AbilityKind k);
int64_t abilityCount(std::span<const ParsedAbility> list, AbilityKind k);
inline bool abilityPresent(std::span<const ParsedAbility> list, AbilityKind k) { return hasAbility(list, k); }
int64_t abilityPerSightType(std::span<const ParsedAbility> list, AbilityKind k, SightType t);
int64_t abilityFirstPerId(std::span<const ParsedAbility> list, AbilityKind k);
int64_t abilityPerFamily(std::span<const ParsedAbility> list, AbilityKind k);
// V1 read in the type's own mode (Present and Count give 1/0 and the count;
// per sight type gives the best level over all types; Unspecified sums).
int64_t abilityValue(std::span<const ParsedAbility> list, AbilityKind k);

// Space Yard abilities encode the resource in Val 1 (1..3) and the rate in Val 2.
Resources spaceYardRates(std::span<const ParsedAbility> list);

} // namespace opense4::game
