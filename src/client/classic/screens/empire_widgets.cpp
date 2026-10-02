#include "client/classic/screens/empire_widgets.hpp"
#include "client/classic/screens/list_widgets.hpp"

#include <algorithm>
#include <cmath>
#include <format>

namespace opense4::client::classic {

std::vector<game::EmpireId> knownEmpires(const UiContext& ui) { return contactedEmpires(ui.state(), ui.session.player()); }

std::vector<game::EmpireId> usAndKnown(const UiContext& ui) {
    std::vector<game::EmpireId> out{ui.session.player()};
    for (game::EmpireId e : knownEmpires(ui)) out.push_back(e);
    return out;
}

void empireLabel(UiContext& ui, game::EmpireId e, bool large, bool framed) {
    const game::GameState& s = ui.state();
    if (!e.valid() || e.index() >= s.empires.size()) {
        ImGui::TextColored(kTextDim, "Unknown");
        return;
    }
    const game::Empire& emp = s.empire(e);
    const Sprite flag = ui.art.flag(emp.race.style, large);
    const Vec2 size = large ? Vec2{26, 18} : Vec2{18, 13};
    const float textH = ImGui::GetTextLineHeight();
    const float y = ImGui::GetCursorPosY() + (framed ? ImGui::GetStyle().FramePadding.y : 0.0f);
    if (flag) {
        ImGui::SetCursorPosY(y + std::max(0.0f, (textH - ui.px(size.y)) * 0.5f));
        image(ui, flag, size);
    } else {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddRectFilled(p, ImVec2(p.x + ui.px(size.x), p.y + ui.px(size.y)), empireColor(s, e));
        ImGui::Dummy(ui.size(size));
    }
    ImGui::SameLine(0, ui.px(5));
    ImGui::SetCursorPosY(y + std::max(0.0f, (ui.px(size.y) - textH) * 0.5f));
    ImGui::PushStyleColor(ImGuiCol_Text, empireColor(s, e));
    ImGui::TextUnformatted(emp.name.c_str());
    ImGui::PopStyleColor();
}

void wrappedText(const std::string& text, ImVec4 color) {
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(color, "%s", text.c_str());
    ImGui::PopTextWrapPos();
}

void progressBar(UiContext& ui, float fraction, const std::string& caption, float width, float height) {
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.16f, 0.42f, 0.78f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.03f, 0.06f, 0.16f, 1.0f));
    ImGui::ProgressBar(std::clamp(fraction, 0.0f, 1.0f), ImVec2(width > 0 ? ui.px(width) : -FLT_MIN, ui.px(height)), caption.c_str());
    ImGui::PopStyleColor(2);
}

void framedImage(UiContext& ui, const Sprite& s, Vec2 frameSize) {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const ImVec2 size = ui.size(frameSize);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), IM_COL32(0, 0, 0, 255));
    image(ui, s, frameSize);
    dl->AddRect(ImVec2(p.x - 1, p.y - 1), ImVec2(p.x + size.x + 1, p.y + size.y + 1), IM_COL32(44, 79, 158, 255));
}

bool inputTextMultiline(const char* id, std::string& text, ImVec2 size) {
    auto resize = [](ImGuiInputTextCallbackData* data) -> int {
        if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
            auto* str = static_cast<std::string*>(data->UserData);
            str->resize(size_t(data->BufTextLen));
            data->Buf = str->data();
        }
        return 0;
    };
    return ImGui::InputTextMultiline(id, text.data(), text.capacity() + 1, size, ImGuiInputTextFlags_CallbackResize, resize, &text);
}

// ---- Galaxy mini-map --------------------------------------------------------------------------

