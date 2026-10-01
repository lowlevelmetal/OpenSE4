// Windows cursor (.cur) files, the mod folder of an install, and raster
// fonts read from files: on fixtures drawn for these tests
// (tests/fixtures/install, written by tools/make_test_fixtures.py), never
// on game files.

#include "assets/assets.hpp"
#include "assets/tiny_font.hpp"
#include "assets/wincursor.hpp"
#include "assets/winfont.hpp"

#include <doctest/doctest.h>

#include <cstring>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

using namespace opense4::assets;

namespace {

const std::filesystem::path kInstall = std::filesystem::path(OPENSE4_FIXTURE_DIR) / "install";

// RGBA of one pixel.
std::array<uint8_t, 4> pixel(const CursorImage& c, int x, int y) {
    std::array<uint8_t, 4> p{};
    std::memcpy(p.data(), &c.rgba[(size_t(y) * size_t(c.width) + size_t(x)) * 4], 4);
    return p;
}

} // namespace

TEST_CASE("cursor: a 1-bit pointer with its hot spot") {
    std::string error;
    const auto c = loadCursor(kInstall / "Pictures/Game/Normal.cur", &error);
    REQUIRE_MESSAGE(c, error);
    CHECK(c->width == 8);
    CHECK(c->height == 8);
    CHECK(c->hotX == 2);
    CHECK(c->hotY == 2);
    // Black, white and transparent pixels; the screen-inverting one comes out opaque black.
    CHECK(pixel(*c, 0, 0) == std::array<uint8_t, 4>{0, 0, 0, 255});
    CHECK(pixel(*c, 1, 2) == std::array<uint8_t, 4>{255, 255, 255, 255});
    CHECK(pixel(*c, 7, 0)[3] == 0);
    CHECK(pixel(*c, 3, 6) == std::array<uint8_t, 4>{0, 0, 0, 255});
    // Rows are stored bottom-up: the top row is the file's last.
    CHECK(pixel(*c, 0, 7)[3] == 255);
    CHECK(pixel(*c, 1, 7)[3] == 0);
}

TEST_CASE("cursor: a 4-bit pointer reads its palette") {
    std::string error;
    const auto c = loadCursor(kInstall / "Pictures/Game/Target.cur", &error);
    REQUIRE_MESSAGE(c, error);
    CHECK(c->hotX == 3);
    CHECK(c->hotY == 4);
    CHECK(pixel(*c, 1, 2) == std::array<uint8_t, 4>{255, 0, 0, 255});
    CHECK(pixel(*c, 0, 0) == std::array<uint8_t, 4>{0, 0, 0, 255});
}

TEST_CASE("cursor: bad files are refused") {
    std::string error;
    CHECK_FALSE(parseCursor(std::vector<uint8_t>{1, 2, 3}, &error));
    CHECK_FALSE(error.empty());
    // A directory that points past the end of the file.
    std::vector<uint8_t> d(6 + 16, 0);
    d[2] = 2;
    d[4] = 1;
    d[6 + 12] = 200;
    error.clear();
    CHECK_FALSE(parseCursor(d, &error));
    CHECK_FALSE(error.empty());
    // A missing file.
    error.clear();
    CHECK_FALSE(loadCursor(kInstall / "Pictures/Game/Nothing.cur", &error));
}

TEST_CASE("install: Path.txt names the mod folder, found before the base tree") {
    CHECK(modDirectoryFromPathTxt("*BEGIN*\nUsing Mod Directory := None\n*END*\n").empty());
    CHECK(modDirectoryFromPathTxt("").empty());
    CHECK(modDirectoryFromPathTxt("Using Mod Directory   :=  My Mod\\  \r\n") == "My Mod");

    const InstallFiles files(kInstall);
    CHECK(files.modDirectory() == "testmod");
    // The mod's copy first.
    const auto modFont = files.findModFirst("fonts/testface.FON");
    REQUIRE(modFont);
    std::string error;
    const auto fromMod = loadFon(*modFont, &error);
    REQUIRE_MESSAGE(fromMod.size() == 1, error);
    CHECK(fromMod[0].face == "Mod Face");
    // Files the mod lacks come from the base tree.
    const auto base = files.findModFirst("Fonts/OnlyBase.fon");
    REQUIRE(base);
    const auto fromBase = loadFon(*base, &error);
    REQUIRE(fromBase.size() == 1);
    CHECK(fromBase[0].face == "Only Base");
    CHECK(fromBase[0].pixelHeight == 5);
    CHECK(fromBase[0].ascent == 4);
    REQUIRE(fromBase[0].glyph('A'));
    CHECK(fromBase[0].glyph('A')->width == 3);
    CHECK(fromBase[0].ink(*fromBase[0].glyph('A'), 1, 0));
    CHECK_FALSE(fromBase[0].ink(*fromBase[0].glyph('A'), 1, 1));
    // Lower-case requests find mixed-case files (the game asks for "normal.cur").
    CHECK(files.findModFirst("pictures/game/normal.cur"));
}

TEST_CASE("tiny font: our own small face covers the printable characters") {
    const BitmapFont f = makeTinyFont();
    CHECK(f.face == kTinyFaceName);
    CHECK(f.pixelHeight == 8);
    CHECK(f.ascent == 6);
    REQUIRE(f.glyphs.size() == 95);
    std::set<int> widths;
    for (int c = ' '; c <= '~'; ++c) {
        const BitmapGlyph* g = f.glyph(uint8_t(c));
        REQUIRE(g);
        CHECK(g->width >= 2);
        CHECK(g->bits.size() == size_t(g->width * f.pixelHeight));
        // Every glyph but the space has ink, and the column after it is blank.
        bool ink = false;
        for (int y = 0; y < f.pixelHeight; ++y) {
            CHECK_FALSE(f.ink(*g, g->width - 1, y));
            for (int x = 0; x < g->width; ++x) ink = ink || f.ink(*g, x, y);
        }
        CHECK(ink == (c != ' '));
        // Row 0 is the cell's empty top.
        for (int x = 0; x < g->width; ++x) CHECK_FALSE(f.ink(*g, x, 0));
    }
    // Digits share one width so counts line up.
    for (int c = '0'; c <= '9'; ++c) widths.insert(f.glyph(uint8_t(c))->width);
    CHECK(widths.size() == 1);
    // Characters outside the face fall back to '?'.
    CHECK(f.glyph(0xe9) == f.glyph('?'));
}
