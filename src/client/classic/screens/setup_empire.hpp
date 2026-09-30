#pragma once

// Empire Setup (docs/spec/06 §1.1, spec 02 §9): six pages editing one empire
// draft, opened from Game Setup -> Players -> Add New / Edit.

#include "client/classic/frontend.hpp"
#include "client/classic/screens/setup_model.hpp"

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

private:
    const game::Rules& rules() const { return *rules_; }
    const ruleset::RacePreset* preset() const;
    void choosePreset(size_t position);
    void resetToTier(int tier);
    void pointsLine(MenuContext& ctx);

    void pageGeneral(MenuContext& ctx);
    void pageEnvironment(MenuContext& ctx);
    void pageCulture(MenuContext& ctx);
    void pageCharacteristics(MenuContext& ctx);
    void pageTraits(MenuContext& ctx);
    void pageDescription(MenuContext& ctx);

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
    std::vector<std::string> surfaces_, atmospheres_, designFiles_;
};

} // namespace opense4::client::classic::setup
