#pragma once

// Pictures from the player's installed classic game, loaded on demand and
// cached as GPU textures (docs/spec/06 §5). Nothing is copied into this
// project; a missing picture yields an empty Sprite and callers draw a
// placeholder.

#include "assets/assets.hpp"
#include "core/math.hpp"
#include "gfx/device.hpp"

#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace opense4::ruleset {
struct VehicleSize;
}

namespace opense4::client::classic {

struct Sprite {
    gfx::TextureId tex;
    Rect uv{{0, 0}, {1, 1}};
    Vec2 size;  // pixels in the source picture
    explicit operator bool() const { return static_cast<bool>(tex); }
};

// Small and large icons in the General picture (research, intelligence,
// three resources, population), in this order.
enum class Icon { Research, Intelligence, Minerals, Organics, Radioactives, Population };

class Art {
public:
    Art(gfx::Device& device, assets::InstallFiles files);
    ~Art();
    Art(const Art&) = delete;
    Art& operator=(const Art&) = delete;

    const assets::InstallFiles& files() const { return files_; }
    // Texture filtering for everything loaded from now on; a change reloads the cache.
    void setFilter(gfx::Filter filter);

    // A whole picture ("Pictures/Game/General.bmp"), black as transparent if colorKey.
    Sprite image(std::string_view relative, bool colorKey = true);
    // The first picture found among candidates.
    Sprite imageAny(std::initializer_list<std::string_view> candidates, bool colorKey = true);
    // One cell of a row-major sheet of cellW×cellH cells.
    Sprite cell(std::string_view sheet, int index, int cellW, int cellH, bool colorKey = true);
    // An arbitrary rectangle of a picture.
    Sprite region(std::string_view picture, int x, int y, int w, int h, bool colorKey = true);

    // Sector objects: SectType Picture Num (0-based); opaque unless colorKey
    // (the system panel keys black only for Mask Background Objs, docs/spec/06 §2.4).
    Sprite planet(int picture, bool colorKey = true);
    Sprite planetPortrait(int picture);
    // Components and facilities: Pic Num (1-based).
    Sprite component(int picNum);
    Sprite componentPortrait(int picNum);
    Sprite facility(int picNum);
    Sprite facilityPortrait(int picNum);

    // Race art by style folder (Pictures/Races/<style>, RaceNeutral, RaceGeneric fallback).
    // Minis turned to `heading` (0..7, 45° steps clockwise from up, nearest-neighbour; docs/spec/06 §2.4).
    Sprite shipMini(std::string_view style, const ruleset::VehicleSize& hull, bool colorKey = true, int heading = 0);
    Sprite shipPortrait(std::string_view style, const ruleset::VehicleSize& hull);
    Sprite groupMini(std::string_view style, std::string_view group, bool colorKey = true, int heading = 0);  // "Fleet", "FighterGroup", ...
    Sprite flag(std::string_view style, bool large = true);
    Sprite racePortrait(std::string_view style);
    // A picture of a race's folder, "<Style>_<suffix>"; with `generic`, the
    // generic race's when the race has none (the Log's group portraits have no
    // such fallback, spec 06 §4.1). Opaque.
    Sprite raceImage(std::string_view style, std::string_view suffix, bool generic);
    Sprite populationMini(std::string_view style);
    Sprite populationPortrait(std::string_view style);

    Sprite icon16(Icon i);
    Sprite icon32(Icon i);
    // Status icons (docs/spec/06 §4.4), 1-based.
    Sprite statusIcon(int number);
    // Command buttons: icon 0..12, state row 0 (normal), 1 (hover), 2 (pressed).
    Sprite commandButton(int icon, int state);
    Sprite eventPicture(std::string_view name);
    // The system panel's background (Systems/<layout>/<bitmap>, ".bmp" added
    // when missing), and the 128x128 picture of the System Report (Systems/<bitmap>).
    Sprite systemBackground(std::string_view bitmap);
    // The intro screen's picture for the layout in use (Game/Screens/<layout>/Intro.bmp).
    Sprite introPicture();
    Sprite systemPicture(std::string_view bitmap);
    // The tactical and replay maps' background (docs/spec/06 §5.3): 432x432,
    // 6 x 6 tiles of 72x72, each picked by `seed` among Systems/<name>Tile<n>.bmp;
    // the top-left 432x432 of the 1024x768 star field when `name` has no tiles.
    // Opaque; it repeats every 12 combat squares.
    Sprite combatBackground(std::string_view name, uint64_t seed);
    bool hasCombatTiles(std::string_view name);

    // An empire's colour: the pixel at (28,13) of its style's _Main.bmp, inside
    // the colour swatch (docs/spec/06 §5.3), as 0xRRGGBB; nullopt without the picture.
    std::optional<uint32_t> swatchColor(std::string_view style);
    // The Art empireColor() reads swatches from (the running game's; nullptr: none).
    static void setColorSource(Art* art);
    static Art* colorSource();

private:
    struct Texture {
        gfx::TextureId id;
        int width = 0;
        int height = 0;
    };
    const Texture* load(std::string_view relative, bool colorKey);
    // A part of a sheet as a texture of its own, cached like the others. Drawn
    // scaled with smoothing, a part that only had its corner of the sheet's
    // texture took in a sliver of the next part at its edges: a line under
    // warp points and asteroid fields, whose next row on the planet sheet is
    // not black. A texture of its own repeats its edge pixels instead.
    Sprite cut(std::string_view sheet, bool colorKey, int x, int y, int w, int h);
    std::map<std::string, assets::Image> sheets_;   // the pixels of the sheets parts are cut from (key as textures_)
    // A picture turned by heading × 45°, cached like the others.
    Sprite rotated(std::string_view relative, int heading, bool colorKey);
    std::string raceFile(std::string_view style, std::string_view suffix);

    gfx::Device& device_;
    assets::InstallFiles files_;
    std::map<std::string, Texture> textures_;  // key: lowercase path + color-key flag; misses cached too
    std::map<std::string, std::optional<uint32_t>> swatches_;  // by style
    gfx::Filter filter_ = gfx::Filter::Linear;
};

} // namespace opense4::client::classic
