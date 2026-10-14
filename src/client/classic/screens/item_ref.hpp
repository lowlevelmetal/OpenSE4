#pragma once

// A catalogue item (component, facility, vehicle size, tech area,
// intelligence project, treaty, formation) and the small item report popup
// that shows one (docs/spec/06 §1.4; item_reports.hpp draws it). Kept apart
// from item_reports.hpp so that the object reports can hand a right-clicked
// facility or component to their owner without the list widgets.

#include "client/classic/screens/race_report.hpp"
#include "client/classic/ui.hpp"

#include <cstdint>

namespace opense4::client::classic {

struct ItemRef {
    // Race: the Race Report of the empire `index` (an owner's flag right-clicked, spec 06 §1.4).
    enum class Kind : uint8_t { None, Component, Facility, Hull, TechArea, IntelProject, Treaty, Formation, Race };
    Kind kind = Kind::None;
    uint32_t index = 0;
    int32_t mount = -1;  // components: weapon mount (CompEnhancement index)

    bool valid() const { return kind != Kind::None; }
    bool operator==(const ItemRef&) const = default;
};

// The item report popup. Call open() from anywhere in the owning window and
// draw() once per frame after the window (outside its Dialog). It closes on
// a click on it, a click elsewhere, or Esc. An empire (Kind::Race) opens its
// Race Report as a modal popup instead (RaceReportPopup), which a report
// popup needs; a window that is no popup opens ScreenId::RaceReport itself.
class ItemReportPopup {
public:
    void open(ItemRef item) {
        if (item.kind == ItemRef::Kind::Race) {
            race_.open(game::EmpireId{item.index});
            return;
        }
        item_ = item;
        request_ = true;
    }
    void draw(UiContext& ui);
    // Its report or the Race Report is open (asked where draw() is called).
    static bool isOpen();

private:
    ItemRef item_;
    bool request_ = false;
    RaceReportPopup race_;
};

} // namespace opense4::client::classic
