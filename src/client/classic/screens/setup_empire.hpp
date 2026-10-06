#pragma once

// Empire Setup (docs/spec/06 §1.1, spec 02 §9, spec 07 session 5): six pages
// editing one empire draft in the original's layout, opened from Game Setup
// -> Players -> Add New (an empty empire) or Edit.

#include "client/classic/frontend.hpp"
#include "client/classic/screens/setup_model.hpp"
#include "client/classic/screens/setup_players.hpp"
#include "client/classic/screens/setup_widgets.hpp"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace opense4::client::classic::setup {

enum class EmpirePage { General, Environment, Culture, Characteristics, Traits, Description, Count };
const char* pageTitle(EmpirePage p);
std::optional<EmpirePage> empirePageFromName(std::string_view name);

class EmpireEditor {
public:
    enum class Result { Editing, Created, Cancelled };

    // `isNew`: Add New (Create Empire) rather than editing a listed empire.
    EmpireEditor(std::shared_ptr<const game::Rules> rules, EmpireDraft draft, int racialPoints, bool isNew);

    Result draw(MenuContext& ctx);
    // The finished empire after Result::Created.
    const game::EmpireSetup& result() const { return result_; }
    void setPage(EmpirePage p) { page_ = p; }
    // The name of the player the game gives computer empires without one of
    // their own (Game Setup's Computer Players), for the Computer Player row.
    void setGameChoice(std::string name) { gameChoice_ = std::move(name); }

private:
    // What the open list picker chooses.
    enum class Pick { None, EmpireName, EmpireType, EmperorTitle, EmperorName, DesignNames, MinisterStyle };

    const game::Rules& rules() const { return *rules_; }
    const ruleset::RacePreset* preset() const;
    void chooseStyle(size_t position);
    void resetToTier(int tier);
    void pointsLine(SetupArea& a, float y);
    void openPicker(Pick what);
    void pickerResult(int row);

    void pageGeneral(SetupArea& a);
    void pageEnvironment(SetupArea& a);
    void pageCulture(SetupArea& a);
    void pageCharacteristics(SetupArea& a);
    void pageTraits(SetupArea& a);
    void pageDescription(SetupArea& a);
    void compareCulturesPopup(SetupArea& a);

    std::shared_ptr<const game::Rules> rules_;
    EmpireDraft draft_;
    int racialPoints_ = 0;
    bool isNew_ = true;
    EmpirePage page_ = EmpirePage::General;
    std::vector<size_t> presets_;  // playable presets (indices into Rules::racePresets)
    size_t presetPos_ = 0;
    std::string password_;
    std::string error_;
    int hoveredTrait_ = -1;
    game::EmpireSetup result_;
    std::vector<std::string> surfaces_, atmospheres_, designFiles_, ministerStyles_;
    ListPicker picker_;
    Pick picking_ = Pick::None;
    std::vector<std::string> pickRows_;
    // OpenSE4's own: a computer empire's own player, when the game's mods offer some.
    bool offersPlayers_ = false;
    std::string gameChoice_ = "Classic AI";
    PlayerPicker players_;
};

} // namespace opense4::client::classic::setup
