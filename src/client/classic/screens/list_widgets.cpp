#include "client/classic/screens/list_widgets.hpp"

#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <string_view>

namespace opense4::client::classic {

// ---- Classic lists -----------------------------------------------------------------------------

std::vector<float> columnEdges(UiContext& ui, std::span<const ListColumn> cols, float width) {
    std::vector<float> x{0.0f};
    float fixed = 0;
    for (const ListColumn& c : cols) fixed += ui.px(c.width);
    for (const ListColumn& c : cols) x.push_back(x.back() + (c.width > 0 ? ui.px(c.width) : std::max(0.0f, width - fixed)));
    return x;
}

namespace {

// Heading colours (spec 06 §5.4): label blue normally; these under the pointer and while held.
constexpr uint32_t kHeadingHot = 0x8ca6ff;   // (140,166,255)
constexpr uint32_t kHeadingHeld = 0xc4d1ff;  // (196,209,255)
constexpr uint32_t kHeadingFixed = 0x808080; // a column that cannot be clicked

} // namespace

void drawHeading(UiContext& ui, ImDrawList* dl, ImVec2 min, ImVec2 max, const ListColumn& col, bool hovered, bool held) {
    const bool active = col.sortable && (hovered || held);
    if (active) {
        // The RowGrid texture behind the label, copied 1:1 from its corner.
        const int w = std::clamp(int(std::lround((max.x - min.x) / ui.k())), 1, 750);
        const int h = std::clamp(int(std::lround((max.y - min.y) / ui.k())), 1, 600);
        if (Sprite grid = ui.art.region("Pictures/Game/Dialogs/RowGrid.bmp", 0, 0, w, h, false))
            dl->AddImage(ImTextureRef(static_cast<ImTextureID>(grid.tex.value)), min, max, {grid.uv.min.x, grid.uv.min.y},
                         {grid.uv.max.x, grid.uv.max.y});
    }
    if (!col.label || !*col.label) return;
    uint32_t color = !col.sortable ? kHeadingFixed : held ? kHeadingHeld : hovered ? kHeadingHot : palette::kLabel;
    if (col.color != 0) color = col.color;
    dl->PushClipRect(min, max, true);
    const ImVec2 at(min.x + ui.px(2), std::floor(min.y + (max.y - min.y - ImGui::GetTextLineHeight()) * 0.5f));
    dl->AddText(at, imColor(color), col.label);
    if (col.icon >= 0)
        if (const Sprite icon = ui.art.icon16(static_cast<Icon>(col.icon))) {
            // The resource's 16 px icon right after the label (observed, spec 07 session 3).
            const ImVec2 p0(std::floor(at.x + ImGui::CalcTextSize(col.label).x + ui.px(2)), std::floor((min.y + max.y - ui.px(16)) * 0.5f));
            dl->AddImage(ImTextureRef(static_cast<ImTextureID>(icon.tex.value)), p0, ImVec2(p0.x + ui.px(16), p0.y + ui.px(16)), {icon.uv.min.x, icon.uv.min.y},
                         {icon.uv.max.x, icon.uv.max.y});
        }
    dl->PopClipRect();
}

int listHeader(UiContext& ui, const char* id, std::span<const ListColumn> cols, float width) {
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const std::vector<float> x = columnEdges(ui, cols, width);
    const float h = ui.px(20);
    ImGui::PushID(id);
    int clicked = -1;
    for (size_t i = 0; i < cols.size(); ++i) {
        ImGui::SetCursorScreenPos(ImVec2(origin.x + x[i], origin.y));
        ImGui::PushID(int(i));
        const ImVec2 size(std::max(1.0f, x[i + 1] - x[i]), h);
        bool hovered = false, held = false;
        if (cols[i].sortable) {
            if (ImGui::InvisibleButton("##head", size)) clicked = int(i);
            hovered = ImGui::IsItemHovered();
            held = ImGui::IsItemActive();
        } else {
            ImGui::Dummy(size);
        }
        ImGui::PopID();
        drawHeading(ui, ImGui::GetWindowDrawList(), ImVec2(origin.x + x[i], origin.y), ImVec2(origin.x + x[i + 1], origin.y + h), cols[i], hovered,
                    held);
    }
    ImGui::PopID();
    ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + h));
    ImGui::Dummy(ImVec2(width, 0));
    return clicked;
}

