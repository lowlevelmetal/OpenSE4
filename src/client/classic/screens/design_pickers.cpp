#include "client/classic/screens/design_pickers.hpp"

#include "client/classic/screens/colony_widgets.hpp"
#include "client/classic/screens/list_widgets.hpp"
#include "client/classic/widgets.hpp"

#include <format>

namespace opense4::client::classic {

namespace {

bool cancelled(UiContext& ui) { return ImGui::Button("Cancel", ImVec2(-FLT_MIN, ui.px(26))) || ImGui::IsKeyPressed(ImGuiKey_Escape, false); }

} // namespace

ListPick nameListWindow(UiContext& ui, const char* id, std::span<const std::string> rows) {
    ListPick out;
    if (!beginModal(ui, id, {340, 370})) return out;
    const float footer = ui.px(26) + ImGui::GetStyle().ItemSpacing.y * 2;
    static constexpr ListColumn kName[] = {{"Name", 0, 0, false}};
    listHeader(ui, "##namehead", kName, listRowsWidth(ui, ImGui::GetContentRegionAvail().x));
    beginList(ui, "##names", ImVec2(0, -footer), kListLineStep, ImGuiChildFlags_AlwaysUseWindowPadding);
    for (size_t i = 0; i < rows.size(); ++i)
        if (ImGui::Selectable(std::format("{}##row{}", rows[i], i).c_str())) out.row = i;
    endList(ui);
    if (cancelled(ui) || out.row) {
        out.done = true;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
    return out;
}

ListPick weaponMountWindow(UiContext& ui, const char* id, std::span<const uint32_t> mounts, int32_t current) {
    ListPick out;
    if (!beginModal(ui, id, {380, 440})) return out;
    const auto& all = ui.rules().data().weaponMounts;
    auto nameOf = [&](size_t row) { return row == 0 ? std::string("Normal Mount") : all[mounts[row - 1]].longName; };
    auto descriptionOf = [&](size_t row) { return row == 0 ? std::string{} : all[mounts[row - 1]].description; };
    constexpr float kRowH = 34.0f;   // the name, and the description in small type under it (inferred)
    const float footer = ui.px(26) + ImGui::GetStyle().ItemSpacing.y * 2;
    const float pane = ui.px(76) + ImGui::GetStyle().ItemSpacing.y;
    static constexpr ListColumn kName[] = {{"Name", 0, 0, false}};
    listHeader(ui, "##mounthead", kName, listRowsWidth(ui, ImGui::GetContentRegionAvail().x));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    beginList(ui, "##mounts", ImVec2(0, -(footer + pane)), kRowH);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float w = ImGui::GetContentRegionAvail().x;
    std::optional<size_t> hovered;
    for (size_t row = 0; row <= mounts.size(); ++row) {
        const ImVec2 a = ImGui::GetCursorScreenPos();
        const std::string name = nameOf(row);
        // Named after the mount (input scripts find it so); the text is drawn below.
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 0));
        const bool selected = row == 0 ? current < 0 : current == static_cast<int32_t>(mounts[row - 1]);
        if (ImGui::Selectable(std::format("{}##mount{}", name, row).c_str(), selected, ImGuiSelectableFlags_None, ImVec2(w, ui.px(kRowH)))) out.row = row;
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) {
            hovered = row;
            // The mount's figures (ours).
            if (row > 0) {
                const auto& wm = all[mounts[row - 1]];
                ImGui::SetTooltip("Damage %d%%, size %d%%, cost %d%%, range %+d, to hit %+d%%", wm.damagePercent, wm.tonnagePercent, wm.costPercent,
                                  wm.rangeModifier, wm.toHitModifier);
            }
        }
        dl->PushClipRect(a, {a.x + w, a.y + ui.px(kRowH)}, true);
        dl->AddText({a.x + ui.px(4), a.y + ui.px(2)}, IM_COL32_WHITE, name.c_str());
        if (const std::string text = descriptionOf(row); !text.empty()) {
            ImGui::PushFont(ui.fonts.small, ui.fontPx(kSmallSize));
            dl->AddText({a.x + ui.px(12), a.y + ui.px(18)}, imColor(palette::kSecondary), text.c_str());
            ImGui::PopFont();
        }
        dl->PopClipRect();
    }
    endList(ui);
    ImGui::PopStyleVar(2);
    // The description pane under the list.
    size_t shown = 0;
    for (size_t k = 0; k < mounts.size(); ++k)
        if (current == static_cast<int32_t>(mounts[k])) shown = k + 1;
    if (hovered) shown = *hovered;
    ImGui::PushStyleColor(ImGuiCol_Border, imColorV(palette::kFrameLight));
    ImGui::BeginChild("##mountdescription", ImVec2(0, ui.px(76)), ImGuiChildFlags_Borders);
    ImGui::PopStyleColor();
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(kDimText, "%s", descriptionOf(shown).c_str());
    ImGui::PopTextWrapPos();
    ImGui::EndChild();
    if (cancelled(ui) || out.row) {
        out.done = true;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
    return out;
}

} // namespace opense4::client::classic
