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

// Spec 05 §7.7: the design types the AI tables and ministers work with (sorted).
constexpr std::array<std::string_view, 39> kAiDesignTypes{
    "Anti-Planet Drone", "Anti-Ship Drone", "Attack Base", "Attack Ship",
    "Base Space Yard", "Boarding Ship", "Cargo Transport", "Carrier",
    "Close Warp Point", "Colony (Gas)", "Colony (Ice)", "Colony (Rock)",
    "Create Black Hole", "Create Nebulae", "Create Planet", "Create Star",
    "Create Storm", "Defense Base", "Defense Ship", "Destroy Black Hole",
    "Destroy Nebulae", "Destroy Planet", "Destroy Star", "Destroy Storm",
    "Drone Carrier", "Fighter", "Kamikaze Attack Ship", "Mine",
    "Mine Layer", "Mine Sweeper", "Open Warp Point", "Population Transport",
    "Recon Satellite", "Satellite", "Satellite Layer", "Space Yard Ship",
    "Troop", "Troop Transport", "Weapon Platform",
};

// In ColonyType order.
constexpr std::array<std::string_view, static_cast<size_t>(ColonyType::Count)> kColonyTypes{
    "Construction Yard", "Farming Colony", "Homeworld", "Intelligence Compound", "Military Installation",
    "Mining Colony",     "Refining Colony", "Research Compound", "Resupply Base",
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
    std::filesystem::path ownDir;      // Ai/<style>, or the race folder when there is no style
};

Locations locate(const std::filesystem::path& root, std::string_view raceStyle, std::string_view ministerStyle) {
    Locations l;
    l.aiDir = findChild(root, "Ai", true);
    if (!ministerStyle.empty()) {
        l.ownDir = findChild(l.aiDir, ministerStyle, true);
    } else if (!raceStyle.empty()) {
        const auto pictures = findChild(root, "Pictures", true);
        for (std::string_view group : {"Races", "RaceNeutral"})
            if (auto d = findChild(findChild(pictures, group, true), raceStyle, true); !d.empty()) {
                l.ownDir = d;
                break;
            }
    }
    return l;
}

// Spec 05 §7.2: the style's (or race's) own file, else Ai/Default_AI_<Name>.txt.
std::filesystem::path lookup(const Locations& l, std::string_view table) {
    if (auto f = findTable(l.ownDir, table); !f.empty()) return f;
    return findTable(l.aiDir, table, "default_");
}

std::optional<datafile::DataFile> readTable(const Locations& l, std::string_view table, AiProfile& p) {
    const auto path = lookup(l, table);
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
    // File order; the last qualifying entry is sent (spec 05 §7.4).
    p.proposeTypes = {{Treaty::Partnership, 30},
                      {Treaty::MilitaryAlliance, 20},
                      {Treaty::TradeResearchAlliance, 12},
                      {Treaty::TradeAlliance, 6},
                      {Treaty::NonAggression, 0}};
    for (size_t i = 0; i < kMessageTypes; ++i) {
        const auto t = static_cast<MessageType>(i);
        if (!isDemand(t)) continue;
        DemandRule d;
        d.sendToFriend = d.sendToEnemy = true;
        d.acceptScorePercent = 140;
        d.acceptFromFriend = true;
        d.acceptFromEnemy = false;
        switch (t) {
            case MessageType::DemandSurrender:
                d.acceptScorePercent = 900;
                d.acceptFromFriend = false;
                d.acceptFromEnemy = true;
                break;
            case MessageType::DemandGift:
            case MessageType::DemandTribute: d.acceptScorePercent = 150; break;
            case MessageType::DemandStopEspionage:
            case MessageType::DemandStopSabotage: d.acceptScorePercent = 120; break;
            default: break;
        }
        p.demands[i] = d;
    }
    return p;
}

