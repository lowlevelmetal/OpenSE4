#include "client/classic/screens/screens.hpp"

namespace opense4::client::classic {

std::unique_ptr<Screen> makeDesigns(const ScreenArgs&) { return makePlaceholder(ScreenId::Designs); }
std::unique_ptr<Screen> makeCreateDesign(const ScreenArgs&) { return makePlaceholder(ScreenId::CreateDesign); }

} // namespace opense4::client::classic
