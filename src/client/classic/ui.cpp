#include "client/classic/ui.hpp"

#include "client/app_settings.hpp"
#include "client/audio.hpp"
#include "client/ui/bitmap_font.hpp"

#include "core/log.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <format>
#include <unordered_map>

namespace opense4::client::classic {

namespace {


Rect dialogRect(DialogSize size) {
    Vec2 s;
    switch (size) {
        // The original's windows are 780 wide; tall lists take the screen height less 50 at each end.
        case DialogSize::Large: s = {780, 475}; break;
        case DialogSize::Tall: return Rect{{(kFrameW - 780) * 0.5f, 50}, {(kFrameW + 780) * 0.5f, kFrameH - 50}};
        case DialogSize::Report: s = {400, 540}; break;
        case DialogSize::Picker: s = {540, 660}; break;
        case DialogSize::Prompt: s = {420, 190}; break;
        case DialogSize::Full: s = {kFrameW, kFrameH}; break;
    }
    const Vec2 min{(kFrameW - s.x) * 0.5f, (kFrameH - s.y) * 0.5f};
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
        if (i > 0) ImGui::SameLine(0, ui.px(compact ? 6 : 14));
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

void heading(UiContext& ui, const char* text) {
    ImGui::PushFont(ui.fonts.bold, ui.fontPx(kTitleSize));
    ImGui::TextColored(imColorV(palette::kHeading), "%s", text);
    ImGui::PopFont();
}

std::string formatNumber(int64_t v) { return std::to_string(v); }

std::string formatDate(uint32_t turn) { return std::format("{}.{}", 2400 + turn / 10, turn % 10); }

ImU32 empireColor(const game::GameState& s, game::EmpireId e) {
    if (!e.valid() || e.index() >= s.empires.size()) return IM_COL32(200, 200, 200, 255);
    const uint32_t c = s.empire(e).color;
    return IM_COL32((c >> 16) & 0xff, (c >> 8) & 0xff, c & 0xff, 255);
}

FrameMapping frameMappingFor(float fw, float fh) {
    const GraphicsSettings& g = appSettings().graphics;
    constexpr float kMaxWide = kFrameH * 21.0f / 9.0f;  // wider screens get bars
    const bool extended = g.widescreen == WidescreenLayout::Extended;
    FrameMapping m;
    float width = extended ? std::clamp(kFrameH * fw / std::max(1.0f, fh), kFrameW, kMaxWide) : kFrameW;
    m.scale = std::min(fw / width, fh / kFrameH);
    if (g.integerScaling && m.scale >= 1.0f) {
        m.scale = std::floor(std::min(fw / kFrameW, fh / kFrameH));
        if (extended) width = std::clamp(fw / m.scale, kFrameW, kMaxWide);
    }
    m.left = (kFrameW - width) * 0.5f;
    m.right = kFrameW - m.left;
    m.offset = {(fw - width * m.scale) * 0.5f - m.left * m.scale, (fh - kFrameH * m.scale) * 0.5f};
    return m;
}

Fonts loadClassicFonts(const Fonts& app, const assets::InstallFiles& files) {
    Fonts out = app;
    auto load = [&](std::string_view file) -> ImFont* {
        const auto path = files.find(std::string("Fonts/") + std::string(file));
        if (!path) return nullptr;
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
    const bool hovered = enabled && ImGui::IsItemHovered();
    const bool held = hovered && ImGui::IsItemActive();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float lw = lineWidth(ui);
    const ImU32 line = imColor(enabled ? palette::kButton : palette::kDisabled);
    const ImU32 text = imColor(!enabled ? palette::kDisabled : hovered ? palette::kButtonHot : palette::kButton);
    const float h = lw * 0.5f;
    if (held) dl->AddRectFilled(a, b, imColor(0x101c40));
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
    dl->AddText({std::floor(x), std::floor(midY - ts.y * 0.5f)}, text, label, labelEnd(label));
    ImGui::PopFont();
    return clicked && enabled;
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
        dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), P(18, 8), IM_COL32_WHITE, title, end);
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

Dialog::Dialog(UiContext& ui, const char* title, DialogSize size, float buttonColumn)
    : ui_(ui), rect_(dialogRect(size)), buttonColumn_(buttonColumn > 0 && buttonColumn == 190.0f ? 180.0f : buttonColumn) {
    ImGui::SetNextWindowPos(ui.at(rect_.min), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ui.size(rect_.size()), ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    visible_ = ImGui::Begin(title, nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                                                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
                                                ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar(2);
    if (visible_) {
        drawWindowFrame(ui.painter(), ImGui::GetWindowDrawList(), rect_, title, buttonColumn_);
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
    emptySlot(ui_.painter(), {buttonColumn_, buttonH_});
}

bool Dialog::close() {
    // Unused slots show as empty boxes down to Close; in taller windows the
    // circuit filler takes the space below it.
    const float room = (ImGui::GetWindowHeight() / ui_.k()) - kButtonH - 2;
    const int lastSlot = int(room / pitch_);
    const int closeSlot = std::max(nextSlot_, std::min(kCloseSlot, lastSlot));
    while (nextSlot_ < closeSlot) spacer();
    const float closeY = std::min(float(nextSlot_) * pitch_, room);
    slotCounts()[ImGui::GetID("##slots")] = nextSlot_ + 1;
    ImGui::SetCursorPos(ImVec2(0, ui_.px(closeY)));
    const bool clicked = classicButton(ui_, "Close", {buttonColumn_, buttonH_});
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
    // Esc or Enter close a window whose bottom button is Close (spec 06 §3.4),
    // when no popup of it and no text field has the keys.
    const bool key = (ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsKeyPressed(ImGuiKey_Enter, false) ||
                      ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false)) &&
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
