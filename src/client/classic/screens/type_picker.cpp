#include "client/classic/screens/type_picker.hpp"

#include "client/classic/screens/colony_logic.hpp"
#include "client/classic/screens/list_widgets.hpp"

#include <algorithm>
#include <cfloat>
#include <format>

namespace opense4::client::classic {

void TypeListPicker::open(Config config, std::optional<std::string> preselect) {
    configure(std::move(config), std::move(preselect));
    request_ = true;
}

void TypeListPicker::configure(Config config, std::optional<std::string> preselect) {
    config_ = std::move(config);
    selected_ = std::move(preselect);
}

TypeListPicker::Result TypeListPicker::drawPopup(UiContext& ui, const std::vector<std::string>& types) {
    const std::string id = std::format("{}###typelist{}", config_.title, static_cast<const void*>(this));
    if (request_) {
        ImGui::OpenPopup(id.c_str());
        request_ = false;
    }
    ImGui::SetNextWindowPos(ui.at({frameW() * 0.5f, frameH() * 0.5f}), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ui.size({380, 480}), ImGuiCond_Always);
    if (!ImGui::BeginPopupModal(id.c_str(), nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings))
        return {};
    Result res = drawBody(ui, types);
    if (res.chosen || res.cancelled) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
    return res;
}

TypeListPicker::Result TypeListPicker::drawBody(UiContext& ui, const std::vector<std::string>& types) {
    Result res;
    if (selected_ && std::find(types.begin(), types.end(), *selected_) == types.end()) selected_.reset();
    if (!config_.text.empty()) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(config_.text.c_str());
        ImGui::PopTextWrapPos();
    }
    // The list fills the window above the two rows of buttons.
    const float buttonH = std::max(ui.px(26), ImGui::GetFrameHeight());
    const float footer = buttonH * 2 + ImGui::GetStyle().ItemSpacing.y * 3;
    const float rowH = ImGui::GetTextLineHeightWithSpacing();
    const float listH = config_.rows > 0 ? rowH * static_cast<float>(std::clamp<size_t>(types.size(), 1, config_.rows)) + ImGui::GetStyle().WindowPadding.y * 2 + rowH * 0.5f
                                         : std::max(ui.px(120), ImGui::GetContentRegionAvail().y - footer);
    beginList(ui, "##typelist", ImVec2(0, listH), kListLineStep, ImGuiChildFlags_AlwaysUseWindowPadding);
    for (size_t i = 0; i < types.size(); ++i) {
        const std::string& t = types[i];
        const bool marked = !config_.marked.empty() && t == config_.marked;
        const std::string label = std::format("{}{}##type{}", t, marked ? " (suggested)" : "", i);
        const bool selected = selected_ && *selected_ == t;
        if (ImGui::Selectable(label.c_str(), selected, ImGuiSelectableFlags_AllowDoubleClick)) {
            // A click on the row already selected (a double click's second
            // click included) chooses it; any other click selects.
            if (selected) res.chosen = t;
            else selected_ = t;
        }
        if (!config_.tagChooser.empty() && config_.tagOption) ui.tagOption(config_.tagChooser, config_.tagOption(t));
    }
    if (types.empty()) ImGui::TextColored(kDimText, "No types yet: Add Type makes one.");
    endList(ui);

    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float w = (ImGui::GetContentRegionAvail().x - spacing) * 0.5f;
    if (ImGui::Button("Add Type", ImVec2(w, buttonH))) prompt_.open(config_.addTitle, config_.addTitle, "");
    ImGui::SameLine();
    ImGui::BeginDisabled(!selected_);
    if (ImGui::Button("Delete Type", ImVec2(w, buttonH)) && selected_) {
        // No question, and no check of what uses the type (confirmed: binary).
        res.deleted = *selected_;
        selected_.reset();
    }
    ImGui::EndDisabled();
    ImGui::BeginDisabled(!selected_);
    const bool ok = ImGui::Button("OK", ImVec2(w, buttonH));
    ImGui::EndDisabled();
    ImGui::SameLine();
    const bool cancel = ImGui::Button("Cancel", ImVec2(w, buttonH));
    if (const std::optional<std::string> name = prompt_.draw(ui)) {
        if (canAddType(types, *name, config_.limit)) {
            res.added = *name;
            selected_ = *name;
        }
    } else if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
        // Keys answer the list while Add Type's question is not in front.
        if (selected_ && (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false))) res.chosen = *selected_;
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !ImGui::IsAnyItemActive()) res.cancelled = true;
    }
    if (ok && selected_) res.chosen = *selected_;
    if (cancel) res.cancelled = true;
    return res;
}

} // namespace opense4::client::classic
