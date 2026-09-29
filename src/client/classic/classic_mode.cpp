#include "client/classic/classic_mode.hpp"

#include "core/log.hpp"
#include "datafile/datafile.hpp"

#include <imgui.h>

#include <cmath>
#include <format>

namespace opense4::client {

namespace {

// Classic main-window geometry at 1024×768 (docs/spec/06-ui-and-assets.md §2,
// docs/spec/07-observations.md).
constexpr float kFrameW = 1024.0f;
constexpr float kFrameH = 768.0f;
constexpr Rect kStatusBar{{0, 0}, {1024, 32}};
constexpr Rect kCommandPanel{{0, 32}, {958, 106}};
constexpr Rect kSystemBackground{{3, 105}, {663, 765}};  // 660×660 picture
constexpr Vec2 kSectorOrigin{8, 110};                      // top-left of sector (0,0)
constexpr float kSectorSize = 50.0f;
constexpr float kSpriteSize = 36.0f;
constexpr Rect kReportPanel{{668, 108}, {958, 470}};
constexpr Rect kGalaxyPanel{{668, 497}, {1018, 762}};
constexpr int kSheetCell = 36;

const Color kFrameBlue = Color::hex(0x2c4f9e);
const Color kFrameBright = Color::hex(0x5b86e0);
const Color kLabelBlue = Color::hex(0x6f9cff);
const Color kSelectYellow = Color::hex(0xffd040);

Vec2 sectorCenter(game::Sector s) {
    return kSectorOrigin + Vec2{kSectorSize * (float(s.x) + 0.5f), kSectorSize * (float(s.y) + 0.5f)};
}

ImVec4 im(Color c) { return ImVec4(c.r, c.g, c.b, c.a); }

std::string percentValues(const std::array<int, 3>& v) { return std::format("{}% / {}% / {}%", v[0], v[1], v[2]); }

} // namespace

std::unique_ptr<ClassicMode> ClassicMode::create(const Platform& platform, const ClassicOptions& options, std::string& error) {
    const auto dataDir = ruleset::findInstalledDataDir(options.installDir);
    if (!dataDir) {
        error = options.installDir.empty()
                    ? "Classic mode needs an installed copy of the classic game, and none was found in your Steam libraries.\n"
                      "Pass --classic-dir=<game directory> to point at it."
                    : std::format("No classic data set found at {}.", options.installDir);
        return nullptr;
    }
    auto loaded = ruleset::loadRuleset(*dataDir);
    if (!loaded.ruleset || !loaded.diagnostics.errors.empty()) {
        error = std::format("The data set at {} has errors:\n", dataDir->string());
        for (size_t i = 0; i < std::min<size_t>(loaded.diagnostics.errors.size(), 15); ++i) error += "\n" + loaded.diagnostics.errors[i];
        return nullptr;
    }

    std::unique_ptr<ClassicMode> mode(new ClassicMode(platform));
    mode->rules_ = std::move(*loaded.ruleset);
    mode->files_ = assets::InstallFiles(dataDir->parent_path());
    log::info("Classic data set: {} ({} components, {} system types)", dataDir->string(), mode->rules_.components.size(),
              mode->rules_.systemTypes.size());

    game::QuadrantOptions q;
    q.quadrantType = options.quadrantType;
    q.systemCount = options.systemCount > 0 ? options.systemCount : 40;
    Rng rng(options.seed);
    auto generated = game::generateQuadrant(mode->rules_, q, rng);
    if (!generated) {
        error = generated.error();
        return nullptr;
    }
    mode->galaxy_ = std::move(generated->galaxy);
    mode->warnings_ = std::move(generated->warnings);
    for (const auto& w : mode->warnings_) log::warn("Generation: {}", w);

    // Placeholder empires until empire setup lands (milestone 2).
    static const std::vector<std::tuple<const char*, const char*, const char*, uint32_t>> kStarts{
        {"Player Empire", "Rock", "Oxygen", 0x3f7fff}, {"Crimson Pact", "Rock", "Carbon Dioxide", 0xe03a3a},
        {"Frost Accord", "Ice", "Methane", 0x3fd07a},  {"Cloud Synod", "Gas Giant", "Hydrogen", 0xe8b030},
        {"Violet Throne", "Rock", "Hydrogen", 0xb060e0}, {"Amber League", "Ice", "Oxygen", 0xe07a30}};
    std::vector<game::EmpireStart> starts;
    for (int i = 0; i < std::clamp(options.empireCount, 1, int(kStarts.size())); ++i) {
        const auto& [name, surface, air, color] = kStarts[size_t(i)];
        starts.push_back({name, surface, air});
        mode->empires_.push_back({name, Color::hex(color), {}});
    }
    auto homes = game::placeHomeworlds(mode->galaxy_, mode->rules_, starts, game::PlacementOptions{}, rng);
    if (!homes) {
        error = homes.error();
        return nullptr;
    }
    for (size_t i = 0; i < homes->size(); ++i) mode->empires_[i].homeworld = (*homes)[i];
    const game::SpaceObject& home = mode->galaxy_.object(mode->empires_.front().homeworld);
    mode->shownSystem_ = home.system;
    mode->selectedSector_ = home.sector;
    mode->selectedObject_ = home.id;

    // Sector-object sprite sheet (docs/spec/06 §5.2): 36 px cells, index = Picture Num.
    mode->planetSheet_ = mode->loadTexture("planets", {"Pictures/Planets/Planets.bmp"}, true);
    if (auto path = mode->files_.find("Pictures/Planets/Planets.bmp"))
        if (auto img = assets::loadImage(*path, false)) {
            mode->sheetWidth_ = img->width;
            mode->sheetHeight_ = img->height;
        }
    if (!mode->planetSheet_) log::warn("Planet sprites not found in the install; drawing placeholders");
    return mode;
}

ClassicMode::~ClassicMode() {
    for (const auto& [key, tex] : textures_)
        if (tex) platform_.device->destroyTexture(tex);
}

gfx::TextureId ClassicMode::loadTexture(const std::string& key, std::initializer_list<std::string_view> candidates, bool colorKey) {
    if (auto it = textures_.find(key); it != textures_.end()) return it->second;
    gfx::TextureId id;
    if (auto path = files_.findAny(candidates))
        if (auto img = assets::loadImage(*path, colorKey))
            id = platform_.device->createTexture(gfx::TextureDesc{img->width, img->height, gfx::Filter::Linear, key.c_str()}, img->rgba.data());
    textures_[key] = id;  // cache misses too
    return id;
}

gfx::TextureId ClassicMode::systemBackground(const game::StarSystem& sys) {
    const std::string& name = rules_.systemTypes[sys.type.index()].backgroundBitmap;
    const std::string file = name.ends_with(".bmp") || name.ends_with(".BMP") ? name : name + ".bmp";
    const std::string a = "Pictures/Systems/1024X768/" + file;
    const std::string b = "Pictures/Game/Screens/1024X768/" + file;
    gfx::TextureId tex = loadTexture("bg:" + name, {a, b}, false);
    if (!tex) tex = loadTexture("bg:Starmap", {"Pictures/Game/Screens/1024X768/Starmap.bmp"}, false);
    return tex;
}

gfx::TextureId ClassicMode::portrait(const game::SpaceObject& obj) {
    const int picture = rules_.sectorObjectTypes[obj.sectorType].picture;
    const std::string file = std::format("Pictures/Planets/p{:04d}.bmp", picture + 1);  // portraits count from 1
    return loadTexture("portrait:" + file, {file}, true);
}

std::vector<game::ObjectId> ClassicMode::objectsAt(game::Sector s) const {
    std::vector<game::ObjectId> out;
    for (game::ObjectId id : galaxy_.system(shownSystem_).objects)
        if (galaxy_.object(id).sector == s) out.push_back(id);
    return out;
}

bool ClassicMode::update(const FrameState& fs) {
    const float fw = float(fs.frame.width), fh = float(fs.frame.height);
    mapping_.scale = std::min(fw / kFrameW, fh / kFrameH);
    mapping_.offset = {(fw - kFrameW * mapping_.scale) * 0.5f, (fh - kFrameH * mapping_.scale) * 0.5f};
    handleInput(fs);
    drawText(fs);
    reportPanel(fs);
    return !ImGui::IsKeyPressed(ImGuiKey_Escape, false);
}

void ClassicMode::handleInput(const FrameState& fs) {
    const ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureMouse || !ImGui::IsMousePosValid() || !ImGui::IsMouseClicked(ImGuiMouseButton_Left)) return;
    const Vec2 p = mapping_.fromFb(Vec2{io.MousePos.x, io.MousePos.y} * fs.fbScale);

