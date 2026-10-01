// The classic screen layouts (docs/spec/06 §2.1, §2.1.1) and the pointer
// rules (§5.8, §1.10.1): headless tables and decisions of the client.

#include "client/classic/layout.hpp"
#include "client/classic/pointer_rules.hpp"

#include <doctest/doctest.h>

#include <string>

using namespace opense4;
using namespace opense4::client::classic;

TEST_CASE("layout: the desktop width alone picks it") {
    CHECK(layoutForDesktop(640) == ScreenLayout::Small);
    CHECK(layoutForDesktop(800) == ScreenLayout::Small);
    CHECK(layoutForDesktop(801) == ScreenLayout::Large);
    CHECK(layoutForDesktop(2560) == ScreenLayout::Large);
    bool ok = false;
    CHECK(parseLayoutName("800x600", &ok) == ScreenLayout::Small);
    CHECK(ok);
    CHECK(parseLayoutName("1024X768", &ok) == ScreenLayout::Large);
    CHECK_FALSE(parseLayoutName("auto", &ok));
    CHECK(ok);
    CHECK_FALSE(parseLayoutName("640x480", &ok));
    CHECK_FALSE(ok);
}

TEST_CASE("layout: what differs at 800x600") {
    const LayoutGeometry& s = geometryFor(ScreenLayout::Small);
    const LayoutGeometry& l = geometryFor(ScreenLayout::Large);
    CHECK(s.frame == Vec2{800, 600});
    CHECK(l.frame == Vec2{1024, 768});
    CHECK(std::string(s.screens) == "Pictures/Game/Screens/800X600/");
    CHECK(std::string(l.systems) == "Pictures/Systems/1024X768/");
    // Regions.
    CHECK(s.systemPanel.min == Vec2{8, 113});
    CHECK(s.systemPanel.size() == Vec2{484, 484});
    CHECK(l.systemPanel.size() == Vec2{652, 652});
    CHECK(s.reportPanel.min == Vec2{499, 34});
    CHECK(l.reportPanel.min == Vec2{667, 109});
    CHECK(s.galaxyPanel.min == Vec2{503, 404});
    CHECK(s.galaxyPanel.size() == Vec2{285, 190});
    // The sector grid: 13 cells and the margin fill the panel.
    CHECK(s.margin + 13 * s.cell == 476);
    CHECK(l.margin + 13 * l.cell == 651);
    CHECK(s.spriteOffset + 36 + s.spriteOffset == s.cell);
    CHECK(l.spriteOffset + 36 + l.spriteOffset == l.cell);
    CHECK(s.keyWithGrid);
    CHECK_FALSE(l.keyWithGrid);
    // RightFiller is drawn only at 1024x768.
    auto hasFiller = [](const LayoutGeometry& g) {
        for (int i = 0; i < g.pieceCount; ++i)
            if (std::string(g.pieces[size_t(i)].file) == "RightFiller.bmp") return true;
        return false;
    };
    CHECK(hasFiller(l));
    CHECK_FALSE(hasFiller(s));
    // The order strip: 5 columns on 4 pages, or 20 on one, and its width.
    CHECK(s.orderColumns * s.orderPages == 20);
    CHECK(l.orderColumns * l.orderPages == 20);
    CHECK(float(s.orderColumns) * 34 + 32 == 202);
    CHECK(float(l.orderColumns) * 34 + 32 == 712);
    CHECK(s.pagerRight == s.orderStrip.x + 16 + float(s.orderColumns) * 34 + 1);
    CHECK(l.pagerRight == l.orderStrip.x + 16 + float(l.orderColumns) * 34 + 1);
    CHECK(s.selectors == Vec2{448, 34});
    CHECK(l.selectors == Vec2{965, 34});
    // Status bar, galaxy map, hint, coordinate line, list windows.
    CHECK(s.gameDateX == 401);
    CHECK(l.resourceIconX[0] == 661);
    CHECK(s.galaxyMap.size() == Vec2{272, 190});
    CHECK(s.galaxyCell * 68 <= s.galaxyMap.size().x);
    CHECK(l.galaxyCell * 47 == l.galaxyMap.size().y);
    // The hint is centred on the system panel.
    CHECK(s.hint.center().x == s.systemPanel.center().x);
    CHECK(l.hint.center().x == l.systemPanel.center().x);
    CHECK(s.coordinateLine == Vec2{18, s.systemPanel.max.y - 20});
    CHECK(l.coordinateLine == Vec2{18, l.systemPanel.max.y - 20});
    CHECK(listWindowHeight(ScreenLayout::Small, 600) == 475);
    CHECK(listWindowHeight(ScreenLayout::Large, 768) == 668);
    CHECK(listWindowHeight(ScreenLayout::Small, 600) - 265 == 210);  // 5 full rows of 36 px and a bit
    CHECK(s.tacticalTitle.flags == 485);
    CHECK(l.tacticalTitle.location == 200);
}

