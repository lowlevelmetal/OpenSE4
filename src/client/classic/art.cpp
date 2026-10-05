#include "client/classic/art.hpp"
#include "client/classic/layout.hpp"

#include "core/rng.hpp"
#include "ruleset/ruleset.hpp"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstring>
#include <format>
#include <map>

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

void Art::setDetail(float scale) {
    // In steps of a half, so that resizing the window remakes them seldom.
    const float detail = std::max(1.0f, std::ceil(scale * 2.0f - 0.01f) / 2.0f);
    if (detail == detail_) return;
    detail_ = detail;
    for (auto it = textures_.begin(); it != textures_.end();) {
        if (it->second.detailed) {
            if (it->second.id) device_.destroyTexture(it->second.id);
            it = textures_.erase(it);
        } else {
            ++it;
        }
    }
}

Vec2 Art::classicSize(std::string_view relative) {
    const auto size = assets::classicPictureSize(relative);
    return size ? Vec2{float(size->first), float(size->second)} : Vec2{};
}

Art::Picture Art::loadPicture(std::string_view relative, bool colorKey) {
    Picture pic;
    bool installed = false;
    const auto path = files_.findPicture(relative, &installed);
    if (!path) {
        files_.noteMissing(relative);
        return pic;
    }
    auto img = assets::loadImage(*path, colorKey);
    if (!img || img->empty()) return pic;
    pic.image = std::move(*img);
    pic.width = pic.image.width;
    pic.height = pic.image.height;
    // A picture larger than the classic one of its kind keeps the classic
    // size in the layout: the kind's, else the install's own copy's.
    auto fitsInto = [&](int w, int h) {
        return w > 0 && h > 0 && pic.image.width >= w && pic.image.height >= h && (pic.image.width > w || pic.image.height > h);
    };
    const Vec2 kind = classicSize(relative);
    if (fitsInto(int(kind.x), int(kind.y))) {
        pic.width = int(kind.x);
        pic.height = int(kind.y);
    } else if (!installed) {
        if (const auto base = files_.findInstalledPicture(relative))
            if (const auto info = assets::probeImageSize(*base); info && fitsInto(info->first, info->second)) {
                pic.width = info->first;
                pic.height = info->second;
            }
    }
    return pic;
}

Art::Texture Art::makeTexture(const assets::Image& img, int w, int h, const std::string& key) {
    Texture t;
    t.width = w;
    t.height = h;
    const assets::Image* use = &img;
    assets::Image smaller;
    if (img.width > w || img.height > h) {
        // Down to the frame's resolution, with an area filter: the GPU then
        // draws it at about one texel a pixel, sharp and without shimmer.
        t.detailed = true;
        const int tw = std::min(img.width, int(std::ceil(float(w) * detail_))), th = std::min(img.height, int(std::ceil(float(h) * detail_)));
        if (tw < img.width || th < img.height) {
            smaller = assets::downscale(img, tw, th);
            use = &smaller;
        }
    }
    t.id = device_.createTexture(gfx::TextureDesc{use->width, use->height, filter_, key.c_str()}, use->rgba.data());
    return t;
}

const Art::Texture* Art::load(std::string_view relative, bool colorKey) {
    const std::string key = lower(relative) + (colorKey ? "#k" : "#o");
    if (auto it = textures_.find(key); it != textures_.end()) return it->second.id ? &it->second : nullptr;
    Texture t;
    if (const Picture pic = loadPicture(relative, colorKey)) t = makeTexture(pic.image, pic.width, pic.height, key);
    auto [it, inserted] = textures_.emplace(key, t);
    return it->second.id ? &it->second : nullptr;
}

const Art::Picture& Art::sheet(std::string_view relative, bool colorKey) {
    const std::string key = lower(relative) + (colorKey ? "#k" : "#o");
    auto it = sheets_.find(key);
    if (it == sheets_.end()) it = sheets_.emplace(key, loadPicture(relative, colorKey)).first;
    return it->second;
}

Sprite Art::image(std::string_view relative, bool colorKey) {
    const Texture* t = load(relative, colorKey);
    return t ? whole(t->id, t->width, t->height) : Sprite{};
}

