#include "client/classic/screens/list_widgets.hpp"

#include <imgui_internal.h>

#include <algorithm>
#include <cmath>

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
    ImGuiWindow* rows = nullptr;
};

std::vector<OpenList>& openLists() {
    static std::vector<OpenList> lists;
    return lists;
}

} // namespace

float listRowsWidth(UiContext& ui, float width) { return std::max(1.0f, width - ui.px(kListArrowW) - ui.px(1)); }

bool listArrow(UiContext& ui, const char* id, bool up, Vec2 size, bool enabled) {
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
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float lw = std::max(1.0f, std::floor(ui.map.scale)) / ui.fbScale, h = lw * 0.5f;
    dl->AddRect({a.x + h, a.y + h}, {b.x - h, b.y - h}, imColor(state), 0.0f, lw);
    // A filled triangle, a little smaller than the box, pointing up or down.
    const float cx = std::floor((a.x + b.x) * 0.5f), cy = (a.y + b.y) * 0.5f;
    const float hw = std::floor(std::min(s.x, s.y) * 0.3f), hh = std::floor(std::min(s.x, s.y) * 0.18f);
    if (up) dl->AddTriangleFilled({cx, cy - hh}, {cx + hw, cy + hh}, {cx - hw, cy + hh}, imColor(state));
    else dl->AddTriangleFilled({cx - hw, cy - hh}, {cx + hw, cy - hh}, {cx, cy + hh}, imColor(state));
    return clicked && enabled;
}

void beginList(UiContext& ui, const char* id, ImVec2 size, float step, ImGuiChildFlags rowsFlags, bool border) {
    const ImVec2 padding = ImGui::GetStyle().WindowPadding;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(ui.px(1), ui.px(1)));
    ImGui::BeginChild(id, size, (border ? ImGuiChildFlags_Borders : ImGuiChildFlags_None) | ImGuiChildFlags_AlwaysUseWindowPadding,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, padding);
    ImGui::BeginChild("##rows", ImVec2(listRowsWidth(ui, avail.x), std::max(1.0f, avail.y)), rowsFlags, ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleVar();
    openLists().push_back({step, ImGui::GetCurrentWindow()});
}

namespace {

// The arrow column at the cursor, `height` ImGui units tall, scrolling `rows`.
void arrowColumn(UiContext& ui, const OpenList& list, float scroll, float maxScroll, float height) {
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const float h = height / ui.k();
    const float arrow = std::min(kListArrowW, std::floor(h * 0.5f));
    const float step = ui.px(list.step);
    ImGui::PushID(list.rows ? static_cast<int>(list.rows->ID) : 0);
    if (listArrow(ui, "##up", true, {kListArrowW, arrow}, scroll > 0.5f)) ImGui::SetScrollY(list.rows, std::max(0.0f, scroll - step));
    ImGui::SetCursorScreenPos({at.x, at.y + ui.px(h - arrow)});
    if (listArrow(ui, "##down", false, {kListArrowW, arrow}, scroll < maxScroll - 0.5f))
        ImGui::SetScrollY(list.rows, std::min(maxScroll, scroll + step));
    ImGui::PopID();
}

} // namespace

void endList(UiContext& ui) {
    if (openLists().empty()) return;
    const OpenList list = openLists().back();
    openLists().pop_back();
    const float scroll = ImGui::GetScrollY(), maxScroll = ImGui::GetScrollMaxY();
    const float height = ImGui::GetWindowHeight();
    ImGui::EndChild();
    ImGui::SameLine(0, ui.px(1));
    arrowColumn(ui, list, scroll, maxScroll, height);
    ImGui::EndChild();
}

bool beginListTable(UiContext& ui, const char* id, int columns, ImGuiTableFlags flags, ImVec2 size, float step) {
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float w = size.x > 0 ? size.x : std::max(1.0f, avail.x + size.x);
    const float h = size.y > 0 ? size.y : std::max(1.0f, avail.y + size.y);
    ImGui::BeginGroup();
    // No scroll bar: the arrow column takes its place.
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 0.0f);
    const bool open = ImGui::BeginTable(id, columns, flags | ImGuiTableFlags_ScrollY, ImVec2(listRowsWidth(ui, w), h));
    ImGui::PopStyleVar();
    if (!open) {
        ImGui::EndGroup();
        return false;
    }
    openLists().push_back({step, ImGui::GetCurrentWindow()});
    return true;
}

void endListTable(UiContext& ui) {
    if (openLists().empty()) {
        ImGui::EndTable();
        ImGui::EndGroup();
        return;
    }
    const OpenList list = openLists().back();
    openLists().pop_back();
    const float scroll = ImGui::GetScrollY(), maxScroll = ImGui::GetScrollMaxY();
    ImGui::EndTable();
    const float height = ImGui::GetItemRectSize().y;
    ImGui::SameLine(0, ui.px(1));
    arrowColumn(ui, list, scroll, maxScroll, height);
    ImGui::EndGroup();
}

} // namespace opense4::client::classic
