#include "client/classic/screens/screens.hpp"

namespace opense4::client::classic {

std::unique_ptr<Screen> makeQueues(const ScreenArgs&) { return makePlaceholder(ScreenId::Queues); }
std::unique_ptr<Screen> makeSetQueue(const ScreenArgs&) { return makePlaceholder(ScreenId::SetQueue); }

} // namespace opense4::client::classic
