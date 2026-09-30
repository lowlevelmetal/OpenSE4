#include "game/ai_data.hpp"

#include "datafile/datafile.hpp"
#include "datafile/reader.hpp"
#include "game/rules.hpp"

#include <algorithm>
#include <format>
#include <map>
#include <memory>
#include <mutex>
#include <optional>

namespace opense4::game::ai {

namespace {

using datafile::keysEqual;

constexpr std::array<std::string_view, kAiStates> kStateNames{
    "Exploration",       "Infrastructure", "Prepare for Attack", "Attack",
    "Secure Holdings After Attack",       "Incursion",          "Prepare for Defense",
    "Defend (Short Term)", "Defend (Long Term)", "Not Connected",
};

std::string lowerAscii(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

// ---- A record with typed, defaulted access ---------------------------------------------------

class Fields {
public:
    explicit Fields(const datafile::Record* r) : r_(r) {}
    bool has(std::string_view key) const { return r_ && r_->find(key); }
    std::string text(std::string_view key, std::string fallback = {}) const {
        const datafile::Field* f = r_ ? r_->find(key) : nullptr;
        return f ? f->value : fallback;
    }
    int num(std::string_view key, int fallback) const {
        const datafile::Field* f = r_ ? r_->find(key) : nullptr;
        if (!f) return fallback;
        const auto v = datafile::parseInteger(f->value);
        if (!v) return fallback;
        return static_cast<int>(std::clamp<int64_t>(*v, INT32_MIN, INT32_MAX));
    }
    int64_t num64(std::string_view key, int64_t fallback) const {
        const datafile::Field* f = r_ ? r_->find(key) : nullptr;
        return f ? datafile::parseInteger(f->value).value_or(fallback) : fallback;
    }
    bool flag(std::string_view key, bool fallback) const {
        const datafile::Field* f = r_ ? r_->find(key) : nullptr;
        return f ? datafile::parseBoolean(f->value).value_or(fallback) : fallback;
    }

private:
    const datafile::Record* r_;
};

// ---- File lookup (names differ in case between installs) --------------------------------------

std::filesystem::path findChild(const std::filesystem::path& dir, std::string_view name, bool wantDirectory) {
    std::error_code ec;
    if (dir.empty() || !std::filesystem::is_directory(dir, ec)) return {};
    std::vector<std::filesystem::path> hits;
    for (const auto& e : std::filesystem::directory_iterator(dir, ec)) {
        if (wantDirectory != e.is_directory(ec)) continue;
        if (keysEqual(e.path().filename().string(), name)) hits.push_back(e.path());
    }
    std::sort(hits.begin(), hits.end());
    return hits.empty() ? std::filesystem::path{} : hits.front();
}

// A file in `dir` whose name ends with "_AI_<table>.txt" (any case).
std::filesystem::path findTable(const std::filesystem::path& dir, std::string_view table, std::string_view prefix = {}) {
    std::error_code ec;
    if (dir.empty() || !std::filesystem::is_directory(dir, ec)) return {};
    const std::string suffix = lowerAscii(std::format("_ai_{}.txt", table));
    const std::string wantPrefix = lowerAscii(std::string(prefix));
    std::vector<std::filesystem::path> hits;
    for (const auto& e : std::filesystem::directory_iterator(dir, ec)) {
        if (!e.is_regular_file(ec)) continue;
        const std::string n = lowerAscii(e.path().filename().string());
        if (n.ends_with(suffix) && (wantPrefix.empty() || n.starts_with(wantPrefix))) hits.push_back(e.path());
    }
    std::sort(hits.begin(), hits.end());
    return hits.empty() ? std::filesystem::path{} : hits.front();
}

struct Locations {
    std::filesystem::path aiDir;       // <root>/Ai
    std::filesystem::path raceDir;     // <root>/Pictures/Races/<style> or RaceNeutral/<style>
    std::filesystem::path styleDir;    // <root>/Ai/<ministerStyle>
};

Locations locate(const std::filesystem::path& root, std::string_view raceStyle, std::string_view ministerStyle) {
    Locations l;
    l.aiDir = findChild(root, "Ai", true);
    if (!raceStyle.empty()) {
        const auto pictures = findChild(root, "Pictures", true);
        for (std::string_view group : {"Races", "RaceNeutral"}) {
            if (auto d = findChild(findChild(pictures, group, true), raceStyle, true); !d.empty()) {
                l.raceDir = d;
                break;
            }
        }
    }
    if (!ministerStyle.empty()) l.styleDir = findChild(l.aiDir, ministerStyle, true);
    return l;
}

enum class Scope { Race, Global, Style };

// Per spec 05 §7.2: race files first, then Ai/Default_*; global files only in Ai/.
std::filesystem::path lookup(const Locations& l, std::string_view table, Scope scope) {
    if (scope == Scope::Style && !l.styleDir.empty())
        if (auto f = findTable(l.styleDir, table); !f.empty()) return f;
    if (scope != Scope::Global && !l.raceDir.empty())
        if (auto f = findTable(l.raceDir, table); !f.empty()) return f;
    return findTable(l.aiDir, table, "default_");
}

std::optional<datafile::DataFile> readTable(const Locations& l, std::string_view table, Scope scope, AiProfile& p) {
    const auto path = lookup(l, table, scope);
    if (path.empty()) return std::nullopt;
    auto file = datafile::load(path);
    if (!file || file->records.empty()) return std::nullopt;
    p.sources.push_back(path.string());
    return std::move(*file);
}

// ---- Built-in defaults (our own tuning, see docs/spec/05 §7) ------------------------------------

std::array<int, kMessageTypes> defaultReceive() {
    std::array<int, kMessageTypes> a{};
    auto set = [&](MessageType t, int v) { a[static_cast<size_t>(t)] = v; };
    set(MessageType::AcceptTreaty, -4);
    set(MessageType::RefuseTreaty, 6);
    set(MessageType::BreakTreaty, 12);
    set(MessageType::DeclareWar, 25);
    set(MessageType::AcceptTrade, -2);
    set(MessageType::RefuseTrade, 3);
    set(MessageType::Gift, -5);
    set(MessageType::Tribute, -5);
    set(MessageType::AcceptGift, -1);
    set(MessageType::RefuseGift, 4);
    set(MessageType::GrantIndependence, -3);
    set(MessageType::DemandGift, 6);
    set(MessageType::DemandTribute, 8);
    set(MessageType::DemandSurrender, 25);
    set(MessageType::DemandRemoveShips, 4);
    set(MessageType::DemandRemoveColonies, 6);
    set(MessageType::DemandLeavePlanet, 6);
    set(MessageType::RequestStopHostilities, 3);
    set(MessageType::RequestBreakTreaty, 3);
    set(MessageType::RequestDeclareWar, 1);
    set(MessageType::DemandStopEspionage, 2);
    set(MessageType::DemandStopSabotage, 2);
    set(MessageType::DemandStopAttacks, 2);
    set(MessageType::AcceptDemand, -4);
    set(MessageType::RefuseDemand, 4);
    return a;
}

PoliticsTable defaultPolitics() {
    PoliticsTable p;
    p.proposeTypes = {{Treaty::Partnership, 20},
                      {Treaty::MilitaryAlliance, 14},
                      {Treaty::TradeResearchAlliance, 8},
                      {Treaty::TradeAlliance, 3},
                      {Treaty::NonAggression, 0}};
    for (size_t i = 0; i < kMessageTypes; ++i) {
        const auto t = static_cast<MessageType>(i);
        if (!isDemand(t)) continue;
        DemandRule d;
        d.sendScorePercent = 85;
        d.sendToFriend = d.sendToEnemy = true;
        d.acceptScorePercent = 140;
        d.acceptFromFriend = true;
        d.acceptFromEnemy = false;
        switch (t) {
            case MessageType::DemandSurrender:
                d.sendScorePercent = 60;
                d.acceptScorePercent = 900;
                d.acceptFromFriend = false;
                d.acceptFromEnemy = true;
                break;
            case MessageType::DemandGift:
            case MessageType::DemandTribute: d.acceptScorePercent = 150; break;
            case MessageType::DemandStopEspionage:
            case MessageType::DemandStopSabotage: d.acceptScorePercent = 120; break;
            case MessageType::RequestSupport: d.sendScorePercent = 110; break;
            default: break;
        }
        p.demands[i] = d;
    }
    return p;
}

std::vector<PlanetTypeRow> defaultPlanetTypes() {
    const StateMask war = maskOf(AiState::PrepareForAttack) | maskOf(AiState::Attack) | maskOf(AiState::SecureHoldings) |
                          maskOf(AiState::Incursion) | maskOf(AiState::PrepareForDefense) | maskOf(AiState::DefendShortTerm) |
                          maskOf(AiState::DefendLongTerm);
    return {
        {kAllStates, "Research Compound", 1, 20, "Small", {}, 0},
        {war, "Military Installation", 1, 10, "Large", {}, 0},
        {kAllStates, "Mining Colony", 1, 30, "", {125, 100, 100}, 0},
        {kAllStates, "Farming Colony", 1, 20, "", {100, 125, 100}, 0},
        {kAllStates, "Refining Colony", 1, 20, "", {100, 100, 125}, 0},
        {kAllStates, "Construction Yard", 1, 10, "Medium", {}, 3},
        {kAllStates, "Resupply Base", 1, 10, "", {}, 0},
        {kAllStates, "Intelligence Compound", 1, 5, "Small", {}, 2},
        {kAllStates, "Mining Colony", 100, 100, "", {}, 0},
    };
}

std::vector<FacilityQueue> defaultFacilities() {
    const std::string minerals = "Resource Generation - Minerals";
    const std::string organics = "Resource Generation - Organics";
    const std::string radioactives = "Resource Generation - Radioactives";
    const std::string research = "Point Generation - Research";
    const std::string intel = "Point Generation - Intelligence";
    return {
        {kAllStates,
         "Homeworld",
         {{"Spaceport", 1}, {"Space Yard", 1}, {"Supply Generation", 1}, {minerals, 2}, {research, 2}, {organics, 1},
          {radioactives, 1}, {intel, 1}, {"Resource Storage - Mineral", 1}, {minerals, 99}}},
        {kAllStates,
         "Mining Colony",
         {{"Spaceport", 1}, {minerals, 3}, {"Supply Generation", 1}, {"Resource Storage - Mineral", 1}, {minerals, 99}}},
        {kAllStates, "Farming Colony", {{"Spaceport", 1}, {organics, 3}, {"Resource Storage - Organics", 1}, {organics, 99}}},
        {kAllStates,
         "Refining Colony",
         {{"Spaceport", 1}, {radioactives, 3}, {"Resource Storage - Radioactives", 1}, {radioactives, 99}}},
        {kAllStates, "Research Compound", {{"Spaceport", 1}, {research, 99}}},
        {kAllStates, "Intelligence Compound", {{"Spaceport", 1}, {intel, 99}}},
        {kAllStates, "Resupply Base", {{"Spaceport", 1}, {"Supply Generation", 1}, {"Component Repair", 1}, {minerals, 99}}},
        {kAllStates,
         "Construction Yard",
         {{"Spaceport", 1}, {"Space Yard", 1}, {minerals, 2}, {"Component Repair", 1}, {minerals, 99}}},
        {kAllStates,
         "Military Installation",
         {{"Spaceport", 1}, {"Space Yard", 1}, {"Planet - Shield Generation", 1}, {"Sensor Level", 1}, {minerals, 99}}},
    };
}

std::vector<VehicleQueue> defaultVehicles() {
    return {
        {maskOf(AiState::Exploration), {{"Colonizer", 20, 2}, {"Attack Ship", 40, 1}, {"Defense Base", 0, 0}}},
        {maskOf(AiState::Infrastructure) | maskOf(AiState::NotConnected),
         {{"Colonizer", 25, 1}, {"Attack Ship", 15, 2}, {"Defense Base", 60, 1}, {"Weapon Platform", 10, 2},
          {"Population Transport", 80, 0}}},
        {maskOf(AiState::PrepareForAttack) | maskOf(AiState::Attack),
         {{"Attack Ship", 5, 4}, {"Colonizer", 60, 0}, {"Troop Transport", 50, 1}, {"Troop", 5, 4}, {"Defense Base", 100, 0}}},
        {maskOf(AiState::SecureHoldings) | maskOf(AiState::Incursion),
         {{"Attack Ship", 8, 3}, {"Colonizer", 40, 1}, {"Defense Base", 60, 1}}},
        {maskOf(AiState::PrepareForDefense) | maskOf(AiState::DefendShortTerm) | maskOf(AiState::DefendLongTerm),
         {{"Defense Base", 15, 2}, {"Attack Ship", 8, 3}, {"Weapon Platform", 5, 4}, {"Satellite", 10, 0}}},
    };
}

DesignTemplate makeTemplate(std::string name, ruleset::VehicleType type, std::string strategy, std::vector<std::string> mustHave, int minSpeed,
                            int desiredSpeed, DensityEntry majority, int shields, int armor, std::vector<DensityEntry> misc = {}) {
    DesignTemplate t;
    t.name = name;
    t.designType = std::move(name);
    t.vehicleType = type;
    t.defaultStrategy = std::move(strategy);
    t.mustHave = std::move(mustHave);
    t.minSpeed = minSpeed;
    t.desiredSpeed = desiredSpeed;
    t.majority = std::move(majority);
    t.shieldsSpacesPerOne = shields;
    t.armorSpacesPerOne = armor;
    t.misc = std::move(misc);
    return t;
}

// The scout is not one of the classic AI design types; the computer keeps a
// couple for exploration (inferred).
DesignTemplate scoutTemplate() {
    return makeTemplate("Scout", ruleset::VehicleType::Ship, "", {"Standard Ship Movement"}, 3, 8, {}, 0, 0,
                        {{"Supply Storage", 150}, {"Sensor Level", 5000}});
}

std::vector<DesignTemplate> defaultDesigns() {
    using ruleset::VehicleType;
    const DensityEntry weapons{"Weapon", 800};
    std::vector<DesignTemplate> out;
    out.push_back(makeTemplate("Attack Ship", VehicleType::Ship, "", {"Weapon"}, 3, 5, weapons, 300, 500,
                               {{"Supply Storage", 600}, {"Point-Defense", 900}}));
    out.push_back(makeTemplate("Defense Ship", VehicleType::Ship, "", {"Weapon"}, 2, 4, weapons, 250, 300, {{"Supply Storage", 700}}));
    out.push_back(makeTemplate("Defense Base", VehicleType::Base, "", {"Weapon"}, 0, 0, weapons, 250, 300, {{"Point-Defense", 700}}));
    for (std::string_view surface : {"Rock", "Ice", "Gas"}) {
        const std::string ability = std::format("Colonize Planet - {}", surface);
        out.push_back(makeTemplate(std::format("Colony ({})", surface), VehicleType::Ship, "", {ability}, 2, 4, {ability, 5000}, 0, 0,
                                   {{"Supply Storage", 400}}));
    }
    out.push_back(scoutTemplate());
    out.push_back(makeTemplate("Population Transport", VehicleType::Ship, "", {"Cargo Storage"}, 2, 4, {"Cargo Storage", 100}, 0, 0,
                               {{"Supply Storage", 500}}));
    out.push_back(makeTemplate("Troop Transport", VehicleType::Ship, "", {"Cargo Storage"}, 3, 4, {"Cargo Storage", 100}, 0, 400,
                               {{"Supply Storage", 500}}));
    out.push_back(makeTemplate("Carrier", VehicleType::Ship, "", {"Launch/Recover Fighters"}, 2, 4, {"Launch/Recover Fighters", 100}, 300,
                               600, {{"Supply Storage", 500}}));
    out.push_back(makeTemplate("Mine Layer", VehicleType::Ship, "", {"Lay Mines"}, 2, 4, {"Lay Mines", 100}, 0, 500, {{"Supply Storage", 500}}));
    out.push_back(makeTemplate("Satellite Layer", VehicleType::Ship, "", {"Launch/Recover Satellites"}, 2, 4,
                               {"Launch/Recover Satellites", 100}, 0, 500, {{"Supply Storage", 500}}));
    out.push_back(makeTemplate("Mine Sweeper", VehicleType::Ship, "", {"Mine Sweeping"}, 3, 5, {"Mine Sweeping", 300}, 0, 400,
                               {{"Supply Storage", 500}}));
    out.push_back(makeTemplate("Base Space Yard", VehicleType::Base, "", {"Space Yard"}, 0, 0, {"Space Yard", 5000}, 0, 300));
    out.push_back(makeTemplate("Space Yard Ship", VehicleType::Ship, "", {"Space Yard"}, 2, 3, {"Space Yard", 5000}, 0, 400,
                               {{"Supply Storage", 400}}));
    out.push_back(makeTemplate("Weapon Platform", VehicleType::WeaponPlatform, "", {"Weapon"}, 0, 0, weapons, 0, 200));
    out.push_back(makeTemplate("Satellite", VehicleType::Satellite, "", {"Weapon"}, 0, 0, weapons, 0, 200));
    out.push_back(makeTemplate("Fighter", VehicleType::Fighter, "", {"Weapon"}, 1, 4, weapons, 0, 0));
    out.push_back(makeTemplate("Mine", VehicleType::Mine, "", {"Weapon"}, 0, 0, weapons, 0, 0));
    out.push_back(makeTemplate("Troop", VehicleType::Troop, "", {"Weapon"}, 0, 0, weapons, 0, 200));
    return out;
}

Speech defaultSpeech() {
    Speech s;
    auto add = [&](std::string_view pool, std::vector<std::string> lines) { s.pools[datafile::normalizeKey(pool)] = std::move(lines); };
    add("Send General Message", {"Greetings from [%OurEmpireName]."});
    add("Send Propose Treaty", {"[%OurEmpireName] offers [%TargetEmpireName] a [%ProposedTreatyName]."});
    add("Send Accept Treaty", {"[%OurEmpireName] agrees to the [%ProposedTreatyName]."});
    add("Send Refuse Treaty", {"[%OurEmpireName] declines the [%ProposedTreatyName]."});
    add("Send Break Treaty", {"[%OurEmpireName] no longer honours its [%TreatyName] with you."});
    add("Send Declare War", {"[%OurEmpireName] is now at war with [%TargetEmpireName]."});
    add("Send Accept Trade", {"The exchange is acceptable."});
    add("Send Refuse Trade", {"The exchange is not acceptable."});
    add("Send Give Gift", {"Please accept this token of goodwill from [%OurEmpireName]."});
    add("Send Offer Tribute", {"[%OurEmpireName] offers this tribute to [%TargetEmpireName]."});
    add("Send Accept Gift", {"[%OurEmpireName] thanks you for the gift."});
    add("Send Refuse Gift", {"[%OurEmpireName] wants nothing from you."});
    add("Send Accept Tribute", {"Your tribute is noted."});
    add("Send Refuse Tribute", {"Keep your tribute."});
    add("Send Accept Demand/Request", {"[%OurEmpireName] will do as you ask."});
    add("Send Refuse Demand/Request", {"[%OurEmpireName] will not do that."});
    add("Send Want a gift", {"[%OurEmpireName] would welcome a gift from [%TargetEmpireName]."});
    add("Send Remove your ships from system", {"Withdraw your ships from [%SystemName]."});
    add("Send Declare war on empire", {"Join [%OurEmpireName] against [%OtherEmpireName]."});
    add("Mega Evil Declarations", {"[%TargetEmpireName] has grown too powerful. [%OurEmpireName] will oppose it."});
    return s;
}

AiProfile makeBuiltin() {
    AiProfile p;
    p.anger.receive = defaultReceive();
    p.politics = defaultPolitics();
    p.general.name = "Built-in";
    p.planetTypes = defaultPlanetTypes();
    p.facilities = defaultFacilities();
    p.vehicles = defaultVehicles();
    p.designs = defaultDesigns();
    p.speech = defaultSpeech();
    p.sources.push_back("built-in");
    return p;
}

// ---- Parsers ------------------------------------------------------------------------------------

void parseAnger(const datafile::Record& rec, AngerTable& a) {
    const Fields f(&rec);
    a.perAttackLocation = f.num("Per Attack Location", a.perAttackLocation);
    a.perNoTreatyShip = f.num("Per No Treaty Ship", a.perNoTreatyShip);
    a.perAllyShip = f.num("Per Ally Ship", a.perAllyShip);
    a.perEnemyShip = f.num("Per Enemy Ship", a.perEnemyShip);
    a.minimum = f.num("Minimum Anger", a.minimum);
    a.regularDecrease = f.num("Regular Decrease", a.regularDecrease);
    a.megaEvilEmpire = f.num("Mega Evil Empire", a.megaEvilEmpire);
    a.attackingWon = f.num("Combat Attacking Won", a.attackingWon);
    a.attackingLost = f.num("Combat Attacking Lost", a.attackingLost);
    a.attackingStalemate = f.num("Combat Attacking Stalemate", a.attackingStalemate);
    a.defendingWon = f.num("Combat Defending Won", a.defendingWon);
    a.defendingLost = f.num("Combat Defending Lost", a.defendingLost);
    a.defendingStalemate = f.num("Combat Defending Stalemate", a.defendingStalemate);
    a.intelligenceAgainstUs = f.num("Intelligence Against Us", a.intelligenceAgainstUs);
    for (size_t i = 0; i < kMessageTypes; ++i) {
        const std::string_view name = angerKeyName(static_cast<MessageType>(i));
        if (!name.empty()) a.receive[i] = f.num(std::format("Receive {}", name), a.receive[i]);
    }
    a.receiveAcceptTribute = f.num("Receive Accept Tribute", a.receiveAcceptTribute);
    a.receiveRefuseTribute = f.num("Receive Refuse Tribute", a.receiveRefuseTribute);
}

TreatyRule parseRule(const Fields& f, std::string_view prefix, TreatyRule r, bool hasFirst50) {
    r.baseAnger = f.num(std::format("{} Base Anger Level", prefix), r.baseAnger);
    r.perOtherWars = f.num(std::format("{} Anger Modifier Per Other Wars", prefix), r.perOtherWars);
    if (hasFirst50) r.first50Turns = f.num(std::format("{} First 50 Turns Modifier", prefix), r.first50Turns);
    r.strongerPercent = f.num(std::format("{} Anger Modifier For Percent Stronger Player", prefix), r.strongerPercent);
    r.strongerAmount = f.num(std::format("{} Anger Modifier For Percent Stronger Player Amount", prefix), r.strongerAmount);
    r.weakerPercent = f.num(std::format("{} Anger Modifier For Percent Weaker Player", prefix), r.weakerPercent);
    r.weakerAmount = f.num(std::format("{} Anger Modifier For Percent Weaker Player Amount", prefix), r.weakerAmount);
    return r;
}

void parsePolitics(const datafile::Record& rec, PoliticsTable& p) {
    const Fields f(&rec);
    p.demandingTonePercent = f.num("Score Percent For Demanding Tone", p.demandingTonePercent);
    p.pleadingTonePercent = f.num("Score Percent To Pleading Tone", p.pleadingTonePercent);
    Treaty t;
    if (parseTreatyName(f.text("Highest Allowed Treaty"), t)) p.highestAllowedTreaty = t;
    p.turnsSinceWarBeforeFriendly = f.num("Turns Since Last War Before Friendly Treaty", p.turnsSinceWarBeforeFriendly);
    p.accept = parseRule(f, "Accept Treaty", p.accept, true);
    p.acceptPerHigherLevel = f.num("Accept Treaty Anger Modifier Per Higher Treaty Level", p.acceptPerHigherLevel);
    p.acceptMinimumChance = f.num("Accept Treaty Minimum Anger Chance", p.acceptMinimumChance);
    p.acceptMinimumTurnsSinceTreaty = f.num("Accept Treaty Minimum Time From Last Treaty", p.acceptMinimumTurnsSinceTreaty);
    p.acceptSubjugationPercent = f.num("Accept Treaty Score Percent To Accept Subjugation Treaty", p.acceptSubjugationPercent);
    p.acceptProtectoratePercent = f.num("Accept Treaty Score Percent To Accept Protectorate Treaty", p.acceptProtectoratePercent);
    p.proposeChancePercent = f.num("Propose Treaty Percent Chance Per Turn", p.proposeChancePercent);
    p.propose = parseRule(f, "Propose Treaty", p.propose, true);
    if (f.has("Propose Treaty Type Count")) {
        std::vector<std::pair<Treaty, int>> types;
        const int n = f.num("Propose Treaty Type Count", 0);
        for (int i = 1; i <= n; ++i) {
            Treaty tt;
            if (!parseTreatyName(f.text(std::format("Propose Treaty Type {}", i)), tt)) continue;
            types.emplace_back(tt, f.num(std::format("Propose Treaty Type {} Anger Level Below Computed", i), 0));
        }
        p.proposeTypes = std::move(types);
    }
    p.breakTreaty = parseRule(f, "Break Treaty", p.breakTreaty, false);
    p.declareWar = parseRule(f, "Declare War", p.declareWar, false);
    p.maxAngerAcceptGift = f.num("Max Anger Level for Accept a Gift", p.maxAngerAcceptGift);
    p.maxAngerAcceptTribute = f.num("Max Anger Level for Accept a Tribute", p.maxAngerAcceptTribute);
    for (size_t i = 0; i < kMessageTypes; ++i) {
        const auto type = static_cast<MessageType>(i);
        if (!isDemand(type)) continue;
        const std::string_view n = angerKeyName(type);
        DemandRule& d = p.demands[i];
        d.sendScorePercent = f.num(std::format("Score Percent to Send {}", n), d.sendScorePercent);
        d.sendToFriend = f.flag(std::format("Will Send To Friend {}", n), d.sendToFriend);
        d.sendToEnemy = f.flag(std::format("Will Send To Enemy {}", n), d.sendToEnemy);
        d.acceptScorePercent = f.num(std::format("Score Percent To Accept {}", n), d.acceptScorePercent);
        d.acceptFromFriend = f.flag(std::format("Will Accept From Friend {}", n), d.acceptFromFriend);
        d.acceptFromEnemy = f.flag(std::format("Will Accept From Enemy {}", n), d.acceptFromEnemy);
    }
    p.giftBaseFriend = f.num("Gift Value Base to Friend", p.giftBaseFriend);
    p.giftBaseEnemy = f.num("Gift Value Base to Enemy", p.giftBaseEnemy);
    p.tributeBaseFriend = f.num("Tribute Value Base to Friend", p.tributeBaseFriend);
    p.tributeBaseEnemy = f.num("Tribute Value Base to Enemy", p.tributeBaseEnemy);
    p.giftPerPercentFriend = f.num("Gift Value to Friend Per Percentage Greater Score", p.giftPerPercentFriend);
    p.giftPerPercentEnemy = f.num("Gift Value to Enemy Per Percentage Greater Score", p.giftPerPercentEnemy);
    p.tributePerPercentFriend = f.num("Tribute Value to Friend Per Percentage Greater Score", p.tributePerPercentFriend);
    p.tributePerPercentEnemy = f.num("Tribute Value to Enemy Per Percentage Greater Score", p.tributePerPercentEnemy);
    p.giftMaxAngerFriend = f.num("Gift to Friend Max Anger", p.giftMaxAngerFriend);
    p.giftMaxAngerEnemy = f.num("Gift to Enemy Max Anger", p.giftMaxAngerEnemy);
    p.tributeMaxAngerFriend = f.num("Tribute to Friend Max Anger", p.tributeMaxAngerFriend);
    p.tributeMaxAngerEnemy = f.num("Tribute to Enemy Max Anger", p.tributeMaxAngerEnemy);
    auto tradeKey = [](std::string_view side) { return std::format("Accept Trade From {} At Percentage Value or Greater of Received", side); };
    p.acceptTradeFriendPercent = f.num(tradeKey("Friend"), p.acceptTradeFriendPercent);
    p.acceptTradeEnemyPercent = f.num(tradeKey("Enemy"), p.acceptTradeEnemyPercent);
}

void parseSettings(const datafile::Record& rec, SettingsTable& s) {
    const Fields f(&rec);
    for (int i = 1; i <= 3; ++i) {
        auto& cap = s.tonnageCaps[static_cast<size_t>(i - 1)];
        cap.first = f.num(std::format("Max Ship Size Tonnage From Start {} Amount", i), cap.first);
        cap.second = f.num(std::format("Max Ship Size Tonnage From Start {} Num Turns", i), cap.second);
    }
    s.turnsBetweenAttacks = f.num("Turns to Wait until next attack", s.turnsBetweenAttacks);
    s.maxMaintenancePercent = f.num("Maximum Maintenance Percent of Revenue", s.maxMaintenancePercent);
    s.maxResearchPoints = f.num64("Maximum Research Point Generation", s.maxResearchPoints);
    s.maxIntelligencePoints = f.num64("Maximum Intelligence Point Generation", s.maxIntelligencePoints);
    s.maxSystemsToDefend = f.num("Maximum Systems to Defend at a Time", s.maxSystemsToDefend);
    s.angryOverAlliedPlanets = f.flag("Get Angry Over Allied Colonizable Planets", s.angryOverAlliedPlanets);
    s.angryOverEnemyPlanets = f.flag("Get Angry Over Enemy Colonizable Planets", s.angryOverEnemyPlanets);
    auto planetsKey = [](std::string_view side) { return std::format("Percentage of {} Planets to consider as Attack Locations for Anger", side); };
    s.alliedPlanetsPercent = f.num(planetsKey("Allied"), s.alliedPlanetsPercent);
    s.enemyPlanetsPercent = f.num(planetsKey("Enemy"), s.enemyPlanetsPercent);
    s.personalityGroup = f.num("Personality Group", s.personalityGroup);
    s.avoidMinefields = f.flag("Ships don't move through minefields", s.avoidMinefields);
    s.avoidRestrictedSystems = f.flag("Ships don't move through restricted systems", s.avoidRestrictedSystems);
    s.clearOrdersOnEnemy = f.flag("Clear orders on encounter enemy", s.clearOrdersOnEnemy);
    s.clearOrdersOnAll = f.flag("Clear orders on encounter all", s.clearOrdersOnAll);
    s.satellitesKeptPercent = f.num("Percentage of total satellites to keep as planetary cargo", s.satellitesKeptPercent);
    s.dronesKeptPercent = f.num("Percentage of total drones to keep as planetary cargo", s.dronesKeptPercent);
    s.antiShipDronesPerTarget = f.num("Number Of Anti-Ship Drones Per Target", s.antiShipDronesPerTarget);
    s.antiPlanetDronesPerTarget = f.num("Number Of Anti-Planet Drones Per Target", s.antiPlanetDronesPerTarget);
    s.antiShipDroneRange = f.num("Maximum Anti-Ship Drone Target System Distance", s.antiShipDroneRange);
    s.antiPlanetDroneRange = f.num("Maximum Anti-Planet Drone Target System Distance", s.antiPlanetDroneRange);
}

void parseFleets(const datafile::Record& rec, FleetsTable& t) {
    const Fields f(&rec);
    if (f.has("Fleets Num Divisions")) {
        std::vector<FleetDivision> divisions;
        const int n = f.num("Fleets Num Divisions", 0);
        for (int i = 1; i <= n; ++i)
            divisions.push_back({f.num(std::format("Fleets Div {} Max Amount of Ships", i), 0),
                                 f.num(std::format("Fleets Div {} Max Amount of Planets", i), 0),
                                 f.num(std::format("Fleets Div {} Num Fleets", i), 0)});
        if (!divisions.empty()) t.divisions = std::move(divisions);
    }
    t.percentInFleets = f.num("Fleets Percentage of Ships For Fleets", t.percentInFleets);
    t.dontUseForTurns = f.num("Fleets Dont Use For Num Turns", t.dontUseForTurns);
    t.defaultFormation = f.text("Fleets Default Formation", t.defaultFormation);
    t.defaultStrategy = f.text("Fleets Default Strategy", t.defaultStrategy);
    t.percentForDefense = f.num("Percentage of Fleets to use for defense", t.percentForDefense);
}

void parseGeneral(const datafile::Record& rec, GeneralInfo& g) {
    const Fields f(&rec);
    g.name = f.text("Name", g.name);
    g.description = f.text("Description", g.description);
    g.demeanor = f.text("Demeanor", g.demeanor);
    g.culture = f.text("Culture", g.culture);
}

std::vector<ResearchRow> parseResearch(const datafile::DataFile& file) {
    std::vector<ResearchRow> out;
    for (const auto& rec : file.records) {
        const Fields f(&rec);
        ResearchRow row;
        row.states = parseStateList(f.text("AI State"));
        row.area = f.text("Tech Area Name");
        row.level = f.num("Tech Area Level", 1);
        row.minPercent = f.num("Tech Area Min Percent", 25);
        if (!row.area.empty()) out.push_back(std::move(row));
    }
    return out;
}

std::vector<PlanetTypeRow> parsePlanetTypes(const datafile::DataFile& file) {
    std::vector<PlanetTypeRow> out;
    for (const auto& rec : file.records) {
        const Fields f(&rec);
        PlanetTypeRow row;
        row.states = parseStateList(f.text("AI State"));
        row.type = f.text("Planet Type");
        row.maxPerSystem = f.num("Max Per System", 100);
        row.percentOfColonies = f.num("Percent of Colonies", 100);
        row.minimumSize = f.text("Minimum Planet Size for Type");
        row.values = {f.num("Mineral Value", 0), f.num("Organics Value", 0), f.num("Radioactives Value", 0)};
        row.maxInEmpire = f.num("Maximum Total in Empire", 0);
        if (!row.type.empty()) out.push_back(std::move(row));
    }
    return out;
}

std::vector<FacilityQueue> parseFacilities(const datafile::DataFile& file) {
    std::vector<FacilityQueue> out;
    for (const auto& rec : file.records) {
        const Fields f(&rec);
        FacilityQueue q;
        q.states = parseStateList(f.text("AI State"));
        q.queueType = f.text("Construction Queue Type");
        const int n = f.num("Num Queue Entries", 0);
        for (int i = 1; i <= n; ++i) {
            FacilityEntry e;
            e.ability = f.text(std::format("Facility {} Ability", i));
            e.amount = f.num(std::format("Facility {} Amount", i), 1);
            if (!e.ability.empty()) q.entries.push_back(std::move(e));
        }
        if (!q.queueType.empty()) out.push_back(std::move(q));
    }
    return out;
}

std::vector<VehicleQueue> parseVehicles(const datafile::DataFile& file) {
    std::vector<VehicleQueue> out;
    for (const auto& rec : file.records) {
        const Fields f(&rec);
        VehicleQueue q;
        q.states = parseStateList(f.text("AI State"));
        const int n = f.num("Num Queue Entries", 0);
        for (int i = 1; i <= n; ++i) {
            VehicleEntry e;
            e.type = f.text(std::format("Entry {} Type", i));
            e.planetsPerItem = f.num(std::format("Entry {} Planet Per Item", i), 0);
            e.mustHave = f.num(std::format("Entry {} Must Have At Least", i), 0);
            if (!e.type.empty()) q.entries.push_back(std::move(e));
        }
        if (q.states) out.push_back(std::move(q));
    }
    return out;
}

bool parseVehicleType(std::string_view text, ruleset::VehicleType& out) {
    for (size_t i = 0; i < static_cast<size_t>(ruleset::VehicleType::Count); ++i) {
        const auto t = static_cast<ruleset::VehicleType>(i);
        if (keysEqual(text, ruleset::displayName(t))) {
            out = t;
            return true;
        }
    }
    return false;
}

std::vector<DesignTemplate> parseDesigns(const datafile::DataFile& file) {
    std::vector<DesignTemplate> out;
    for (const auto& rec : file.records) {
        const Fields f(&rec);
        DesignTemplate t;
        t.name = f.text("Name");
        if (t.name.empty()) continue;
        t.designType = f.text("Design Type", t.name);
        if (!parseVehicleType(f.text("Vehicle Type", "Ship"), t.vehicleType)) t.vehicleType = ruleset::VehicleType::Ship;
        t.defaultStrategy = f.text("Default Strategy");
        t.minTonnage = f.num("Size Minimum Tonnage", 0);
        t.maxTonnage = f.num("Size Maximum Tonnage", 1'000'000);
        const int must = f.num("Num Must Have At Least 1 Ability", 0);
        for (int i = 1; i <= must; ++i)
            if (auto a = f.text(std::format("Must Have Ability {}", i)); !a.empty()) t.mustHave.push_back(std::move(a));
        t.minSpeed = f.num("Minimum Speed", 0);
        t.desiredSpeed = f.num("Desired Speed", t.minSpeed);
        for (int i = 1; i <= 5; ++i) {
            t.majorityFamilies[static_cast<size_t>(i - 1)] = f.num(std::format("Majority Weapon Family Pick {}", i), 0);
            t.secondaryFamilies[static_cast<size_t>(i - 1)] = f.num(std::format("Secondary Weapon Family Pick {}", i), 0);
        }
        t.shieldsSpacesPerOne = f.num("Shields Spaces Per One", 0);
        t.armorSpacesPerOne = f.num("Armor Spaces Per One", 0);
        t.majority = {f.text("Majority Comp Ability"), f.num("Majority Comp Spaces Per One", 0)};
        t.secondary = {f.text("Secondary Comp Ability"), f.num("Secondary Comp Spaces Per One", 0)};
        const int misc = f.num("Num Misc Abilities", 0);
        for (int i = 1; i <= misc; ++i) {
            DensityEntry d{f.text(std::format("Misc Ability {} Name", i)), f.num(std::format("Misc Ability {} Spaces Per One", i), 0)};
            if (!d.ability.empty()) t.misc.push_back(std::move(d));
        }
        out.push_back(std::move(t));
    }
    return out;
}

Speech parseSpeech(const datafile::DataFile& file) {
    Speech s;
    std::unordered_map<std::string, std::string> values;
    std::vector<std::pair<std::string, int>> counts;
    for (const auto& rec : file.records)
        for (const auto& fld : rec.fields) {
            const std::string key = datafile::normalizeKey(fld.key);
            values.emplace(key, fld.value);
            if (key.starts_with("number of "))
                counts.emplace_back(key.substr(10), static_cast<int>(datafile::parseInteger(fld.value).value_or(0)));
        }
    for (const auto& [pool, n] : counts) {
        std::vector<std::string> lines;
        // "Number of Mega Evil Declarations" numbers its lines in the singular.
        const std::string singular = pool.ends_with('s') ? pool.substr(0, pool.size() - 1) : pool;
        for (int i = 1; i <= n; ++i) {
            auto it = values.find(std::format("{} {}", pool, i));
            if (it == values.end()) it = values.find(std::format("{} {}", singular, i));
            if (it != values.end() && !it->second.empty()) lines.push_back(it->second);
        }
        if (!lines.empty()) s.pools[pool] = std::move(lines);
    }
    return s;
}

std::vector<ruleset::CombatStrategy> parseStrategies(const datafile::DataFile& file) {
    std::vector<ruleset::CombatStrategy> out;
    for (const auto& rec : file.records) {
        ruleset::CombatStrategy st;
        for (const auto& fld : rec.fields) {
            if (keysEqual(fld.key, "Name") && st.name.empty()) st.name = fld.value;
            else st.settings.emplace_back(fld.key, fld.value);
        }
        if (!st.name.empty()) out.push_back(std::move(st));
    }
    return out;
}

} // namespace

// ---- Public -------------------------------------------------------------------------------------

std::string_view displayName(AiState s) { return s < AiState::Count ? kStateNames[static_cast<size_t>(s)] : "?"; }

bool parseAiState(std::string_view text, AiState& out) {
    for (size_t i = 0; i < kAiStates; ++i)
        if (keysEqual(text, kStateNames[i])) {
            out = static_cast<AiState>(i);
            return true;
        }
    return false;
}

AiState stateOf(const Empire& e) {
    return e.aiState >= 0 && e.aiState < static_cast<int>(kAiStates) ? static_cast<AiState>(e.aiState) : AiState::Exploration;
}

StateMask parseStateList(std::string_view text) {
    StateMask mask = 0;
    while (!text.empty()) {
        const size_t comma = text.find(',');
        const std::string_view item = text.substr(0, comma);
        AiState s;
        if (parseAiState(item, s)) mask = static_cast<StateMask>(mask | maskOf(s));
        if (comma == std::string_view::npos) break;
        text.remove_prefix(comma + 1);
    }
    return mask;
}

bool isDemand(MessageType t) { return t >= MessageType::DemandGift && t <= MessageType::DemandStopAttacks; }

std::string_view angerKeyName(MessageType t) {
    switch (t) {
        case MessageType::General: return "General Message";
        case MessageType::ProposeTreaty: return "Propose Treaty";
        case MessageType::AcceptTreaty: return "Accept Treaty";
        case MessageType::RefuseTreaty: return "Refuse Treaty";
        case MessageType::CounterTreaty: return "Offer Counter Treaty Proposal";
        case MessageType::BreakTreaty: return "Break Treaty";
        case MessageType::DeclareWar: return "Declare War";
        case MessageType::ProposeTrade: return "Propose Trade";
        case MessageType::AcceptTrade: return "Accept Trade";
        case MessageType::RefuseTrade: return "Refuse Trade";
        case MessageType::CounterTrade: return "Offer Counter Trade Proposal";
        case MessageType::Gift: return "Give Gift";
        case MessageType::Tribute: return "Offer Tribute";
        case MessageType::AcceptGift: return "Accept Gift";
        case MessageType::RefuseGift: return "Refuse Gift";
        case MessageType::Surrender: return "Surrender";
        case MessageType::GrantIndependence: return "Grant independence to colony";
        case MessageType::DemandGift: return "Want a gift";
        case MessageType::DemandTribute: return "Want a tribute";
        case MessageType::DemandSurrender: return "Demand your surrender";
        case MessageType::DemandRemoveShips: return "Remove your ships from system";
        case MessageType::DemandRemoveColonies: return "Remove your colonies from system";
        case MessageType::DemandLeavePlanet: return "Leave planet";
        case MessageType::RequestStopHostilities: return "Stop hostile actions against empire";
        case MessageType::RequestBreakTreaty: return "Break treaty with empire";
        case MessageType::RequestDeclareWar: return "Declare war on empire";
        case MessageType::RequestMakePeace: return "Make peace with empire";
        case MessageType::RequestSupport: return "Support us against another empire";
        case MessageType::RequestAttackEmpire: return "Attack empire in system";
        case MessageType::RequestAttackPlanet: return "Attack planet";
        case MessageType::DemandStopEspionage: return "Stop espionage activities";
        case MessageType::DemandStopSabotage: return "Stop sabotage activities";
        case MessageType::DemandStopAttacks: return "Stop attacks in system";
        case MessageType::AcceptDemand: return "Accept Demand/Request";
        case MessageType::RefuseDemand: return "Refuse Demand/Request";
        case MessageType::Count: break;
    }
    return {};
}

bool parseTreatyName(std::string_view text, Treaty& out) {
    for (size_t i = 0; i < static_cast<size_t>(Treaty::Count); ++i) {
        const auto t = static_cast<Treaty>(i);
        if (keysEqual(text, displayName(t))) {
            out = t;
            return true;
        }
    }
    if (keysEqual(text, "Trade and Research Alliance")) {
        out = Treaty::TradeResearchAlliance;
        return true;
    }
    if (keysEqual(text, "Non Aggression")) {
        out = Treaty::NonAggression;
        return true;
    }
    return false;
}

const std::vector<std::string>* Speech::pool(std::string_view name) const {
    auto it = pools.find(datafile::normalizeKey(name));
    return it != pools.end() && !it->second.empty() ? &it->second : nullptr;
}

const DesignTemplate* AiProfile::design(std::string_view aiType) const {
    for (const auto& t : designs)
        if (keysEqual(t.name, aiType)) return &t;
    return nullptr;
}

const VehicleQueue* AiProfile::vehicleQueue(AiState s) const {
    for (const auto& q : vehicles)
        if (q.states & maskOf(s)) return &q;
    return nullptr;
}

const FacilityQueue* AiProfile::facilityQueue(AiState s, std::string_view queueType) const {
    const FacilityQueue* any = nullptr;
    for (const auto& q : facilities) {
        if (!keysEqual(q.queueType, queueType)) continue;
        if (q.states & maskOf(s)) return &q;
        if (!any) any = &q;
    }
    return any;  // a queue type with no row for this state uses its first row (inferred)
}

const AiProfile& builtinProfile() {
    static const AiProfile p = makeBuiltin();
    return p;
}

AiProfile loadProfile(const std::filesystem::path& gameRoot, std::string_view raceStyle, std::string_view ministerStyle) {
    AiProfile p = builtinProfile();
    p.sources.clear();
    const Locations where = locate(gameRoot, raceStyle, ministerStyle);
    const Scope personal = ministerStyle.empty() ? Scope::Race : Scope::Style;

    if (auto f = readTable(where, "Anger", personal, p)) parseAnger(f->records.front(), p.anger);
    if (auto f = readTable(where, "Politics", personal, p)) parsePolitics(f->records.front(), p.politics);
    if (auto f = readTable(where, "Settings", personal, p)) parseSettings(f->records.front(), p.settings);
    if (auto f = readTable(where, "General", personal, p)) parseGeneral(f->records.front(), p.general);
    if (auto f = readTable(where, "Fleets", Scope::Race, p)) parseFleets(f->records.front(), p.fleets);
    if (auto f = readTable(where, "Research", Scope::Race, p)) {
        auto rows = parseResearch(*f);
        if (!rows.empty()) p.research = std::move(rows);
    }
    if (auto f = readTable(where, "DesignCreation", Scope::Race, p)) {
        auto designs = parseDesigns(*f);
        if (!designs.empty()) {
            p.designs = std::move(designs);
            if (!p.design("Scout")) p.designs.push_back(scoutTemplate());
        }
    }
    if (auto f = readTable(where, "Construction_Facilities", Scope::Global, p)) {
        auto rows = parseFacilities(*f);
        if (!rows.empty()) p.facilities = std::move(rows);
    }
    if (auto f = readTable(where, "Construction_Vehicles", Scope::Global, p)) {
        auto rows = parseVehicles(*f);
        if (!rows.empty()) p.vehicles = std::move(rows);
    }
    if (auto f = readTable(where, "Planet_Types", Scope::Global, p)) {
        auto rows = parsePlanetTypes(*f);
        if (!rows.empty()) p.planetTypes = std::move(rows);
    }
    if (auto f = readTable(where, "Speech", ministerStyle.empty() ? Scope::Global : Scope::Style, p)) {
        auto speech = parseSpeech(*f);
        if (!speech.pools.empty()) p.speech = std::move(speech);
    }
    if (auto f = readTable(where, "Strategies", Scope::Global, p)) p.strategies = parseStrategies(*f);
    if (p.sources.empty()) p.sources.push_back("built-in");
    return p;
}

const AiProfile& profileFor(const Rules& r, std::string_view raceStyle, std::string_view ministerStyle) {
    if (r.gameRoot().empty()) return builtinProfile();
    static std::mutex mutex;
    static std::map<std::string, std::unique_ptr<const AiProfile>> cache;
    const std::string key = std::format("{}|{}|{}", r.gameRoot().string(), datafile::normalizeKey(raceStyle),
                                        datafile::normalizeKey(ministerStyle));
    std::lock_guard lock(mutex);
    auto it = cache.find(key);
    if (it == cache.end())
        it = cache.emplace(key, std::make_unique<const AiProfile>(loadProfile(r.gameRoot(), raceStyle, ministerStyle))).first;
    return *it->second;
}

const AiProfile& profileFor(const Rules& r, const Empire& e) { return profileFor(r, e.race.style); }

} // namespace opense4::game::ai
