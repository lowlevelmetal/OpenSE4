// Mods' pictures beyond the original's formats (docs/sdk/packages-and-data.md
// "Pictures"): a PNG next to a BMP of the same name, in each layer and across
// layers; a PNG's own transparency; pictures larger than the classic ones of
// their kind kept at the classic size in the layout, their textures made down
// to the frame's resolution (client/classic/art.hpp, through a device that
// only records textures). Every picture is made by the test.

#include "image_files.hpp"
#include "temp_dir.hpp"

#include "assets/assets.hpp"
#include "client/classic/art.hpp"
#include "ruleset/ruleset.hpp"

#include <doctest/doctest.h>

#include <filesystem>
#include <map>
#include <string>

using namespace opense4;
using namespace opense4::test;
namespace fs = std::filesystem;
using client::classic::Art;
using client::classic::Sprite;

namespace {

// A device that keeps each texture's pixels in memory.
class RecordingDevice final : public gfx::Device {
public:
    struct Texture {
        int width = 0, height = 0;
        std::vector<uint8_t> rgba;
        gfx::Filter filter = gfx::Filter::Linear;
    };
    gfx::Backend backend() const override { return gfx::Backend::OpenGL; }
    const std::string& deviceName() const override { return name_; }
    gfx::TextureId createTexture(const gfx::TextureDesc& d, const void* rgba) override {
        Texture t{d.width, d.height, {}, d.filter};
        if (rgba) t.rgba.assign(static_cast<const uint8_t*>(rgba), static_cast<const uint8_t*>(rgba) + size_t(d.width) * size_t(d.height) * 4);
        textures_[++next_] = std::move(t);
        ++created;
        return gfx::TextureId{next_};
    }
    void updateTexture(gfx::TextureId, int, int, int, int, const void*, int) override {}
    void destroyTexture(gfx::TextureId id) override {
        textures_.erase(id.value);
        ++destroyed;
    }
    std::optional<gfx::FrameInfo> beginFrame(Color) override { return std::nullopt; }
    void draw(const gfx::DrawBatch&) override {}
    void endFrame() override {}
    void setVSync(bool) override {}
    void requestCapture() override {}
    std::optional<gfx::Image> takeCapture() override { return std::nullopt; }
    void waitIdle() override {}

    const Texture& texture(const Sprite& s) const { return textures_.at(s.tex.value); }
    int created = 0, destroyed = 0;

private:
    std::string name_ = "recording";
    std::map<uint32_t, Texture> textures_;
    uint32_t next_ = 0;
};

const uint8_t* pixelAt(const RecordingDevice::Texture& t, int x, int y) { return &t.rgba[(size_t(y) * size_t(t.width) + size_t(x)) * 4]; }

ruleset::VehicleSize hull(std::string bitmap) {
    ruleset::VehicleSize h;
    h.primaryBitmap = bitmap;
    h.alternateBitmap = std::move(bitmap);
    return h;
}

} // namespace

