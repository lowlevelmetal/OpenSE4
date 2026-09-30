#include "client/classic/screens/setup_widgets.hpp"

#include <algorithm>
#include <cfloat>

namespace opense4::client::classic::setup {

namespace {

constexpr float kButtonColumn = 190.0f;
constexpr float kButtonHeight = 28.0f;

ImTextureRef texRef(const Sprite& s) { return ImTextureRef(static_cast<ImTextureID>(s.tex.value)); }

int resizeCallback(ImGuiInputTextCallbackData* data) {
    if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
        auto* s = static_cast<std::string*>(data->UserData);
        s->resize(static_cast<size_t>(data->BufTextLen));
        data->Buf = s->data();
    }
    return 0;
}

} // namespace

// ---- SetupFrame --------------------------------------------------------------------------------

SetupFrame::SetupFrame(MenuContext& ctx, const char* title) : ctx_(ctx) {
    ImDrawList* bg = ImGui::GetBackgroundDrawList();
    const ImVec2 a = ctx.at({0, 0}), b = ctx.at({kFrameW, kFrameH});
    if (Sprite art = ctx.art.imageAny({"Pictures/Game/Screens/1024X768/Intro.bmp", "Pictures/Game/Screens/800X600/Intro.bmp"}, false))
        bg->AddImage(texRef(art), a, b, ImVec2(art.uv.min.x, art.uv.min.y), ImVec2(art.uv.max.x, art.uv.max.y));
    bg->AddRectFilled(a, b, IM_COL32(0, 0, 0, 170));

    ImGui::SetNextWindowPos(ctx.at({6, 6}));
    ImGui::SetNextWindowSize(ctx.size({kFrameW - 12, kFrameH - 12}));
    ImGui::PushFont(ctx.fonts.regular, kTextSize * ctx.k());
    visible_ = ImGui::Begin(title, nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                                ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
                                                ImGuiWindowFlags_NoBringToFrontOnFocus);
}

SetupFrame::~SetupFrame() {
    endChild();
    ImGui::End();
    ImGui::PopFont();
}

void SetupFrame::endChild() {
    if (inChild_) ImGui::EndChild();
    inChild_ = false;
}

void SetupFrame::beginContent() {
    endChild();
    ImGui::BeginChild("##content", ImVec2(-(ctx_.px(kButtonColumn) + ImGui::GetStyle().ItemSpacing.x), 0), ImGuiChildFlags_None);
    inChild_ = true;
}

void SetupFrame::beginButtons() {
    endChild();
    ImGui::SameLine();
    ImGui::BeginChild("##buttons", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
    inChild_ = true;
}

bool SetupFrame::pageButton(const char* label, bool active) {
    if (active) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.36f, 0.75f, 1));
        ImGui::PushStyleColor(ImGuiCol_Text, kHighlight);
    }
    const bool clicked = ImGui::Button(label, ImVec2(-FLT_MIN, ctx_.px(kButtonHeight)));
    if (active) ImGui::PopStyleColor(2);
    return clicked;
}

void SetupFrame::toBottom(int count) {
    const float h = ctx_.px(kButtonHeight) + ImGui::GetStyle().ItemSpacing.y;
    const float y = ImGui::GetWindowHeight() - static_cast<float>(count) * h;
    if (ImGui::GetCursorPosY() < y) ImGui::SetCursorPosY(y);
}

bool SetupFrame::button(const char* label, bool enabled) {
    ImGui::BeginDisabled(!enabled);
    const bool clicked = ImGui::Button(label, ImVec2(-FLT_MIN, ctx_.px(kButtonHeight)));
    ImGui::EndDisabled();
    return clicked;
}

bool escapePressed() {
    return ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !ImGui::IsAnyItemActive() && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopup);
}

// ---- Text ----------------------------------------------------------------------------------

void heading(MenuContext& ctx, const char* text) {
    ImGui::PushFont(ctx.fonts.bold, kTitleSize * ctx.k());
    ImGui::TextColored(kLabelBlue, "%s", text);
    ImGui::PopFont();
}

