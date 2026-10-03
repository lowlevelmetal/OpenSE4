#include "client/classic/screens/setup_widgets.hpp"

#include "client/classic/screens/list_widgets.hpp"
#include "client/script/items.hpp"
#include "datafile/datafile.hpp"
#include "ruleset/ruleset.hpp"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <format>
#include <optional>

namespace opense4::client::classic::setup {

namespace {

ImTextureRef texRef(const Sprite& s) { return ImTextureRef(static_cast<ImTextureID>(s.tex.value)); }

int resizeCallback(ImGuiInputTextCallbackData* data) {
    if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
        auto* s = static_cast<std::string*>(data->UserData);
        s->resize(static_cast<size_t>(data->BufTextLen));
        data->Buf = s->data();
    }
    return 0;
}

// The setup area: 800×600 centred on the frame (spec 06 §2.1.1).
constexpr float kAreaW = 800.0f, kAreaH = 600.0f;
// The content frame and the buttons under it (spec 07 session 5).
constexpr Vec2 kFrameMin{216, 0}, kFrameMax{799, 549};
constexpr Vec2 kBeginMin{496, 562}, kBeginMax{644, 587};
constexpr Vec2 kCancelMin{649, 562}, kCancelMax{797, 587};
constexpr float kRowH = 18.0f;

float lineWidth(const Painter& p) { return std::max(1.0f, std::floor(p.map.scale)) / p.fbScale; }

// The lamps of General.bmp: 13 px cells from x 178 (blue, green, red, grey).
Sprite lampSprite(Art& art, bool on) { return art.region("Pictures/Game/General.bmp", 178 + 13 * (on ? 1 : 0), 0, 13, 13); }

// Which row of a lamp or check list was clicked last (drawn hatched), kept
// in Dear ImGui's storage under the list's id.
int lastClicked(ImGuiID list) { return ImGui::GetStateStorage()->GetInt(list, -1); }
void setLastClicked(ImGuiID list, int row) { ImGui::GetStateStorage()->SetInt(list, row); }

} // namespace

// ---- The area --------------------------------------------------------------------------------

void drawStarfield(MenuContext& ctx) {
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const ImVec2 size = ImGui::GetIO().DisplaySize;
    dl->AddRectFilled({0, 0}, size, IM_COL32_BLACK);
    const Sprite sky = ctx.art.image("Pictures/Game/Screens/1024X768/Starmap.bmp", false);
    if (!sky) return;
    const float tw = ctx.px(sky.size.x), th = ctx.px(sky.size.y);
    for (float y = 0; y < size.y; y += th)
        for (float x = 0; x < size.x; x += tw)
            dl->AddImage(texRef(sky), {x, y}, {x + tw, y + th}, {sky.uv.min.x, sky.uv.min.y}, {sky.uv.max.x, sky.uv.max.y});
}