std::vector<PlanetTypeRow> defaultPlanetTypes() {
    const StateMask war = maskOf(AiState::PrepareForAttack) | maskOf(AiState::Attack) | maskOf(AiState::SecureHoldings) |
                          maskOf(AiState::DefendShortTerm);
    return {
        {kAllStates, "Research Compound", 1, 20, "Small", {}, 0},
        {war, "Military Installation", 1, 10, "Large", {}, 0},
        {kAllStates, "Mining Colony", 0, 40, "", {120, 0, 0}, 0},
        {kAllStates, "Farming Colony", 0, 20, "", {0, 120, 0}, 0},
        {kAllStates, "Refining Colony", 0, 20, "", {0, 0, 120}, 0},
        {kAllStates, "Construction Yard", 1, 10, "Medium", {}, 3},
        {kAllStates, "Resupply Base", 1, 10, "", {}, 0},
        {kAllStates, "Intelligence Compound", 1, 5, "Small", {}, 2},
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
        {parseStateList("Exploration, Not Connected"),
         {{"Colonizer", 20, 2}, {"Attack Ship", 40, 2}, {"Defense Base", 0, 0}}},
        {parseStateList("Infrastructure"),
         {{"Colonizer", 25, 1}, {"Attack Ship", 15, 3}, {"Defense Base", 60, 1}, {"Weapon Platform", 10, 2},
          {"Population Transport", 80, 0}}},
        {parseStateList("Prepare for Attack, Attack"),
         {{"Attack Ship", 5, 4}, {"Colonizer", 60, 0}, {"Troop Transport", 50, 1}, {"Troop", 5, 4}, {"Defense Base", 100, 0}}},
        {parseStateList("Secure Holdings After Attack, Incursion"),
         {{"Attack Ship", 8, 3}, {"Colonizer", 40, 1}, {"Defense Base", 60, 1}}},
        {parseStateList("Prepare for Defense, Defend (Short Term), Defend (Long Term)"),
         {{"Defense Base", 15, 2}, {"Attack Ship", 8, 3}, {"Weapon Platform", 5, 4}, {"Satellite", 10, 0}}},
    };
}

// Our own weapon families (the Weapon Family numbers of our test content).
constexpr std::array<int, 5> kBeamFamilies{200, 201, 202, 0, 0};

DesignTemplate makeTemplate(std::string name, ruleset::VehicleType type, std::vector<std::string> mustHave, int minSpeed,
                            int desiredSpeed, DensityEntry majority, int shields, int armor, std::vector<DensityEntry> misc = {},
                            std::array<int, 5> families = kBeamFamilies) {
    DesignTemplate t;
    t.majorityFamilies = families;
    t.name = name;
    t.designType = std::move(name);
    t.vehicleType = type;
    t.minTonnage = 1;
    t.maxTonnage = 5000;
    t.mustHave = std::move(mustHave);
    t.minSpeed = minSpeed;
    t.desiredSpeed = desiredSpeed;
    t.majority = std::move(majority);
    t.shieldsSpacesPerOne = shields;
    t.armorSpacesPerOne = armor;
    t.misc = std::move(misc);
    return t;
}

