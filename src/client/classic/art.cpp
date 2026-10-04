#include "client/classic/art.hpp"
#include "client/classic/layout.hpp"

#include "core/rng.hpp"
#include "ruleset/ruleset.hpp"

#include <cstring>

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

Art* gColorSource = nullptr;

std::string systemFile(std::string_view bitmap) {
    std::string file(bitmap);
    if (!lower(file).ends_with(".bmp")) file += ".bmp";
    return file;
}

} // namespace

Art::Art(gfx::Device& device, assets::InstallFiles files) : device_(device), files_(std::move(files)) {}

Art::~Art() {
    if (gColorSource == this) gColorSource = nullptr;
    for (auto& [key, t] : textures_)
        if (t.id) device_.destroyTexture(t.id);
}

void Art::setFilter(gfx::Filter filter) {
    if (filter == filter_) return;
    filter_ = filter;
    for (auto& [key, t] : textures_)
        if (t.id) device_.destroyTexture(t.id);
    textures_.clear();
    sheets_.clear();
}

const Art::Texture* Art::load(std::string_view relative, bool colorKey) {
    const std::string key = lower(relative) + (colorKey ? "#k" : "#o");
    if (auto it = textures_.find(key); it != textures_.end()) return it->second.id ? &it->second : nullptr;
    Texture t;
    if (auto path = files_.find(relative)) {
        if (auto img = assets::loadImage(*path, colorKey); img && !img->empty()) {
            t.id = device_.createTexture(gfx::TextureDesc{img->width, img->height, filter_, key.c_str()}, img->rgba.data());
            t.width = img->width;
            t.height = img->height;
        }
    } else {
        files_.noteMissing(relative);
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
        if (files_.find(c))
            if (Sprite s = image(c, colorKey)) return s;
    if (candidates.size() > 0) files_.noteMissing(*candidates.begin());
    return {};
}

Sprite Art::cell(std::string_view sheet, int index, int cellW, int cellH, bool colorKey) {
    const Texture* t = load(sheet, colorKey);
    if (!t || index < 0 || cellW <= 0) return {};
    const int cols = t->width / cellW;
    if (cols <= 0) return {};
    return cut(sheet, colorKey, (index % cols) * cellW, (index / cols) * cellH, cellW, cellH);
}

Sprite Art::region(std::string_view picture, int x, int y, int w, int h, bool colorKey) { return cut(picture, colorKey, x, y, w, h); }

Sprite Art::cut(std::string_view sheet, bool colorKey, int x, int y, int w, int h) {
    const Texture* t = load(sheet, colorKey);
    if (!t || x < 0 || y < 0 || w <= 0 || h <= 0 || x + w > t->width || y + h > t->height) return {};
    if (x == 0 && y == 0 && w == t->width && h == t->height) return whole(t->id, w, h);
    const std::string sheetKey = lower(sheet) + (colorKey ? "#k" : "#o");
    const std::string key = std::format("{}@{},{},{},{}", sheetKey, x, y, w, h);
    if (auto it = textures_.find(key); it != textures_.end()) return it->second.id ? whole(it->second.id, it->second.width, it->second.height) : Sprite{};
    auto pixels = sheets_.find(sheetKey);
    if (pixels == sheets_.end()) {
        assets::Image img;
        if (const auto path = files_.find(sheet))
            if (auto loaded = assets::loadImage(*path, colorKey)) img = std::move(*loaded);
        pixels = sheets_.emplace(sheetKey, std::move(img)).first;
    }
    Texture c;
    const assets::Image& img = pixels->second;
    if (x + w <= img.width && y + h <= img.height) {
        std::vector<uint8_t> part(size_t(w) * size_t(h) * 4);
        for (int row = 0; row < h; ++row)
            std::memcpy(part.data() + size_t(row) * size_t(w) * 4, img.rgba.data() + (size_t(y + row) * size_t(img.width) + size_t(x)) * 4, size_t(w) * 4);
        c.id = device_.createTexture(gfx::TextureDesc{w, h, filter_, key.c_str()}, part.data());
        c.width = w;
        c.height = h;
    }
    textures_.emplace(key, c);
    return c.id ? whole(c.id, w, h) : Sprite{};
}

Sprite Art::planet(int picture, bool colorKey) { return cell("Pictures/Planets/Planets.bmp", picture, 36, 36, colorKey); }

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

Sprite Art::rotated(std::string_view relative, int heading, bool colorKey) {
    heading = ((heading % 8) + 8) % 8;
    if (heading == 0) return image(relative, colorKey);
    const std::string key = lower(relative) + std::format("#r{}", heading) + (colorKey ? "#k" : "#o");
    if (auto it = textures_.find(key); it != textures_.end()) return it->second.id ? whole(it->second.id, it->second.width, it->second.height) : Sprite{};
    Texture t;
    const auto path = files_.find(relative);
    if (!path) files_.noteMissing(relative);
    if (path)
        if (auto img = assets::loadImage(*path, colorKey); img && !img->empty()) {
            const assets::Image turned = assets::rotateNearest(*img, 45.0 * heading, colorKey);
            t.id = device_.createTexture(gfx::TextureDesc{turned.width, turned.height, filter_, key.c_str()}, turned.rgba.data());
            t.width = turned.width;
            t.height = turned.height;
        }
    textures_.emplace(key, t);
    return t.id ? whole(t.id, t.width, t.height) : Sprite{};
}

Sprite Art::shipMini(std::string_view style, const ruleset::VehicleSize& hull, bool colorKey, int heading) {
    for (const std::string* bitmap : {&hull.primaryBitmap, &hull.alternateBitmap}) {
        const std::string file = raceFile(style, std::format("Mini_{}.bmp", *bitmap));
        if (files_.find(file))
            if (Sprite s = rotated(file, heading, colorKey)) return s;
    }
    files_.noteMissing(raceFile(style, std::format("Mini_{}.bmp", hull.primaryBitmap)));
    return {};
}

Sprite Art::shipPortrait(std::string_view style, const ruleset::VehicleSize& hull) {
    if (Sprite s = image(raceFile(style, std::format("Portrait_{}.bmp", hull.primaryBitmap)))) return s;
    return image(raceFile(style, std::format("Portrait_{}.bmp", hull.alternateBitmap)));
}

Sprite Art::groupMini(std::string_view style, std::string_view group, bool colorKey, int heading) {
    return rotated(raceFile(style, std::format("Mini_{}.bmp", group)), heading, colorKey);
}

Sprite Art::flag(std::string_view style, bool large) {
    const std::string file = raceFile(style, "Main.bmp");
    return large ? region(file, 0, 0, 26, 18, false) : region(file, 26, 0, 14, 10, false);
}

Sprite Art::racePortrait(std::string_view style) { return image(raceFile(style, "Race_Portrait.bmp"), false); }

Sprite Art::raceImage(std::string_view style, std::string_view suffix, bool generic) {
    if (generic) return image(raceFile(style, suffix), false);
    for (std::string_view folder : {"Races", "RaceNeutral"}) {
        const std::string path = std::format("Pictures/{}/{}/{}_{}", folder, style, style, suffix);
        if (files_.find(path)) return image(path, false);
    }
    return {};
}

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
    // Only from Systems/<resolution>/ of the layout in use (confirmed: binary;
    // docs/spec/06 §2.1.1); the plain star field when missing.
    const char* folder = layoutGeometry().systems;
    if (Sprite s = image(std::format("{}{}", folder, systemFile(bitmap)), false)) return s;
    return image(std::format("{}Starmap.bmp", folder), false);
}