TEST_CASE("pictures: a PNG beside a BMP of the same name, the PNG first in each layer, a later layer first") {
    TempDir dir("pictures_find");
    const fs::path install = dir.path() / "install", a = dir.path() / "a", b = dir.path() / "b";
    writeBmp(install / "Pictures/Events/Storm.bmp", 4, 4, solid(10, 10, 10));
    writeBmp(install / "Pictures/Events/Calm.bmp", 4, 4, solid(20, 20, 20));
    writePng(install / "Pictures/Events/Calm.png", 4, 4, solid(30, 30, 30));
    writePng(a / "pictures/events/storm.PNG", 4, 4, solid(40, 40, 40));
    writeBmp(b / "Pictures/Events/Storm.bmp", 4, 4, solid(50, 50, 50));
    writeBytes(install / "Sounds/ping.wav", {1});
    writeBytes(a / "Sounds/Ping.ogg", {2});
    writeBytes(install / "Music/Theme.mp3", {3});
    writeBytes(b / "Music/Theme.ogg", {4});
    writeBytes(b / "Music/Theme.mp3", {5});

    assets::InstallFiles files(install);
    // The install alone: the PNG of the same folder first.
    CHECK(files.findPicture("Pictures/Events/Calm.bmp")->filename() == "Calm.png");
    CHECK(files.findPicture("Pictures/Events/Storm.bmp")->filename() == "Storm.bmp");
    files.addLayer(a, "a");
    bool installed = true;
    CHECK(files.findPicture("Pictures/Events/Storm.bmp", &installed)->filename() == "storm.PNG");
    CHECK_FALSE(installed);
    files.addLayer(b, "b");
    // The later layer wins, whatever the formats.
    CHECK(files.findPicture("PICTURES/events/storm.bmp", &installed)->generic_string().find("/b/") != std::string::npos);
    CHECK(files.findPicture("Pictures/Events/Calm.bmp", &installed)->filename() == "Calm.png");
    CHECK(installed);
    // The install's own picture, whatever the mods hold.
    CHECK(files.findInstalledPicture("Pictures/Events/Storm.bmp")->generic_string().find("/install/") != std::string::npos);
    CHECK_FALSE(files.findPicture("Pictures/Events/Nothing.bmp"));
    // A name asked for as a PNG is that file only.
    CHECK(files.findPicture("Pictures/Events/Calm.png")->filename() == "Calm.png");
    // Sounds and music: an OGG of the same base name first in each layer.
    CHECK(files.findSound("Sounds/ping.wav")->filename() == "Ping.ogg");
    CHECK(files.findSound("Music/Theme.mp3")->filename() == "Theme.ogg");
    CHECK(files.findSound("Sounds/other.wav") == std::nullopt);
    // Several names of one sound: a mod's under the second name before the install's under the first.
    writeBytes(install / "Sounds/New/ping.wav", {6});
    const std::vector<std::string> names{"Sounds/New/ping.wav", "Sounds/ping.wav"};
    CHECK(files.findSoundAmong(names)->filename() == "Ping.ogg");
    CHECK(assets::InstallFiles(install).findSoundAmong(names)->generic_string().ends_with("New/ping.wav"));
    // find() itself is as it was: the name as given.
    CHECK(files.find("Sounds/ping.wav")->filename() == "ping.wav");
    // The mods' files under a folder, as spelled.
    CHECK(files.layerFiles("Pictures/Events") == std::vector<std::string>{"Pictures/Events/Storm.bmp", "pictures/events/storm.PNG"});
}

