#include "client/classic/ui.hpp"

#include "learn/ids.hpp"

#include "client/app_settings.hpp"
#include "client/audio.hpp"
#include "client/script/items.hpp"
#include "client/ui/bitmap_font.hpp"
#include "assets/tiny_font.hpp"

#include "core/log.hpp"

#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <format>
#include <unordered_map>

namespace opense4::client::classic {

namespace {


Rect dialogRect(DialogSize size) {
    // Dialogs have the same size in both layouts, centred on the frame (docs/spec/06
    // §2.1.1); only the four list windows follow the layout.
    const float fw = frameW(), fh = frameH();
    Vec2 s;
    switch (size) {
        case DialogSize::Large: s = {780, 475}; break;
        case DialogSize::Tall: s = {780, listWindowHeight(screenLayout(), fh)}; break;
        case DialogSize::Report: s = {400, 540}; break;
        case DialogSize::Picker: s = {540, 660}; break;
        case DialogSize::Prompt: s = {420, 190}; break;
        case DialogSize::Full: s = {fw, fh}; break;
    }
    // Our own taller windows (the pickers) fit the 800x600 frame (inferred).
    s.y = std::min(s.y, size == DialogSize::Full ? fh : fh - 20.0f);
    const Vec2 min{std::floor((fw - s.x) * 0.5f), std::floor((fh - s.y) * 0.5f)};
    return Rect{min, min + s};
}

} // namespace

void image(UiContext& ui, const Sprite& s, Vec2 frameSize, Color tint) {
    if (!s) {
        ImGui::Dummy(ui.size(frameSize));
        return;
    }
    ImGui::ImageWithBg(ImTextureRef(static_cast<ImTextureID>(s.tex.value)), ui.size(frameSize), ImVec2(s.uv.min.x, s.uv.min.y),
                       ImVec2(s.uv.max.x, s.uv.max.y), ImVec4(0, 0, 0, 0), ImVec4(tint.r, tint.g, tint.b, tint.a));
}

bool imageButton(UiContext& ui, const char* id, const Sprite& s, Vec2 frameSize, bool enabled) {
    ImGui::BeginDisabled(!enabled);
    bool clicked = false;
    if (s)
        clicked = ImGui::ImageButton(id, ImTextureRef(static_cast<ImTextureID>(s.tex.value)), ui.size(frameSize), ImVec2(s.uv.min.x, s.uv.min.y),
                                     ImVec2(s.uv.max.x, s.uv.max.y));
    else
        clicked = ImGui::Button(id, ui.size(frameSize));
    ImGui::EndDisabled();
    return clicked;
}

void resources(UiContext& ui, const game::Resources& r, bool compact) {
    // The classic order: the amount in the resource's colour, then its icon.
    static constexpr std::array<Icon, 3> kIcons{Icon::Minerals, Icon::Organics, Icon::Radioactives};
    static constexpr std::array<uint32_t, 3> kColors{palette::kMinerals, palette::kOrganics, palette::kRadioactives};
    for (size_t i = 0; i < 3; ++i) {
        if (i > 0) ImGui::SameLine(0, ui.px(compact ? 6.0f : 14.0f));
        ImGui::TextColored(imColorV(kColors[i]), "%s", formatNumber(r.v[i]).c_str());
        if (Sprite s = ui.art.icon16(kIcons[i])) {
            ImGui::SameLine(0, ui.px(1));
            image(ui, s, {14, 14});
        }
    }
}

void labelValue(UiContext& ui, const char* label, const std::string& value, float valueColumn) {
    ImGui::TextColored(kLabelBlue, "%s", label);
    ImGui::SameLine(ui.px(valueColumn));
    ImGui::TextUnformatted(value.c_str());
}

void heading(UiContext& ui, const char* text) { heading(ui.painter(), text); }

void heading(const Painter& p, const char* text) {
    ImGui::PushFont(p.fonts.bold, p.fontPx(kTitleSize));
    ImGui::TextColored(imColorV(palette::kHeading), "%s", text);
    ImGui::PopFont();
}

std::string formatNumber(int64_t v) { return std::to_string(v); }

std::string formatDate(uint32_t turn) { return std::format("{}.{}", 2400 + turn / 10, turn % 10); }

uint32_t empireRgb(const game::GameState& s, game::EmpireId e) {
    if (!e.valid() || e.index() >= s.empires.size()) return 0xc8c8c8;
    // The swatch in the race's _Main.bmp (docs/spec/06 §5.3); the setup's colour without the art.
    if (Art* art = Art::colorSource())
        if (auto c = art->swatchColor(s.empire(e).race.style)) return *c;
    return s.empire(e).color;
}

ImU32 empireColor(const game::GameState& s, game::EmpireId e) { return imColor(empireRgb(s, e)); }

FrameMapping frameMappingFor(float fw, float fh) {
    const GraphicsSettings& g = appSettings().graphics;
    const float frameWidth = frameW(), frameHeight = frameH();
    const float maxWide = frameHeight * 21.0f / 9.0f;  // wider screens get bars
    // The 800x600 layout keeps its classic 4:3 frame (inferred: its panels have no wide arrangement).
    const bool extended = g.widescreen == WidescreenLayout::Extended && screenLayout() == ScreenLayout::Large;
    FrameMapping m;
    float width = extended ? std::clamp(frameHeight * fw / std::max(1.0f, fh), frameWidth, maxWide) : frameWidth;
    m.scale = std::min(fw / width, fh / frameHeight);
    if (g.integerScaling && m.scale >= 1.0f) {
        m.scale = std::floor(std::min(fw / frameWidth, fh / frameHeight));
        if (extended) width = std::clamp(fw / m.scale, frameWidth, maxWide);
    }
    m.left = (frameWidth - width) * 0.5f;
    m.right = frameWidth - m.left;
    m.offset = {(fw - width * m.scale) * 0.5f - m.left * m.scale, (fh - frameHeight * m.scale) * 0.5f};
    return m;
}

Fonts loadClassicFonts(const Fonts& app, const assets::InstallFiles& files) {
    // The install's raster fonts, each from the active mod's Fonts folder first,
    // then the base one (docs/spec/06 §5.4). The game registers FutSml, FutMed,
    // SE4 Block 1 Large and SE4 Text button; it never selects the Block face, so
    // only the other three are loaded. Each missing file keeps our own font.
    Fonts out = app;
    out.ownRegular = app.ownRegular ? app.ownRegular : app.regular;
    out.ownBold = app.ownBold ? app.ownBold : app.bold;
    auto load = [&](std::string_view file) -> ImFont* {
        const auto path = files.findModFirst(std::string("Fonts/") + std::string(file));
        if (!path) {
            log::warn("Font Fonts/{} is not in the install; using OpenSE4's own", file);
            return nullptr;
        }
        std::string error;
        auto fonts = assets::loadFon(*path, &error);
        if (fonts.empty()) {
            log::warn("Font {}: {}", path->string(), error);
            return nullptr;
        }
        return addBitmapFont(ImGui::GetIO().Fonts, std::move(fonts.front()));
    };
    ImFont* text = load("FutMed.fon");
    ImFont* small = load("FutSml.fon");
    ImFont* title = load("SE4TXBTN.FON");
    if (text) {
        out.regular = out.medium = text;
        out.bitmap = true;
    }
    out.small = small ? small : out.regular;
    if (title) out.bold = title;
    // The map numbers: our own small raster face for the system's Small Fonts.
    out.tiny = addBitmapFont(ImGui::GetIO().Fonts, assets::makeTinyFont());
    return out;
}

// ---- Classic buttons and frames ---------------------------------------------------------------

namespace {

constexpr const char* kMainParts = "Pictures/Game/Dialogs/MainParts.bmp";

// Lines in whole framebuffer pixels (at least one) so rails stay crisp when scaled.
float lineWidth(const Painter& ui) { return std::max(1.0f, std::floor(ui.map.scale)) / ui.fbScale; }

void frameRect(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 c, float w) { dl->AddRect(a, b, c, 0.0f, w); }

// End of the visible part of a label ("Name##id" shows "Name").
const char* labelEnd(const char* label) {
    const char* hash = std::strstr(label, "##");
    return hash ? hash : label + std::strlen(label);
}

} // namespace

bool classicButton(const Painter& ui, const char* label, Vec2 frameSize, int style, bool on, bool enabled) {
    const ImVec2 a = ImGui::GetCursorScreenPos();
    const ImVec2 size = ui.size(frameSize);
    const ImVec2 b{a.x + size.x, a.y + size.y};
    ImGui::PushID(label);
    ImGui::BeginDisabled(!enabled);
    const bool clicked = ImGui::InvisibleButton("##classic", size);
    ImGui::EndDisabled();
    ImGui::PopID();
    script::reportItem(label);   // input scripts find it by its label
    const bool hovered = enabled && ImGui::IsItemHovered();
    const bool held = hovered && ImGui::IsItemActive();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float lw = lineWidth(ui);
    // The state colour, which the caption and the outline share (docs/spec/06 §5.4);
    // under the pointer and while held the RowGrid texture lies behind the label.
    const uint32_t state = !enabled ? palette::kDisabled : held ? palette::kButtonHeld : hovered ? palette::kButtonHot : palette::kButton;
    const ImU32 line = imColor(state);
    const ImU32 text = line;
    const float h = lw * 0.5f;
    if (hovered || held) {
        const int gw = std::max(1, int(frameSize.x)), gh = std::max(1, int(frameSize.y));
        if (Sprite grid = ui.art.region("Pictures/Game/Dialogs/Rowgrid.bmp", 0, 0, gw, gh, false))
            dl->AddImage(ImTextureRef(static_cast<ImTextureID>(grid.tex.value)), a, b, {grid.uv.min.x, grid.uv.min.y}, {grid.uv.max.x, grid.uv.max.y});
    }
    if (style == 1) {
        // Tabs have the top-right corner cut off.
        const float c = ui.px(9);
        const ImVec2 pts[] = {{a.x + h, a.y + h}, {b.x - c, a.y + h}, {b.x - h, a.y + c}, {b.x - h, b.y - h}, {a.x + h, b.y - h}};
        dl->AddPolyline(pts, 5, line, lw, ImDrawFlags_Closed);
    } else {
        frameRect(dl, {a.x + h, a.y + h}, {b.x - h, b.y - h}, line, lw);
    }

    ImGui::PushFont(ui.fonts.bold, ui.fontPx(kTitleSize));
    const ImVec2 ts = ImGui::CalcTextSize(label, nullptr, true);
    const float lamp = ui.px(13), gap = ui.px(3);
    const float midY = (a.y + b.y) * 0.5f;
    float x;
    if (style == 2) {
        const float box = ui.px(16);
        const ImVec2 b0{a.x + ui.px(4), midY - box * 0.5f};
        frameRect(dl, b0, {b0.x + box, b0.y + box}, imColor(enabled ? palette::kSecondary : palette::kDisabled), lw);
        if (on)
            if (Sprite s = ui.art.region("Pictures/Game/General.bmp", 191, 0, 13, 13))
                dl->AddImage(ImTextureRef(static_cast<ImTextureID>(s.tex.value)), {b0.x + (box - lamp) * 0.5f, b0.y + (box - lamp) * 0.5f},
                             {b0.x + (box + lamp) * 0.5f, b0.y + (box + lamp) * 0.5f}, {s.uv.min.x, s.uv.min.y}, {s.uv.max.x, s.uv.max.y});
        x = b0.x + box + ui.px(4);
        x = std::max(x, (a.x + b.x - ts.x) * 0.5f);
    } else {
        const bool withLamp = style == 1 && on;
        const float group = ts.x + (withLamp ? lamp + gap : 0.0f);
        x = (a.x + b.x - group) * 0.5f;
        if (withLamp) {
            if (Sprite s = ui.art.region("Pictures/Game/General.bmp", 191, 0, 13, 13))
                dl->AddImage(ImTextureRef(static_cast<ImTextureID>(s.tex.value)), {x, midY - lamp * 0.5f}, {x + lamp, midY + lamp * 0.5f},
                             {s.uv.min.x, s.uv.min.y}, {s.uv.max.x, s.uv.max.y});
            x += lamp + gap;
        }
    }
    // Centred across; the top of the text at (button height - text height) / 2 + 2 (spec 06 §5.4).
    const float textH = ts.y / ui.k();
    const float top = a.y + ui.px(std::floor((frameSize.y - textH) * 0.5f) + 2.0f);
    dl->AddText({std::floor(x), std::floor(top)}, text, label, labelEnd(label));
    ImGui::PopFont();
    return clicked && enabled;
}

void combatTitleStrip(UiContext& ui, const Dialog& d, std::string_view location, std::string_view turn, const std::vector<std::string>& flagStyles,
                      int phase) {
    const TitleStrip& t = layoutGeometry().tacticalTitle;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImFont* body = ui.fonts.medium ? ui.fonts.medium : ImGui::GetFont();
    const float size = ui.fontPx(kTextSize);
    // Each text stops short of the next one (ours: a long name would run over it).
    auto text = [&](float x, float until, ImU32 color, std::string_view str) {
        dl->PushClipRect(d.at({x, 4}), d.at({until - 4, 31}), true);
        dl->AddText(body, size, d.at({x, 9 + kTextLead}), color, str.data(), str.data() + str.size());
        dl->PopClipRect();
    };
    text(t.location, t.locationValue, imColor(palette::kLabel), "Location");
    text(t.locationValue, t.turn, IM_COL32_WHITE, location);
    text(t.turn, t.turnValue, imColor(palette::kLabel), "Turn");
    text(t.turnValue, t.empires, IM_COL32_WHITE, turn);
    text(t.empires, t.flags, imColor(palette::kLabel), "Empires");
    const size_t room = frameW() < 1024.0f ? 6 : 10;
    for (size_t i = 0; i < flagStyles.size() && i < room; ++i) {
        const Vec2 at{t.flags + 28.0f * float(i), 7};
        if (const Sprite flag = flagStyles[i].empty() ? Sprite{} : ui.art.flag(flagStyles[i], true))
            dl->AddImage(ImTextureRef(static_cast<ImTextureID>(flag.tex.value)), d.at(at), d.at(at + Vec2{26, 18}), {flag.uv.min.x, flag.uv.min.y},
                         {flag.uv.max.x, flag.uv.max.y});
        if (int(i) == phase) dl->AddRect(d.at(at - Vec2{1, 0}), d.at(at + Vec2{27, 19}), IM_COL32(255, 255, 0, 255));
    }
}

void emptySlot(const Painter& ui, Vec2 frameSize) {
    const ImVec2 a = ImGui::GetCursorScreenPos();
    const ImVec2 size = ui.size(frameSize);
    const float lw = lineWidth(ui), h = lw * 0.5f;
    frameRect(ImGui::GetWindowDrawList(), {a.x + h, a.y + h}, {a.x + size.x - h, a.y + size.y - h}, imColor(palette::kDisabled), lw);
    ImGui::Dummy(size);
}

void drawWindowFrame(const Painter& ui, ImDrawList* dl, const Rect& r, const char* title, float buttonColumn) {
    const float lw = lineWidth(ui), h = lw * 0.5f;
    const float w = r.size().x, ht = r.size().y;
    auto P = [&](float x, float y) { return ui.at(r.min + Vec2{x, y}); };
    auto box = [&](float x0, float y0, float x1, float y1, uint32_t c) {
        const ImVec2 a = P(x0, y0), b = P(x1, y1);
        frameRect(dl, {a.x + h, a.y + h}, {b.x - h, b.y - h}, imColor(c), lw);
    };
    dl->AddRectFilled(P(0, 0), P(w, ht), IM_COL32_BLACK);
    if (!title && buttonColumn <= 0) {
        // Menus without a title (the Game Menu) have a plain double box.
        box(0, 0, w, ht, palette::kFrame);
        box(3, 3, w - 3, ht - 3, palette::kFrameLight);
        return;
    }

    // Side pipes from the frame parts sheet: a top cap, a straight run, a bottom cap; the right side mirrored.
    auto piece = [&](float sx, float sy, float sw, float sh, float x, float y, float dh, bool mirror) {
        Sprite s = ui.art.region(kMainParts, int(sx), int(sy), int(sw), int(sh), false);
        if (!s) return;
        ImVec2 uv0{s.uv.min.x, s.uv.min.y}, uv1{s.uv.max.x, s.uv.max.y};
        if (mirror) std::swap(uv0.x, uv1.x);
        dl->AddImage(ImTextureRef(static_cast<ImTextureID>(s.tex.value)), P(x, y), P(x + sw, y + dh), uv0, uv1);
    };
    for (const bool right : {false, true}) {
        const float x = right ? w - 13 : 0;
        piece(0, 0, 13, 57, x, 0, 57, right);
        for (float y = 57; y < ht - 43; y += 20) piece(0, 60, 13, std::min(20.0f, ht - 43 - y), x, y, std::min(20.0f, ht - 43 - y), right);
        piece(27, 43, 13, 43, x, ht - 43, 43, right);
    }
    // Rails, the title strip and the panel boxes.
    const ImU32 rail = imColor(palette::kFrameLight);
    dl->AddLine(P(12, 1), P(w - 12, 1), rail, lw);
    dl->AddLine(P(12, ht - 2), P(w - 12, ht - 2), rail, lw);
    const float top = title ? 31.0f : 4.0f;
    if (title) box(10, 4, w - 10, 31, palette::kFrame);
    if (buttonColumn > 0) {
        const float split = w - 15 - buttonColumn - 8;
        box(10, top, split - 3, ht - 4, palette::kFrame);
        box(split + 2, top, w - 10, ht - 4, palette::kFrame);
    } else {
        box(10, top, w - 10, ht - 4, palette::kFrame);
    }
    if (title && *title) {
        ImGui::PushFont(ui.fonts.bold, ui.fontPx(kTitleSize));
        const char* end = labelEnd(title);
        // Large dialog titles: the Button face, white, at (17,11) (spec 06 §5.4).
        dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), P(17, 11 + kTitleLead), IM_COL32_WHITE, title, end);
        ImGui::PopFont();
    }
}