void note(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, kDim);
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}

void labelValue(MenuContext& ctx, const char* label, const std::string& value, float valueColumn) {
    ImGui::TextColored(kLabelBlue, "%s", label);
    ImGui::SameLine(ctx.px(valueColumn));
    ImGui::TextUnformatted(value.c_str());
}

// ---- Lamps ----------------------------------------------------------------------------------

bool lamp(MenuContext& ctx, const char* label, bool& value, bool enabled) {
    ImGui::PushID(label);
    const ImGuiStyle& style = ImGui::GetStyle();
    const float h = ImGui::GetFrameHeight();
    const ImVec2 textSize = ImGui::CalcTextSize(label);
    ImGui::BeginDisabled(!enabled);
    const bool clicked = ImGui::InvisibleButton("##lamp", ImVec2(h + style.ItemInnerSpacing.x + textSize.x, h));
    const bool hovered = ImGui::IsItemHovered();
    ImGui::EndDisabled();
    const ImVec2 p = ImGui::GetItemRectMin();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 c(p.x + h * 0.5f, p.y + h * 0.5f);
    const float r = std::max(3.0f, h * 0.26f);
    // GetColorU32 applies the style alpha, so lamps dim inside BeginDisabled blocks too.
    auto col = [](ImU32 c32) { return ImGui::GetColorU32(c32); };
    dl->AddCircleFilled(c, r + ctx.px(2), col(IM_COL32(6, 12, 28, 255)));
    if (value) {
        dl->AddCircleFilled(c, r + ctx.px(3), col(enabled ? IM_COL32(60, 230, 90, 60) : IM_COL32(60, 120, 70, 40)));
        dl->AddCircleFilled(c, r, col(enabled ? IM_COL32(70, 235, 100, 255) : IM_COL32(60, 120, 70, 255)));
        dl->AddCircleFilled(ImVec2(c.x - r * 0.3f, c.y - r * 0.3f), r * 0.35f, col(IM_COL32(210, 255, 215, 200)));
    } else {
        dl->AddCircleFilled(c, r, col(IM_COL32(18, 42, 26, 255)));
    }
    dl->AddCircle(c, r + ctx.px(1), col(hovered && enabled ? IM_COL32(150, 190, 255, 255) : IM_COL32(60, 95, 180, 255)), 0, ctx.px(1));
    const ImU32 textColor = !enabled ? ImGui::GetColorU32(ImGuiCol_TextDisabled)
                            : hovered ? ImGui::GetColorU32(kHighlight)
                                      : ImGui::GetColorU32(ImGuiCol_Text);
    dl->AddText(ImVec2(p.x + h + style.ItemInnerSpacing.x, p.y + (h - textSize.y) * 0.5f), textColor, label);
    ImGui::PopID();
    if (clicked && enabled) value = !value;
    return clicked && enabled;
}

bool lampChoice(MenuContext& ctx, const char* id, int& value, std::span<const std::string> labels, bool vertical) {
    bool changed = false;
    ImGui::PushID(id);
    for (size_t i = 0; i < labels.size(); ++i) {
        if (i > 0 && !vertical) ImGui::SameLine(0, ctx.px(18));
        ImGui::PushID(static_cast<int>(i));
        bool on = value == static_cast<int>(i);
        if (lamp(ctx, labels[i].c_str(), on) && value != static_cast<int>(i)) {
            value = static_cast<int>(i);
            changed = true;
        }
        ImGui::PopID();
    }
    ImGui::PopID();
    return changed;
}

bool lampChoice(MenuContext& ctx, const char* id, int& value, std::initializer_list<const char*> labels, bool vertical) {
    std::vector<std::string> list(labels.begin(), labels.end());
    return lampChoice(ctx, id, value, std::span<const std::string>(list), vertical);
}

// ---- Text fields ------------------------------------------------------------------------------

bool inputText(const char* id, std::string& value, float width, ImGuiInputTextFlags flags) {
    ImGui::SetNextItemWidth(width);
    return ImGui::InputText(id, value.data(), value.capacity() + 1, flags | ImGuiInputTextFlags_CallbackResize, resizeCallback, &value);
}