    if (kGalaxyPanel.contains(p)) {
        std::optional<game::SystemId> best;
        float bestDist = 10.0f;
        const Rect inner = kGalaxyPanel.expanded(-8.0f);
        const float sx = inner.size().x / float(std::max(1, galaxy_.width)), sy = inner.size().y / float(std::max(1, galaxy_.height));
        for (const game::StarSystem& sys : galaxy_.systems) {
            const Vec2 c = inner.min + Vec2{(float(sys.position.x) + 0.5f) * sx, (float(sys.position.y) + 0.5f) * sy};
            if (const float d = distance(c, p); d < bestDist) {
                bestDist = d;
                best = sys.id;
            }
        }
        if (best) {
            shownSystem_ = *best;
            selectedSector_.reset();
            selectedObject_.reset();
        }
        return;
    }

    const Vec2 rel = (p - kSectorOrigin) / kSectorSize;
    const game::Sector s{int(std::floor(rel.x)), int(std::floor(rel.y))};
    if (rel.x >= 0 && rel.y >= 0 && s.valid()) {
        const auto here = objectsAt(s);
        if (here.empty()) {
            selectedSector_.reset();
            selectedObject_.reset();
            return;
        }
        // Clicking the same sector again cycles through its objects.
        auto it = selectedObject_ ? std::find(here.begin(), here.end(), *selectedObject_) : here.end();
        selectedObject_ = (it == here.end() || std::next(it) == here.end()) ? here.front() : *std::next(it);
        selectedSector_ = s;
    }
}