MiniMapResult miniMap(UiContext& ui, const char* id, Vec2 frameSize, const MiniMapStyle& style) {
    MiniMapResult result;
    const game::GameState& s = ui.state();
    const game::Galaxy& g = s.galaxy;
    const game::Empire& me = ui.me();

    const ImVec2 size = ui.size(frameSize);
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const ImVec2 p1{p0.x + size.x, p0.y + size.y};
    ImGui::InvisibleButton(id, size);
    const bool hovered = ImGui::IsItemHovered();
    const bool clicked = style.clickable && ImGui::IsItemClicked(ImGuiMouseButton_Left);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p0, p1, IM_COL32(2, 5, 12, 255));
    dl->AddRect(p0, p1, IM_COL32(44, 79, 158, 255));
    if (g.systems.empty()) return result;

    // Uniform scale, centred.
    const float pad = ui.px(8);
    const float cell = std::min((size.x - 2 * pad) / float(std::max(1, g.width)), (size.y - 2 * pad) / float(std::max(1, g.height)));
    const ImVec2 origin{p0.x + (size.x - cell * float(g.width)) * 0.5f, p0.y + (size.y - cell * float(g.height)) * 0.5f};
    auto pos = [&](game::SystemId sys) {
        const game::GalaxyPos gp = g.system(sys).position;
        return ImVec2(origin.x + (float(gp.x) + 0.5f) * cell, origin.y + (float(gp.y) + 0.5f) * cell);
    };
    dl->PushClipRect(p0, p1, true);
    const ImU32 grid = IM_COL32(16, 34, 74, 200);
    for (int x = 0; x <= g.width; x += 2)
        dl->AddLine(ImVec2(origin.x + float(x) * cell, origin.y), ImVec2(origin.x + float(x) * cell, origin.y + float(g.height) * cell), grid);
    for (int y = 0; y <= g.height; y += 2)
        dl->AddLine(ImVec2(origin.x, origin.y + float(y) * cell), ImVec2(origin.x + float(g.width) * cell, origin.y + float(y) * cell), grid);

    // Warp links we know.
    const auto& known = me.knowledge.knownWarpLink;
    for (const game::SpaceObject& o : g.objects) {
        if (o.kind != game::ObjectKind::WarpPoint || !o.destination.valid() || o.destination.index() >= g.objects.size()) continue;
        if (o.id.index() >= known.size() || !known[o.id.index()] || o.destination < o.id) continue;
        dl->AddLine(pos(o.system), pos(g.object(o.destination).system), IM_COL32(72, 104, 168, 220), 1.0f);
    }

    // Owners of known colonies (own ones, and others in explored systems).
    std::vector<std::vector<game::EmpireId>> owners(g.systems.size());
    if (style.owners)
        for (const auto& c : s.colonies) {
            if (!c) continue;
            const game::SystemId sys = g.object(c->planet).system;
            if (c->owner != me.id && !me.hasExplored(sys)) continue;
            auto& list = owners[sys.index()];
            if (std::find(list.begin(), list.end(), c->owner) == list.end()) list.push_back(c->owner);
        }

    const float r = std::max(2.0f, ui.px(3.0f));
    std::optional<game::SystemId> nearest;
    float bestDist = ui.px(9) * ui.px(9);
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    for (const game::StarSystem& sys : g.systems) {
        const ImVec2 p = pos(sys.id);
        ImU32 color = me.hasExplored(sys.id) ? IM_COL32(200, 208, 220, 255) : IM_COL32(80, 88, 102, 255);
        const auto& who = owners[sys.id.index()];
        if (who.size() == 1) color = empireColor(s, who.front());
        bool filled = false;
        for (const auto& [fs, fc] : style.fills)
            if (fs == sys.id) {
                color = fc;
                filled = true;
            }
        if (who.size() > 1 && !filled) {
            dl->AddTriangleFilled(ImVec2(p.x, p.y - r * 1.4f), ImVec2(p.x - r * 1.2f, p.y + r), ImVec2(p.x + r * 1.2f, p.y + r),
                                  IM_COL32(255, 255, 255, 255));
        } else if (filled) {
            dl->AddCircleFilled(p, r * 1.2f, color);
        } else {
            dl->AddCircle(p, r, color, 0, 1.5f);
        }
        if (std::find(style.highlight.begin(), style.highlight.end(), sys.id) != style.highlight.end()) {
            dl->AddCircleFilled(p, r * 0.7f, IM_COL32(255, 208, 64, 255));
            dl->AddCircle(p, r * 2.2f, IM_COL32(255, 208, 64, 255), 0, 1.5f);
        }
        if (hovered) {
            const float dx = mouse.x - p.x, dy = mouse.y - p.y;
            if (dx * dx + dy * dy < bestDist) {
                bestDist = dx * dx + dy * dy;
                nearest = sys.id;
            }
        }
    }
    dl->PopClipRect();

    if (nearest) {
        result.hovered = nearest;
        if (me.hasExplored(*nearest)) ImGui::SetTooltip("%s", g.system(*nearest).name.c_str());
        if (clicked) result.clicked = nearest;
    }
    return result;
}