int tableHeadings(UiContext& ui, std::span<const ListColumn> cols) {
    const float h = ui.px(20);
    ImGui::TableNextRow(ImGuiTableRowFlags_Headers, h);
    int clicked = -1;
    const float padX = ImGui::GetStyle().CellPadding.x, padY = ImGui::GetStyle().CellPadding.y;
    for (size_t i = 0; i < cols.size(); ++i) {
        if (!ImGui::TableSetColumnIndex(int(i))) continue;
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const float w = std::max(1.0f, ImGui::GetContentRegionAvail().x);
        // The heading fills its cell, padding included.
        const ImVec2 min(at.x - padX, at.y - padY), max(at.x + w + padX, at.y - padY + h);
        ImGui::PushID(int(i));
        bool hovered = false, held = false;
        if (cols[i].sortable) {
            if (ImGui::InvisibleButton("##head", ImVec2(w, std::max(1.0f, h - 2 * padY)))) clicked = int(i);
            hovered = ImGui::IsItemHovered();
            held = ImGui::IsItemActive();
        } else {
            ImGui::Dummy(ImVec2(w, std::max(1.0f, h - 2 * padY)));
        }
        ImGui::PopID();
        drawHeading(ui, ImGui::GetWindowDrawList(), min, max, cols[i], hovered, held);
    }
    return clicked;
}

float listRowsHeight(UiContext& ui) {
    const ImGuiWindow* root = ImGui::GetCurrentWindowRead()->RootWindow;
    return std::max(ui.px(kListRowH), root->Size.y - ui.px(265));
}

// ---- Lists with the arrow column ----------------------------------------------------------------