void ClassicMode::render(gfx::Renderer2D& r, const FrameState& fs) {
    const float fw = float(fs.frame.width), fh = float(fs.frame.height);
    // Frame coordinates -> clip space, letterboxed and centered.
    const Vec2 topLeft = mapping_.fromFb({0, 0});
    const Vec2 bottomRight = mapping_.fromFb({fw, fh});
    r.begin(Mat4::ortho2D(topLeft.x, bottomRight.x, topLeft.y, bottomRight.y), fs.frame, mapping_.scale);
    drawFrame(r);
    drawSystemPanel(r, fs.time);
    drawGalaxyPanel(r);
    r.flush();
}

void ClassicMode::drawFrame(gfx::Renderer2D& r) {
    r.rect(Rect{{0, 0}, {kFrameW, kFrameH}}, Color::hex(0x02040a));
    r.rect(kStatusBar, Color::hex(0x060c1c));
    r.rect(kCommandPanel, Color::hex(0x0a1224));
    // Placeholder command buttons (12 command + a paged order strip), classic proportions.
    for (int row = 0; row < 2; ++row)
        for (int col = 0; col < 6; ++col)
            r.rectOutline(Rect::fromPosSize({14.0f + float(col) * 36.0f, 36.0f + float(row) * 36.0f}, {34, 34}), 1.0f, kFrameBlue);
    for (int row = 0; row < 2; ++row)
        for (int col = 0; col < 18; ++col)
            r.rectOutline(Rect::fromPosSize({250.0f + float(col) * 36.0f, 36.0f + float(row) * 36.0f}, {34, 34}), 1.0f, kFrameBlue.withAlpha(0.5f));
    for (const Rect& panel : {kStatusBar, kCommandPanel, kSystemBackground, kReportPanel, kGalaxyPanel})
        r.rectOutline(panel, 1.5f, kFrameBright);
    r.rect(Rect{{960, 108}, {1022, 470}}, Color::hex(0x07101f));  // right-hand strip
    r.rectOutline(Rect{{0, 0}, {kFrameW, kFrameH}}, 2.0f, kFrameBlue);
}