bool inputMultiline(const char* id, std::string& value, ImVec2 size) {
    return ImGui::InputTextMultiline(id, value.data(), value.capacity() + 1, size,
                                     ImGuiInputTextFlags_CallbackResize | ImGuiInputTextFlags_WordWrap, resizeCallback, &value);
}

bool inputWithSuggestions(MenuContext& ctx, const char* id, std::string& value, const std::vector<std::string>& suggestions, float width) {
    ImGui::PushID(id);
    const float arrow = ImGui::GetFrameHeight();
    bool changed = inputText("##text", value, std::max(ctx.px(40), width - arrow));
    if (!suggestions.empty()) {
        ImGui::SameLine(0, 0);
        if (ImGui::ArrowButton("##more", ImGuiDir_Down)) ImGui::OpenPopup("##suggestions");
        ImGui::SetNextWindowSizeConstraints(ImVec2(width, 0), ImVec2(std::max(width, ctx.px(260)), ctx.px(300)));
        if (ImGui::BeginPopup("##suggestions")) {
            for (size_t i = 0; i < suggestions.size(); ++i) {
                ImGui::PushID(static_cast<int>(i));
                if (ImGui::Selectable(suggestions[i].c_str(), suggestions[i] == value)) {
                    value = suggestions[i];
                    changed = true;
                }
                ImGui::PopID();
            }
            ImGui::EndPopup();
        }
    }
    ImGui::PopID();
    return changed;
}

// ---- Sprites ---------------------------------------------------------------------------------

void sprite(const Sprite& s, ImVec2 size) {
    if (s) {
        ImGui::Image(texRef(s), size, ImVec2(s.uv.min.x, s.uv.min.y), ImVec2(s.uv.max.x, s.uv.max.y));
        return;
    }
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy(size);
    ImGui::GetWindowDrawList()->AddRect(p, ImVec2(p.x + size.x, p.y + size.y), IM_COL32(50, 70, 120, 255));
}

bool spriteButton(const char* id, const Sprite& s, ImVec2 size, bool selected) {
    const ImVec4 bg = selected ? ImVec4(0.3f, 0.5f, 1.0f, 0.55f) : ImVec4(0, 0, 0, 0);
    if (s) return ImGui::ImageButton(id, texRef(s), size, ImVec2(s.uv.min.x, s.uv.min.y), ImVec2(s.uv.max.x, s.uv.max.y), bg);
    return ImGui::Button(id, size);
}

bool arrowButton(MenuContext& ctx, const char* id, bool left, Vec2 frameSize) {
    const bool clicked = ImGui::InvisibleButton(id, ctx.size(frameSize));
    // SmallLeftRightArrows: left and right halves of 16x38, four state rows (normal, hover, pressed, disabled).
    const int state = ImGui::IsItemActive() ? 2 : ImGui::IsItemHovered() ? 1 : 0;
    const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (Sprite s = ctx.art.region("Pictures/Game/Buttons/SmallLeftRightArrows.bmp", left ? 0 : 16, state * 38, 16, 38, false)) {
        dl->AddImage(texRef(s), a, b, ImVec2(s.uv.min.x, s.uv.min.y), ImVec2(s.uv.max.x, s.uv.max.y));
    } else {
        const ImU32 col = state == 0 ? IM_COL32(90, 120, 220, 255) : IM_COL32(150, 180, 255, 255);
        const float mx = (a.x + b.x) * 0.5f, my = (a.y + b.y) * 0.5f, w = (b.x - a.x) * 0.35f, h = (b.y - a.y) * 0.3f;
        if (left) dl->AddTriangleFilled(ImVec2(mx - w, my), ImVec2(mx + w, my - h), ImVec2(mx + w, my + h), col);
        else dl->AddTriangleFilled(ImVec2(mx + w, my), ImVec2(mx - w, my - h), ImVec2(mx - w, my + h), col);
    }
    return clicked;
}

} // namespace opense4::client::classic::setup
