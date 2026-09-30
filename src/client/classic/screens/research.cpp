#include "client/classic/screens/screens.hpp"

namespace opense4::client::classic {

std::unique_ptr<Screen> makeResearch(const ScreenArgs&) { return makePlaceholder(ScreenId::Research); }
std::unique_ptr<Screen> makeTechTree(const ScreenArgs&) { return makePlaceholder(ScreenId::TechTree); }
std::unique_ptr<Screen> makeIntelligence(const ScreenArgs&) { return makePlaceholder(ScreenId::Intelligence); }

} // namespace opense4::client::classic
