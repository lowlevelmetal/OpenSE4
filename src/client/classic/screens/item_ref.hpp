#pragma once

// A catalogue item (component, facility, vehicle size, tech area,
// intelligence project, treaty, formation) and the small item report popup
// that shows one (docs/spec/06 §1.4; item_reports.hpp draws it). Kept apart
// from item_reports.hpp so that the object reports can hand a right-clicked
// facility or component to their owner without the list widgets.

#include "client/classic/ui.hpp"

#include <cstdint>

namespace opense4::client::classic {

struct ItemRef {
    enum class Kind : uint8_t { None, Component, Facility, Hull, TechArea, IntelProject, Treaty, Formation };
    Kind kind = Kind::None;
    uint32_t index = 0;
    int32_t mount = -1;  // components: weapon mount (CompEnhancement index)

    bool valid() const { return kind != Kind::None; }
    bool operator==(const ItemRef&) const = default;
};

// The item report popup. Call open() from anywhere in the owning window and
// draw() once per frame after the window (outside its Dialog). It closes on
// a click on it, a click elsewhere, or Esc.
class ItemReportPopup {
public:
    void open(ItemRef item) {
        item_ = item;
        request_ = true;
    }
    void draw(UiContext& ui);

private:
    ItemRef item_;
    bool request_ = false;
};

} // namespace opense4::client::classic
