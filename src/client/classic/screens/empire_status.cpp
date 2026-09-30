#include "client/classic/screens/screens.hpp"

namespace opense4::client::classic {

std::unique_ptr<Screen> makeEmpireStatus(const ScreenArgs&) { return makePlaceholder(ScreenId::EmpireStatus); }
std::unique_ptr<Screen> makeEmpireOptions(const ScreenArgs&) { return makePlaceholder(ScreenId::EmpireOptions); }
std::unique_ptr<Screen> makeMinisters(const ScreenArgs&) { return makePlaceholder(ScreenId::Ministers); }
std::unique_ptr<Screen> makeSystemsToAvoid(const ScreenArgs&) { return makePlaceholder(ScreenId::SystemsToAvoid); }
std::unique_ptr<Screen> makeWaypoints(const ScreenArgs&) { return makePlaceholder(ScreenId::Waypoints); }
std::unique_ptr<Screen> makeStrategies(const ScreenArgs&) { return makePlaceholder(ScreenId::Strategies); }
std::unique_ptr<Screen> makeRepairPriorities(const ScreenArgs&) { return makePlaceholder(ScreenId::RepairPriorities); }

} // namespace opense4::client::classic