SetupArea::SetupArea(MenuContext& ctx, const char* window, const char* title, Decoration decoration)
    : ctx_(ctx), origin_{std::floor((frameW() - kAreaW) * 0.5f), std::floor((frameH() - kAreaH) * 0.5f)} {
    drawStarfield(ctx);
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    ImGui::PushFont(ctx.fonts.regular, ctx.painter().fontPx(kTextSize));
    visible_ = ImGui::Begin(window, nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                                 ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoBringToFrontOnFocus |
                                                 ImGuiWindowFlags_NoScrollWithMouse);
    if (!visible_) return;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    // The screen's name in white in the title font at (1,1).
    text({1, 1}, title, kWhite, Face::Title);
    // The content frame: an outer #647EC7 line, an inner #4F65A2 line 2 px
    // inside it, hatched pieces in the corners.
    const float lw = lineWidth(painter()), h = lw * 0.5f;
    const ImVec2 a = at(kFrameMin), b = at(kFrameMax + Vec2{1, 1});
    dl->AddRect({a.x + h, a.y + h}, {b.x - h, b.y - h}, imColor(kBoxRgb), 0.0f, lw);
    const ImVec2 ia = at(kFrameMin + Vec2{2, 2}), ib = at(kFrameMax + Vec2{-1, -1});
    dl->AddRect({ia.x + h, ia.y + h}, {ib.x - h, ib.y - h}, imColor(kInnerRgb), 0.0f, lw);
    for (const Vec2 corner : {kFrameMin + Vec2{3, 3}, Vec2{kFrameMax.x - 10, kFrameMin.y + 3}, Vec2{kFrameMin.x + 3, kFrameMax.y - 10},
                              kFrameMax + Vec2{-10, -10}})
        hatch(corner, corner + Vec2{8, 8});

    // Two pictures under the page buttons, drawn from the launch's seed.
    if (decoration == Decoration::None || !ctx.rules) return;
    const auto& types = ctx.rules->data().sectorObjectTypes;
    auto pick = [&](std::string_view kind, uint64_t salt) -> std::optional<int> {
        std::vector<int> pictures;
        for (const auto& t : types)
            if (datafile::keysEqual(t.physicalType, kind)) pictures.push_back(t.picture);
        if (pictures.empty()) return std::nullopt;
        return pictures[static_cast<size_t>((ctx.seed * 0x9e3779b97f4a7c15ull + salt) % pictures.size())];
    };
    auto centred = [&](const Sprite& s, Vec2 c) {
        if (!s) return;
        const float side = std::min(128.0f, std::max(s.size.x, s.size.y));
        picture(s, c - Vec2{side * 0.5f, side * 0.5f}, c + Vec2{side * 0.5f, side * 0.5f});
    };
    if (decoration == Decoration::GameSetup) {
        if (auto star = pick("Star", 1)) centred(ctx.art.planetPortrait(*star), {63, 341});
        if (auto planet = pick("Planet", 2)) centred(ctx.art.planetPortrait(*planet), {133, 451});
    } else {
        if (auto planet = pick("Planet", 3)) centred(ctx.art.planetPortrait(*planet), {63, 341});
        const auto& presets = ctx.rules->racePresets();
        const auto& sizes = ctx.rules->data().vehicleSizes;
        std::vector<const ruleset::VehicleSize*> ships;
        for (const auto& v : sizes)
            if (v.type == ruleset::VehicleType::Ship) ships.push_back(&v);
        if (!presets.empty() && !ships.empty()) {
            const auto& style = presets[static_cast<size_t>((ctx.seed + 5) % presets.size())];
            const auto* hull = ships[static_cast<size_t>((ctx.seed / 3 + 7) % ships.size())];
            centred(ctx.art.shipPortrait(style.folder, *hull), {133, 451});
        }
    }
}

SetupArea::~SetupArea() {
    ImGui::End();
    ImGui::PopFont();
    ImGui::PopStyleVar(3);
}

ImVec2 SetupArea::at(Vec2 p) const { return ctx_.at(origin_ + p); }

void SetupArea::place(Vec2 p) const { ImGui::SetCursorScreenPos(at(p)); }

ImFont* SetupArea::font(Face f) const {
    switch (f) {
        case Face::Small: return ctx_.fonts.small ? ctx_.fonts.small : ctx_.fonts.regular;
        case Face::Title: return ctx_.fonts.bold;
        case Face::Body: break;
    }
    return ctx_.fonts.regular;
}

float SetupArea::fontSize(Face f) const {
    return painter().fontPx(f == Face::Small ? kSmallSize : f == Face::Title ? kTitleSize : kTextSize);
}

namespace {
float leadOf(Face f) { return f == Face::Small ? kSmallLead : f == Face::Title ? kTitleLead : kTextLead; }
} // namespace

void SetupArea::text(Vec2 p, std::string_view s, uint32_t rgb, Face face) const {
    ImGui::GetWindowDrawList()->AddText(font(face), fontSize(face), at(p - Vec2{0, leadOf(face)}), imColor(rgb), s.data(), s.data() + s.size());
}

float SetupArea::textWidth(std::string_view s, Face face) const {
    return font(face)->CalcTextSizeA(fontSize(face), FLT_MAX, 0.0f, s.data(), s.data() + s.size()).x / ctx_.k();
}

void SetupArea::textRight(Vec2 p, std::string_view s, uint32_t rgb, Face face) const { text({p.x - textWidth(s, face), p.y}, s, rgb, face); }

