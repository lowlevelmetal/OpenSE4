#include "command_samples.hpp"

#include "engine_fixture.hpp"

#include <string>

namespace opense4::test {

using namespace opense4::game;

Design warbirdDesign(const Rules& r) {
    Design d;
    d.name = "Warbird";
    d.designType = "Attack Ship";
    d.hull = hullIndex(r, "Test Frigate");
    for (auto c : {"Test Bridge", "Test Life Support", "Test Crew Quarters", "Test Engine", "Test Laser", "Test Armor Plate"})
        d.entries.push_back({componentIndex(r, c), -1});
    return d;
}

std::vector<VehicleId> vehiclesOf(const GameState& s, EmpireId e) {
    std::vector<VehicleId> out;
    for (const Vehicle& v : s.vehicles)
        if (v.owner == e) out.push_back(v.id);
    return out;
}

// Orders touching queues, fleets, designs, research, messages and lists.
EmpireOrders busyOrders(const Rules& r, const GameState& s, EmpireId me, int round) {
    EmpireOrders o{me, s.turn, {}};
    const EmpireId other{static_cast<uint32_t>((me.index() + 1) % s.empires.size())};
    const Empire& e = s.empire(me);
    ObjectId homePlanet;
    for (const auto& c : s.colonies)
        if (c && c->owner == me && c->homeworld) homePlanet = c->planet;
    const cmd::QueueTarget home{homePlanet, {}};
    const std::vector<VehicleId> ships = vehiclesOf(s, me);

    if (round == 0) {
        Design d = warbirdDesign(r);
        d.name += std::to_string(me.index());
        o.commands.push_back(cmd::CreateDesign{d});
        if (ships.size() >= 2) o.commands.push_back(cmd::CreateFleet{"Home Guard", {ships[0], ships[1]}});
        o.commands.push_back(cmd::SetStrategy{-1, {"Careful", {{"Break Off At", "50"}, {"Target", "Nearest"}}}, false});
        o.commands.push_back(cmd::SetRepairPriorities{{"Ships", "Bases"}});
        o.commands.push_back(cmd::SetDesignTypes{{"Attack Ship", "Scout", "Colony Ship"}});
    }
    if (!e.designs.empty()) {
        QueueItem item;
        item.design = e.designs.front();
        o.commands.push_back(cmd::QueueAdd{home, item, -1});
    }
    QueueItem lab;
    lab.kind = QueueItem::Kind::Facility;
    lab.facility = facilityIndex(r, "Test Lab");
    o.commands.push_back(cmd::QueueAdd{home, lab, 0});
    o.commands.push_back(cmd::QueueFlags{home, false, round % 2 == 1, false, -1});
    o.commands.push_back(cmd::SetResearch{{{techArea(r, "Test Beams"), 0}, {techArea(r, "Test Construction"), 0}}, round % 2 == 0, false});
    o.commands.push_back(cmd::SetIntel{{IntelProjectOrder{0, other, {}, {}, {}, {}, 0}}, true, false});

    DiplomaticMessage m;
    m.to = other;
    m.type = MessageType::General;
    m.text = "Greetings from empire " + std::to_string(me.index()) + ", round " + std::to_string(round);
    PackageItem gift;
    gift.resources = Resources{10, 5, 1};
    m.offer.push_back(gift);
    o.commands.push_back(cmd::SendMessage{m});

    Waypoint wp{"Rally " + std::to_string(round), {SystemId{0u}, Sector{2, 3}}, true};
    o.commands.push_back(cmd::SetWaypoint{round % 10, wp});
    o.commands.push_back(cmd::SetSystemNote{SystemId{0u}, "note " + std::to_string(round)});
    o.commands.push_back(cmd::SetSystemFlags{SystemId{1u}, true, round % 2 == 0});
    o.commands.push_back(cmd::TagMinefield{{SystemId{1u}, Sector{4, 4}}, true});

    if (ships.size() >= 3) {
        Order move;
        move.kind = OrderKind::MoveTo;
        move.location = {SystemId{static_cast<uint32_t>((round + 1) % s.galaxy.systems.size())}, Sector{6, 6}};
        o.commands.push_back(cmd::SetOrders{ships[2], {}, {move}, false});
        o.commands.push_back(cmd::Rename{ships[2], {}, {}, {}, "Wanderer " + std::to_string(round)});
    }
    return o;
}

game::EmpireOrders everyCommandSample(const Rules& r) {
    Order order{OrderKind::LoadCargo, {SystemId{3u}, Sector{1, 12}}, ObjectId{4u}, VehicleId{5u}, DesignId{6u}, -1};
    const cmd::QueueTarget yard{ObjectId{9u}, VehicleId{10u}};
    QueueItem item{QueueItem::Kind::Upgrade, DesignId{2u}, 17, 3, Resources{1, 2, 3}};
    Design design = warbirdDesign(r);
    design.obsolete = true;
    design.lost = 4;
    DiplomaticMessage message;
    message.id = MessageId{8u};
    message.from = EmpireId{0u};
    message.to = EmpireId{1u};
    message.type = MessageType::ProposeTrade;
    message.tone = 2;
    message.text = "Trade?";
    message.treaty = Treaty::TradeAlliance;
    PackageItem tech;
    tech.kind = PackageItem::Kind::Technology;
    tech.tech = ruleset::TechAreaId{3u};
    message.offer = {tech};
    PackageItem planet;
    planet.kind = PackageItem::Kind::Planet;
    planet.planet = ObjectId{12u};
    message.request = {planet};
    message.thirdEmpire = EmpireId{2u};
    message.inReplyTo = MessageId{7u};

    EmpireOrders orders{EmpireId{1u}, 42, {}};
    auto& c = orders.commands;
    c.push_back(cmd::SetOrders{VehicleId{1u}, FleetId{2u}, {order, Order{}}, true});
    c.push_back(cmd::CreateFleet{"Strike Group", {VehicleId{3u}, VehicleId{4u}}});
    c.push_back(cmd::JoinFleet{FleetId{5u}, VehicleId{6u}});
    c.push_back(cmd::LeaveFleet{VehicleId{7u}});
    c.push_back(cmd::DisbandFleet{FleetId{8u}});
    c.push_back(cmd::SetFleetOptions{FleetId{9u}, 2, 3});
    c.push_back(cmd::SetVehicleStrategy{DesignId{4u}, 5});
    c.push_back(cmd::Rename{VehicleId{1u}, FleetId{2u}, DesignId{3u}, ObjectId{4u}, "New Name \xE2\x9C\x93"});
    c.push_back(cmd::Scrap{VehicleId{11u}, ObjectId{12u}, 3});
    c.push_back(cmd::Mothball{VehicleId{13u}, false});
    c.push_back(cmd::SetMinister{VehicleId{14u}, ObjectId{15u}, true, false});
    c.push_back(cmd::QueueAdd{yard, item, 2});
    c.push_back(cmd::QueueRemove{yard, 4});
    c.push_back(cmd::QueueMove{yard, 1, 5});
    c.push_back(cmd::QueueSetCount{yard, 2, 9});
    c.push_back(cmd::QueueFlags{yard, true, true, true, 7});
    c.push_back(cmd::Retrofit{VehicleId{16u}, DesignId{17u}});
    c.push_back(cmd::SetColonyType{ObjectId{18u}, "Research"});
    c.push_back(cmd::AbandonPlanet{ObjectId{19u}});
    c.push_back(cmd::TransferCargo{VehicleId{20u}, ObjectId{21u}, VehicleId{22u}, ObjectId{23u}, DesignId{24u}, EmpireId{2u}, 1234567890123});
    c.push_back(cmd::CreateDesign{design});
    c.push_back(cmd::SetDesignObsolete{DesignId{25u}, false});
    c.push_back(cmd::DeleteDesign{DesignId{26u}});
    c.push_back(cmd::SetResearch{{{ruleset::TechAreaId{1u}, 50}, {ruleset::TechAreaId{2u}, 0}}, false, true});
    c.push_back(cmd::SetIntel{{{2, EmpireId{0u}, ObjectId{3u}, VehicleId{4u}, EmpireId{2u}, ruleset::TechAreaId{5u}, 99}}, false, true});
    c.push_back(cmd::SendMessage{message});
    c.push_back(cmd::AnswerMessage{MessageId{27u}, true, "Agreed."});
    c.push_back(cmd::SetWaypoint{3, Waypoint{"Alpha", {SystemId{1u}, Sector{0, 12}}, true}});
    c.push_back(cmd::SetWaypoint{4, std::nullopt});
    c.push_back(cmd::SetSystemFlags{SystemId{28u}, std::nullopt, false});
    c.push_back(cmd::SetSystemNote{SystemId{29u}, "Watch this one"});
    c.push_back(cmd::TagMinefield{{SystemId{30u}, Sector{7, 8}}, false});
    c.push_back(cmd::SetStrategy{2, {"Aggressive", {{"Break Off At", "10"}}}, true});
    c.push_back(cmd::SetRepairPriorities{{"Bases", "Ships", "Units"}});
    c.push_back(cmd::SetDesignTypes{{"Carrier"}});
    c.push_back(cmd::SetColonyTypes{{"Mining", "Farming"}});
    c.push_back(cmd::SetEmpireOptions{true, std::string("verifier")});
    c.push_back(cmd::SetEmpireOptions{std::nullopt, std::nullopt});
    c.push_back(cmd::SetMinisters{kAllMinisters, std::string("Aggressive"), true, false, std::nullopt, true});
    c.push_back(cmd::SetEncounterOptions{EncounterClear::Any});
    c.push_back(cmd::EnterSector{VehicleId{31u}, FleetId{}, {SystemId{32u}, Sector{3, 4}}, false});
    c.push_back(cmd::EditDesign{DesignId{3u}, design});
    c.push_back(cmd::OpenVehicleReport{VehicleId{33u}});
    c.push_back(cmd::QueueReplaceFacility{yard, 3, 34});
    c.push_back(cmd::DecideWar{EmpireId{2u}});
    c.push_back(cmd::SetInterfaceOptions{InterfaceOptions{.confirmEndTurn = false, .facilityMarkers = 0x0a5, .logFilter = 3}});
    c.push_back(cmd::CarryOutDemand{MessageId{35u}});
    c.push_back(cmd::UseDemandEntry{cmd::DemandList::Peace, EmpireId{2u}});
    c.push_back(cmd::JettisonCargo{VehicleId{36u}, ObjectId{}, {{EmpireId{1u}, 12}}, {{DesignId{37u}, 3}}});
    c.push_back(cmd::CloakColony{ObjectId{38u}, false});
    c.push_back(cmd::Analyze{VehicleId{39u}});
    c.push_back(cmd::SelfDestruct{VehicleId{40u}});
    c.push_back(cmd::FireOn{VehicleId{41u}});
    c.push_back(cmd::SetEmail{"someone@example.org"});
    c.push_back(cmd::EnterSector{{}, {}, {SystemId{32u}, Sector{5, 6}}, true, {VehicleId{42u}, VehicleId{43u}}});
    c.push_back(cmd::OrderTagged{{VehicleId{44u}, VehicleId{45u}}, {Order{OrderKind::MoveTo, {SystemId{1u}, Sector{2, 3}}}}, true});
    c.push_back(cmd::SetFleetLeader{FleetId{46u}, VehicleId{47u}});

    return orders;
}

} // namespace opense4::test
