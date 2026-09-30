#include "client/classic/screens/screens.hpp"

namespace opense4::client::classic {

std::unique_ptr<Screen> makeHelp(const ScreenArgs&) { return makePlaceholder(ScreenId::Help); }

} // namespace opense4::client::classic
