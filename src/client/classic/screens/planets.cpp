#include "client/classic/screens/screens.hpp"

namespace opense4::client::classic {

std::unique_ptr<Screen> makePlanets(const ScreenArgs&) { return makePlaceholder(ScreenId::Planets); }
std::unique_ptr<Screen> makeColonies(const ScreenArgs&) { return makePlaceholder(ScreenId::Colonies); }

} // namespace opense4::client::classic