// ---- Dialog --------------------------------------------------------------------------------

namespace {

constexpr float kButtonH = 28.0f;
constexpr float kSlotPitch = 31.0f;
constexpr float kPanelTop = 35.0f;
constexpr float kPanelBottom = 9.0f;
constexpr int kCloseSlot = 13;  // Close sits in the 14th slot, like the 475-pixel windows

// Buttons each dialog drew last frame, by button-column id.
std::unordered_map<ImGuiID, int>& slotCounts() {
    static std::unordered_map<ImGuiID, int> counts;
    return counts;
}

} // namespace

void UiContext::tag(std::string_view name, ImVec2 min, ImVec2 max) {
#ifndef NDEBUG
    // Every tag a window registers must be one lessons can name (learn/ids.cpp).
    static std::vector<std::string> reported;
    if (!learn::isUiTag(name) && std::find(reported.begin(), reported.end(), name) == reported.end()) {
        reported.emplace_back(name);
        log::warn("UI tag '{}' is not listed in learn/ids.cpp", name);
    }
#endif
    tags.push_back({std::string(name), min, max});
}

void UiContext::promptWindow() {
    const ImVec2 pos = ImGui::GetWindowPos(), size = ImGui::GetWindowSize();
    promptAreas.emplace_back(pos, ImVec2(pos.x + size.x, pos.y + size.y));
}