float SetupArea::textWrapped(Vec2 p, std::string_view s, float width, uint32_t rgb, Face face) const {
    ImFont* f = font(face);
    const float fs = fontSize(face);
    const ImVec2 dim = f->CalcTextSizeA(fs, FLT_MAX, px(width), s.data(), s.data() + s.size());
    ImGui::GetWindowDrawList()->AddText(f, fs, at(p - Vec2{0, leadOf(face)}), imColor(rgb), s.data(), s.data() + s.size(), px(width));
    return dim.y / ctx_.k();
}

void SetupArea::box(Vec2 a, Vec2 b, uint32_t rgb) const {
    const float lw = lineWidth(painter()), h = lw * 0.5f;
    const ImVec2 p0 = at(a), p1 = at(b + Vec2{1, 1});
    ImGui::GetWindowDrawList()->AddRect({p0.x + h, p0.y + h}, {p1.x - h, p1.y - h}, imColor(rgb), 0.0f, lw);
}

void SetupArea::hatch(Vec2 a, Vec2 b) const {
    const int w = std::max(1, int(b.x - a.x)), hgt = std::max(1, int(b.y - a.y));
    if (Sprite grid = ctx_.art.region("Pictures/Game/Dialogs/Rowgrid.bmp", 0, 0, w, hgt, false))
        ImGui::GetWindowDrawList()->AddImage(texRef(grid), at(a), at(b), {grid.uv.min.x, grid.uv.min.y}, {grid.uv.max.x, grid.uv.max.y});
    else ImGui::GetWindowDrawList()->AddRectFilled(at(a), at(b), IM_COL32(30, 40, 80, 255));
}

void SetupArea::picture(const Sprite& s, Vec2 a, Vec2 b, bool frame) const {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (s) dl->AddImage(texRef(s), at(a), at(b), {s.uv.min.x, s.uv.min.y}, {s.uv.max.x, s.uv.max.y});
    else if (frame) dl->AddRectFilled(at(a), at(b), IM_COL32(4, 8, 20, 255));
    if (frame) box(a, b - Vec2{1, 1});
}

void SetupArea::lamp(Vec2 c, bool on, bool enabled) const {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 a = at(c - Vec2{6.5f, 6.5f}), b = at(c + Vec2{6.5f, 6.5f});
    const ImU32 tint = enabled ? IM_COL32_WHITE : IM_COL32(255, 255, 255, 110);
    if (const Sprite s = lampSprite(ctx_.art, on)) dl->AddImage(texRef(s), a, b, {s.uv.min.x, s.uv.min.y}, {s.uv.max.x, s.uv.max.y}, tint);
    else dl->AddCircleFilled(at(c), px(5), on ? IM_COL32(80, 220, 90, 255) : IM_COL32(60, 90, 200, 255));
}

// ---- Buttons ---------------------------------------------------------------------------------

bool SetupArea::pageButton(int slot, const char* label, bool current) {
    // One every 30 px from area y 22, x 2–204, 26 px tall (spec 07 session 5).
    const Vec2 a{2, 22 + 30.0f * float(slot)}, b{205, a.y + 26};
    place(a);
    ImGui::PushID(label);
    const bool clicked = ImGui::InvisibleButton("##page", size(b - a));
    ImGui::PopID();
    script::reportItem(label);   // input scripts find a page by its label
    const bool hovered = ImGui::IsItemHovered(), held = hovered && ImGui::IsItemActive();
    if (current || hovered) hatch(a + Vec2{1, 1}, b - Vec2{1, 1});
    // A tab: the right end cut at a slant.
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float lw = lineWidth(painter()), h = lw * 0.5f;
    const ImVec2 p0 = at(a), p1 = at(b);
    const float c = px(9);
    const ImU32 line = imColor(held ? palette::kButtonHeld : hovered ? palette::kButtonHot : palette::kButton);
    const ImVec2 pts[] = {{p0.x + h, p0.y + h}, {p1.x - c, p0.y + h}, {p1.x - h, p0.y + c}, {p1.x - h, p1.y - h}, {p0.x + h, p1.y - h}};
    dl->AddPolyline(pts, 5, line, lw, ImDrawFlags_Closed);
    // The label in the button font, centred; the current page's with a green lamp before it.
    ImGui::PushFont(ctx_.fonts.bold, painter().fontPx(kTitleSize));
    const ImVec2 ts = ImGui::CalcTextSize(label);
    const float lampW = current ? px(13 + 4) : 0.0f;
    float x = std::floor((p0.x + p1.x - ts.x - lampW) * 0.5f);
    const float midY = (p0.y + p1.y) * 0.5f;
    if (current) {
        lamp({(x - p0.x) / ctx_.k() + a.x + 6.5f, a.y + 13}, true);
        x += lampW;
    }
    dl->AddText({x, std::floor(midY - ts.y * 0.5f) + px(1)}, imColor(held ? palette::kButtonHeld : kPageRgb), label);
    ImGui::PopFont();
    return clicked;
}

