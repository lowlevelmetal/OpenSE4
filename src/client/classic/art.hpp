#pragma once

// Pictures from the player's installed classic game, loaded on demand and
// cached as GPU textures (docs/spec/06 §5). Nothing is copied into this
// project; a missing picture yields an empty Sprite and callers draw a
// placeholder.

#include "assets/assets.hpp"
#include "core/math.hpp"
#include "gfx/device.hpp"

#include <map>
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

    // Sector objects: SectType Picture Num (0-based).
    Sprite planet(int picture);
    Sprite planetPortrait(int picture);
    // Components and facilities: Pic Num (1-based).
    Sprite component(int picNum);
    Sprite componentPortrait(int picNum);
    Sprite facility(int picNum);
    Sprite facilityPortrait(int picNum);

    // Race art by style folder (Pictures/Races/<style>, RaceNeutral, RaceGeneric fallback).
    Sprite shipMini(std::string_view style, const ruleset::VehicleSize& hull);
    Sprite shipPortrait(std::string_view style, const ruleset::VehicleSize& hull);
    Sprite groupMini(std::string_view style, std::string_view group);  // "Fleet", "FighterGroup", ...
    Sprite flag(std::string_view style, bool large = true);
    Sprite racePortrait(std::string_view style);
    Sprite populationMini(std::string_view style);
    Sprite populationPortrait(std::string_view style);

    Sprite icon16(Icon i);
    Sprite icon32(Icon i);
    // Status icons (docs/spec/06 §4.4), 1-based.
    Sprite statusIcon(int number);
    // Command buttons: icon 0..12, state row 0 (normal), 1 (hover), 2 (pressed).
    Sprite commandButton(int icon, int state);
    Sprite eventPicture(std::string_view name);
    Sprite systemBackground(std::string_view bitmap);

private:
    struct Texture {
        gfx::TextureId id;
        int width = 0;
        int height = 0;
    };
    const Texture* load(std::string_view relative, bool colorKey);
    std::string raceFile(std::string_view style, std::string_view suffix);

    gfx::Device& device_;
    assets::InstallFiles files_;
    std::map<std::string, Texture> textures_;  // key: lowercase path + color-key flag; misses cached too
    gfx::Filter filter_ = gfx::Filter::Linear;
};

} // namespace opense4::client::classic