Sprite Art::introPicture() {
    // The layout's own intro picture, else the other one (docs/spec/06 §1.1, §2.1.1).
    const bool small = screenLayout() == ScreenLayout::Small;
    return small ? imageAny({"Pictures/Game/Screens/800X600/Intro.bmp", "Pictures/Game/Screens/1024X768/Intro.bmp"}, false)
                 : imageAny({"Pictures/Game/Screens/1024X768/Intro.bmp", "Pictures/Game/Screens/800X600/Intro.bmp"}, false);
}

Sprite Art::systemPicture(std::string_view bitmap) {
    if (bitmap.empty()) return {};
    return image(std::format("Pictures/Systems/{}", systemFile(bitmap)), false);
}

std::optional<uint32_t> Art::swatchColor(std::string_view style) {
    const std::string key(style);
    if (auto it = swatches_.find(key); it != swatches_.end()) return it->second;
    std::optional<uint32_t> color;
    const std::string main = raceFile(style, "Main.bmp");
    if (!files_.find(main)) files_.noteMissing(main);
    if (auto path = files_.find(main))
        if (auto img = assets::loadImage(*path, false); img && img->width > 28 && img->height > 13) {
            const uint8_t* p = &img->rgba[(static_cast<size_t>(13) * static_cast<size_t>(img->width) + 28) * 4];
            color = (uint32_t{p[0]} << 16) | (uint32_t{p[1]} << 8) | uint32_t{p[2]};
        }
    swatches_.emplace(key, color);
    return color;
}