void UiContext::tagTab(std::string_view tab, bool shown) {
    if (!drawing) return;
    std::string name = std::string(windowId(*drawing)) + ":" + std::string(tab);
    if (shown) facts.tabs.push_back(name);
    tagItem(name);
}

void UiContext::tagWindow(ImVec2 min, ImVec2 max) {
    if (!drawing || windowTagged) return;
    windowTagged = true;
    drawingWindow = ImGui::GetCurrentWindow()->ID;
    tag(std::string("window:") + std::string(windowId(*drawing)), min, max);
}

Dialog::Dialog(UiContext& ui, const char* title, DialogSize size, float buttonColumn)
    : Dialog(ui.painter(), title, dialogRect(size), buttonColumn) {
    game_ = &ui;
    if (visible_) ui.tagWindow(ui.at(rect_.min), ui.at(rect_.max));
}

Dialog::Dialog(UiContext& ui, const char* title, Vec2 size, float buttonColumn)
    : Dialog(ui.painter(), title,
             Rect{Vec2{(frameW() - size.x) * 0.5f, (frameH() - size.y) * 0.5f}, Vec2{(frameW() + size.x) * 0.5f, (frameH() + size.y) * 0.5f}},
             buttonColumn) {
    game_ = &ui;
    if (visible_) ui.tagWindow(ui.at(rect_.min), ui.at(rect_.max));
}