TEST_CASE("pictures: a PNG keeps its transparency; black is transparent only in the classic formats") {
    TempDir dir("pictures_alpha");
    auto pixels = [](int x, int) -> std::array<uint8_t, 4> {
        if (x == 0) return {0, 0, 0, 255};      // opaque black
        if (x == 1) return {200, 100, 50, 128};  // half transparent
        return {0, 0, 0, 0};
    };
    writePng(dir.path() / "a.png", 3, 1, pixels);
    writeBmp(dir.path() / "a.bmp", 3, 1, pixels);
    writeBytes(dir.path() / "renamed.bmp", pngBytes(3, 1, pixels));   // PNG data under a classic name

    const auto png = assets::loadImage(dir.path() / "a.png", true);
    REQUIRE(png);
    CHECK(png->rgba[3] == 255);
    CHECK(png->rgba[7] == 128);
    CHECK(png->rgba[11] == 0);
    const auto renamed = assets::loadImage(dir.path() / "renamed.bmp", true);
    REQUIRE(renamed);
    CHECK(renamed->rgba == png->rgba);
    const auto bmp = assets::loadImage(dir.path() / "a.bmp", true);
    REQUIRE(bmp);
    CHECK(bmp->rgba[3] == 0);   // black: transparent
    CHECK(bmp->rgba[7] == 255);
    CHECK(assets::loadImage(dir.path() / "a.bmp", false)->rgba[3] == 255);

    const auto info = assets::probeImage(dir.path() / "renamed.bmp");
    REQUIRE(info);
    CHECK(info->format == assets::ImageFormat::Png);
    CHECK(info->alpha);
    CHECK(info->width == 3);
    CHECK(assets::probeImage(dir.path() / "a.bmp")->format == assets::ImageFormat::Bmp);
    CHECK_FALSE(assets::probeImage(dir.path() / "a.bmp")->alpha);
    CHECK(assets::probeImageSize(dir.path() / "a.png") == std::pair{3, 1});
    writeBytes(dir.path() / "broken.png", {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n', 0, 0, 0});
    CHECK_FALSE(assets::probeImage(dir.path() / "broken.png"));
    writeBytes(dir.path() / "text.bmp", {'h', 'i'});
    CHECK(assets::probeImage(dir.path() / "text.bmp").error().find("not a BMP, PNG or JPEG") != std::string::npos);
}

TEST_CASE("pictures: made smaller by averaging, transparent pixels not darkening the edges") {
    assets::Image img;
    img.width = 4;
    img.height = 2;
    // Left half red, right half blue; the right column transparent black.
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 4; ++x) {
            const bool red = x < 2, clear = x == 3;
            img.rgba.insert(img.rgba.end(), {uint8_t(clear ? 0 : red ? 240 : 0), 0, uint8_t(clear || red ? 0 : 240), uint8_t(clear ? 0 : 255)});
        }
    const assets::Image half = assets::downscale(img, 2, 1);
    REQUIRE(half.width == 2);
    REQUIRE(half.rgba.size() == 8);
    CHECK(std::vector<uint8_t>(half.rgba.begin(), half.rgba.begin() + 4) == std::vector<uint8_t>{240, 0, 0, 255});
    // Blue and transparent: blue at half cover, not a darker blue.
    CHECK(std::vector<uint8_t>(half.rgba.begin() + 4, half.rgba.end()) == std::vector<uint8_t>{0, 0, 240, 128});
    // Three to two: each target pixel takes a pixel and a half.
    const assets::Image third = assets::downscale(img, 3, 2);
    CHECK(third.rgba[0] == 240);
    CHECK(third.rgba[4] == 120);   // two thirds of a red pixel and two thirds of a blue one
    CHECK(third.rgba[6] == 120);
}

TEST_CASE("pictures: the classic size of each kind") {
    CHECK(Art::classicSize("Pictures/Races/Terran/Terran_Mini_Frigate.bmp") == Vec2{36, 36});
    CHECK(Art::classicSize("Pictures/RaceGeneric/Generic_Portrait_Frigate.bmp") == Vec2{128, 128});
    CHECK(Art::classicSize("Pictures/RaceNeutral/Drift/Drift_Race_Portrait.bmp") == Vec2{128, 128});
    CHECK(Art::classicSize("Pictures/Races/Terran/Terran_Pop_Mini.bmp") == Vec2{20, 20});
    CHECK(Art::classicSize("Pictures/Races/Terran/Terran_Pop_Portrait.bmp") == Vec2{36, 36});
    CHECK(Art::classicSize("Pictures/Races/Terran/Terran_Main.bmp") == Vec2{100, 20});
    CHECK(Art::classicSize("Pictures/Components/Comp_012.bmp") == Vec2{128, 128});
    CHECK(Art::classicSize("Pictures/Facilities/Facil_003.bmp") == Vec2{128, 128});
    CHECK(Art::classicSize("Pictures/Planets/p0007.bmp") == Vec2{128, 128});
    CHECK(Art::classicSize("Pictures/Events/Plague.bmp") == Vec2{128, 128});
    CHECK(Art::classicSize("Pictures/Systems/Nebula.bmp") == Vec2{128, 128});
    CHECK(Art::classicSize("Pictures/Systems/NebulaTile3.bmp") == Vec2{72, 72});
    CHECK(Art::classicSize("Pictures/Systems/1024X768/Nebula.bmp") == Vec2{660, 660});
    CHECK(Art::classicSize("Pictures/Systems/800X600/Nebula.bmp") == Vec2{490, 490});
    // Sheets and frame pieces: the install's own copy decides.
    CHECK(Art::classicSize("Pictures/Components/Components.bmp") == Vec2{});
    CHECK(Art::classicSize("Pictures/Game/General.bmp") == Vec2{});
}

TEST_CASE("pictures: a larger picture keeps the classic size, its texture made down to the frame's resolution") {
    TempDir dir("pictures_art");
    const fs::path install = dir.path() / "install", mod = dir.path() / "mod";
    writeBmp(install / "Pictures/RaceGeneric/Generic_Portrait_Frigate.bmp", 128, 128, solid(0, 80, 0));
    writeBmp(install / "Pictures/RaceGeneric/Generic_Mini_Frigate.bmp", 36, 36, solid(0, 80, 0));
    // A sheet of two 36x36 cells, red then green.
    writeBmp(install / "Pictures/Components/Components.bmp", 72, 36, [](int x, int) { return std::array<uint8_t, 4>{uint8_t(x < 36 ? 255 : 0), uint8_t(x < 36 ? 0 : 255), 0, 255}; });
    writeBmp(install / "Pictures/Events/Plague.bmp", 128, 128, solid(1, 2, 3));
    // The mod's: a portrait at twice the size, a mini at twice, the sheet at twice (blue then white), an event at 300x300.
    writePng(mod / "Pictures/RaceGeneric/Generic_Portrait_Frigate.png", 256, 256, [](int x, int) {
        return std::array<uint8_t, 4>{uint8_t(x % 2 ? 200 : 100), 0, 0, 255};
    });
    writePng(mod / "Pictures/RaceGeneric/Generic_Mini_Frigate.png", 72, 72, solid(9, 9, 200));
    writePng(mod / "Pictures/Components/Components.png", 144, 72, [](int x, int) { return std::array<uint8_t, 4>{uint8_t(x < 72 ? 0 : 255), uint8_t(x < 72 ? 0 : 255), 255, 255}; });
    writePng(mod / "Pictures/Events/Plague.png", 300, 300, solid(50, 60, 70));
    // A new race's emblem picture at twice the size, its colour swatch at (28,13) in classic pixels.
    writePng(mod / "Pictures/Races/Zorg/Zorg_Main.png", 200, 40,
             [](int x, int y) { return x >= 56 && x < 58 && y >= 26 && y < 28 ? std::array<uint8_t, 4>{12, 34, 56, 255} : std::array<uint8_t, 4>{0, 0, 0, 255}; });

    RecordingDevice device;
    assets::InstallFiles files(install);
    files.addLayer(mod, "mod");
    Art art(device, std::move(files));
    const ruleset::VehicleSize frigate = hull("Frigate");

    // The portrait: 128x128 in the layout, its texture 128x128 at a detail of 1 (averaged: no odd columns left).
    Sprite portrait = art.shipPortrait("Terran", frigate);
    REQUIRE(portrait);
    CHECK(portrait.size == Vec2{128, 128});
    CHECK(device.texture(portrait).width == 128);
    CHECK(pixelAt(device.texture(portrait), 5, 5)[0] == 150);
    // Twice the frame's resolution: the whole picture; and back.
    art.setDetail(2.0f);
    portrait = art.shipPortrait("Terran", frigate);
    CHECK(portrait.size == Vec2{128, 128});
    CHECK(device.texture(portrait).width == 256);
    art.setDetail(1.6f);   // in steps of a half: 2
    CHECK(art.detail() == 2.0f);
    CHECK(device.texture(art.shipPortrait("Terran", frigate)).width == 256);
    art.setDetail(1.2f);
    CHECK(art.detail() == 1.5f);
    CHECK(device.texture(art.shipPortrait("Terran", frigate)).width == 192);
    // An event picture of 300x300: at 128 classic pixels, its texture at most the detail allows.
    const Sprite plague = art.eventPicture("Plague");
    CHECK(plague.size == Vec2{128, 128});
    CHECK(device.texture(plague).width == 192);
    // The mini, turned too.
    const Sprite mini = art.shipMini("Terran", frigate, true, 2);
    CHECK(mini.size == Vec2{36, 36});
    CHECK(device.texture(mini).width == 54);
    art.setDetail(1.0f);
    // A sheet at twice the install's size: cells at the classic places, from the larger picture.
    const Sprite second = art.component(2);
    REQUIRE(second);
    CHECK(second.size == Vec2{36, 36});
    CHECK(device.texture(second).width == 36);
    CHECK(pixelAt(device.texture(second), 10, 10)[0] == 255);   // white: the mod's second cell
    CHECK(pixelAt(device.texture(second), 10, 10)[1] == 255);
    const Sprite first = art.component(1);
    CHECK(pixelAt(device.texture(first), 10, 10)[2] == 255);
    CHECK(pixelAt(device.texture(first), 10, 10)[0] == 0);
    // The swatch at the classic place of the larger emblem picture.
    CHECK(art.swatchColor("Zorg") == 0x0c2238u);
    // The flag: the classic 26x18 part of it.
    CHECK(art.flag("Zorg").size == Vec2{26, 18});
}

TEST_CASE("pictures: the install's own pictures are drawn as they were") {
    TempDir dir("pictures_classic");
    const fs::path install = dir.path() / "install";
    writeBmp(install / "Pictures/RaceGeneric/Generic_Portrait_Frigate.bmp", 128, 128, solid(0, 80, 0));
    writeBmp(install / "Pictures/Game/General.bmp", 64, 64, solid(7, 8, 9));
    RecordingDevice device;
    Art art(device, assets::InstallFiles(install));
    art.setDetail(3.0f);
    const Sprite portrait = art.shipPortrait("Terran", hull("Frigate"));
    CHECK(portrait.size == Vec2{128, 128});
    CHECK(device.texture(portrait).width == 128);
    const Sprite icon = art.region("Pictures/Game/General.bmp", 16, 0, 16, 16);
    CHECK(icon.size == Vec2{16, 16});
    CHECK(device.texture(icon).width == 16);
    const int before = device.created;
    art.setDetail(1.0f);   // nothing to remake
    CHECK(art.shipPortrait("Terran", hull("Frigate")).tex == portrait.tex);
    CHECK(device.created == before);
}

TEST_CASE("pictures: a design's own picture, and the hull's where this computer lacks it") {
    TempDir dir("pictures_design");
    const fs::path install = dir.path() / "install", mod = dir.path() / "mod";
    writeBmp(install / "Pictures/RaceGeneric/Generic_Portrait_Frigate.bmp", 128, 128, solid(0, 80, 0));
    writeBmp(install / "Pictures/RaceGeneric/Generic_Mini_Frigate.bmp", 36, 36, solid(0, 80, 0));
    writePng(mod / "Pictures/RaceGeneric/Generic_Mini_Lancer.png", 36, 36, solid(200, 0, 0));
    writePng(mod / "Pictures/RaceGeneric/Generic_Portrait_Lancer.png", 128, 128, solid(200, 0, 0));
    writePng(mod / "Pictures/Races/Terran/Terran_Mini_Wasp.png", 36, 36, solid(0, 0, 200));
    writePng(mod / "Pictures/Races/Terran/Terran_Portrait_Wasp.bmp", 128, 128, solid(0, 0, 200));
    writePng(mod / "Pictures/RaceGeneric/Generic_Mini_Half.png", 36, 36, solid(1, 1, 1));   // no portrait: not offered
    RecordingDevice device;
    assets::InstallFiles files(install);
    files.addLayer(mod, "mod");
    Art art(device, std::move(files));
    const ruleset::VehicleSize frigate = hull("Frigate");
    CHECK(pixelAt(device.texture(art.designPortrait("Terran", frigate, "Lancer")), 0, 0)[0] == 200);
    CHECK(pixelAt(device.texture(art.designMini("Terran", frigate, "Lancer")), 0, 0)[0] == 200);
    CHECK(pixelAt(device.texture(art.designMini("Terran", frigate, "")), 0, 0)[1] == 80);
    CHECK(pixelAt(device.texture(art.designMini("Terran", frigate, "Missing")), 0, 0)[1] == 80);   // an asset mod not here
    CHECK(art.hasShipPicture("Terran", "Wasp"));
    CHECK_FALSE(art.hasShipPicture("Klingle", "Wasp"));
    CHECK(art.modShipPictures("Terran") == std::vector<std::string>{"Lancer", "Wasp"});
    CHECK(art.modShipPictures("Other") == std::vector<std::string>{"Lancer"});
}