void ClassicMode::drawSystemPanel(gfx::Renderer2D& r, double time) {
    const game::StarSystem& sys = galaxy_.system(shownSystem_);
    if (gfx::TextureId bg = systemBackground(sys)) r.sprite(bg, kSystemBackground, Rect{{0, 0}, {1, 1}});
    else r.rect(kSystemBackground, Color::hex(0x000000));

    // One sprite per occupied sector (the first object), like the original.
    std::map<game::Sector, std::vector<game::ObjectId>> bySector;
    for (game::ObjectId id : sys.objects) bySector[galaxy_.object(id).sector].push_back(id);
    for (const auto& [sector, ids] : bySector) {
        // Prefer showing the most prominent object in a stack.
        game::ObjectId shown = ids.front();
        for (game::ObjectId id : ids)
            if (galaxy_.object(id).kind == game::ObjectKind::Star) shown = id;
        const game::SpaceObject& o = galaxy_.object(shown);
        const Vec2 c = sectorCenter(sector);
        const Rect dst = Rect::fromCenter(c, {kSpriteSize * 0.5f, kSpriteSize * 0.5f});
        const int pic = rules_.sectorObjectTypes[o.sectorType].picture;
        const int cols = sheetWidth_ / kSheetCell;
        if (planetSheet_ && cols > 0) {
            const float u0 = float((pic % cols) * kSheetCell) / float(sheetWidth_);
            const float v0 = float((pic / cols) * kSheetCell) / float(sheetHeight_);
            r.sprite(planetSheet_, dst, Rect{{u0, v0}, {u0 + float(kSheetCell) / float(sheetWidth_), v0 + float(kSheetCell) / float(sheetHeight_)}});
        } else {
            r.disc(c, 12.0f, o.kind == game::ObjectKind::Star ? Color::hex(0xffe080) : Color::hex(0x8090a0));
        }
        // Homeworlds get their owner's color bar at the top right (population bar in the original).
        for (const Empire& e : empires_)
            if (e.homeworld == o.id) r.rect(Rect::fromPosSize(c + Vec2{12, -18}, {6, 3}), e.color);
    }

    if (selectedSector_) {
        // Four yellow corner brackets around the selected sector.
        const Vec2 c = sectorCenter(*selectedSector_);
        const float h = kSectorSize * 0.5f - 2.0f, l = 8.0f;
        const float pulse = 0.75f + 0.25f * float(std::sin(time * 5.0));
        const Color y = kSelectYellow.withAlpha(pulse);
        for (float sx : {-1.0f, 1.0f})
            for (float sy : {-1.0f, 1.0f}) {
                const Vec2 corner = c + Vec2{sx * h, sy * h};
                r.line(corner, corner - Vec2{sx * l, 0}, 2.0f, y);
                r.line(corner, corner - Vec2{0, sy * l}, 2.0f, y);
            }
    }
}