Dialog::Dialog(const Painter& ui, const char* title, DialogSize size, float buttonColumn) : Dialog(ui, title, dialogRect(size), buttonColumn) {}

Dialog::Dialog(const Painter& ui, const char* title, const Rect& rect, float buttonColumn)
    : ui_(ui), rect_(rect), buttonColumn_(buttonColumn > 0 && buttonColumn == 190.0f ? 180.0f : buttonColumn) {
    ImGui::SetNextWindowPos(ui.at(rect_.min), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ui.size(rect_.size()), ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    visible_ = ImGui::Begin(title, nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                                                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
                                                ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar(2);
    if (visible_) {
        drawWindowFrame(ui, ImGui::GetWindowDrawList(), rect_, title, buttonColumn_);
        if (ImGui::IsWindowAppearing()) ImGui::SetWindowFocus();
    }
}

Dialog::~Dialog() {
    endChild();
    ImGui::End();
}

void Dialog::titleText(float x, ImU32 color, std::string_view text) {
    ImGui::GetWindowDrawList()->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ui_.at(rect_.min + Vec2{x, 12}), color, text.data(),
                                        text.data() + text.size());
}

void Dialog::titleIcon(float x, const Sprite& icon) {
    if (!icon) return;
    ImGui::GetWindowDrawList()->AddImage(ImTextureRef(static_cast<ImTextureID>(icon.tex.value)), ui_.at(rect_.min + Vec2{x, 9}),
                                         ui_.at(rect_.min + Vec2{x + 16, 25}), {icon.uv.min.x, icon.uv.min.y}, {icon.uv.max.x, icon.uv.max.y});
}

void Dialog::endChild() {
    if (inChild_) ImGui::EndChild();
    inChild_ = false;
}

void Dialog::beginContent() {
    endChild();
    const float w = rect_.size().x, h = rect_.size().y;
    const float right = buttonColumn_ > 0 ? w - 15 - buttonColumn_ - 14 : w - 15;
    ImGui::SetCursorPos(ui_.size({15, kPanelTop}));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ui_.size({2, 2}));
    ImGui::BeginChild("##content", ui_.size({right - 15, h - kPanelTop - kPanelBottom}), ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::PopStyleVar();
    inChild_ = true;
}

void Dialog::beginButtons() {
    endChild();
    const float w = rect_.size().x, h = rect_.size().y;
    ImGui::SetCursorPos(ui_.size({w - 15 - buttonColumn_, kPanelTop}));
    ImGui::BeginChild("##buttons", ui_.size({buttonColumn_, h - kPanelTop - kPanelBottom + 2}), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground);
    inChild_ = true;
    buttonsStarted_ = true;
    nextSlot_ = 0;
    // Windows with more buttons than the column holds (ours, not the original's) pack them
    // closer, using the count from the previous frame.
    const float room = h - kPanelTop - kPanelBottom;
    const int used = slotCounts()[ImGui::GetID("##slots")];
    pitch_ = kSlotPitch;
    buttonH_ = kButtonH;
    if (used > 0 && float(used - 1) * kSlotPitch + kButtonH > room) {
        pitch_ = std::floor((room - kButtonH) / float(used - 1));
        buttonH_ = std::min(kButtonH, pitch_ - 2);
    }
}

bool Dialog::slot(const char* label, int style, bool on, bool enabled) {
    ImGui::SetCursorPos(ImVec2(0, ui_.px(float(nextSlot_) * pitch_)));
    ++nextSlot_;
    const bool clicked = classicButton(ui_, label, {buttonColumn_, buttonH_}, style, on, enabled);
    if (clicked) audio().play("button");
    return clicked;
}

bool Dialog::button(const char* label, bool enabled) { return slot(label, 0, false, enabled); }
bool Dialog::tab(const char* label, bool selected, bool enabled) { return slot(label, 1, selected, enabled); }
bool Dialog::check(const char* label, bool on, bool enabled) { return slot(label, 2, on, enabled); }

void Dialog::spacer() {
    ImGui::SetCursorPos(ImVec2(0, ui_.px(float(nextSlot_) * pitch_)));
    ++nextSlot_;
    emptySlot(ui_, {buttonColumn_, buttonH_});
}

bool Dialog::close() { return close(true, "Close"); }

bool Dialog::close(bool enabled, const char* label) {
    // Unused slots show as empty boxes down to Close; in taller windows the
    // circuit filler takes the space below it.
    const float room = (ImGui::GetWindowHeight() / ui_.k()) - kButtonH - 2;
    const int lastSlot = int(room / pitch_);
    const int closeSlot = std::max(nextSlot_, std::min(kCloseSlot, lastSlot));
    while (nextSlot_ < closeSlot) spacer();
    const float closeY = std::min(float(nextSlot_) * pitch_, room);
    slotCounts()[ImGui::GetID("##slots")] = nextSlot_ + 1;
    ImGui::SetCursorPos(ImVec2(0, ui_.px(closeY)));
    const bool clicked = classicButton(ui_, label, {buttonColumn_, buttonH_}, 0, false, enabled);
    if (game_ && game_->drawing) game_->tagItem(std::string(windowId(*game_->drawing)) + ":close");
    const float below = closeY + kSlotPitch;
    if (buttonColumn_ >= 150 && room + kButtonH - below > 40)
        if (Sprite filler = ui_.art.image("Pictures/Game/Screens/1024X768/RightFiller.bmp", false)) {
            // Tile the filler across the column below Close.
            const ImVec2 origin = ImGui::GetWindowPos();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const float fw = filler.size.x, fh = filler.size.y, bottom = room + kButtonH;
            for (float y = below; y < bottom; y += fh)
                for (float x = 0; x < buttonColumn_; x += fw) {
                    const float cw = std::min(fw, buttonColumn_ - x), ch = std::min(fh, bottom - y);
                    const ImVec2 a{origin.x + ui_.px(x), origin.y + ui_.px(y)};
                    dl->AddImage(ImTextureRef(static_cast<ImTextureID>(filler.tex.value)), a, {a.x + ui_.px(cw), a.y + ui_.px(ch)},
                                 {filler.uv.min.x, filler.uv.min.y},
                                 {filler.uv.min.x + (filler.uv.max.x - filler.uv.min.x) * cw / fw,
                                  filler.uv.min.y + (filler.uv.max.y - filler.uv.min.y) * ch / fh});
                }
        }
    ++nextSlot_;
    // Esc or Enter close a window whose bottom button is Close; Esc alone one
    // whose bottom button is Cancel (spec 06 §3.4); never while it is dim, or
    // a popup of it or a text field has the keys.
    const bool isClose = std::strcmp(label, "Close") == 0;
    const bool key = enabled &&
                     (ImGui::IsKeyPressed(ImGuiKey_Escape, false) ||
                      (isClose && (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false)))) &&
                     ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput && !ImGui::IsAnyItemActive() &&
                     !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
    if (clicked || key) {
        keep_ = false;
        audio().play("close");
    }
    return clicked || key;
}