bool SetupArea::button(Vec2 a, Vec2 b, const char* label, bool enabled) {
    place(a);
    ImGui::PushFont(ctx_.fonts.bold, painter().fontPx(kTitleSize));
    const bool clicked = classicButton(painter(), label, b - a + Vec2{1, 1}, 0, false, enabled);
    ImGui::PopFont();
    return clicked;
}

bool SetupArea::beginButton(const char* label, bool enabled) { return button(kBeginMin, kBeginMax, label, enabled); }

bool SetupArea::cancelButton() {
    const bool escape = escapePressed();
    return button(kCancelMin, kCancelMax, "Cancel") || escape;
}

void SetupArea::status(std::string_view s, const ImVec4& color) {
    if (s.empty()) return;
    const ImU32 c = ImGui::ColorConvertFloat4ToU32(color);
    ImFont* f = font(Face::Small);
    const float fs = fontSize(Face::Small);
    ImGui::GetWindowDrawList()->AddText(f, fs, at({216, 555}), c, s.data(), s.data() + s.size(), px(272));
}

// ---- Lists ----------------------------------------------------------------------------------

bool SetupArea::lampList(const char* id, Vec2 a, Vec2 b, int& value, std::span<const std::string> labels, bool enabled, bool scroll) {
    box(a, b, enabled ? kBoxRgb : kDimBoxRgb);
    bool changed = false;
    ImGui::PushID(id);
    const ImGuiID listId = ImGui::GetID("##rows");
    const int last = lastClicked(listId);
    const Vec2 inner = a + Vec2{1, 1};
    const float w = b.x - a.x - 1;
    if (scroll) {
        place(inner);
        beginList(painter(), "##list", size({w, b.y - a.y - 1}), kRowH, ImGuiChildFlags_None, false);
    }
    // Rows in screen units from the list's own top left (scrolled with it).
    const ImVec2 top = scroll ? ImGui::GetCursorScreenPos() : at(inner);
    const float rowW = scroll ? listRowsWidth(painter(), px(w)) : px(w);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    for (size_t i = 0; i < labels.size(); ++i) {
        const ImVec2 r0{top.x, top.y + px(kRowH) * float(i)};
        ImGui::SetCursorScreenPos(r0);
        ImGui::PushID(static_cast<int>(i));
        ImGui::BeginDisabled(!enabled);
        const bool clicked = ImGui::InvisibleButton("##row", ImVec2(rowW, px(kRowH)));
        ImGui::EndDisabled();
        ImGui::PopID();
        script::reportItem(labels[i]);   // input scripts find a row by its label
        if (static_cast<int>(i) == last) {
            if (Sprite grid = ctx_.art.region("Pictures/Game/Dialogs/Rowgrid.bmp", 0, 0, int(rowW / ctx_.k()), int(kRowH), false))
                dl->AddImage(texRef(grid), r0, {r0.x + rowW, r0.y + px(kRowH)}, {grid.uv.min.x, grid.uv.min.y}, {grid.uv.max.x, grid.uv.max.y});
        }
        const bool on = value == static_cast<int>(i);
        const ImU32 tint = enabled ? IM_COL32_WHITE : IM_COL32(255, 255, 255, 110);
        if (const Sprite s = lampSprite(ctx_.art, on))
            dl->AddImage(texRef(s), {r0.x + px(2), r0.y + px(2.5f)}, {r0.x + px(15), r0.y + px(15.5f)}, {s.uv.min.x, s.uv.min.y},
                         {s.uv.max.x, s.uv.max.y}, tint);
        dl->AddText({r0.x + px(20), r0.y + px(1)}, imColor(enabled ? kWhite : kDimTextRgb), labels[i].c_str());
        if (clicked && enabled) {
            setLastClicked(listId, static_cast<int>(i));
            if (value != static_cast<int>(i)) {
                value = static_cast<int>(i);
                changed = true;
            }
        }
    }
    if (scroll) {
        ImGui::SetCursorScreenPos(top);
        ImGui::Dummy(ImVec2(rowW, px(kRowH) * float(labels.size())));
        endList(painter());
    }
    ImGui::PopID();
    return changed;
}