void ClassicMode::drawGalaxyPanel(gfx::Renderer2D& r) {
    r.rect(kGalaxyPanel, Color::hex(0x02050c));
    const Rect inner = kGalaxyPanel.expanded(-8.0f);
    const float sx = inner.size().x / float(std::max(1, galaxy_.width)), sy = inner.size().y / float(std::max(1, galaxy_.height));
    // Faint quadrant grid.
    for (int x = 0; x <= galaxy_.width; x += 2)
        r.line({inner.min.x + float(x) * sx, inner.min.y}, {inner.min.x + float(x) * sx, inner.max.y}, 1.0f, Color::hex(0x10224a, 0.8f));
    for (int y = 0; y <= galaxy_.height; y += 2)
        r.line({inner.min.x, inner.min.y + float(y) * sy}, {inner.max.x, inner.min.y + float(y) * sy}, 1.0f, Color::hex(0x10224a, 0.8f));
    auto pos = [&](const game::StarSystem& s) {
        return inner.min + Vec2{(float(s.position.x) + 0.5f) * sx, (float(s.position.y) + 0.5f) * sy};
    };
    for (const game::SpaceObject& o : galaxy_.objects) {
        if (o.kind != game::ObjectKind::WarpPoint || !o.destination.valid() || o.destination < o.id) continue;
        r.line(pos(galaxy_.system(o.system)), pos(galaxy_.system(galaxy_.object(o.destination).system)), 1.0f, Color::hex(0x4868a8, 0.9f));
    }
    for (const game::StarSystem& s : galaxy_.systems) {
        Color c = Color::hex(0xc8d0dc);
        for (const Empire& e : empires_)
            if (galaxy_.object(e.homeworld).system == s.id) c = e.color;
        r.ring(pos(s), 3.2f, 1.5f, c);
        if (s.id == shownSystem_) {
            r.disc(pos(s), 2.0f, kSelectYellow);
            r.ring(pos(s), 6.0f, 1.5f, kSelectYellow);
        }
    }
}

void ClassicMode::drawText(const FrameState& fs) {
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const float k = mapping_.scale / fs.fbScale;  // frame px -> ImGui units
    auto at = [&](Vec2 p) {
        const Vec2 q = mapping_.toFb(p) / fs.fbScale;
        return ImVec2{q.x, q.y};
    };
    ImFont* font = platform_.fonts->medium;
    auto text = [&](Vec2 p, float size, Color c, const std::string& s) { dl->AddText(font, size * k, at(p), c.toRgba8(), s.c_str()); };

    const Empire& me = empires_.front();
    text({36, 8}, 14, Color::hex(0xe8eefc), me.name);
    text({300, 8}, 14, kLabelBlue, "Game Date");
    text({385, 8}, 14, Color::hex(0xe8eefc), "2400.0");
    text({560, 8}, 14, Color::hex(0x5b9cff), "Minerals -");
    text({680, 8}, 14, Color::hex(0x50d060), "Organics -");
    text({800, 8}, 14, Color::hex(0xff5050), "Radioactives -");

    const game::StarSystem& sys = galaxy_.system(shownSystem_);
    text({14, 114}, 16, Color::hex(0xffffff), sys.name);

    // Stack counts and warp point destinations, as in the original system panel.
    std::map<game::Sector, int> counts;
    for (game::ObjectId id : sys.objects) ++counts[galaxy_.object(id).sector];
    for (const auto& [sector, n] : counts)
        if (n > 1) text(sectorCenter(sector) + Vec2{10, 8}, 11, Color::hex(0xffffff), std::to_string(n));
    for (game::ObjectId id : galaxy_.warpPoints(sys.id)) {
        const game::SpaceObject& wp = galaxy_.object(id);
        if (!wp.destination.valid()) continue;
        const std::string& dest = galaxy_.system(galaxy_.object(wp.destination).system).name;
        const ImVec2 size = font->CalcTextSizeA(11 * k, FLT_MAX, 0.0f, dest.c_str());
        const Vec2 c = sectorCenter(wp.sector);
        dl->AddText(font, 11 * k, ImVec2{at(c).x - size.x * 0.5f, at(c + Vec2{0, 18}).y}, Color::hex(0xb8c8ff).toRgba8(), dest.c_str());
    }
    text({676, 500}, 12, kLabelBlue, std::format("Quadrant: {} ({} systems)", galaxy_.quadrantType, galaxy_.systems.size()));
}