Sprite Art::imageAny(std::initializer_list<std::string_view> candidates, bool colorKey) {
    for (std::string_view c : candidates)
        if (files_.findPicture(c))
            if (Sprite s = image(c, colorKey)) return s;
    if (candidates.size() > 0) files_.noteMissing(*candidates.begin());
    return {};
}

Sprite Art::cell(std::string_view sheetName, int index, int cellW, int cellH, bool colorKey) {
    const Picture& pic = sheet(sheetName, colorKey);
    if (!pic || index < 0 || cellW <= 0) return {};
    const int cols = pic.width / cellW;
    if (cols <= 0) return {};
    return cut(sheetName, colorKey, (index % cols) * cellW, (index / cols) * cellH, cellW, cellH);
}

Sprite Art::region(std::string_view picture, int x, int y, int w, int h, bool colorKey) { return cut(picture, colorKey, x, y, w, h); }

Sprite Art::cut(std::string_view sheetName, bool colorKey, int x, int y, int w, int h) {
    const Picture& pic = sheet(sheetName, colorKey);
    if (!pic || x < 0 || y < 0 || w <= 0 || h <= 0 || x + w > pic.width || y + h > pic.height) return {};
    if (x == 0 && y == 0 && w == pic.width && h == pic.height) return image(sheetName, colorKey);
    const std::string sheetKey = lower(sheetName) + (colorKey ? "#k" : "#o");
    const std::string key = std::format("{}@{},{},{},{}", sheetKey, x, y, w, h);
    if (auto it = textures_.find(key); it != textures_.end()) return it->second.id ? whole(it->second.id, it->second.width, it->second.height) : Sprite{};
    // The part's pixels: classic places scaled to the sheet's resolution.
    const assets::Image& img = pic.image;
    const double fx = double(img.width) / pic.width, fy = double(img.height) / pic.height;
    const int x0 = int(std::lround(x * fx)), y0 = int(std::lround(y * fy));
    const int pw = std::max(1, std::min(img.width - x0, int(std::lround((x + w) * fx)) - x0));
    const int ph = std::max(1, std::min(img.height - y0, int(std::lround((y + h) * fy)) - y0));
    Texture c;
    if (x0 + pw <= img.width && y0 + ph <= img.height) {
        assets::Image part;
        part.width = pw;
        part.height = ph;
        part.rgba.resize(size_t(pw) * size_t(ph) * 4);
        for (int row = 0; row < ph; ++row)
            std::memcpy(part.rgba.data() + size_t(row) * size_t(pw) * 4, img.rgba.data() + (size_t(y0 + row) * size_t(img.width) + size_t(x0)) * 4,
                        size_t(pw) * 4);
        c = makeTexture(part, w, h, key);
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
        if (files_.findPicture(path)) return path;
    }
    return std::format("Pictures/RaceGeneric/Generic_{}", suffix);
}

Sprite Art::rotated(std::string_view relative, int heading, bool colorKey) {
    heading = ((heading % 8) + 8) % 8;
    if (heading == 0) return image(relative, colorKey);
    const std::string key = lower(relative) + std::format("#r{}", heading) + (colorKey ? "#k" : "#o");
    if (auto it = textures_.find(key); it != textures_.end()) return it->second.id ? whole(it->second.id, it->second.width, it->second.height) : Sprite{};
    Texture t;
    // Turned at the picture's own resolution, then made down like the others.
    if (const Picture pic = loadPicture(relative, colorKey)) t = makeTexture(assets::rotateNearest(pic.image, 45.0 * heading, colorKey), pic.width, pic.height, key);
    textures_.emplace(key, t);
    return t.id ? whole(t.id, t.width, t.height) : Sprite{};
}

Sprite Art::shipMini(std::string_view style, const ruleset::VehicleSize& hull, bool colorKey, int heading) {
    for (const std::string* bitmap : {&hull.primaryBitmap, &hull.alternateBitmap}) {
        const std::string file = raceFile(style, std::format("Mini_{}.bmp", *bitmap));
        if (files_.findPicture(file))
            if (Sprite s = rotated(file, heading, colorKey)) return s;
    }
    files_.noteMissing(raceFile(style, std::format("Mini_{}.bmp", hull.primaryBitmap)));
    return {};
}