bool SetupArea::lampList(const char* id, Vec2 a, Vec2 b, int& value, std::initializer_list<const char*> labels, bool enabled) {
    const std::vector<std::string> list(labels.begin(), labels.end());
    return lampList(id, a, b, value, std::span<const std::string>(list), enabled);
}

bool SetupArea::checkList(const char* id, Vec2 a, Vec2 b, std::span<Check> rows, bool scroll) {
    box(a, b);
    bool changed = false;
    ImGui::PushID(id);
    const Vec2 inner = a + Vec2{1, 1};
    const float w = b.x - a.x - 1;
    if (scroll) {
        place(inner);
        beginList(painter(), "##list", size({w, b.y - a.y - 1}), kRowH, ImGuiChildFlags_None, false);
    }
    const ImVec2 top = scroll ? ImGui::GetCursorScreenPos() : at(inner);
    const float rowW = scroll ? listRowsWidth(painter(), px(w)) : px(w);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float lw = lineWidth(painter());
    for (size_t i = 0; i < rows.size(); ++i) {
        Check& row = rows[i];
        const ImVec2 r0{top.x, top.y + px(kRowH) * float(i)};
        ImGui::SetCursorScreenPos(r0);
        ImGui::PushID(static_cast<int>(i));
        ImGui::BeginDisabled(!row.enabled);
        const bool clicked = ImGui::InvisibleButton("##row", ImVec2(rowW, px(kRowH)));
        ImGui::EndDisabled();
        ImGui::PopID();
        script::reportItem(row.label);   // input scripts find a row by its label
        // The 16×17 box at x+2, a green lamp in it when on.
        const ImVec2 b0{r0.x + px(1), r0.y + px(0.5f)}, b1{b0.x + px(16), b0.y + px(17)};
        dl->AddRect(b0, b1, imColor(row.enabled ? kExplainRgb : kDimBoxRgb), 0.0f, lw);
        if (row.value && *row.value)
            if (const Sprite s = lampSprite(ctx_.art, true))
                dl->AddImage(texRef(s), {b0.x + px(1.5f), b0.y + px(2)}, {b0.x + px(14.5f), b0.y + px(15)}, {s.uv.min.x, s.uv.min.y},
                             {s.uv.max.x, s.uv.max.y}, row.enabled ? IM_COL32_WHITE : IM_COL32(255, 255, 255, 110));
        dl->AddText({r0.x + px(20), r0.y + px(1)}, imColor(row.enabled ? kWhite : kDimTextRgb), row.label.c_str());
        if (clicked && row.enabled && row.value) {
            *row.value = !*row.value;
            changed = true;
        }
    }
    if (scroll) {
        ImGui::SetCursorScreenPos(top);
        ImGui::Dummy(ImVec2(rowW, px(kRowH) * float(rows.size())));
        endList(painter());
    }
    ImGui::PopID();
    return changed;
}

