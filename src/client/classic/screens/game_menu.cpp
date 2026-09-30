#include "client/classic/screens/screens.hpp"

namespace opense4::client::classic {

std::unique_ptr<Screen> makeGameMenu(const ScreenArgs&) { return makePlaceholder(ScreenId::GameMenu); }
std::unique_ptr<Screen> makeSaveGame(const ScreenArgs&) { return makePlaceholder(ScreenId::SaveGame); }
std::unique_ptr<Screen> makeLoadGame(const ScreenArgs&) { return makePlaceholder(ScreenId::LoadGame); }

} // namespace opense4::client::classic
