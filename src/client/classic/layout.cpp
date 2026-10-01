#include "client/classic/layout.hpp"

#include <cctype>
#include <string>

namespace opense4::client::classic {

namespace {

LayoutGeometry large() {
    LayoutGeometry g;
    g.layout = ScreenLayout::Large;
    g.frame = {1024, 768};
    g.screens = "Pictures/Game/Screens/1024X768/";
    g.systems = "Pictures/Systems/1024X768/";
    g.statusBar = Rect::fromPosSize({11, 5}, {1002, 24});
    g.commandPanel = Rect::fromPosSize({11, 34}, {1002, 72});
    g.systemPanel = Rect::fromPosSize({8, 113}, {652, 652});
    g.reportPanel = Rect::fromPosSize({667, 109}, {290, 361});
    g.galaxyPanel = Rect::fromPosSize({671, 479}, {342, 284});
    g.pieces = {{{"Top.bmp", {0, 0}},
                 {"Toptitle.bmp", {0, 29}},
                 {"Topsys.bmp", {0, 106}},
                 {"Bottom.bmp", {0, 760}},
                 {"Left.bmp", {0, 0}},
                 {"Middle.bmp", {655, 106}},
                 {"Right.bmp", {957, 0}},
                 {"RightFiller.bmp", {958, 107}},
                 {"Topgal.bmp", {662, 470}}}};
    g.pieceCount = 9;
    g.cell = 50;
    g.margin = 1;
    g.spriteOffset = 7;
    g.keyWithGrid = false;
    g.orderColumns = 20;
    g.orderPages = 1;
    g.pagerLeft = 231;
    g.pagerRight = 927;
    g.selectors = {965, 34};
    g.gameDateX = 11 + 410;
    g.resourceIconX = {11 + 650, 11 + 720, 11 + 790};
    g.galaxyMap = Rect::fromPosSize({0, 20}, {342, 235});
    g.galaxyCell = 5;
    g.hint = Rect{{184, 123}, {484, 153}};
    g.coordinateLine = {18, 745};
    g.tacticalTitle = {17, 200, 260, 410, 450, 500, 555};
    return g;
}

LayoutGeometry small() {
    LayoutGeometry g;
    g.layout = ScreenLayout::Small;
    g.frame = {800, 600};
    g.screens = "Pictures/Game/Screens/800X600/";
    g.systems = "Pictures/Systems/800X600/";
    g.statusBar = Rect::fromPosSize({11, 5}, {778, 24});
    g.commandPanel = Rect::fromPosSize({11, 34}, {485, 72});
    g.systemPanel = Rect::fromPosSize({8, 113}, {484, 484});
    g.reportPanel = Rect::fromPosSize({499, 34}, {290, 361});
    g.galaxyPanel = Rect::fromPosSize({503, 404}, {285, 190});
    // The 800x600 RightFiller is a 1x1 file the game skips.
    g.pieces = {{{"Top.bmp", {0, 0}},
                 {"Toptitle.bmp", {0, 29}},
                 {"Topsys.bmp", {0, 106}},
                 {"Bottom.bmp", {0, 594}},
                 {"Left.bmp", {0, 0}},
                 {"Middle.bmp", {490, 29}},
                 {"Right.bmp", {788, 0}},
                 {"Topgal.bmp", {494, 395}},
                 {"", {0, 0}}}};
    g.pieceCount = 8;
    g.cell = 36;
    g.margin = 8;
    g.spriteOffset = 0;
    g.keyWithGrid = true;
    g.orderColumns = 5;
    g.orderPages = 4;
    g.pagerLeft = 231;
    g.pagerRight = 417;
    g.selectors = {448, 34};
    g.gameDateX = 11 + 390;
    g.resourceIconX = {11 + 570, 11 + 640, 11 + 710};
    g.galaxyMap = Rect::fromPosSize({7, 0}, {272, 190});
    g.galaxyCell = 4;
    g.hint = Rect{{100, 123}, {400, 153}};
    g.coordinateLine = {18, 577};
    g.tacticalTitle = {17, 180, 240, 370, 410, 430, 485};
    return g;
}

} // namespace

ScreenLayout layoutForDesktop(int desktopWidth) { return desktopWidth <= 800 ? ScreenLayout::Small : ScreenLayout::Large; }

std::optional<ScreenLayout> parseLayoutName(std::string_view name, bool* ok) {
    std::string lower;
    for (const char c : name) lower += char(std::tolower(static_cast<unsigned char>(c)));
    if (ok) *ok = true;
    if (lower == "800x600" || lower == "small") return ScreenLayout::Small;
    if (lower == "1024x768" || lower == "large") return ScreenLayout::Large;
    if (ok && lower != "auto" && !lower.empty()) *ok = false;
    return std::nullopt;
}

const char* layoutName(ScreenLayout l) { return l == ScreenLayout::Small ? "800x600" : "1024x768"; }

const LayoutGeometry& geometryFor(ScreenLayout l) {
    static const LayoutGeometry kLarge = large(), kSmall = small();
    return l == ScreenLayout::Small ? kSmall : kLarge;
}

OrderPlace orderPlace(const LayoutGeometry& g, int place) {
    const int column = place / 2;
    return {column / g.orderColumns, column % g.orderColumns, place % 2};
}

int turnOrderPage(const LayoutGeometry& g, int page, int direction) {
    if (g.orderPages <= 1) return 0;
    return ((page + direction) % g.orderPages + g.orderPages) % g.orderPages;
}

namespace {
ScreenLayout gLayout = ScreenLayout::Large;
}

ScreenLayout screenLayout() { return gLayout; }
void setScreenLayout(ScreenLayout l) { gLayout = l; }

float listWindowHeight(ScreenLayout l, float frameHeight) { return l == ScreenLayout::Small ? 475.0f : frameHeight - 100.0f; }

} // namespace opense4::client::classic
