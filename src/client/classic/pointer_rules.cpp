#include "client/classic/pointer_rules.hpp"

namespace opense4::client::classic {

std::string_view pointerFile(Pointer p) {
    static constexpr std::array<std::string_view, kPointerCount> kFiles{
        "normal.cur", "hourglass.cur", "select.cur", "target.cur", "arrown.cur", "arrowne.cur",
        "arrowe.cur", "arrowse.cur",   "arrows.cur", "arrowsw.cur", "arroww.cur", "arrownw.cur",
    };
    return kFiles[static_cast<size_t>(p)];
}

Pointer arrowPointer(int dx, int dy) {
    const int sx = (dx > 0) - (dx < 0), sy = (dy > 0) - (dy < 0);
    if (sx > 0) return sy < 0 ? Pointer::ArrowNE : sy > 0 ? Pointer::ArrowSE : Pointer::ArrowE;
    if (sx < 0) return sy < 0 ? Pointer::ArrowNW : sy > 0 ? Pointer::ArrowSW : Pointer::ArrowW;
    return sy < 0 ? Pointer::ArrowN : sy > 0 ? Pointer::ArrowS : Pointer::Normal;
}

Pointer tacticalPointer(const TacticalPointerFacts& f) {
    // Before Begin, outside the map and while the window is busy (a battle
    // turn playing): Normal. The program-wide Hourglass is not the window's.
    if (!f.begun || !f.overMap || f.busy) return Pointer::Normal;
    if (f.aiming) return Pointer::Select;   // anywhere over the map
    if (!f.selected) return Pointer::Normal;
    if (f.overOtherEmpire) return Pointer::Target;
    if (f.selectedIsDrone) return Pointer::Normal;
    return arrowPointer(f.dx, f.dy);
}

} // namespace opense4::client::classic