std::vector<DesignTemplate> defaultDesigns() {
    using ruleset::VehicleType;
    const DensityEntry weapons{"Weapon", 100};
    std::vector<DesignTemplate> out;
    out.push_back(makeTemplate("Attack Ship", VehicleType::Ship, {"Weapon"}, 2, 4, weapons, 300, 400,
                               {{"Supply Storage", 500}, {"Point-Defense", 900}}));
    out.push_back(makeTemplate("Defense Ship", VehicleType::Ship, {"Weapon"}, 2, 3, weapons, 250, 300, {{"Supply Storage", 700}}));
    out.push_back(makeTemplate("Defense Base", VehicleType::Base, {"Weapon"}, 0, 0, weapons, 250, 300, {{"Point-Defense", 700}}));
    for (std::string_view surface : {"Rock", "Ice", "Gas"}) {
        const std::string ability = std::format("Colonize Planet - {}", surface);
        out.push_back(makeTemplate(std::format("Colony ({})", surface), VehicleType::Ship, {ability}, 2, 3, {ability, 0}, 0, 0,
                                   {{"Supply Storage", 10000}}));
    }
    out.push_back(makeTemplate("Population Transport", VehicleType::Ship, {"Cargo Storage"}, 2, 3, {"Cargo Storage", 100}, 0, 0,
                               {{"Supply Storage", 10000}}));
    out.push_back(makeTemplate("Troop Transport", VehicleType::Ship, {"Cargo Storage"}, 2, 3, {"Cargo Storage", 100}, 0, 400,
                               {{"Supply Storage", 10000}}));
    out.push_back(makeTemplate("Carrier", VehicleType::Ship, {"Launch/Recover Fighters"}, 2, 3, {"Launch/Recover Fighters", 100}, 300,
                               600, {{"Supply Storage", 10000}}));
    out.push_back(makeTemplate("Mine Layer", VehicleType::Ship, {"Lay Mines"}, 2, 3, {"Lay Mines", 100}, 0, 500, {{"Supply Storage", 10000}}));
    out.push_back(makeTemplate("Satellite Layer", VehicleType::Ship, {"Launch/Recover Satellites"}, 2, 3,
                               {"Launch/Recover Satellites", 100}, 0, 500, {{"Supply Storage", 10000}}));
    out.push_back(makeTemplate("Mine Sweeper", VehicleType::Ship, {"Mine Sweeping"}, 2, 4, {"Mine Sweeping", 300}, 0, 400,
                               {{"Supply Storage", 10000}}));
    out.push_back(makeTemplate("Base Space Yard", VehicleType::Base, {"Space Yard"}, 0, 0, {"Space Yard", 10000}, 0, 300));
    out.push_back(makeTemplate("Space Yard Ship", VehicleType::Ship, {"Space Yard"}, 2, 3, {"Space Yard", 10000}, 0, 400,
                               {{"Supply Storage", 10000}}));
    out.push_back(makeTemplate("Weapon Platform", VehicleType::WeaponPlatform, {"Weapon"}, 0, 0, weapons, 0, 200));
    out.push_back(makeTemplate("Satellite", VehicleType::Satellite, {"Weapon"}, 0, 0, weapons, 0, 200, {}, {207, 200, 0, 0, 0}));
    out.push_back(makeTemplate("Fighter", VehicleType::Fighter, {"Weapon"}, 1, 4, weapons, 0, 0, {}, {204, 0, 0, 0, 0}));
    out.push_back(makeTemplate("Mine", VehicleType::Mine, {"Weapon"}, 0, 0, weapons, 0, 0, {}, {205, 0, 0, 0, 0}));
    out.push_back(makeTemplate("Troop", VehicleType::Troop, {"Weapon"}, 0, 0, weapons, 0, 200, {}, {206, 0, 0, 0, 0}));
    return out;
}