namespace {

struct OpenList {
    float step = kListRowH;
    ImGuiWindow* box = nullptr;   // the framed box
    ImGuiWindow* rows = nullptr;  // the window that scrolls (the rows' child or the table's)
    bool disabled = false;
    std::string name;             // the list id without its "##"
};

std::vector<OpenList>& openLists() {
    static std::vector<OpenList> lists;
    return lists;
}

// Repeats of a held arrow every 100 ms after the click (spec 06 §7 Q89).
constexpr float kRepeat = 0.1f;

// Steps one press of an InvisibleButton just drawn gives this frame.
int pressSteps(bool pressed) {
    if (pressed) return 1;
    if (!ImGui::IsItemActive()) return 0;
    const float t = ImGui::GetIO().MouseDownDuration[ImGuiMouseButton_Left];
    if (t <= 0.0f) return 0;
    return ImGui::CalcTypematicRepeatAmount(t - ImGui::GetIO().DeltaTime, t, kRepeat, kRepeat);
}

// The outline of the thumb (and of our own arrows): 1 frame pixel, inside [a, b].
void outline(const Painter& ui, ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 c) {
    // Four filled edges: crisp at any scale.
    const float lw = std::max(1.0f, std::floor(ui.map.scale)) / ui.fbScale;
    if (b.x - a.x <= 2 * lw || b.y - a.y <= 2 * lw) {
        dl->AddRectFilled(a, b, c);
        return;
    }
    dl->AddRectFilled(a, {b.x, a.y + lw}, c);
    dl->AddRectFilled({a.x, b.y - lw}, b, c);
    dl->AddRectFilled({a.x, a.y + lw}, {a.x + lw, b.y - lw}, c);
    dl->AddRectFilled({b.x - lw, a.y + lw}, {b.x, b.y - lw}, c);
}

void glyph(const Painter& ui, ImDrawList* dl, ImVec2 a, ImVec2 b, ArrowGlyph g, ImU32 c) {
    outline(ui, dl, a, b, c);
    const float cx = std::floor((a.x + b.x) * 0.5f), cy = std::floor((a.y + b.y) * 0.5f);
    const float s = std::min(b.x - a.x, b.y - a.y);
    const float hw = std::floor(s * 0.3f), hh = std::floor(s * 0.18f);
    switch (g) {
        case ArrowGlyph::Up: dl->AddTriangleFilled({cx, cy - hh}, {cx + hw, cy + hh}, {cx - hw, cy + hh}, c); break;
        case ArrowGlyph::Down: dl->AddTriangleFilled({cx - hw, cy - hh}, {cx + hw, cy - hh}, {cx, cy + hh}, c); break;
        case ArrowGlyph::Left: dl->AddTriangleFilled({cx - hh, cy}, {cx + hh, cy - hw}, {cx + hh, cy + hw}, c); break;
        case ArrowGlyph::Right: dl->AddTriangleFilled({cx + hh, cy}, {cx - hh, cy + hw}, {cx - hh, cy - hw}, c); break;
        case ArrowGlyph::Stop: dl->AddRectFilled({cx - hh, cy - hh}, {cx + hh, cy + hh}, c); break;
    }
}

// One arrow of the column `size` ImGui units square at the cursor; the steps to scroll.
int columnArrow(const Painter& ui, const char* id, bool up, float size, bool enabled) {
    const ImVec2 a = ImGui::GetCursorScreenPos();
    const ImVec2 b{a.x + size, a.y + size};
    ImGui::BeginDisabled(!enabled);
    const bool pressed = ImGui::InvisibleButton(id, ImVec2(size, size), ImGuiButtonFlags_PressedOnClick);
    ImGui::EndDisabled();
    const int steps = enabled ? pressSteps(pressed) : 0;
    const bool hovered = enabled && ImGui::IsItemHovered();
    const bool held = enabled && ImGui::IsItemActive();
    // The sheet's state rows: 0 normal, 1 under the pointer, 2 held, 3 dim.
    const int state = !enabled ? 3 : held ? 2 : hovered ? 1 : 0;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (Sprite s = ui.art.region("Pictures/Game/Buttons/Arrows.bmp", up ? 24 : 0, state * 24, 24, 24, false))
        dl->AddImage(ImTextureRef(static_cast<ImTextureID>(s.tex.value)), a, b, {s.uv.min.x, s.uv.min.y}, {s.uv.max.x, s.uv.max.y});
    else {
        const uint32_t c = state == 3 ? palette::kDisabled : state == 2 ? palette::kButtonHeld : state == 1 ? palette::kButtonHot : palette::kButton;
        glyph(ui, dl, a, b, up ? ArrowGlyph::Up : ArrowGlyph::Down, imColor(c));
    }
    return steps;
}

// The column at the cursor, `height` ImGui units tall, scrolling `list.rows`;
// its parts' rectangles go to `parts` (up, down, track, thumb).
void arrowColumn(const Painter& ui, const OpenList& list, float height, std::array<std::pair<ImVec2, ImVec2>, 4>& parts) {
    ImGuiWindow* rows = list.rows;
    const float scroll = rows ? rows->Scroll.y : 0.0f, maxScroll = rows ? rows->ScrollMax.y : 0.0f;
    const float visible = rows ? rows->InnerRect.GetHeight() : height;
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const float w = ui.px(kListArrowW);
    const float arrow = std::min(w, std::floor(height * 0.5f));
    const float step = ui.px(list.step);
    float target = scroll;
    ImGui::PushID(list.name.c_str());
    // The up arrow at the top, the down arrow at the bottom.
    target -= float(columnArrow(ui, "##up", true, arrow, !list.disabled && scroll > 0.5f)) * step;
    parts[0] = {ImGui::GetItemRectMin(), ImGui::GetItemRectMax()};
    ImGui::SetCursorScreenPos({at.x, at.y + height - arrow});
    target += float(columnArrow(ui, "##down", false, arrow, !list.disabled && scroll < maxScroll - 0.5f)) * step;
    parts[1] = {ImGui::GetItemRectMin(), ImGui::GetItemRectMax()};
    // The track between them and its thumb.
    const float trackTop = at.y + arrow, trackH = std::max(0.0f, height - 2 * arrow);
    const float content = visible + maxScroll;
    const float thumbH = trackH <= 0 ? 0.0f : std::min(trackH, std::max(ui.px(6), content > 0 ? std::floor(trackH * visible / content) : trackH));
    const float thumbY = trackTop + (maxScroll > 0 ? std::floor((trackH - thumbH) * scroll / maxScroll) : 0.0f);
    parts[2] = {{at.x, trackTop}, {at.x + w, trackTop + trackH}};
    parts[3] = {{at.x, thumbY}, {at.x + w, thumbY + thumbH}};
    if (trackH >= 1.0f) {
        ImGui::SetCursorScreenPos({at.x, trackTop});
        ImGui::BeginDisabled(list.disabled);
        ImGui::InvisibleButton("##track", ImVec2(w, trackH), ImGuiButtonFlags_PressedOnClick);
        ImGui::EndDisabled();
        if (ImGui::IsItemActive() && maxScroll > 0 && trackH > thumbH) {
            // Straight to the pointer: the thumb centred on it, a whole row at a time.
            const float frac = std::clamp((ImGui::GetIO().MousePos.y - trackTop - thumbH * 0.5f) / (trackH - thumbH), 0.0f, 1.0f);
            target = step > 0 ? std::round(frac * maxScroll / step) * step : frac * maxScroll;
            if (frac >= 1.0f) target = maxScroll;
        }
        if (thumbH >= 1.0f)
            outline(ui, ImGui::GetWindowDrawList(), parts[3].first, parts[3].second,
                    imColor(list.disabled ? palette::kDisabled : palette::kFrameLight));
    }
    ImGui::PopID();
    if (rows && target != scroll) ImGui::SetScrollY(rows, std::clamp(target, 0.0f, maxScroll));
}

// The mouse wheel over the list: a row per notch (observed, spec 07 session 5).
// The lists' windows take no wheel themselves (ImGuiWindowFlags_NoScrollWithMouse).
void wheel(const Painter& ui, const OpenList& list) {
    ImGuiWindow* rows = list.rows;
    const float step = ui.px(list.step);
    const float notches = ImGui::GetIO().MouseWheel;
    if (!rows || notches == 0.0f || list.disabled) return;
    const ImGuiWindow* hovered = GImGui->HoveredWindow;
    // Over the box or anything in it that does not scroll by itself.
    bool over = false;
    for (const ImGuiWindow* w = hovered; w; w = w->ParentWindow) {
        if (w == list.box) {
            over = true;
            break;
        }
        if (w != rows && w->ScrollMax.y > 0.0f && !(w->Flags & ImGuiWindowFlags_NoScrollWithMouse)) return;
        if (!(w->Flags & ImGuiWindowFlags_ChildWindow)) break;
    }
    if (!over) return;
    ImGui::SetScrollY(rows, std::clamp(rows->Scroll.y - notches * step, 0.0f, rows->ScrollMax.y));
}

std::string listName(const char* id) {
    std::string_view s(id);
    while (s.starts_with('#')) s.remove_prefix(1);
    return std::string(s);
}

// Opens the framed box; the rows go at (2,2), (W − 30) × (H − 4) frame pixels.
ImVec2 openBox(const Painter& ui, const char* id, ImVec2 size, bool border) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(ui.px(kListInset), ui.px(kListInset)));
    ImGui::PushStyleColor(ImGuiCol_Border, imColorV(palette::kFrameLight));
    ImGui::BeginChild(id, size, (border ? ImGuiChildFlags_Borders : ImGuiChildFlags_None) | ImGuiChildFlags_AlwaysUseWindowPadding,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    return {listRowsWidth(ui, avail.x + ui.px(2 * kListInset)), std::max(1.0f, avail.y)};
}

