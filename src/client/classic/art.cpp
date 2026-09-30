#include "client/classic/art.hpp"

#include "ruleset/ruleset.hpp"

#include <format>

namespace opense4::client::classic {

namespace {

std::string lower(std::string_view s) {
    std::string out(s);
    for (char& c : out)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return out;
}

Sprite whole(gfx::TextureId id, int w, int h) { return {id, Rect{{0, 0}, {1, 1}}, Vec2{float(w), float(h)}}; }

Sprite sub(gfx::TextureId id, int texW, int texH, int x, int y, int w, int h) {
    if (!id || texW <= 0 || texH <= 0 || x < 0 || y < 0 || x + w > texW || y + h > texH) return {};
    const float u0 = float(x) / float(texW), v0 = float(y) / float(texH);
    return {id, Rect{{u0, v0}, {u0 + float(w) / float(texW), v0 + float(h) / float(texH)}}, Vec2{float(w), float(h)}};
}

} // namespace

Art::Art(gfx::Device& device, assets::InstallFiles files) : device_(device), files_(std::move(files)) {}

Art::~Art() {
    for (auto& [key, t] : textures_)
        if (t.id) device_.destroyTexture(t.id);
}

void Art::setFilter(gfx::Filter filter) {
    if (filter == filter_) return;
    filter_ = filter;
    for (auto& [key, t] : textures_)
        if (t.id) device_.destroyTexture(t.id);
    textures_.clear();
}

const Art::Texture* Art::load(std::string_view relative, bool colorKey) {
    const std::string key = lower(relative) + (colorKey ? "#k" : "#o");
    if (auto it = textures_.find(key); it != textures_.end()) return it->second.id ? &it->second : nullptr;
    Texture t;
    if (auto path = files_.find(relative))
        if (auto img = assets::loadImage(*path, colorKey); img && !img->empty()) {
            t.id = device_.createTexture(gfx::TextureDesc{img->width, img->height, filter_, key.c_str()}, img->rgba.data());
            t.width = img->width;
            t.height = img->height;
        }
    auto [it, inserted] = textures_.emplace(key, t);
    return it->second.id ? &it->second : nullptr;
}

Sprite Art::image(std::string_view relative, bool colorKey) {
    const Texture* t = load(relative, colorKey);
    return t ? whole(t->id, t->width, t->height) : Sprite{};
}

Sprite Art::imageAny(std::initializer_list<std::string_view> candidates, bool colorKey) {
    for (std::string_view c : candidates)
        if (Sprite s = image(c, colorKey)) return s;
    return {};
}

Sprite Art::cell(std::string_view sheet, int index, int cellW, int cellH, bool colorKey) {
    const Texture* t = load(sheet, colorKey);
    if (!t || index < 0 || cellW <= 0) return {};
    const int cols = t->width / cellW;
    if (cols <= 0) return {};
    return sub(t->id, t->width, t->height, (index % cols) * cellW, (index / cols) * cellH, cellW, cellH);
}

Sprite Art::region(std::string_view picture, int x, int y, int w, int h, bool colorKey) {
    const Texture* t = load(picture, colorKey);
    return t ? sub(t->id, t->width, t->height, x, y, w, h) : Sprite{};
}

Sprite Art::planet(int picture) { return cell("Pictures/Planets/Planets.bmp", picture, 36, 36); }

Sprite Art::planetPortrait(int picture) { return image(std::format("Pictures/Planets/p{:04d}.bmp", picture + 1)); }

Sprite Art::component(int picNum) { return cell("Pictures/Components/Components.bmp", picNum - 1, 36, 36); }

Sprite Art::componentPortrait(int picNum) { return image(std::format("Pictures/Components/Comp_{:03d}.bmp", picNum)); }

Sprite Art::facility(int picNum) { return cell("Pictures/Facilities/Facility.bmp", picNum - 1, 36, 36); }

Sprite Art::facilityPortrait(int picNum) { return image(std::format("Pictures/Facilities/Facil_{:03d}.bmp", picNum)); }

std::string Art::raceFile(std::string_view style, std::string_view suffix) {
    // Style folders live under Races/ or RaceNeutral/; RaceGeneric has everything as a fallback.
    for (std::string_view folder : {"Races", "RaceNeutral"}) {
        std::string path = std::format("Pictures/{}/{}/{}_{}", folder, style, style, suffix);
        if (files_.find(path)) return path;
    }
    return std::format("Pictures/RaceGeneric/Generic_{}", suffix);
}

Sprite Art::shipMini(std::string_view style, const ruleset::VehicleSize& hull) {
    if (Sprite s = image(raceFile(style, std::format("Mini_{}.bmp", hull.primaryBitmap)))) return s;
    return image(raceFile(style, std::format("Mini_{}.bmp", hull.alternateBitmap)));
}

Sprite Art::shipPortrait(std::string_view style, const ruleset::VehicleSize& hull) {
    if (Sprite s = image(raceFile(style, std::format("Portrait_{}.bmp", hull.primaryBitmap)))) return s;
    return image(raceFile(style, std::format("Portrait_{}.bmp", hull.alternateBitmap)));
}

Sprite Art::groupMini(std::string_view style, std::string_view group) { return image(raceFile(style, std::format("Mini_{}.bmp", group))); }

Sprite Art::flag(std::string_view style, bool large) {
    const std::string file = raceFile(style, "Main.bmp");
    return large ? region(file, 0, 0, 26, 18, false) : region(file, 26, 0, 14, 10, false);
}

Sprite Art::racePortrait(std::string_view style) { return image(raceFile(style, "Race_Portrait.bmp"), false); }

Sprite Art::populationMini(std::string_view style) { return image(raceFile(style, "Pop_Mini.bmp")); }

Sprite Art::populationPortrait(std::string_view style) { return image(raceFile(style, "Pop_Portrait.bmp")); }

Sprite Art::icon16(Icon i) { return region("Pictures/Game/General.bmp", static_cast<int>(i) * 16, 0, 16, 16); }

Sprite Art::icon32(Icon i) { return region("Pictures/Game/General.bmp", static_cast<int>(i) * 32, 32, 32, 32); }

Sprite Art::statusIcon(int number) {
    if (number < 1) return {};
    const int i = number - 1;
    return region("Pictures/Game/General.bmp", (i % 19) * 20, i / 19 == 0 ? 136 : 156, 20, 20);
}

Sprite Art::commandButton(int icon, int state) { return region("Pictures/Game/Buttons/Main.bmp", icon * 34, state * 34, 34, 34, false); }

Sprite Art::eventPicture(std::string_view name) {
    if (name.empty()) return {};
    return image(std::format("Pictures/Events/{}.bmp", name), false);
}

Sprite Art::systemBackground(std::string_view bitmap) {
    std::string file(bitmap);
    if (!file.ends_with(".bmp") && !file.ends_with(".BMP")) file += ".bmp";
    if (Sprite s = imageAny({std::format("Pictures/Systems/1024X768/{}", file), std::format("Pictures/Game/Screens/1024X768/{}", file)},
                            false))
        return s;
    return image("Pictures/Game/Screens/1024X768/Starmap.bmp", false);
}

} // namespace opense4::client::classic
