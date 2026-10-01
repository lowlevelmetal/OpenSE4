#pragma once

// The classic game's two fixed screen layouts, 1024x768 and 800x600 (docs/spec/06
// §2.1, §2.1.1, confirmed: binary): where the main window's regions, frame strips,
// order strip, selectors and texts go in each, which art folders each reads, and
// how the layout is chosen. Headless, tested in tests/test_client_layout.cpp.

#include "core/math.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace opense4::client::classic {

enum class ScreenLayout : uint8_t {
    Large,  // 1024x768
    Small,  // 800x600
};

// The original's choice: the desktop width alone; 800 px or less gives 800x600.
ScreenLayout layoutForDesktop(int desktopWidth);
// "800x600", "1024x768", "auto" (any case) as a command line or settings value;
// nullopt for "auto", and for anything else `ok` is set false.
std::optional<ScreenLayout> parseLayoutName(std::string_view name, bool* ok = nullptr);
const char* layoutName(ScreenLayout l);

// One frame strip, drawn 1:1 with black transparent at its top-left corner.
struct FramePiece {
    const char* file;  // in the layout's Screens folder
    Vec2 at;
};

// The tactical window's title strip (Tactical Combat and Combat Replay), x in the window.
struct TitleStrip {
    float title, location, locationValue, turn, turnValue, empires, flags;
};

struct LayoutGeometry {
    ScreenLayout layout = ScreenLayout::Large;
    Vec2 frame;                 // 1024x768 or 800x600
    const char* screens;        // frame strips and the intro picture
    const char* systems;        // system panel backgrounds (490x490 or 660x660)

    // Regions (§2.1), frame pixels, as the spec gives them.
    Rect statusBar, commandPanel, systemPanel, reportPanel, galaxyPanel;
    // Frame strips in drawing order; RightFiller only where it is drawn (1024x768).
    std::array<FramePiece, 9> pieces;
    int pieceCount = 0;

    // The system panel's 13 x 13 sectors (§2.4): cell size, margin inside the
    // panel, and where a 36x36 sprite sits in its cell.
    float cell = 50, margin = 1, spriteOffset = 7;
    // Black is transparent for unmasked system types too while the grid is shown.
    bool keyWithGrid = false;

    // The order strip (§2.3): from (230,36), a 16 px pager half at each end;
    // columns per page and pages; the pager arrows' x (y 44).
    Vec2 orderStrip{230, 36};
    int orderColumns = 20, orderPages = 1;
    float pagerLeft = 231, pagerRight = 927;
    Vec2 selectors;             // the three previous | icon | next selectors (48 x 24 each)

    // Status bar (§2.2), x from the frame's left edge.
    float gameDateX = 421;
    std::array<float, 3> resourceIconX{};

    // The galaxy panel's map area, relative to the panel, and its grid cell.
    Rect galaxyMap;
    float galaxyCell = 5;

    // The hover hint (§2.3) and the coordinate line (§2.4), frame pixels.
    Rect hint;
    Vec2 coordinateLine;

    TitleStrip tacticalTitle;
};

const LayoutGeometry& geometryFor(ScreenLayout l);

// The order strip's places: 40 places filled column by column, top then
// bottom (place 0 is column 0 top, place 1 column 0 bottom, ...). Which page a
// place is on, and its column on that page.
struct OrderPlace {
    int page = 0;
    int column = 0;
    int row = 0;
};
OrderPlace orderPlace(const LayoutGeometry& g, int place);
// The page the pager arrows lead to: they wrap at 800x600 and do nothing at
// 1024x768, where every order is on one page.
int turnOrderPage(const LayoutGeometry& g, int page, int direction);

// The four list windows (Planets, Colonies, Ships\Units, Construction
// Queues): 780 wide, 475 tall at 800x600 like every large dialog, the
// desktop's height less 100 at 1024x768 (§2.1.1).
float listWindowHeight(ScreenLayout l, float frameHeight);

// The layout in use, for the whole client (the mode sets it every frame).
ScreenLayout screenLayout();
void setScreenLayout(ScreenLayout l);
inline const LayoutGeometry& layoutGeometry() { return geometryFor(screenLayout()); }
// The frame's size in the layout in use: 1024x768 or 800x600.
inline float frameW() { return layoutGeometry().frame.x; }
inline float frameH() { return layoutGeometry().frame.y; }

} // namespace opense4::client::classic
