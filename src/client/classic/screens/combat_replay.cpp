#include "client/classic/screens/screens.hpp"

namespace opense4::client::classic {

std::unique_ptr<Screen> makeCombatReplay(const ScreenArgs&) { return makePlaceholder(ScreenId::CombatReplay); }

} // namespace opense4::client::classic
