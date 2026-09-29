#include "sim/names.hpp"

#include <array>
#include <string_view>

namespace opense4::sim {

namespace {

constexpr std::array<std::string_view, 40> kStarts{
    "Al", "Ar", "Be", "Bra", "Ca", "Cor", "Da", "Del", "El", "Er", "Fa", "Gal", "Ha", "Hel",
    "Ir", "Ka", "Kor", "Lu", "Ly", "Ma", "Mir", "Ne", "Nor", "Or", "Pe", "Pra", "Qua", "Ra",
    "Rho", "Sa", "Sel", "Ta", "Tor", "Ul", "Ve", "Vor", "Xan", "Ya", "Ze", "Zor"};
constexpr std::array<std::string_view, 24> kMiddles{
    "ra", "le", "ni", "to", "ma", "ri", "ve", "lo", "sha", "da", "ga", "mo",
    "ne", "ki", "the", "ru", "si", "va", "de", "la", "no", "ta", "qui", "be"};
constexpr std::array<std::string_view, 22> kEnds{
    "n", "s", "x", "ris", "tar", "on", "us", "a", "ia", "eon", "ar",
    "is", "or", "ax", "une", "ion", "ek", "yl", "os", "ae", "um", "eth"};
constexpr std::array<std::string_view, 12> kGreek{
    "Alpha", "Beta", "Gamma", "Delta", "Epsilon", "Zeta", "Eta", "Theta", "Kappa", "Sigma", "Tau", "Omega"};

template <size_t N>
std::string_view pick(Rng& rng, const std::array<std::string_view, N>& items) {
    return items[rng.below(N)];
}

} // namespace

std::string generateStarName(Rng& rng) {
    std::string name(pick(rng, kStarts));
    if (rng.percent(55)) name += pick(rng, kMiddles);
    name += pick(rng, kEnds);
    if (rng.percent(12)) name = std::string(pick(rng, kGreek)) + " " + name;
    return name;
}

std::string romanNumeral(int n) {
    static constexpr std::array<std::pair<int, std::string_view>, 13> kTable{{
        {1000, "M"}, {900, "CM"}, {500, "D"}, {400, "CD"}, {100, "C"}, {90, "XC"}, {50, "L"},
        {40, "XL"}, {10, "X"}, {9, "IX"}, {5, "V"}, {4, "IV"}, {1, "I"}}};
    std::string out;
    for (const auto& [value, symbol] : kTable)
        while (n >= value) {
            out += symbol;
            n -= value;
        }
    return out;
}

} // namespace opense4::sim
