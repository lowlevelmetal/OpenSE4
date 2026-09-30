#include "game/types.hpp"

#include "datafile/datafile.hpp"
#include "ruleset/ruleset.hpp"

#include <array>

namespace opense4::game {

namespace {

constexpr std::array<std::string_view, kSightTypes> kSightNames{"EM Active", "EM Passive", "Psychic", "Gravitic", "Temporal"};

constexpr std::array<std::string_view, kCharacteristics> kCharacteristicNames{
    "Physical Strength", "Intelligence",        "Cunning",           "Environmental Resistance", "Reproduction",
    "Happiness",         "Aggressiveness",      "Defensiveness",     "Political Savvy",          "Mining Aptitude",
    "Farming Aptitude",  "Refining Aptitude",   "Construction Aptitude", "Repair Aptitude",      "Maintenance Aptitude",
};

} // namespace

Resources Resources::from(const ruleset::Cost& c) { return {c.minerals, c.organics, c.radioactives}; }

std::string_view displayName(Resource r) {
    switch (r) {
        case Resource::Minerals: return "Minerals";
        case Resource::Organics: return "Organics";
        case Resource::Radioactives: return "Radioactives";
    }
    return "?";
}

std::string_view displayName(Treaty t) {
    switch (t) {
        case Treaty::War: return "War";
        case Treaty::NonIntercourse: return "Non-Intercourse";
        case Treaty::None: return "None";
        case Treaty::NonAggression: return "Non-Aggression";
        case Treaty::Subjugation: return "Subjugation";
        case Treaty::Protectorate: return "Protectorate";
        case Treaty::TradeAlliance: return "Trade Alliance";
        case Treaty::TradeResearchAlliance: return "Trade & Research Alliance";
        case Treaty::MilitaryAlliance: return "Military Alliance";
        case Treaty::Partnership: return "Partnership";
        case Treaty::Count: break;
    }
    return "?";
}

std::string_view displayName(Mood m) {
    switch (m) {
        case Mood::Jubilant: return "Jubilant";
        case Mood::Happy: return "Happy";
        case Mood::Indifferent: return "Indifferent";
        case Mood::Unhappy: return "Unhappy";
        case Mood::Angry: return "Angry";
        case Mood::Rioting: return "Rioting";
    }
    return "?";
}

std::string_view displayName(SightType t) { return t < SightType::Count ? kSightNames[static_cast<size_t>(t)] : "?"; }

bool parseSightType(std::string_view s, SightType& out) {
    for (size_t i = 0; i < kSightTypes; ++i)
        if (datafile::keysEqual(s, kSightNames[i])) {
            out = static_cast<SightType>(i);
            return true;
        }
    return false;
}

std::string_view displayName(Characteristic c) {
    return c < Characteristic::Count ? kCharacteristicNames[static_cast<size_t>(c)] : "?";
}

bool parseCharacteristic(std::string_view s, Characteristic& out) {
    for (size_t i = 0; i < kCharacteristics; ++i)
        if (datafile::keysEqual(s, kCharacteristicNames[i])) {
            out = static_cast<Characteristic>(i);
            return true;
        }
    return false;
}

std::string_view displayName(LogCategory c) {
    switch (c) {
        case LogCategory::Construction: return "Construction";
        case LogCategory::Research: return "Research";
        case LogCategory::Intelligence: return "Intelligence";
        case LogCategory::Events: return "Events";
        case LogCategory::Politics: return "Politics";
        case LogCategory::Combat: return "Combat";
        case LogCategory::Misc: return "Miscellaneous";
    }
    return "?";
}

} // namespace opense4::game