// Closes the box: the column at its right, the wheel, the tags.
void closeBox(const Painter& ui, const OpenList& list, ImVec2 rowsMin, float height) {
    wheel(ui, list);
    ImGuiWindow* box = list.box;
    std::array<std::pair<ImVec2, ImVec2>, 4> parts{};
    ImGui::SetCursorScreenPos({box->Pos.x + box->Size.x - ui.px(kListColumnW - kListInset), rowsMin.y});
    arrowColumn(ui, list, height, parts);
    ImGui::EndChild();
    if (UiContext* tags = ui.tagger) {
        UiContext::ListParts p;
        p.min = ImGui::GetItemRectMin();
        p.max = ImGui::GetItemRectMax();
        p.parts = parts;
        p.valid = true;
        if (tags->drawing && !list.name.empty()) tags->tagListParts(std::string(windowId(*tags->drawing)) + ":" + list.name, p);
        tags->lastList = p;
    }
}

} // namespace

float listRowsWidth(const Painter& ui, float width) { return std::max(1.0f, width - ui.px(kListColumnW + kListInset)); }

int listArrow(const Painter& ui, const char* id, bool up, bool enabled) { return columnArrow(ui, id, up, ui.px(kListArrowW), enabled); }

bool arrowButton(const Painter& ui, const char* id, ArrowGlyph g, Vec2 size, bool enabled) {
    const ImVec2 a = ImGui::GetCursorScreenPos();
    const ImVec2 s = ui.size(size);
    const ImVec2 b{a.x + s.x, a.y + s.y};
    ImGui::PushItemFlag(ImGuiItemFlags_ButtonRepeat, true);
    ImGui::BeginDisabled(!enabled);
    const bool clicked = ImGui::InvisibleButton(id, s);
    ImGui::EndDisabled();
    ImGui::PopItemFlag();
    const bool hovered = enabled && ImGui::IsItemHovered();
    const bool held = hovered && ImGui::IsItemActive();
    // The button blue in its four states (spec 06 §5.4), as the column's buttons.
    const uint32_t state = !enabled ? palette::kDisabled : held ? palette::kButtonHeld : hovered ? palette::kButtonHot : palette::kButton;
    glyph(ui, ImGui::GetWindowDrawList(), a, b, g, imColor(state));
    return clicked && enabled;
}