bool SetupArea::checkBox(const char* id, Vec2 boxAt, std::string_view label, bool& value, bool enabled, float labelX, float wrap) {
    const float lx = labelX > 0 ? labelX : boxAt.x + 19;
    const float textW = wrap > 0 ? wrap : textWidth(label);
    place(boxAt);
    ImGui::PushID(id);
    ImGui::BeginDisabled(!enabled);
    const bool clicked = ImGui::InvisibleButton("##check", size({lx - boxAt.x + textW, 17}));
    ImGui::EndDisabled();
    ImGui::PopID();
    script::reportItem(label);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 b0 = at(boxAt), b1 = at(boxAt + Vec2{16, 17});
    dl->AddRect(b0, b1, imColor(enabled ? kExplainRgb : kDimBoxRgb), 0.0f, lineWidth(painter()));
    if (value)
        if (const Sprite s = lampSprite(ctx_.art, true))
            dl->AddImage(texRef(s), {b0.x + px(1.5f), b0.y + px(2)}, {b0.x + px(14.5f), b0.y + px(15)}, {s.uv.min.x, s.uv.min.y}, {s.uv.max.x, s.uv.max.y},
                         enabled ? IM_COL32_WHITE : IM_COL32(255, 255, 255, 110));
    if (wrap > 0) textWrapped({lx, boxAt.y + 4}, label, wrap, enabled ? kWhite : kDimTextRgb);
    else text({lx, boxAt.y + 4}, label, enabled ? kWhite : kDimTextRgb);
    if (clicked && enabled) value = !value;
    return clicked && enabled;
}

// ---- Fields ---------------------------------------------------------------------------------

bool SetupArea::spin(const char* id, Vec2 left, float valueW, int64_t& value, int64_t step, int64_t lo, int64_t hi, std::string_view shown,
                     bool enabled) {
    ImGui::PushID(id);
    const int64_t before = value;
    place(left);
    if (arrowButton(painter(), "##less", ArrowGlyph::Left, {20, 20}, enabled)) value = std::max(lo, value - step);
    // The value box (#2D2D2D lines and a #606060 value while dim), which takes typing.
    const Vec2 va = left + Vec2{24, 0}, vb = va + Vec2{valueW, 19};
    box(va, vb, enabled ? kBoxRgb : kDimBoxRgb);
    place(va + Vec2{1, 1});
    ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Text, imColor(enabled ? kWhite : kDimTextRgb));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, size({4, 2.5f}));
    ImGui::SetNextItemWidth(px(valueW - 1));
    ImGui::BeginDisabled(!enabled);
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.*s", int(shown.size()), shown.data());
    if (ImGui::InputText("##value", buf, sizeof buf, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll)) {
        long long typed = 0;
        if (std::sscanf(buf, "%lld", &typed) == 1) value = std::clamp<int64_t>(typed, lo, hi);
    }
    ImGui::EndDisabled();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);
    place(vb + Vec2{5, -19});
    if (arrowButton(painter(), "##more", ArrowGlyph::Right, {20, 20}, enabled)) value = std::min(hi, value + step);
    ImGui::PopID();
    return value != before;
}

bool SetupArea::edit(const char* id, Vec2 a, Vec2 b, std::string& value, ImGuiInputTextFlags flags, bool enabled) {
    box(a, b, enabled ? kBoxRgb : kDimBoxRgb);
    place(a + Vec2{1, 1});
    ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, size({4, (b.y - a.y - 1 - kTextSize) * 0.5f}));
    ImGui::BeginDisabled(!enabled);
    const bool changed = inputText(id, value, px(b.x - a.x - 1), flags);
    ImGui::EndDisabled();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
    return changed;
}

bool SetupArea::editMultiline(const char* id, Vec2 a, Vec2 b, std::string& value) {
    box(a, b);
    place(a + Vec2{1, 1});
    ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(0, 0, 0, 0));
    const bool changed = inputMultiline(id, value, size(b - a - Vec2{1, 1}));
    ImGui::PopStyleColor(2);
    return changed;
}

bool SetupArea::dropButton(const char* id, Vec2 at, bool enabled) {
    place(at);
    return arrowButton(painter(), id, ArrowGlyph::Down, {20, 20}, enabled);
}

bool SetupArea::arrow(const char* id, Vec2 at, bool up, bool enabled) {
    place(at);
    return arrowButton(painter(), id, up ? ArrowGlyph::Up : ArrowGlyph::Down, {24, 24}, enabled);
}

bool SetupArea::sideArrow(const char* id, Vec2 a, Vec2 b, bool left, bool enabled) {
    place(a);
    return arrowButton(painter(), id, left ? ArrowGlyph::Left : ArrowGlyph::Right, b - a, enabled);
}

// ---- Free functions ---------------------------------------------------------------------------

bool escapePressed() {
    return ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !ImGui::IsAnyItemActive() && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopup);
}