Sprite Art::shipPortrait(std::string_view style, const ruleset::VehicleSize& hull) {
    if (Sprite s = image(raceFile(style, std::format("Portrait_{}.bmp", hull.primaryBitmap)))) return s;
    return image(raceFile(style, std::format("Portrait_{}.bmp", hull.alternateBitmap)));
}

bool Art::hasShipPicture(std::string_view style, std::string_view picture) {
    return !picture.empty() && files_.findPicture(raceFile(style, std::format("Mini_{}.bmp", picture))).has_value();
}

Sprite Art::designMini(std::string_view style, const ruleset::VehicleSize& hull, std::string_view picture, bool colorKey, int heading) {
    // The design's own picture where this computer has it: an asset mod may be missing here.
    if (hasShipPicture(style, picture))
        if (Sprite s = rotated(raceFile(style, std::format("Mini_{}.bmp", picture)), heading, colorKey)) return s;
    return shipMini(style, hull, colorKey, heading);
}

Sprite Art::designPortrait(std::string_view style, const ruleset::VehicleSize& hull, std::string_view picture) {
    if (!picture.empty()) {
        const std::string file = raceFile(style, std::format("Portrait_{}.bmp", picture));
        if (files_.findPicture(file))
            if (Sprite s = image(file)) return s;
    }
    return shipPortrait(style, hull);
}

std::vector<std::string> Art::modShipPictures(std::string_view style) const {
    // "<Style>_Mini_<name>" in the race's folders and "Generic_Mini_<name>",
    // with a portrait of the same name beside it.
    std::map<std::string, std::string> found;   // lowercase name -> as spelled
    auto scan = [&](const std::string& folder, std::string_view prefix) {
        const std::vector<std::string> files = files_.layerFiles(folder);
        auto has = [&](std::string_view kind, std::string_view name) {
            const std::string want = lower(std::format("{}/{}{}{}", folder, prefix, kind, name));
            return std::any_of(files.begin(), files.end(), [&](const std::string& f) {
                const std::string l = lower(f);
                return l == want + ".bmp" || l == want + ".png";
            });
        };
        for (const std::string& f : files) {
            const size_t slash = f.rfind('/');
            const std::string name = f.substr(slash + 1);
            const std::string l = lower(name);
            const std::string head = lower(std::format("{}Mini_", prefix));
            if (!l.starts_with(head) || (!l.ends_with(".bmp") && !l.ends_with(".png"))) continue;
            if (slash != folder.size()) continue;   // not in a subfolder
            const std::string base = name.substr(head.size(), name.size() - head.size() - 4);
            if (base.empty() || !has("Portrait_", base)) continue;
            found.emplace(lower(base), base);
        }
    };
    if (!style.empty())
        for (std::string_view folder : {"Races", "RaceNeutral"})
            scan(std::format("Pictures/{}/{}", folder, style), std::format("{}_", style));
    scan("Pictures/RaceGeneric", "Generic_");
    std::vector<std::string> out;
    for (auto& [key, name] : found) out.push_back(std::move(name));
    return out;
}

Sprite Art::groupMini(std::string_view style, std::string_view group, bool colorKey, int heading) {
    return rotated(raceFile(style, std::format("Mini_{}.bmp", group)), heading, colorKey);
}

Sprite Art::groupPortrait(std::string_view style, std::string_view group) { return image(raceFile(style, std::format("Portrait_{}.bmp", group))); }

Sprite Art::flag(std::string_view style, bool large) {
    const std::string file = raceFile(style, "Main.bmp");
    return large ? region(file, 0, 0, 26, 18, false) : region(file, 26, 0, 14, 10, false);
}

Sprite Art::racePortrait(std::string_view style) { return image(raceFile(style, "Race_Portrait.bmp"), false); }