void ClassicMode::reportPanel(const FrameState& fs) {
    const float k = mapping_.scale / fs.fbScale;
    const Vec2 pos = mapping_.toFb(kReportPanel.min + Vec2{6, 6}) / fs.fbScale;
    const Vec2 size = kReportPanel.size() * k - Vec2{12, 12} * k;
    ImGui::SetNextWindowPos(ImVec2{pos.x, pos.y});
    ImGui::SetNextWindowSize(ImVec2{size.x, size.y});
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(2, 2));
    ImGui::Begin("##classic_report", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground);
    ImGui::PushFont(platform_.fonts->regular, 13.0f * k);
    auto field = [&](const char* label, const std::string& value) {
        ImGui::TextColored(im(kLabelBlue), "%s", label);
        ImGui::SameLine(95.0f * k);
        ImGui::TextUnformatted(value.c_str());
    };

    const game::StarSystem& sys = galaxy_.system(shownSystem_);
    if (!selectedObject_) {
        const ruleset::SystemType& type = rules_.systemTypes[sys.type.index()];
        ImGui::PushFont(platform_.fonts->bold, 16.0f * k);
        ImGui::TextUnformatted(sys.name.c_str());
        ImGui::PopFont();
        field("System Type", type.name);
        field("Location", std::format("{}, {}", sys.position.x, sys.position.y));
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("%s", type.description.c_str());
        ImGui::PopTextWrapPos();
        for (const auto& a : sys.abilities) ImGui::BulletText("%s %s", a.type.c_str(), a.value1.c_str());
        ImGui::Spacing();
        ImGui::TextColored(im(kLabelBlue), "Objects");
        for (game::ObjectId id : sys.objects) {
            const game::SpaceObject& o = galaxy_.object(id);
            if (ImGui::Selectable(std::format("{}##{}", o.name, id.value).c_str())) {
                selectedObject_ = id;
                selectedSector_ = o.sector;
            }
        }
    } else {
        const game::SpaceObject& o = galaxy_.object(*selectedObject_);
        const ruleset::SectorObjectType& st = rules_.sectorObjectTypes[o.sectorType];
        if (gfx::TextureId tex = portrait(o)) {
            ImGui::Image(ImTextureRef(static_cast<ImTextureID>(tex.value)), ImVec2(96 * k, 96 * k));
            ImGui::SameLine();
        }
        ImGui::BeginGroup();
        ImGui::PushFont(platform_.fonts->bold, 16.0f * k);
        ImGui::TextUnformatted(o.name.c_str());
        ImGui::PopFont();
        switch (o.kind) {
            case game::ObjectKind::Planet:
            case game::ObjectKind::Asteroids:
                ImGui::TextColored(im(kLabelBlue), "Type");
                ImGui::Text("  %s - %s", o.surface.c_str(), o.size.c_str());
                ImGui::TextColored(im(kLabelBlue), "Atmosphere");
                ImGui::Text("  %s", o.atmosphere.c_str());
                ImGui::TextColored(im(kLabelBlue), "Conditions");
                ImGui::Text("  %d%%", o.conditions);
                break;
            case game::ObjectKind::Star:
            case game::ObjectKind::DestroyedStar:
                ImGui::TextColored(im(kLabelBlue), "Star");
                ImGui::Text("  %s, %s", o.starColor.c_str(), o.size.c_str());
                ImGui::Text("  %s age, %s", o.starAge.c_str(), o.starLuminosity.c_str());
                break;
            case game::ObjectKind::WarpPoint:
                ImGui::TextColored(im(kLabelBlue), "Destination");
                ImGui::Text("  %s", o.destination.valid() ? galaxy_.system(galaxy_.object(o.destination).system).name.c_str() : "-");
                break;
            default: ImGui::Text("%s", std::string(game::displayName(o.kind)).c_str()); break;
        }
        ImGui::EndGroup();
        if (o.kind == game::ObjectKind::Planet || o.kind == game::ObjectKind::Asteroids) field("Value", percentValues(o.value));
        for (const Empire& e : empires_)
            if (e.homeworld == o.id) field("Colony Type", std::format("Homeworld of the {}", e.name));
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("%s", st.description.c_str());
        ImGui::PopTextWrapPos();
        for (const auto& a : o.abilities) ImGui::BulletText("%s", a.description.empty() ? a.type.c_str() : a.description.c_str());
        if (ImGui::SmallButton("System report")) selectedObject_.reset();
    }
    ImGui::PopFont();
    ImGui::End();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

} // namespace opense4::client
