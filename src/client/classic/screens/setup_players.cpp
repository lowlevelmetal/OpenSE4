#include "client/classic/screens/setup_players.hpp"

#include "client/classic/layout.hpp"
#include "client/classic/screens/list_widgets.hpp"
#include "client/classic/screens/setup_widgets.hpp"
#include "client/script/items.hpp"
#include "game/players.hpp"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <format>
#include <vector>

namespace opense4::client::classic::setup {

namespace {

ImTextureRef texRef(const Sprite& s) { return ImTextureRef(static_cast<ImTextureID>(s.tex.value)); }

void drawLamp(MenuContext& ctx, ImDrawList* dl, ImVec2 centre, bool on) {
    const Painter p = ctx.painter();
    const ImVec2 a{centre.x - p.px(6.5f), centre.y - p.px(6.5f)}, b{centre.x + p.px(6.5f), centre.y + p.px(6.5f)};
    // The lamps of General.bmp, as the setup screens' lamp lists draw them.
    if (const Sprite s = ctx.art.region("Pictures/Game/General.bmp", 178 + 13 * (on ? 1 : 0), 0, 13, 13))
        dl->AddImage(texRef(s), a, b, {s.uv.min.x, s.uv.min.y}, {s.uv.max.x, s.uv.max.y});
    else
        dl->AddCircleFilled(centre, p.px(5), on ? IM_COL32(80, 220, 90, 255) : IM_COL32(60, 90, 200, 255));
}

// The window's frame and title, centred on the frame; false when it is not shown.
bool beginWindow(MenuContext& ctx, const char* id, Vec2 size, std::string_view title, Vec2& min) {
    min = {std::floor((frameW() - size.x) * 0.5f), std::floor((frameH() - size.y) * 0.5f)};
    ImGui::SetNextWindowPos(ctx.at(min), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ctx.size(size), ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    const bool visible = ImGui::BeginPopupModal(id, nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove);
    ImGui::PopStyleVar(2);
    if (!visible) return false;
    const Painter p = ctx.painter();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    drawWindowFrame(p, dl, Rect{min, min + size}, nullptr, 0);
    const ImVec2 o = ImGui::GetWindowPos();
    const std::string t(title);
    dl->AddText(ctx.fonts.bold, p.fontPx(kTitleSize), ImVec2(o.x + p.px(10), o.y + p.px(10)), IM_COL32_WHITE, t.c_str());
    return true;
}

// Wrapped reading text at a window point; returns its height in frame pixels.
float wrappedText(MenuContext& ctx, ImDrawList* dl, ImVec2 at, std::string_view text, float width, ImU32 color) {
    const Painter p = ctx.painter();
    ImFont* font = ctx.fonts.regular;
    const float size = p.textPx(kTextSize);
    const ImVec2 extent = font->CalcTextSizeA(size, FLT_MAX, p.px(width), text.data(), text.data() + text.size());
    dl->AddText(font, size, at, color, text.data(), text.data() + text.size(), p.px(width));
    return extent.y / p.k();
}

struct PickerRow {
    std::optional<game::Controller> player;
    std::string name, mod, description, item;
};

} // namespace

bool offersComputerPlayers(const game::Rules& r) { return !computerPlayerChoices(r).empty(); }

bool playersLine(MenuContext& ctx, std::string_view text, float textBottom, ImVec2 textLeft, ImVec2 buttonAt, ImVec2 buttonSize, float width) {
    const Painter p = ctx.painter();
    // In the small face, as the line about the mods, its last line ending at textBottom.
    ImFont* font = p.fonts.small ? p.fonts.small : p.fonts.regular;
    const float size = p.fontPx(kSmallSize);
    const ImVec2 extent = font->CalcTextSizeA(size, FLT_MAX, width, text.data(), text.data() + text.size());
    const ImVec2 at(textLeft.x, textBottom - extent.y);
    ImGui::GetWindowDrawList()->AddText(font, size, at, imColor(palette::kSecondary), text.data(), text.data() + text.size(), width);
    script::reportItem(text, at, ImVec2(at.x + width, at.y + extent.y));   // input scripts read it
    ImGui::SetCursorScreenPos(buttonAt);
    ImGui::PushFont(p.fonts.bold, p.fontPx(kTitleSize));
    const bool open = classicButton(p, "Computer Players", Vec2{buttonSize.x / p.k(), buttonSize.y / p.k()});
    ImGui::PopFont();
    return open;
}

bool PlayerPicker::draw(MenuContext& ctx, const game::Rules& r, std::optional<game::Controller>& choice, const Options& o) {
    if (!open_) return false;
    constexpr const char* kId = "Computer Players##playerpicker";
    if (pending_) {
        ImGui::OpenPopup(kId);
        pending_ = false;
    }
    Vec2 min;
    const Vec2 size{500, 450};
    if (!beginWindow(ctx, kId, size, o.title, min)) {
        open_ = false;
        return false;
    }
    const Painter p = ctx.painter();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetWindowPos();
    auto P = [&](float x, float y) { return ImVec2(origin.x + p.px(x), origin.y + p.px(y)); };
    const float questionH = wrappedText(ctx, dl, P(10, 36), o.question, 480, imColor(kExplainRgb));

    std::vector<PickerRow> rows;
    if (o.gameChoiceRow)
        rows.push_back({std::nullopt, "The game's choice", o.gameChoice, "Played as the computer empires without a player of their own are.",
                        "player:game"});
    rows.push_back({game::Controller{}, "Classic AI", "", "The game's own computer player, with its ministers.", "player:builtin"});
    for (const ComputerPlayerChoice& c : computerPlayerChoices(r))
        rows.push_back({c.controller, c.name, std::format("{} ({})", c.modName, c.controller.mod), c.description, "player:" + c.label});

    // The list under the question, down to the check box and Done.
    const float listTop = std::max(64.0f, 40 + questionH + 8);
    const float listBottom = o.seesEverything ? 370.0f : 400.0f;
    const float lw = std::max(1.0f, std::floor(p.map.scale)) / p.fbScale;
    dl->AddRect(P(10, listTop), P(490, listBottom), imColor(palette::kFrameLight), 0.0f, lw);
    ImGui::SetCursorScreenPos(P(11, listTop + 1));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    beginList(p, "##playerrows", p.size({478, listBottom - listTop - 2}), 17.0f, ImGuiChildFlags_None, false);
    const float rowW = ImGui::GetContentRegionAvail().x;
    ImDrawList* rowsDl = ImGui::GetWindowDrawList();
    bool changed = false;
    ImFont* font = ctx.fonts.regular;
    for (size_t i = 0; i < rows.size(); ++i) {
        const PickerRow& row = rows[i];
        const float textW = rowW / p.k() - 30;
        const float descH = font->CalcTextSizeA(p.textPx(kTextSize), FLT_MAX, p.px(textW), row.description.c_str()).y / p.k();
        const float rowH = 20 + descH + 6;
        ImGui::PushID(static_cast<int>(i));
        const ImVec2 r0 = ImGui::GetCursorScreenPos();
        if (ImGui::InvisibleButton("##row", ImVec2(rowW, p.px(rowH)))) {
            changed = choice != row.player;
            choice = row.player;
        }
        script::reportItem(row.item);   // input scripts: player:builtin, player:<mod id>:<name>, player:game
        const bool lit = choice == row.player;
        if (lit || ImGui::IsItemHovered())
            if (Sprite grid = ctx.art.region("Pictures/Game/Dialogs/Rowgrid.bmp", 0, 0, int(rowW / p.k()), int(rowH), false))
                rowsDl->AddImage(texRef(grid), r0, {r0.x + rowW, r0.y + p.px(rowH)}, {grid.uv.min.x, grid.uv.min.y}, {grid.uv.max.x, grid.uv.max.y});
        drawLamp(ctx, rowsDl, ImVec2(r0.x + p.px(10), r0.y + p.px(10)), lit);
        rowsDl->AddText(font, p.fontPx(kTextSize), ImVec2(r0.x + p.px(22), r0.y + p.px(3)), IM_COL32_WHITE, row.name.c_str());
        if (!row.mod.empty()) {
            const float nameW = font->CalcTextSizeA(p.fontPx(kTextSize), FLT_MAX, 0.0f, row.name.c_str()).x;
            rowsDl->AddText(font, p.fontPx(kTextSize), ImVec2(r0.x + p.px(22) + nameW + p.px(8), r0.y + p.px(3)), imColor(kHeadingRgb), row.mod.c_str());
        }
        rowsDl->AddText(font, p.textPx(kTextSize), ImVec2(r0.x + p.px(22), r0.y + p.px(20)), imColor(kExplainRgb), row.description.c_str(), nullptr,
                        p.px(textW));
        ImGui::PopID();
    }
    endList(p);
    ImGui::PopStyleVar(2);

    if (o.seesEverything) {
        ImGui::SetCursorScreenPos(P(10, 378));
        constexpr const char* kLabel = "Computer players see everything";
        if (ImGui::InvisibleButton("##seeall", p.size({480, 18}))) {
            *o.seesEverything = !*o.seesEverything;
            changed = true;
        }
        script::reportItem(kLabel);
        drawLamp(ctx, dl, P(19, 387), *o.seesEverything);
        dl->AddText(font, p.fontPx(kTextSize), P(31, 380), IM_COL32_WHITE, kLabel);
    }
    ImGui::SetCursorScreenPos(P(12, 412));
    const bool done = classicButton(p, "Done", {476, 26}) || (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !ImGui::IsAnyItemActive());
    if (done) {
        ImGui::CloseCurrentPopup();
        open_ = false;
    }
    ImGui::EndPopup();
    return changed;
}

bool LimitsWindow::draw(MenuContext& ctx, game::GameOptions& o) {
    if (!open_) return false;
    constexpr const char* kId = "Computer Player Limits##limits";
    if (pending_) {
        ImGui::OpenPopup(kId);
        pending_ = false;
    }
    Vec2 min;
    const Vec2 size{520, 240};
    if (!beginWindow(ctx, kId, size, "Computer Player Limits", min)) {
        open_ = false;
        return false;
    }
    const Painter p = ctx.painter();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetWindowPos();
    auto P = [&](float x, float y) { return ImVec2(origin.x + p.px(x), origin.y + p.px(y)); };
    const float textH = wrappedText(ctx, dl, P(10, 36),
                                    "How much a computer player of a mod may do for one request of the game, counted the same way on "
                                    "every computer. A player that goes over fails that request, and the classic AI answers it.",
                                    500, imColor(kExplainRgb));
    bool changed = false;
    // Whole numbers: bytecodes in millions, memory in KiB.
    auto row = [&](const char* label, float y, int64_t& value, int64_t unit, int64_t lo, int64_t hi) {
        dl->AddText(ctx.fonts.regular, p.fontPx(kTextSize), P(12, y + 3), imColor(kHeadingRgb), label);
        ImGui::SetCursorScreenPos(P(330, y));
        ImGui::SetNextItemWidth(p.px(178));
        int64_t shown = value / unit;
        const int64_t step = 1, fast = 10;
        ImGui::PushID(label);
        if (ImGui::InputScalar("##value", ImGuiDataType_S64, &shown, &step, &fast)) {
            value = std::clamp(shown, lo, hi) * unit;
            changed = true;
        }
        ImGui::PopID();
        script::reportItem(label);
    };
    const float y = std::min(std::max(80.0f, 36 + textH + 12), 100.0f);
    row("Planning (politics, orders, economy), million bytecodes", y, o.aiPlanningBudget, 1'000'000, 1, 100'000);
    row("Any other request, million bytecodes", y + 32, o.aiCallBudget, 1'000'000, 1, 100'000);
    row("Memory each player keeps, KiB", y + 64, o.aiMemoryLimit, 1024, 1, 1 << 20);
    ImGui::SetCursorScreenPos(P(12, 204));
    if (classicButton(p, "Restore Defaults", {200, 26})) {
        const game::GameOptions defaults;
        o.aiPlanningBudget = defaults.aiPlanningBudget;
        o.aiCallBudget = defaults.aiCallBudget;
        o.aiMemoryLimit = defaults.aiMemoryLimit;
        changed = true;
    }
    ImGui::SetCursorScreenPos(P(308, 204));
    const bool done = classicButton(p, "Done", {200, 26}) || (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !ImGui::IsAnyItemActive());
    if (done) {
        ImGui::CloseCurrentPopup();
        open_ = false;
    }
    ImGui::EndPopup();
    return changed;
}

} // namespace opense4::client::classic::setup