bool inputText(const char* id, std::string& value, float width, ImGuiInputTextFlags flags) {
    ImGui::SetNextItemWidth(width);
    return ImGui::InputText(id, value.data(), value.capacity() + 1, flags | ImGuiInputTextFlags_CallbackResize, resizeCallback, &value);
}

bool inputMultiline(const char* id, std::string& value, ImVec2 size) {
    return ImGui::InputTextMultiline(id, value.data(), value.capacity() + 1, size,
                                     ImGuiInputTextFlags_CallbackResize | ImGuiInputTextFlags_WordWrap, resizeCallback, &value);
}

// ---- List picker ----------------------------------------------------------------------------

void ListPicker::open(std::string title, std::string heading, std::vector<std::string> rows) {
    title_ = std::move(title);
    heading_ = std::move(heading);
    rows_ = std::move(rows);
    open_ = true;
    pending_ = true;   // opened by draw(), in the ID scope it is drawn in
}

int ListPicker::draw(MenuContext& ctx) {
    if (!open_) return -1;
    if (pending_) {
        ImGui::OpenPopup("##listpicker");
        pending_ = false;
    }
    int chosen = -1;
    const Painter p = ctx.painter();
    const Vec2 size{340, 370};
    const Vec2 min{std::floor((frameW() - size.x) * 0.5f), std::floor((frameH() - size.y) * 0.5f)};
    ImGui::SetNextWindowPos(ctx.at(min), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ctx.size(size), ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    const bool visible = ImGui::BeginPopupModal("##listpicker", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove);
    ImGui::PopStyleVar(2);
    if (!visible) {
        open_ = false;
        return -1;
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 o = ImGui::GetWindowPos();
    auto P = [&](float x, float y) { return ImVec2(o.x + p.px(x), o.y + p.px(y)); };
    drawWindowFrame(p, dl, Rect{min, min + size}, nullptr, 0);
    // The picker's name in the title font at its top left.
    dl->AddText(ctx.fonts.bold, p.fontPx(kTitleSize), P(10, 10), IM_COL32_WHITE, title_.c_str());
    // The list (10,36)–(329,319): a 20 px heading, 17 px rows, the arrow column.
    const float lw = std::max(1.0f, std::floor(p.map.scale)) / p.fbScale;
    dl->AddRect(P(10, 36), P(330, 320), imColor(palette::kFrameLight), 0.0f, lw);
    dl->AddText(ctx.fonts.regular, p.fontPx(kTextSize), P(14, 37), imColor(palette::kLabel), heading_.c_str());
    dl->AddLine(P(11, 56), P(329, 56), imColor(palette::kFrame), lw);
    ImGui::SetCursorScreenPos(P(11, 57));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    beginList(p, "##pickrows", p.size({318, 262}), 17.0f, ImGuiChildFlags_None, false);
    const float rowW = ImGui::GetContentRegionAvail().x;
    for (size_t i = 0; i < rows_.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        const ImVec2 r0 = ImGui::GetCursorScreenPos();
        if (ImGui::InvisibleButton("##pick", ImVec2(rowW, p.px(17)))) chosen = static_cast<int>(i);
        script::reportItem(rows_[i]);
        if (ImGui::IsItemHovered())
            if (Sprite grid = ctx.art.region("Pictures/Game/Dialogs/Rowgrid.bmp", 0, 0, int(rowW / p.k()), 17, false))
                ImGui::GetWindowDrawList()->AddImage(texRef(grid), r0, {r0.x + rowW, r0.y + p.px(17)}, {grid.uv.min.x, grid.uv.min.y},
                                                     {grid.uv.max.x, grid.uv.max.y});
        ImGui::GetWindowDrawList()->AddText({r0.x + p.px(4), r0.y}, IM_COL32_WHITE, rows_[i].c_str());
        ImGui::PopID();
    }
    endList(p);
    ImGui::PopStyleVar(2);
    ImGui::SetCursorScreenPos(P(12, 332));
    const bool cancel = classicButton(p, "Cancel", {316, 26}) || (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !ImGui::IsAnyItemActive());
    if (chosen >= 0 || cancel) {
        ImGui::CloseCurrentPopup();
        open_ = false;
    }
    ImGui::EndPopup();
    return chosen;
}

} // namespace opense4::client::classic::setup
