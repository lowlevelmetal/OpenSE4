#include "client/classic/screens/screens.hpp"

namespace opense4::client::classic {

std::unique_ptr<Screen> makeGalaxyMap(const ScreenArgs&) { return makePlaceholder(ScreenId::GalaxyMap); }

} // namespace opense4::client::classic