Speech defaultSpeech() {
    Speech s;
    auto add = [&](std::string_view pool, std::vector<std::string> lines) { s.pools[datafile::normalizeKey(pool)] = std::move(lines); };
    add("Send General Message", {"Greetings from [%OurEmpireName]."});
    add("Send Propose Treaty", {"[%OurEmpireName] offers [%TargetEmpireName] a [%ProposedTreatyName]."});
    add("Send Offer Counter Treaty Proposal", {"[%OurEmpireName] could agree to a [%ProposedTreatyName] instead."});
    add("Send Accept Treaty", {"[%OurEmpireName] agrees to the [%ProposedTreatyName]."});
    add("Send Refuse Treaty", {"[%OurEmpireName] declines the [%ProposedTreatyName]."});
    add("Send Break Treaty", {"[%OurEmpireName] no longer honours its [%TreatyName] with you."});
    add("Send Declare War", {"[%OurEmpireName] is now at war with [%TargetEmpireName]."});
    add("Send Accept Trade", {"The exchange is acceptable."});
    add("Send Refuse Trade", {"The exchange is not acceptable."});
    add("Send Give Gift", {"Please accept this from [%OurEmpireName]."});
    add("Send Offer Tribute", {"[%OurEmpireName] offers this tribute to [%TargetEmpireName]."});
    add("Send Accept Gift", {"[%OurEmpireName] thanks you for the gift."});
    add("Send Refuse Gift", {"[%OurEmpireName] wants nothing from you."});
    add("Send Accept Tribute", {"Your tribute is noted."});
    add("Send Refuse Tribute", {"Keep your tribute."});
    add("Send Accept Demand/Request", {"[%OurEmpireName] will do as you ask."});
    add("Send Refuse Demand/Request", {"[%OurEmpireName] will not do that."});
    add("Send Remove your ships from system", {"Withdraw your ships from [%SystemName]."});
    add("Send Remove your colonies from system", {"Leave [%SystemName]."});
    add("Send Declare war on empire", {"Join [%OurEmpireName] against [%OtherEmpireName]."});
    add("Send Break treaty with empire", {"End your treaty with [%OtherEmpireName]."});
    add("Send Make peace with empire", {"Make peace with [%OtherEmpireName]."});
    add("Send Attack empire in system", {"Strike [%OtherEmpireName] in [%SystemName]."});
    add("Send Demand your surrender", {"[%OurEmpireName] demands your surrender."});
    add("Send Stop attacks in system", {"Stop your attacks in [%SystemName]."});
    add("Send Stop espionage activities", {"Stop spying on [%OurEmpireName]."});
    add("Send Stop sabotage activities", {"Stop sabotaging [%OurEmpireName]."});
    add("Send Propose Trade", {"[%OurEmpireName] proposes an exchange."});
    add("Send Offer Counter Trade Proposal", {"[%OurEmpireName] could accept this exchange instead."});
    add("Send Want a gift", {"[%OurEmpireName] would welcome a gift."});
    add("Send Want a tribute", {"[%OurEmpireName] expects a tribute."});
    add("Send Leave planet", {"Leave [%PlanetName]."});
    add("Send Stop hostile actions against empire", {"Stop your hostile acts against [%OtherEmpireName]."});
    add("Send Support us against another empire", {"Stand with [%OurEmpireName] against [%OtherEmpireName]."});
    add("Send Attack planet", {"Strike at [%PlanetName]."});
    add("Send Surrender", {"[%OurEmpireName] lays down its arms before [%TargetEmpireName]."});
    add("Send Grant independence to colony", {"[%OurEmpireName] releases [%PlanetName]."});
    add("Mega Evil Declarations", {"[%TargetEmpireName] has grown too powerful. [%OurEmpireName] will oppose it."});
    // The General messages that answer acknowledgements and refused
    // requests (spec 05 §7.4 "Which messages get an answer", §7.5 AI_Speech).
    const std::array<std::pair<std::string_view, std::pair<std::string_view, std::string_view>>, 15> responses{{
        {"Accept Treaty", {"[%OurEmpireName] welcomes the agreement.", "Let us see whether this agreement holds."}},
        {"Refuse Treaty", {"[%OurEmpireName] regrets your answer.", "As you wish."}},
        {"Break Treaty", {"[%OurEmpireName] had hoped for better.", "[%OurEmpireName] will remember this."}},
        {"Declare War", {"We did not expect this from [%TargetEmpireName].", "[%OurEmpireName] accepts your challenge."}},
        {"Accept Trade", {"A good exchange for both of us.", "The exchange is noted."}},
        {"Refuse Trade", {"Perhaps another time.", "So be it."}},
        {"Accept Gift", {"[%OurEmpireName] is glad you liked it.", "Enjoy it."}},
        {"Refuse Gift", {"[%OurEmpireName] is disappointed.", "Your loss."}},
        {"Accept Tribute", {"[%OurEmpireName] is pleased.", "Remember who is generous."}},
        {"Refuse Tribute", {"[%OurEmpireName] is surprised.", "As you wish."}},
        {"Want a gift", {"[%OurEmpireName] has nothing to spare now.", "[%OurEmpireName] gives nothing."}},
        {"Want a tribute", {"[%OurEmpireName] cannot pay now.", "[%OurEmpireName] pays no tribute."}},
        {"Demand your surrender", {"That is no way to speak to a friend.", "[%OurEmpireName] will never surrender."}},
        {"Surrender", {"[%OurEmpireName] accepts your surrender.", "[%OurEmpireName] accepts your surrender."}},
        {"Grant independence to colony", {"[%OurEmpireName] thanks you.", "[%OurEmpireName] notes it."}},
    }};
    for (const auto& [type, lines] : responses) {
        add(std::format("Response Friend {}", type), {std::string(lines.first)});
        add(std::format("Response Enemy {}", type), {std::string(lines.second)});
    }
    // The verdicts on the thirteen demands, "remove ships" to "stop attacks".
    for (size_t i = static_cast<size_t>(MessageType::DemandRemoveShips); i <= static_cast<size_t>(MessageType::DemandStopAttacks); ++i) {
        const std::string_view type = angerKeyName(static_cast<MessageType>(i));
        add(std::format("Response Friend YES {}", type), {"[%OurEmpireName] will do as you ask."});
        add(std::format("Response Friend NO {}", type), {"[%OurEmpireName] cannot do that, friend."});
        add(std::format("Response Enemy YES {}", type), {"[%OurEmpireName] agrees, this time."});
        add(std::format("Response Enemy NO {}", type), {"[%OurEmpireName] will not do that."});
    }
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

// Keys absent from the file take the spec's defaults (spec 05 §7.5), not the built-in ones.
void parseSettings(const datafile::Record& rec, SettingsTable& s) {
    const Fields f(&rec);
    const SettingsTable d;
    for (int i = 1; i <= 3; ++i) {
        auto& cap = s.tonnageCaps[static_cast<size_t>(i - 1)];
        cap.first = f.num(std::format("Max Ship Size Tonnage From Start {} Amount", i), 0);
        cap.second = f.num(std::format("Max Ship Size Tonnage From Start {} Num Turns", i), 0);
    }
    s.turnsBetweenAttacks = f.num("Turns to Wait until next attack", d.turnsBetweenAttacks);
    s.maxMaintenancePercent = f.num("Maximum Maintenance Percent of Revenue", d.maxMaintenancePercent);
    s.maxResearchPoints = f.num64("Maximum Research Point Generation", d.maxResearchPoints);
    s.maxIntelligencePoints = f.num64("Maximum Intelligence Point Generation", d.maxIntelligencePoints);
    s.maxSystemsToDefend = f.num("Maximum Systems to Defend at a Time", d.maxSystemsToDefend);
    s.angryOverAlliedPlanets = f.flag("Get Angry Over Allied Colonizable Planets", d.angryOverAlliedPlanets);
    s.angryOverEnemyPlanets = f.flag("Get Angry Over Enemy Colonizable Planets", d.angryOverEnemyPlanets);
    auto planetsKey = [](std::string_view side) { return std::format("Percentage of {} Planets to consider as Attack Locations for Anger", side); };
    s.alliedPlanetsPercent = f.num(planetsKey("Allied"), d.alliedPlanetsPercent);
    s.enemyPlanetsPercent = f.num(planetsKey("Enemy"), d.enemyPlanetsPercent);
    s.personalityGroup = f.num("Personality Group", d.personalityGroup);
    s.avoidMinefields = f.flag("Ships don't move through minefields", d.avoidMinefields);
    s.avoidRestrictedSystems = f.flag("Ships don't move through restricted systems", d.avoidRestrictedSystems);
    s.clearOrdersOnEnemy = f.flag("Clear orders on encounter enemy", d.clearOrdersOnEnemy);
    s.clearOrdersOnAll = f.flag("Clear orders on encounter all", d.clearOrdersOnAll);
    s.satellitesKeptPercent = f.num("Percentage of total satellites to keep as planetary cargo", d.satellitesKeptPercent);
    s.dronesKeptPercent = f.num("Percentage of total drones to keep as planetary cargo", d.dronesKeptPercent);
    s.antiShipDronesPerTarget = f.num("Number Of Anti-Ship Drones Per Target", d.antiShipDronesPerTarget);
    s.antiPlanetDronesPerTarget = f.num("Number Of Anti-Planet Drones Per Target", d.antiPlanetDronesPerTarget);
    s.antiShipDroneRange = f.num("Maximum Anti-Ship Drone Target System Distance", d.antiShipDroneRange);
    s.antiPlanetDroneRange = f.num("Maximum Anti-Planet Drone Target System Distance", d.antiPlanetDroneRange);
}

void parseFleets(const datafile::Record& rec, FleetsTable& t) {
    const Fields f(&rec);
    std::vector<FleetDivision> divisions;
    const int n = f.num("Fleets Num Divisions", 0);
    for (int i = 1; i <= n; ++i)
        divisions.push_back({f.num(std::format("Fleets Div {} Max Amount of Ships", i), 0),
                             f.num(std::format("Fleets Div {} Max Amount of Planets", i), 0),
                             f.num(std::format("Fleets Div {} Num Fleets", i), 0)});
    t.divisions = std::move(divisions);
    t.percentInFleets = f.num("Fleets Percentage of Ships For Fleets", 0);
    t.dontUseForTurns = f.num("Fleets Dont Use For Num Turns", 0);
    t.defaultFormation = f.text("Fleets Default Formation");
    t.defaultStrategy = f.text("Fleets Default Strategy");
    t.percentForDefense = f.num("Percentage of Fleets to use for defense", 0);
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
        row.level = f.num("Tech Area Level", 0);
        row.minPercent = f.num("Tech Area Min Percent", 0);
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
        row.maxPerSystem = f.num("Max Per System", 0);
        row.percentOfColonies = f.num("Percent of Colonies", 0);
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
            e.amount = f.num(std::format("Facility {} Amount", i), 0);
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

std::vector<UnitQueue> parseUnits(const datafile::DataFile& file) {
    // A leading reserve key can make the whole file one record, so records
    // are split where `Colony Type` repeats.
    std::vector<datafile::Record> rows(1);
    for (const auto& rec : file.records)
        for (const auto& fld : rec.fields) {
            if (keysEqual(fld.key, "Colony Type") && rows.back().find(fld.key)) rows.emplace_back();
            rows.back().fields.push_back(fld);
        }
    std::vector<UnitQueue> out;
    for (const auto& rec : rows) {
        const Fields f(&rec);
        UnitQueue q;
        q.colonyType = f.text("Colony Type");
        const int n = f.num("Num Queue Entries", 0);
        for (int i = 1; i <= n; ++i) {
            UnitEntry e;
            e.type = f.text(std::format("Entry {} Type", i));
            e.maxKt = std::min(f.num(std::format("Entry {} Maximum in kT", i), 0), 65'000);
            if (!e.type.empty()) q.entries.push_back(std::move(e));
        }
        if (f.has("Colony Type")) out.push_back(std::move(q));
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
        // A missing key is 0 (spec 05 §7.5): a template without a maximum never finds a hull.
        t.minTonnage = f.num("Size Minimum Tonnage", 0);
        t.maxTonnage = f.num("Size Maximum Tonnage", 0);
        const int must = f.num("Num Must Have At Least 1 Ability", 0);
        for (int i = 1; i <= must; ++i)
            if (auto a = f.text(std::format("Must Have Ability {}", i)); !a.empty()) t.mustHave.push_back(std::move(a));
        t.minSpeed = f.num("Minimum Speed", 0);
        t.desiredSpeed = f.num("Desired Speed", 0);
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
    for (size_t i = 0; i < kAiStates; ++i)
        if (text.find(kStateNames[i]) != std::string_view::npos) mask = static_cast<StateMask>(mask | (1u << i));
    return mask;
}

bool isDemand(MessageType t) { return t >= MessageType::DemandGift && t <= MessageType::DemandStopAttacks; }

std::span<const std::string_view> aiDesignTypes() { return kAiDesignTypes; }

bool isAiDesignType(std::string_view name) {
    return std::any_of(kAiDesignTypes.begin(), kAiDesignTypes.end(), [&](std::string_view t) { return keysEqual(t, name); });
}

std::string_view displayName(ColonyType t) { return t < ColonyType::Count ? kColonyTypes[static_cast<size_t>(t)] : std::string_view{}; }

ColonyType parseColonyType(std::string_view text) {
    if (keysEqual(text, "Imperial Center")) return ColonyType::Homeworld;
    for (size_t i = 0; i < kColonyTypes.size(); ++i)
        if (keysEqual(text, kColonyTypes[i])) return static_cast<ColonyType>(i);
    return ColonyType::Count;
}

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
    const VehicleQueue* last = nullptr;
    for (const auto& q : vehicles)
        if (q.states & maskOf(s)) last = &q;
    return last;
}

const FacilityQueue* AiProfile::facilityQueue(AiState s, std::string_view queueType) const {
    const FacilityQueue* last = nullptr;
    for (const auto& q : facilities)
        if ((q.states & maskOf(s)) && keysEqual(q.queueType, queueType)) last = &q;
    return last;
}

const UnitQueue* AiProfile::unitQueue(std::string_view colonyType) const {
    if (colonyType.empty()) return nullptr;
    const std::string want = lowerAscii(std::string(colonyType));
    const UnitQueue* last = nullptr;
    for (const auto& q : units)
        if (lowerAscii(q.colonyType).find(want) != std::string::npos) last = &q;
    return last;
}

const AiProfile& builtinProfile() {
    static const AiProfile p = makeBuiltin();
    return p;
}

AiProfile loadProfile(const std::filesystem::path& gameRoot, std::string_view raceStyle, std::string_view ministerStyle) {
    AiProfile p = builtinProfile();
    p.sources.clear();
    const Locations where = locate(gameRoot, raceStyle, ministerStyle);

    if (auto f = readTable(where, "Anger", p)) parseAnger(f->records.front(), p.anger);
    if (auto f = readTable(where, "Politics", p)) parsePolitics(f->records.front(), p.politics);
    if (auto f = readTable(where, "Settings", p)) parseSettings(f->records.front(), p.settings);
    if (auto f = readTable(where, "General", p)) parseGeneral(f->records.front(), p.general);
    if (auto f = readTable(where, "Fleets", p)) parseFleets(f->records.front(), p.fleets);
    if (auto f = readTable(where, "Research", p)) p.research = parseResearch(*f);
    if (auto f = readTable(where, "DesignCreation", p)) p.designs = parseDesigns(*f);
    if (auto f = readTable(where, "Construction_Facilities", p)) p.facilities = parseFacilities(*f);
    if (auto f = readTable(where, "Construction_Vehicles", p)) p.vehicles = parseVehicles(*f);
    if (auto f = readTable(where, "Planet_Types", p)) p.planetTypes = parsePlanetTypes(*f);
    if (auto f = readTable(where, "Speech", p)) {
        auto speech = parseSpeech(*f);
        if (!speech.pools.empty()) p.speech = std::move(speech);
    }
    if (auto f = readTable(where, "Strategies", p)) p.strategies = parseStrategies(*f);
    // Not shipped with the stock install (spec 05 §7.5 "Units file"): the
    // reserve for units in the first record, then the rows.
    if (auto f = readTable(where, "Construction_Units", p)) {
        p.unitsFile = true;
        p.unitReservePercent = Fields(&f->records.front()).num("Percentage of Resources To Reserve For Unit Construction", 0);
        p.units = parseUnits(*f);
    }
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

const AiProfile& profileFor(const Rules& r, const Empire& e) { return profileFor(r, e.race.style, ministerStyleOf(e)); }

std::vector<std::string> ministerStyles(const Rules& r) {
    std::vector<std::string> out;
    const auto aiDir = findChild(r.gameRoot(), "Ai", true);
    std::error_code ec;
    if (aiDir.empty() || !std::filesystem::is_directory(aiDir, ec)) return out;
    for (const auto& dir : std::filesystem::directory_iterator(aiDir, ec)) {
        if (!dir.is_directory(ec)) continue;
        bool tables = false;
        for (const auto& f : std::filesystem::directory_iterator(dir.path(), ec))
            tables = tables || (f.is_regular_file(ec) && lowerAscii(f.path().filename().string()).find("_ai_") != std::string::npos);
        if (tables) out.push_back(dir.path().filename().string());
    }
    std::sort(out.begin(), out.end(), [](const std::string& a, const std::string& b) { return lowerAscii(a) < lowerAscii(b); });
    return out;
}

const std::vector<std::string>& designNameList(const Rules& r, std::string_view file) {
    static const std::vector<std::string> kNone;
    if (r.gameRoot().empty() || file.empty()) return kNone;
    static std::mutex mutex;
    static std::map<std::string, std::unique_ptr<const std::vector<std::string>>> cache;
    const std::string key = std::format("{}|{}", r.gameRoot().string(), datafile::normalizeKey(file));
    std::lock_guard lock(mutex);
    auto it = cache.find(key);
    if (it == cache.end()) {
        auto names = std::make_unique<std::vector<std::string>>();
        if (auto path = findChild(findChild(r.gameRoot(), "Dsgnname", true), file, false); !path.empty())
            if (auto loaded = datafile::load(path)) *names = loaded->entries;
        it = cache.emplace(key, std::move(names)).first;
    }
    return *it->second;
}

} // namespace opense4::game::ai
