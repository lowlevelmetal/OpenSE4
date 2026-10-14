#pragma once

// The Race Report (docs/spec/06 §1.4, §1.10.1, §7 Q87, Q120): a report
// window of its own, borderless, 310x420; the pages 290x327 at (10,10), the
// four 72x30 tabs Detail, Descr, Race and Tech at (10,340), a 153x30 Close
// button centred under them at (79,380). It opens from Empires (a portrait's
// right-click, Our Race), Communicate, a population's right-click and the
// owner's flag on an object report's Detail page, modal over the window that
// holds what was clicked; Esc closes it too (inferred, as the Combat Piece
// Report).

#include "client/classic/ui.hpp"

#include "game/state.hpp"

namespace opense4::client::classic {

// The report's frame, pages, tabs and Close, drawn into the current window,
// whose 310x420 area starts at `min` (frame pixels).
class RaceReportView {
public:
    static constexpr Vec2 kSize{310, 420};

    // The empire it shows, on Detail.
    void show(game::EmpireId e) {
        empire_ = e;
        tab_ = Tab::Detail;
    }
    // False when Close or Esc closes it (with the close sound).
    bool draw(UiContext& ui, Vec2 min);

private:
    enum class Tab { Detail, Descr, Race, Tech };
    void detail(UiContext& ui, const game::Empire& e) const;
    void descr(UiContext& ui, const game::Empire& e) const;
    void race(UiContext& ui, const game::Empire& e) const;
    void tech(UiContext& ui, const game::Empire& e) const;

    game::EmpireId empire_;
    Tab tab_ = Tab::Detail;
};

// The Race Report over a report popup. Dear ImGui keeps every window but a
// modal popup's own below that popup, so a report shown in a popup (the
// Ships\Units, Planets and Colonies windows', the Fleet Report's members')
// opens the Race Report as a modal popup of its own, drawn inside it: call
// open() from the popup and draw() before its EndPopup(). Elsewhere the Race
// Report window opens (ScreenId::RaceReport).
inline constexpr const char* kRaceReportPopup = "##racereport";
class RaceReportPopup {
public:
    void open(game::EmpireId e) {
        view_.show(e);
        request_ = true;
    }
    void draw(UiContext& ui);

private:
    RaceReportView view_;
    bool request_ = false;
};

} // namespace opense4::client::classic
