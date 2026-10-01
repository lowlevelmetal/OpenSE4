#include "client/classic/screens/screens.hpp"

#include <array>
#include <cctype>

namespace opense4::client::classic {

namespace {

class PlaceholderScreen final : public Screen {
public:
    explicit PlaceholderScreen(ScreenId id) : id_(id) {}
    bool draw(UiContext& ui) override {
        Dialog d(ui, screenTitle(id_), DialogSize::Large);
        if (d.open()) {
            d.beginContent();
            ImGui::TextDisabled("This window is not available yet.");
            d.beginButtons();
            d.close();
        }
        return d.keepOpen();
    }

private:
    ScreenId id_;
};

} // namespace

std::unique_ptr<Screen> makePlaceholder(ScreenId id) { return std::make_unique<PlaceholderScreen>(id); }

const char* screenTitle(ScreenId id) {
    switch (id) {
        case ScreenId::GameMenu: return "Game Menu";
        case ScreenId::Designs: return "Designs";
        case ScreenId::CreateDesign: return "Create Design";
        case ScreenId::Planets: return "Planets";
        case ScreenId::Colonies: return "Colonies";
        case ScreenId::Ships: return "Ships \\ Units";
        case ScreenId::Queues: return "Construction Queues";
        case ScreenId::SetQueue: return "Set Construction Queue";
        case ScreenId::Research: return "Research";
        case ScreenId::TechTree: return "Tech Tree";
        case ScreenId::Empires: return "Empires";
        case ScreenId::Log: return "Log";
        case ScreenId::EmpireStatus: return "Empire Status";
        case ScreenId::Help: return "Help";
        case ScreenId::GalaxyMap: return "Galaxy Map";
        case ScreenId::EmpireOptions: return "Empire Options";
        case ScreenId::Ministers: return "Ministers";
        case ScreenId::SystemsToAvoid: return "Systems To Avoid";
        case ScreenId::Waypoints: return "Waypoints";
        case ScreenId::Strategies: return "Strategies";
        case ScreenId::RepairPriorities: return "Repair Priorities";
        case ScreenId::FleetTransfer: return "Fleet Transfer";
        case ScreenId::CargoTransfer: return "Cargo Transfer";
        case ScreenId::LaunchRecover: return "Launch \\ Recover Units";
        case ScreenId::Scrap: return "Scrap";
        case ScreenId::ViewOrders: return "View Orders";
        case ScreenId::SelectWaypoint: return "Select Waypoint";
        case ScreenId::StellarManipulation: return "Stellar Manipulation";
        case ScreenId::Rename: return "Change Name";
        case ScreenId::AbandonPlanet: return "Abandon Planet";
        case ScreenId::Communicate: return "Communicate";
        case ScreenId::Intelligence: return "Intelligence";
        case ScreenId::TreatyGrid: return "Treaty Grid";
        case ScreenId::Scores: return "Scores";
        case ScreenId::Comparisons: return "Comparisons";
        case ScreenId::History: return "History";
        case ScreenId::RaceReport: return "Race Report";
        case ScreenId::VictoryConditions: return "Victory Conditions";
        case ScreenId::CombatReplay: return "Combat Replay";
        case ScreenId::TacticalCombat: return "Tactical Combat";
        case ScreenId::TacticalOrders: return "Tactical Combat Orders";
        case ScreenId::TacticalOptions: return "Tactical Combat Options";
        case ScreenId::CombatSimulator: return "Combat Simulator";
        case ScreenId::StrategicCombat: return "Strategic Combat";
        case ScreenId::GroundCombat: return "Ground Combat";
        case ScreenId::SaveGame: return "Save Game";
        case ScreenId::LoadGame: return "Load Game";
        case ScreenId::Options: return "Options";
        case ScreenId::Settings: return "Settings";
        case ScreenId::Count: break;
    }
    return "";
}

std::optional<ScreenId> screenFromName(std::string_view name) {
    auto squash = [](std::string_view in) {
        std::string out;
        for (char c : in)
            if (std::isalnum(static_cast<unsigned char>(c))) out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return out;
    };
    const std::string want = squash(name);
    static constexpr std::array<std::pair<ScreenId, const char*>, 11> kAliases{{{ScreenId::Ships, "ShipsUnits"},
                                                                                 {ScreenId::Ships, "Ships"},
                                                                                {ScreenId::Queues, "Queues"},
                                                                                {ScreenId::SetQueue, "SetQueue"},
                                                                                {ScreenId::Queues, "ConstructionQueues"},
                                                                                {ScreenId::EmpireStatus, "Status"},
                                                                                {ScreenId::CombatReplay, "Replay"},
                                                                                {ScreenId::TacticalCombat, "Tactical"},
                                                                                {ScreenId::CombatSimulator, "Simulator"},
                                                                                {ScreenId::StrategicCombat, "Strategic"},
                                                                                {ScreenId::GroundCombat, "Ground"}}};
    for (const auto& [id, alias] : kAliases)
        if (squash(alias) == want) return id;
    for (int i = 0; i < static_cast<int>(ScreenId::Count); ++i) {
        const auto id = static_cast<ScreenId>(i);
        if (squash(screenTitle(id)) == want) return id;
    }
    return std::nullopt;
}

std::unique_ptr<Screen> makeScreen(ScreenId id, const ScreenArgs& args) {
    switch (id) {
        case ScreenId::GameMenu: return makeGameMenu(args);
        case ScreenId::Designs: return makeDesigns(args);
        case ScreenId::CreateDesign: return makeCreateDesign(args);
        case ScreenId::Planets: return makePlanets(args);
        case ScreenId::Colonies: return makeColonies(args);
        case ScreenId::Ships: return makeShips(args);
        case ScreenId::Queues: return makeQueues(args);
        case ScreenId::SetQueue: return makeSetQueue(args);
        case ScreenId::Research: return makeResearch(args);
        case ScreenId::TechTree: return makeTechTree(args);
        case ScreenId::Empires: return makeEmpires(args);
        case ScreenId::Log: return makeLog(args);
        case ScreenId::EmpireStatus: return makeEmpireStatus(args);
        case ScreenId::Help: return makeHelp(args);
        case ScreenId::GalaxyMap: return makeGalaxyMap(args);
        case ScreenId::EmpireOptions: return makeEmpireOptions(args);
        case ScreenId::Ministers: return makeMinisters(args);
        case ScreenId::SystemsToAvoid: return makeSystemsToAvoid(args);
        case ScreenId::Waypoints: return makeWaypoints(args);
        case ScreenId::Strategies: return makeStrategies(args);
        case ScreenId::RepairPriorities: return makeRepairPriorities(args);
        case ScreenId::FleetTransfer: return makeFleetTransfer(args);
        case ScreenId::CargoTransfer: return makeCargoTransfer(args);
        case ScreenId::LaunchRecover: return makeLaunchRecover(args);
        case ScreenId::Scrap: return makeScrap(args);
        case ScreenId::ViewOrders: return makeViewOrders(args);
        case ScreenId::SelectWaypoint: return makeSelectWaypoint(args);
        case ScreenId::StellarManipulation: return makeStellarManipulation(args);
        case ScreenId::Rename: return makeRename(args);
        case ScreenId::AbandonPlanet: return makeAbandonPlanet(args);
        case ScreenId::Communicate: return makeCommunicate(args);
        case ScreenId::Intelligence: return makeIntelligence(args);
        case ScreenId::TreatyGrid: return makeTreatyGrid(args);
        case ScreenId::Scores: return makeScores(args);
        case ScreenId::Comparisons: return makeComparisons(args);
        case ScreenId::History: return makeHistory(args);
        case ScreenId::RaceReport: return makeRaceReport(args);
        case ScreenId::VictoryConditions: return makeVictoryConditions(args);
        case ScreenId::CombatReplay: return makeCombatReplay(args);
        case ScreenId::TacticalCombat: return makeTacticalCombat(args);
        case ScreenId::TacticalOrders: return makeTacticalOrders(args);
        case ScreenId::TacticalOptions: return makeTacticalOptions(args);
        case ScreenId::CombatSimulator: return makeCombatSimulator(args);
        case ScreenId::StrategicCombat: return makeStrategicCombat(args);
        case ScreenId::GroundCombat: return makeGroundCombat(args);
        case ScreenId::SaveGame: return makeSaveGame(args);
        case ScreenId::LoadGame: return makeLoadGame(args);
        case ScreenId::Options: return makeOptions(args);
        case ScreenId::Settings: return makeSettings(args);
        case ScreenId::Count: break;
    }
    return nullptr;
}

} // namespace opense4::client::classic