void beginList(const Painter& ui, const char* id, ImVec2 size, float step, ImGuiChildFlags rowsFlags, bool border) {
    const ImVec2 padding = ImGui::GetStyle().WindowPadding;
    const bool disabled = (GImGui->CurrentItemFlags & ImGuiItemFlags_Disabled) != 0;
    const ImVec2 rows = openBox(ui, id, size, border);
    ImGuiWindow* box = ImGui::GetCurrentWindow();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, padding);
    ImGui::BeginChild("##rows", rows, rowsFlags, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    openLists().push_back({step, box, ImGui::GetCurrentWindow(), disabled, listName(id)});
}

void endList(const Painter& ui) {
    if (openLists().empty()) return;
    const OpenList list = openLists().back();
    openLists().pop_back();
    const ImVec2 rowsMin = ImGui::GetWindowPos();
    const float height = ImGui::GetWindowHeight();
    ImGui::EndChild();
    closeBox(ui, list, rowsMin, height);
}

bool beginListTable(const Painter& ui, const char* id, int columns, ImGuiTableFlags flags, ImVec2 size, float step) {
    const bool disabled = (GImGui->CurrentItemFlags & ImGuiItemFlags_Disabled) != 0;
    const ImVec2 rows = openBox(ui, id, size, true);
    ImGuiWindow* box = ImGui::GetCurrentWindow();
    // No scroll bar: the arrow column takes its place; the box is the frame.
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 0.0f);
    const bool open = ImGui::BeginTable("##table", columns, (flags & ~ImGuiTableFlags_BordersOuter) | ImGuiTableFlags_ScrollY, rows);
    ImGui::PopStyleVar();
    if (!open) {
        ImGui::EndChild();
        return false;
    }
    ImGuiWindow* inner = ImGui::GetCurrentWindow();
    inner->Flags |= ImGuiWindowFlags_NoScrollWithMouse;  // the wheel is the list's (wheel())
    openLists().push_back({step, box, inner, disabled, listName(id)});
    return true;
}

void endListTable(const Painter& ui) {
    if (openLists().empty()) {
        ImGui::EndTable();
        ImGui::EndChild();
        return;
    }
    const OpenList list = openLists().back();
    openLists().pop_back();
    const ImVec2 rowsMin = list.rows->Pos;
    const float height = list.rows->Size.y;
    ImGui::EndTable();
    closeBox(ui, list, rowsMin, height);
}

} // namespace opense4::client::classic