// ---- Keys in dialogs -------------------------------------------------------------------------------

namespace {
bool pressed(ImGuiKey k) { return ImGui::IsKeyPressed(k, false); }
bool enterPressed() { return pressed(ImGuiKey_Enter) || pressed(ImGuiKey_KeypadEnter); }
} // namespace

std::optional<bool> yesNoKey() {
    if (ImGui::IsWindowAppearing() || ImGui::GetIO().WantTextInput) return std::nullopt;
    if (pressed(ImGuiKey_Y)) return true;
    if (pressed(ImGuiKey_N) || pressed(ImGuiKey_Escape) || enterPressed()) return false;
    return std::nullopt;
}

bool okKey() { return !ImGui::IsWindowAppearing() && !ImGui::GetIO().WantTextInput && (pressed(ImGuiKey_Escape) || enterPressed()); }

std::optional<bool> tacticalStrategicKey() {
    if (ImGui::IsWindowAppearing()) return std::nullopt;
    if (pressed(ImGuiKey_T)) return true;
    if (pressed(ImGuiKey_S)) return false;
    return std::nullopt;
}

void YesNoPrompt::open(std::string question, std::string title) {
    question_ = std::move(question);
    title_ = std::move(title);
    pending_ = true;
}

bool YesNoPrompt::draw(UiContext& ui) {
    const std::string id = title_ + "###yesno";
    if (pending_) {
        ImGui::OpenPopup(id.c_str());
        pending_ = false;
    }
    ImGui::SetNextWindowPos(ui.at({frameW() * 0.5f, frameH() * 0.5f}), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ui.size({400, 0}), ImGuiCond_Always);
    if (!ImGui::BeginPopupModal(id.c_str(), nullptr,
                                ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                    ImGuiWindowFlags_AlwaysAutoResize | kPromptFlags))
        return false;
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(question_.c_str());
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    const std::optional<bool> key = yesNoKey();
    const float w = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    const bool yes = ImGui::Button("Yes", ImVec2(w, ui.px(26))) || key == true;
    ImGui::SameLine();
    const bool no = ImGui::Button("No", ImVec2(w, ui.px(26))) || key == false;
    if (yes || no) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
    return yes;
}

