#include "client/classic/screens/screens.hpp"

namespace opense4::client::classic {

std::unique_ptr<Screen> makeShips(const ScreenArgs&) { return makePlaceholder(ScreenId::Ships); }
std::unique_ptr<Screen> makeFleetTransfer(const ScreenArgs&) { return makePlaceholder(ScreenId::FleetTransfer); }
std::unique_ptr<Screen> makeCargoTransfer(const ScreenArgs&) { return makePlaceholder(ScreenId::CargoTransfer); }
std::unique_ptr<Screen> makeLaunchRecover(const ScreenArgs&) { return makePlaceholder(ScreenId::LaunchRecover); }
std::unique_ptr<Screen> makeScrap(const ScreenArgs&) { return makePlaceholder(ScreenId::Scrap); }
std::unique_ptr<Screen> makeViewOrders(const ScreenArgs&) { return makePlaceholder(ScreenId::ViewOrders); }
std::unique_ptr<Screen> makeSelectWaypoint(const ScreenArgs&) { return makePlaceholder(ScreenId::SelectWaypoint); }
std::unique_ptr<Screen> makeStellarManipulation(const ScreenArgs&) { return makePlaceholder(ScreenId::StellarManipulation); }
std::unique_ptr<Screen> makeRename(const ScreenArgs&) { return makePlaceholder(ScreenId::Rename); }

} // namespace opense4::client::classic
