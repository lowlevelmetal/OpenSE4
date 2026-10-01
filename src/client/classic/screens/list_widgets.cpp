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
    dl->AddText(ImVec2(min.x + ui.px(2), std::floor(min.y + (max.y - min.y - ImGui::GetTextLineHeight()) * 0.5f)), imColor(color), col.label);
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

} // namespace opense4::client::classic
