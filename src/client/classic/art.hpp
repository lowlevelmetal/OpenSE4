#pragma once

// Pictures from the player's installed classic game, loaded on demand and
// cached as GPU textures (docs/spec/06 §5). Nothing is copied into this
// project; a missing picture yields an empty Sprite and callers draw a
// placeholder.
//
// Mods (docs/sdk/packages-and-data.md "Pictures"): every picture asked for by
// its classic ".bmp" name may also be a PNG of the same base name (with its
// own transparency), and may be larger than the classic picture of its kind
// (a portrait at twice the size, a sheet of components drawn at twice the
// resolution). A larger picture keeps the classic size in the layout: its
// Sprite::size is the classic size, and its texture is made smaller with an
// area filter down to the frame's own resolution (setDetail), so that it is
// drawn sharp on a large window and smooth on a small one.

#include "assets/assets.hpp"
#include "core/math.hpp"
#include "gfx/device.hpp"

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace opense4::ruleset {
struct VehicleSize;
}

namespace opense4::client::classic {

struct Sprite {
    gfx::TextureId tex;
    Rect uv{{0, 0}, {1, 1}};
    Vec2 size;  // the picture's size in the classic layout (frame pixels): a larger picture's classic size
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
    // The frame's pixels per classic pixel (FrameMapping::scale): pictures
    // larger than their classic size are made down to this resolution, in
    // steps of a half; a change remakes those.
    void setDetail(float framebufferPerFramePixel);
    float detail() const { return detail_; }

    // The size a picture of the install has in the classic layout, by its
    // kind (docs/sdk/packages-and-data.md "Larger pictures"): minis 36×36,
    // portraits 128×128, a race's Main.bmp 100×20... (0,0) when the kind has
    // no fixed size (then the install's own copy of the picture decides).
    static Vec2 classicSize(std::string_view relative);

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
    // A design's pictures: its own (game::Design::picture, a base name as a
    // hull's bitmap names are) when it has one and this computer has it, else
    // its hull's.
    Sprite designMini(std::string_view style, const ruleset::VehicleSize& hull, std::string_view picture, bool colorKey = true, int heading = 0);
    Sprite designPortrait(std::string_view style, const ruleset::VehicleSize& hull, std::string_view picture);
    // Whether the race style has a picture of that base name (its mini, in its
    // folder or the generic one).
    bool hasShipPicture(std::string_view style, std::string_view picture);
    // The ship pictures the mods add for a race style (in its folder or the
    // generic one): base names that have a mini and a portrait, as spelled,
    // sorted (the designer's picture choice).
    std::vector<std::string> modShipPictures(std::string_view style) const;
    Sprite groupMini(std::string_view style, std::string_view group, bool colorKey = true, int heading = 0);  // "Fleet", "FighterGroup", ...
    Sprite groupPortrait(std::string_view style, std::string_view group);  // the race's Portrait_<group>, RaceGeneric's without one
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
        int width = 0;   // the classic size (Sprite::size)
        int height = 0;
        bool detailed = false;  // from a larger picture: remade when the detail changes
    };
    // A picture's pixels as loaded, and its classic size.
    struct Picture {
        assets::Image image;
        int width = 0;
        int height = 0;
        explicit operator bool() const { return !image.empty(); }
    };
    Picture loadPicture(std::string_view relative, bool colorKey);
    // A texture of `img` shown at w×h classic pixels: made smaller to the detail when it is larger.
    Texture makeTexture(const assets::Image& img, int w, int h, const std::string& key);
    const Texture* load(std::string_view relative, bool colorKey);
    // A sheet's pixels and classic size, cached for the parts cut from it.
    const Picture& sheet(std::string_view relative, bool colorKey);
    // A part of a sheet as a texture of its own, cached like the others. Drawn
    // scaled with smoothing, a part that only had its corner of the sheet's
    // texture took in a sliver of the next part at its edges: a line under
    // warp points and asteroid fields, whose next row on the planet sheet is
    // not black. A texture of its own repeats its edge pixels instead.
    Sprite cut(std::string_view sheet, bool colorKey, int x, int y, int w, int h);
    std::map<std::string, Picture> sheets_;   // the pixels of the sheets parts are cut from (key as textures_)
    // A picture turned by heading × 45°, cached like the others.
    Sprite rotated(std::string_view relative, int heading, bool colorKey);
    std::string raceFile(std::string_view style, std::string_view suffix);

    gfx::Device& device_;
    assets::InstallFiles files_;
    std::map<std::string, Texture> textures_;  // key: lowercase path + color-key flag; misses cached too
    std::map<std::string, std::optional<uint32_t>> swatches_;  // by style
    gfx::Filter filter_ = gfx::Filter::Linear;
    float detail_ = 1.0f;
};

} // namespace opense4::client::classic