// ---- Project queues -----------------------------------------------------------------------------

void projectPageButtons(Dialog& d, int& page) {
    for (int p = 0; p < kMaxProjects / kProjectsPerPage; ++p) {
        const std::string label = std::format("Projects {}-{}", p * kProjectsPerPage + 1, (p + 1) * kProjectsPerPage);
        if (d.tab(label.c_str(), page == p)) page = p;
    }
}

void ReorderPopup::open(std::vector<std::string> rows) {
    rows_ = std::move(rows);
    order_.resize(rows_.size());
    for (size_t i = 0; i < order_.size(); ++i) order_[i] = i;
    selected_ = 0;
    pending_ = true;
}

std::optional<std::vector<size_t>> ReorderPopup::draw(UiContext& ui) {
    if (pending_) {
        ImGui::OpenPopup("Reorder");
        pending_ = false;
    }
    std::optional<std::vector<size_t>> result;
    const ImVec2 size = ui.size({460, 400});
    ImGui::SetNextWindowSize(size, ImGuiCond_Always);
    ImGui::SetNextWindowPos(ui.at({(frameW() - 460) * 0.5f, (frameH() - 400) * 0.5f}), ImGuiCond_Always);
    if (!ImGui::BeginPopupModal("Reorder", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings))
        return result;

    const float buttons = ui.px(150);
    beginList(ui, "##rows", ImVec2(-(buttons + ImGui::GetStyle().ItemSpacing.x), 0), kListLineStep, ImGuiChildFlags_AlwaysUseWindowPadding);
    for (size_t i = 0; i < order_.size(); ++i) {
        const std::string label = std::format("{:>2}. {}", i + 1, rows_[order_[i]]);
        if (ImGui::Selectable(label.c_str(), selected_ == int(i))) selected_ = int(i);
    }
    endList(ui);
    ImGui::SameLine();
    ImGui::BeginChild("##moves", ImVec2(0, 0));
    const ImVec2 bs(-FLT_MIN, ui.px(26));
    const size_t sel = size_t(std::max(0, selected_));
    const bool any = !order_.empty();
    auto move = [&](size_t to) {
        if (moveEntry(order_, sel, to)) selected_ = int(std::min(to, order_.size() - 1));
    };
    ImGui::BeginDisabled(!any || sel == 0);
    if (ImGui::Button("Move Up", bs)) move(sel - 1);
    ImGui::EndDisabled();
    ImGui::BeginDisabled(!any || sel + 1 >= order_.size());
    if (ImGui::Button("Move Down", bs)) move(sel + 1);
    ImGui::EndDisabled();
    ImGui::BeginDisabled(!any || sel == 0);
    if (ImGui::Button("Move To Top", bs)) move(0);
    ImGui::EndDisabled();
    ImGui::BeginDisabled(!any || sel + 1 >= order_.size());
    if (ImGui::Button("Move To Bottom", bs)) move(order_.size() - 1);
    ImGui::EndDisabled();
    const float y = ImGui::GetWindowHeight() - 2 * ui.px(26) - ImGui::GetStyle().ItemSpacing.y;
    if (ImGui::GetCursorPosY() < y) ImGui::SetCursorPosY(y);
    if (ImGui::Button("OK", bs)) {
        result = order_;
        ImGui::CloseCurrentPopup();
    }
    if (ImGui::Button("Cancel", bs) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) ImGui::CloseCurrentPopup();
    ImGui::EndChild();
    ImGui::EndPopup();
    return result;
}

void StatusLine::draw() const {
    if (text_.empty()) return;
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(error_ ? kTextBad : kTextGood, "%s", text_.c_str());
    ImGui::PopTextWrapPos();
}

} // namespace opense4::client::classic
