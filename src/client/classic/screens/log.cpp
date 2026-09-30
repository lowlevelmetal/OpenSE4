#include "client/classic/screens/screens.hpp"

namespace opense4::client::classic {

std::unique_ptr<Screen> makeLog(const ScreenArgs&) { return makePlaceholder(ScreenId::Log); }

} // namespace opense4::client::classic