TEST_CASE("layout: order places and the pager") {
    const LayoutGeometry& s = geometryFor(ScreenLayout::Small);
    const LayoutGeometry& l = geometryFor(ScreenLayout::Large);
    // Places fill columns top then bottom.
    CHECK(orderPlace(s, 0).page == 0);
    CHECK(orderPlace(s, 1).row == 1);
    CHECK(orderPlace(s, 9).column == 4);
    CHECK(orderPlace(s, 10).page == 1);
    CHECK(orderPlace(s, 10).column == 0);
    CHECK(orderPlace(s, 39).page == 3);
    CHECK(orderPlace(l, 39).page == 0);
    CHECK(orderPlace(l, 39).column == 19);
    // The arrows wrap at 800x600 and do nothing at 1024x768.
    CHECK(turnOrderPage(s, 0, -1) == 3);
    CHECK(turnOrderPage(s, 3, 1) == 0);
    CHECK(turnOrderPage(s, 1, 1) == 2);
    CHECK(turnOrderPage(l, 0, 1) == 0);
}

TEST_CASE("pointers: files and the tactical map's rules") {
    CHECK(pointerFile(Pointer::Normal) == "normal.cur");
    CHECK(pointerFile(Pointer::ArrowNW) == "arrownw.cur");
    // Signs of the column and row offsets (rows grow downwards).
    CHECK(arrowPointer(3, -2) == Pointer::ArrowNE);
    CHECK(arrowPointer(1, 0) == Pointer::ArrowE);
    CHECK(arrowPointer(2, 5) == Pointer::ArrowSE);
    CHECK(arrowPointer(0, 1) == Pointer::ArrowS);
    CHECK(arrowPointer(-1, 1) == Pointer::ArrowSW);
    CHECK(arrowPointer(-4, 0) == Pointer::ArrowW);
    CHECK(arrowPointer(-1, -1) == Pointer::ArrowNW);
    CHECK(arrowPointer(0, -3) == Pointer::ArrowN);
    CHECK(arrowPointer(0, 0) == Pointer::Normal);

    TacticalPointerFacts f;
    f.begun = true;
    f.overMap = true;
    f.selected = true;
    f.dx = 2;
    CHECK(tacticalPointer(f) == Pointer::ArrowE);
    f.overOtherEmpire = true;
    CHECK(tacticalPointer(f) == Pointer::Target);
    f.aiming = true;
    CHECK(tacticalPointer(f) == Pointer::Select);
    f.aiming = false;
    f.overOtherEmpire = false;
    f.selectedIsDrone = true;
    CHECK(tacticalPointer(f) == Pointer::Normal);
    f.selectedIsDrone = false;
    f.begun = false;
    CHECK(tacticalPointer(f) == Pointer::Normal);  // before Begin
    f.begun = true;
    f.overMap = false;
    CHECK(tacticalPointer(f) == Pointer::Normal);  // outside the map
    f.overMap = true;
    f.busy = true;
    CHECK(tacticalPointer(f) == Pointer::Normal);  // the window is busy
    f.busy = false;
    f.selected = false;
    CHECK(tacticalPointer(f) == Pointer::Normal);
}