bool Art::hasCombatTiles(std::string_view name) {
    return !name.empty() && files_.find(std::format("Pictures/Systems/{}Tile1.bmp", name)).has_value();
}

Sprite Art::combatBackground(std::string_view name, uint64_t seed) {
    constexpr int kSize = 432, kTile = 72;
    const std::string key = std::format("#combat#{}#{}", lower(name), seed);
    if (auto it = textures_.find(key); it != textures_.end()) return it->second.id ? whole(it->second.id, it->second.width, it->second.height) : Sprite{};
    std::vector<assets::Image> tiles;
    for (int n = 1; n <= 100 && hasCombatTiles(name); ++n) {
        const auto path = files_.find(std::format("Pictures/Systems/{}Tile{}.bmp", name, n));
        if (!path) break;
        if (auto img = assets::loadImage(*path, false); img && img->width >= kTile && img->height >= kTile) tiles.push_back(std::move(*img));
    }
    assets::Image picture;
    if (!tiles.empty()) {
        picture.width = picture.height = kSize;
        picture.rgba.assign(size_t(kSize) * kSize * 4, 255);
        Rng rng(seed);
        for (int ty = 0; ty < kSize / kTile; ++ty)
            for (int tx = 0; tx < kSize / kTile; ++tx) {
                const assets::Image& t = tiles[static_cast<size_t>(rng.below(tiles.size()))];
                for (int y = 0; y < kTile; ++y)
                    std::memcpy(&picture.rgba[(size_t(ty * kTile + y) * kSize + size_t(tx * kTile)) * 4], &t.rgba[size_t(y) * size_t(t.width) * 4],
                                size_t(kTile) * 4);
            }
    } else if (auto path = files_.find("Pictures/Systems/1024X768/Starmap.bmp")) {
        if (auto img = assets::loadImage(*path, false)) picture = assets::crop(*img, 0, 0, kSize, kSize);
    } else {
        files_.noteMissing("Pictures/Systems/1024X768/Starmap.bmp");
    }
    Texture t;
    if (!picture.empty()) {
        t.id = device_.createTexture(gfx::TextureDesc{picture.width, picture.height, filter_, key.c_str()}, picture.rgba.data());
        t.width = picture.width;
        t.height = picture.height;
    }
    textures_.emplace(key, t);
    return t.id ? whole(t.id, t.width, t.height) : Sprite{};
}

void Art::setColorSource(Art* art) { gColorSource = art; }

Art* Art::colorSource() { return gColorSource; }

} // namespace opense4::client::classic
