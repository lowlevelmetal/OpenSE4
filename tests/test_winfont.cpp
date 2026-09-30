// Windows bitmap font parsing, on fonts built here byte by byte (no game files).

#include "assets/winfont.hpp"

#include <doctest/doctest.h>

#include <cstring>
#include <string>
#include <vector>

using namespace opense4::assets;

namespace {

void put16(std::vector<uint8_t>& d, size_t at, unsigned v) {
    if (d.size() < at + 2) d.resize(at + 2);
    d[at] = uint8_t(v);
    d[at + 1] = uint8_t(v >> 8);
}
void put32(std::vector<uint8_t>& d, size_t at, uint32_t v) {
    put16(d, at, v & 0xffff);
    put16(d, at + 2, v >> 16);
}

// A v2 font, 5 pixels tall, with 'A' (3 px wide: a box) and 'B' (10 px wide: two byte columns).
std::vector<uint8_t> makeFnt() {
    std::vector<uint8_t> d(118 + 3 * 4, 0);
    put16(d, 0, 0x0200);
    put16(d, 68, 7);    // points
    put16(d, 74, 4);    // ascent
    put16(d, 76, 1);    // internal leading
    put16(d, 83, 400);  // weight
    put16(d, 88, 5);    // pixel height
    d[95] = 'A';
    d[96] = 'B';
    d[97] = 0;  // default: 'A'
    // Glyph 'A': 3 wide, one byte column.
    const size_t a = d.size();
    for (int row : {0xe0, 0xa0, 0xa0, 0xa0, 0xe0}) d.push_back(uint8_t(row));
    // Glyph 'B': 10 wide, two byte columns: a full first row, then the rightmost pixel of each row.
    const size_t b = d.size();
    for (int row : {0xff, 0x00, 0x00, 0x00, 0x00}) d.push_back(uint8_t(row));
    for (int row : {0xc0, 0x40, 0x40, 0x40, 0x40}) d.push_back(uint8_t(row));
    put16(d, 118, 3);
    put16(d, 120, unsigned(a));
    put16(d, 122, 10);
    put16(d, 124, unsigned(b));
    put16(d, 126, 0);  // the sentinel entry
    put16(d, 128, 0);
    const size_t face = d.size();
    for (char c : std::string("Test Face")) d.push_back(uint8_t(c));
    d.push_back(0);
    put32(d, 105, uint32_t(face));
    put32(d, 2, uint32_t(d.size()));
    return d;
}

// Wraps a FNT in a minimal MZ + NE module with one RT_FONT resource.
std::vector<uint8_t> makeFon(const std::vector<uint8_t>& fnt) {
    std::vector<uint8_t> d(0x40, 0);
    d[0] = 'M';
    d[1] = 'Z';
    const size_t ne = 0x80;
    put32(d, 0x3c, ne);
    d.resize(ne + 0x40, 0);
    d[ne] = 'N';
    d[ne + 1] = 'E';
    put16(d, ne + 0x24, 0x40);  // resource table right after the header
    const size_t rt = ne + 0x40;
    const unsigned shift = 4;
    put16(d, rt, shift);
    put16(d, rt + 2, 0x8007);  // an RT_FONTDIR block that must be skipped
    put16(d, rt + 4, 1);
    put16(d, rt + 22, 0x8008);  // RT_FONT
    put16(d, rt + 24, 1);
    const size_t fontAt = 0x200;
    put16(d, rt + 30, unsigned(fontAt >> shift));
    put16(d, rt + 32, unsigned((fnt.size() + 15) >> shift));
    put16(d, rt + 42, 0);  // end of the type list
    d.resize(fontAt, 0);
    d.insert(d.end(), fnt.begin(), fnt.end());
    d.resize(fontAt + ((fnt.size() + 15) & ~size_t(15)), 0);
    return d;
}

} // namespace

TEST_CASE("bitmap font: FNT glyph bitmaps") {
    std::string error;
    const auto f = parseFnt(makeFnt(), &error);
    REQUIRE_MESSAGE(f, error);
    CHECK(f->face == "Test Face");
    CHECK(f->pixelHeight == 5);
    CHECK(f->ascent == 4);
    CHECK(f->internalLeading == 1);
    REQUIRE(f->glyphs.size() == 2);
    const BitmapGlyph& a = *f->glyph('A');
    CHECK(a.width == 3);
    CHECK(f->ink(a, 0, 0));
    CHECK(f->ink(a, 2, 0));
    CHECK_FALSE(f->ink(a, 1, 2));
    CHECK(f->ink(a, 1, 4));
    const BitmapGlyph& b = *f->glyph('B');
    CHECK(b.width == 10);
    for (int x = 0; x < 10; ++x) CHECK(f->ink(b, x, 0) == (x < 8 || x == 8 || x == 9));
    CHECK(f->ink(b, 9, 3));
    CHECK_FALSE(f->ink(b, 8, 3));
    CHECK_FALSE(f->ink(b, 0, 3));
    // Missing characters use the default glyph.
    CHECK(f->glyph('z') == f->glyph('A'));
}

TEST_CASE("bitmap font: NE container") {
    std::string error;
    const auto fonts = parseFon(makeFon(makeFnt()), &error);
    REQUIRE_MESSAGE(fonts.size() == 1, error);
    CHECK(fonts[0].face == "Test Face");
    CHECK(fonts[0].glyphs.size() == 2);
}

TEST_CASE("bitmap font: bad input is rejected") {
    std::string error;
    CHECK(parseFon(std::vector<uint8_t>{'M', 'Z', 0, 0}, &error).empty());
    CHECK_FALSE(error.empty());
    auto fnt = makeFnt();
    fnt.resize(120);  // cut through the character table
    error.clear();
    CHECK_FALSE(parseFnt(fnt, &error));
    CHECK_FALSE(error.empty());
}

TEST_CASE("bitmap font: Windows-1252 mapping") {
    CHECK(unicodeToCp1252(U'A') == 'A');
    CHECK(unicodeToCp1252(U'é') == 0xe9);
    CHECK(unicodeToCp1252(U'—') == 0x97);
    CHECK(unicodeToCp1252(U'€') == 0x80);
    CHECK(unicodeToCp1252(U'一') == 0);
}