Sprite Art::raceImage(std::string_view style, std::string_view suffix, bool generic) {
    if (generic) return image(raceFile(style, suffix), false);
    for (std::string_view folder : {"Races", "RaceNeutral"}) {
        const std::string path = std::format("Pictures/{}/{}/{}_{}", folder, style, style, suffix);
        if (files_.findPicture(path)) return image(path, false);
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
    // The pixel in the classic places, wherever a larger picture has it.
    if (const Picture& pic = sheet(main, false); pic && pic.width > 28 && pic.height > 13) {
        const assets::Image& img = pic.image;
        const int x = std::min(img.width - 1, int(std::floor((28.5 * img.width) / pic.width)));
        const int y = std::min(img.height - 1, int(std::floor((13.5 * img.height) / pic.height)));
        const uint8_t* p = &img.rgba[(static_cast<size_t>(y) * static_cast<size_t>(img.width) + static_cast<size_t>(x)) * 4];
        color = (uint32_t{p[0]} << 16) | (uint32_t{p[1]} << 8) | uint32_t{p[2]};
    }
    swatches_.emplace(key, color);
    return color;
}

bool Art::hasCombatTiles(std::string_view name) {
    return !name.empty() && files_.findPicture(std::format("Pictures/Systems/{}Tile1.bmp", name)).has_value();
}

Sprite Art::combatBackground(std::string_view name, uint64_t seed) {
    constexpr int kSize = 432, kTile = 72;
    const std::string key = std::format("#combat#{}#{}", lower(name), seed);
    if (auto it = textures_.find(key); it != textures_.end()) return it->second.id ? whole(it->second.id, it->second.width, it->second.height) : Sprite{};
    std::vector<assets::Image> tiles;
    for (int n = 1; n <= 100 && hasCombatTiles(name); ++n) {
        const std::string file = std::format("Pictures/Systems/{}Tile{}.bmp", name, n);
        if (!files_.findPicture(file)) break;
        if (Picture tile = loadPicture(file, false); tile && tile.image.width >= kTile && tile.image.height >= kTile) tiles.push_back(std::move(tile.image));
    }
    assets::Image picture;
    if (!tiles.empty()) {
        // Larger tiles (a mod's): the picture at the resolution of the
        // smallest, up to the detail; every other tile made to that size.
        int tile = INT_MAX;
        for (const assets::Image& t : tiles) tile = std::min({tile, t.width, t.height});
        tile = std::clamp(tile, kTile, std::max(kTile, int(std::ceil(float(kTile) * detail_))));
        for (assets::Image& t : tiles)
            if (t.width != tile || t.height != tile) t = assets::downscale(t, tile, tile);
        const int size = kSize / kTile * tile;
        picture.width = picture.height = size;
        picture.rgba.assign(size_t(size) * size_t(size) * 4, 255);
        Rng rng(seed);
        for (int ty = 0; ty < kSize / kTile; ++ty)
            for (int tx = 0; tx < kSize / kTile; ++tx) {
                const assets::Image& t = tiles[static_cast<size_t>(rng.below(tiles.size()))];
                for (int y = 0; y < tile; ++y)
                    std::memcpy(&picture.rgba[(size_t(ty * tile + y) * size_t(size) + size_t(tx * tile)) * 4], &t.rgba[size_t(y) * size_t(t.width) * 4],
                                size_t(tile) * 4);
            }
    } else if (const Picture& sky = sheet("Pictures/Systems/1024X768/Starmap.bmp", false)) {
        // Its top left 432x432 classic pixels.
        const int w = int(std::lround(double(kSize) * sky.image.width / sky.width)), h = int(std::lround(double(kSize) * sky.image.height / sky.height));
        picture = assets::crop(sky.image, 0, 0, w, h);
    }
    Texture t;
    if (!picture.empty()) t = makeTexture(picture, kSize, kSize, key);
    textures_.emplace(key, t);
    return t.id ? whole(t.id, t.width, t.height) : Sprite{};
}

void Art::setColorSource(Art* art) { gColorSource = art; }

Art* Art::colorSource() { return gColorSource; }

} // namespace opense4::client::classic