bool UiContext::setOptions(const game::InterfaceOptions& o) {
    if (o == options()) return true;
    return session.issue(game::cmd::SetInterfaceOptions{o}).ok;
}

void applyClassicStyle() {
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 0.0f;
    style.ChildRounding = 0.0f;
    style.FrameRounding = 0.0f;
    style.PopupRounding = 0.0f;
    style.GrabRounding = 0.0f;
    style.TabRounding = 0.0f;
    style.ScrollbarRounding = 0.0f;
    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.FrameBorderSize = 1.0f;
    style.WindowTitleAlign = ImVec2(0.0f, 0.5f);
    style.ItemSpacing = ImVec2(6, 2);
    style.FramePadding = ImVec2(4, 2);
    style.CellPadding = ImVec2(4, 1);
    style.WindowPadding = ImVec2(8, 6);
    style.ScrollbarSize = 14.0f;
    ImVec4* c = style.Colors;
    const ImVec4 black{0, 0, 0, 1};
    const ImVec4 frame = imColorV(palette::kFrame);
    const ImVec4 button = imColorV(palette::kButton);
    c[ImGuiCol_Text] = ImVec4(1, 1, 1, 1);
    c[ImGuiCol_TextDisabled] = imColorV(palette::kDim);
    c[ImGuiCol_WindowBg] = black;
    c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PopupBg] = black;
    c[ImGuiCol_Border] = frame;
    c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TitleBg] = black;
    c[ImGuiCol_TitleBgActive] = black;
    c[ImGuiCol_TitleBgCollapsed] = black;
    c[ImGuiCol_MenuBarBg] = black;
    c[ImGuiCol_Button] = black;
    c[ImGuiCol_ButtonHovered] = imColorV(0x0c1838);
    c[ImGuiCol_ButtonActive] = imColorV(0x18285a);
    c[ImGuiCol_FrameBg] = black;
    c[ImGuiCol_FrameBgHovered] = imColorV(0x0a1430);
    c[ImGuiCol_FrameBgActive] = imColorV(0x101c40);
    c[ImGuiCol_CheckMark] = imColorV(0x40e040);
    c[ImGuiCol_SliderGrab] = button;
    c[ImGuiCol_SliderGrabActive] = imColorV(palette::kLabel);
    c[ImGuiCol_Header] = imColorV(0x1a2c64);
    c[ImGuiCol_HeaderHovered] = imColorV(0x122050);
    c[ImGuiCol_HeaderActive] = imColorV(0x223a7a);
    c[ImGuiCol_Separator] = frame;
    c[ImGuiCol_SeparatorHovered] = button;
    c[ImGuiCol_SeparatorActive] = button;
    c[ImGuiCol_ScrollbarBg] = black;
    c[ImGuiCol_ScrollbarGrab] = frame;
    c[ImGuiCol_ScrollbarGrabHovered] = button;
    c[ImGuiCol_ScrollbarGrabActive] = imColorV(palette::kLabel);
    c[ImGuiCol_Tab] = black;
    c[ImGuiCol_TabHovered] = imColorV(0x122050);
    c[ImGuiCol_TabSelected] = imColorV(0x1a2c64);
    c[ImGuiCol_TableHeaderBg] = black;
    c[ImGuiCol_TableBorderStrong] = frame;
    c[ImGuiCol_TableBorderLight] = imColorV(palette::kFrame, 0.5f);
    c[ImGuiCol_TableRowBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TableRowBgAlt] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TextSelectedBg] = imColorV(0x2a3c80);
    c[ImGuiCol_NavCursor] = button;
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, 0.35f);
}

} // namespace opense4::client::classic
