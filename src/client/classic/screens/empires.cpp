#include "client/classic/screens/screens.hpp"

namespace opense4::client::classic {

std::unique_ptr<Screen> makeEmpires(const ScreenArgs&) { return makePlaceholder(ScreenId::Empires); }
std::unique_ptr<Screen> makeCommunicate(const ScreenArgs&) { return makePlaceholder(ScreenId::Communicate); }
std::unique_ptr<Screen> makeTreatyGrid(const ScreenArgs&) { return makePlaceholder(ScreenId::TreatyGrid); }
std::unique_ptr<Screen> makeScores(const ScreenArgs&) { return makePlaceholder(ScreenId::Scores); }
std::unique_ptr<Screen> makeComparisons(const ScreenArgs&) { return makePlaceholder(ScreenId::Comparisons); }
std::unique_ptr<Screen> makeHistory(const ScreenArgs&) { return makePlaceholder(ScreenId::History); }
std::unique_ptr<Screen> makeRaceReport(const ScreenArgs&) { return makePlaceholder(ScreenId::RaceReport); }
std::unique_ptr<Screen> makeVictoryConditions(const ScreenArgs&) { return makePlaceholder(ScreenId::VictoryConditions); }

} // namespace opense4::client::classic
