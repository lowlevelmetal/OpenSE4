// Stellar Manipulation (order B, docs/spec/06 §1.3, spec 01 §9): a view of
// the vehicle's sector and one button per manipulation, lit by the vehicle's
// own ability (confirmed: binary). A click asks for Construct's size, then
// "Confirm Action" while the Empire Options say so, then the target: none,
// the destination system in the Galaxy Map's destination mode (Open Warp
// Point), or an object of the sector (Pick Object when there are several).
// The order is appended to the vehicle's own list, never its fleet's; in a
// simultaneous game the window then closes, in a turn-based one the list is
// carried out at once and the effect's film strip plays. Ours: hovering a
// button plays its film strip from Pictures/Stellar and says what the
// order's checks would say now.

#include "client/classic/screens/colony_widgets.hpp"
#include "client/classic/screens/screens.hpp"
#include "client/classic/screens/ships_common.hpp"

#include "client/script/items.hpp"

#include "game/design.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <memory>

namespace opense4::client::classic {

namespace {

using namespace shipui;

void drawSprite(ImDrawList* dl, const Sprite& s, ImVec2 a, ImVec2 b) {
    if (!s) return;
    dl->AddImage(ImTextureRef(static_cast<ImTextureID>(s.tex.value)), a, b, ImVec2(s.uv.min.x, s.uv.min.y), ImVec2(s.uv.max.x, s.uv.max.y));
}

// What the Pick Object window and the "Cannot Perform Action" box call the
// object an action takes.
const char* targetWord(game::StellarAction a) {
    switch (stellarTargetKind(a).value_or(game::ObjectKind::Star)) {
        case game::ObjectKind::WarpPoint: return "warp point";
        case game::ObjectKind::Storm: return "storm";
        case game::ObjectKind::Planet: return "planet";
        default: return "star";
    }
}

class StellarScreen final : public Screen {
public:
    explicit StellarScreen(const ScreenArgs& args) : vehicle_(args.vehicle) {}

    bool draw(UiContext& ui) override {
        if (!ownVehicle(ui, vehicle_)) vehicle_ = defaultVehicle(ui);
        Dialog d(ui, screenTitle(ScreenId::StellarManipulation), DialogSize::Large);
        if (!d.open()) return d.keepOpen();
        // The destination picked in the Galaxy Map's destination mode. (A
        // command carried out at once may change the vehicles: each step
        // looks the vehicle up again.)
        if (*destination_) {
            const game::SystemId to = **destination_;
            destination_->reset();
            if (const game::Vehicle* v = ownVehicle(ui, vehicle_); v && pending_ == game::StellarAction::OpenWarpPoint)
                give(ui, *v, stellarOrder(*v, *pending_, {}, 0, to));
        }
        const game::Vehicle* v = ownVehicle(ui, vehicle_);

        d.beginContent();
        if (!v) ImGui::TextColored(kDim, "You have no ships.");
        else content(ui, *v);

        d.beginButtons();
        hovered_.reset();
        std::optional<game::StellarAction> clicked;
        for (int i = 0; i < static_cast<int>(game::StellarAction::Count); ++i) {
            const auto a = static_cast<game::StellarAction>(i);
            const StellarCheck check = v ? checkStellar(ui.rules(), ui.state(), *v, a) : StellarCheck{};
            if (d.button(stellarInfo(a).name, check.lit)) clicked = a;
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) hovered_ = a;
        }
        if (clicked && v) start(ui, *v, *clicked);
        if (d.close()) return false;
        popups(ui);
        if (closeNow_) return false;
        return d.keepOpen();
    }

private:
    // The first own ship with any manipulation ability, else the first ship.
    static game::VehicleId defaultVehicle(const UiContext& ui) {
        const game::GameState& s = ui.state();
        for (const game::Vehicle& v : s.vehicles) {
            if (v.owner != ui.session.player()) continue;
            for (int i = 0; i < static_cast<int>(game::StellarAction::Count); ++i)
                if (checkStellar(ui.rules(), s, v, static_cast<game::StellarAction>(i)).lit) return v.id;
        }
        return firstOwnShip(ui);
    }

    void content(UiContext& ui, const game::Vehicle& v) {
        const game::GameState& s = ui.state();
        const game::Rules& r = ui.rules();
        const game::StarSystem& sys = s.galaxy.system(v.location.system);
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const float side = std::min(avail.y, avail.x * 0.64f);
        const ImVec2 p0 = ImGui::GetCursorScreenPos();
        const ImVec2 p1{p0.x + side, p0.y + side};
        ImGui::Dummy(ImVec2(side, side));
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->PushClipRect(p0, p1, true);
        dl->AddRectFilled(p0, p1, IM_COL32(0, 0, 0, 255));
        // The system's background, or the plain star field for types with Non-Tiled Center Pic (docs/spec/06 §5.3).
        const ruleset::SystemType& type = r.data().systemTypes[sys.type.index()];
        drawSprite(dl, ui.art.systemBackground(type.nonTiledCenterPicture ? std::string("Starmap") : type.backgroundBitmap), p0, p1);

        // What is in the sector: object portraits along the top, the ship at the lower right.
        const float big = side * 0.30f;
        float x = p0.x + side * 0.08f;
        for (game::ObjectId id : sys.objects) {
            const game::SpaceObject& o = s.galaxy.object(id);
            if (o.sector != v.location.sector) continue;
            const ImVec2 a{x, p0.y + side * 0.10f};
            drawSprite(dl, ui.art.planetPortrait(r.data().sectorObjectTypes[o.sectorType].picture), a, ImVec2(a.x + big, a.y + big));
            dl->AddText(ImVec2(a.x, a.y + big + ui.px(2)), IM_COL32(200, 210, 235, 255), o.name.c_str());
            x += big + side * 0.04f;
        }
        const ImVec2 shipA{p0.x + side * 0.52f, p0.y + side * 0.52f};
        drawSprite(dl, vehiclePortrait(ui, v), shipA, ImVec2(shipA.x + big * 1.2f, shipA.y + big * 1.2f));

        // The film strip of the hovered action, or of the one just carried out.
        const std::optional<game::StellarAction> film = hovered_ ? hovered_ : shown_;
        if (film) {
            const StellarInfo& info = stellarInfo(*film);
            if (hovered_ != lastFilm_) {
                filmStart_ = ui.time;
                lastFilm_ = hovered_;
            }
            // 12 frames a second, then hold the finished effect for a second before looping.
            const double t = std::fmod((ui.time - filmStart_) * 12.0, double(info.frames + 12));
            const int frame = std::min(static_cast<int>(t), info.frames - 1);
            if (Sprite f = ui.art.cell(std::format("Pictures/Stellar/{}.bmp", info.picture), frame, 128, 128)) {
                const float fs = side * 0.46f;
                const ImVec2 c{p0.x + side * 0.5f, p0.y + side * 0.5f};
                drawSprite(dl, f, ImVec2(c.x - fs * 0.5f, c.y - fs * 0.5f), ImVec2(c.x + fs * 0.5f, c.y + fs * 0.5f));
            }
        }
        dl->AddText(ImVec2(p0.x + ui.px(8), p0.y + ui.px(6)), ImGui::ColorConvertFloat4ToU32(kLabelBlue),
                    std::format("{} System", sys.name).c_str());
        dl->PopClipRect();
        dl->AddRect(p0, p1, IM_COL32(68, 106, 216, 255));

        ImGui::SameLine(0, ui.px(14));
        ImGui::BeginGroup();
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
        heading(ui, "Vehicle");
        ImGui::TextUnformatted(v.name.c_str());
        ImGui::TextColored(kDim, "%s", sectorName(s, v.location, ui.session.player()).c_str());
        if (const game::Fleet* f = s.fleet(v.fleet))
            ImGui::TextColored(kDim, "In fleet %s: the order goes to this vehicle's own list only.", f->name.c_str());
        ImGui::Dummy(ImVec2(0, ui.px(10)));
        if (hovered_) {
            // Ours: what the order's checks would say if it were carried out now.
            const StellarCheck check = checkStellar(r, s, v, *hovered_);
            heading(ui, stellarInfo(*hovered_).name);
            ImGui::TextColored(check.possible ? ImVec4(0.9f, 0.93f, 1, 1) : kBad, "%s", check.reason.c_str());
        } else {
            ImGui::TextColored(kDim, "Point at an action to see what it needs. A manipulation component is used up when the "
                                     "order is carried out, and several actions destroy the ship itself.");
        }
        ImGui::Dummy(ImVec2(0, ui.px(10)));
        status_.draw(ui);
        ImGui::PopTextWrapPos();
        ImGui::EndGroup();
    }

    // ---- Giving the order (spec 06 §1.3) --------------------------------------------------------

    // A lit button: Construct's size first, then the question, then the target.
    void start(UiContext& ui, const game::Vehicle& v, game::StellarAction a) {
        pending_ = a;
        size_ = {};
        status_.clear();
        if (a == game::StellarAction::CreateConstructedPlanet) {
            sizes_ = constructSizes(ui.rules(), ui.state(), v);
            if (sizes_.empty()) {
                notice_.open("No constructed world is small enough for this vehicle's device.", "Cannot Perform Action");
                return;
            }
            askSize_ = true;
            return;
        }
        ask(ui, v);
    }

    // "Confirm Action" while the Empire Options' "confirm stellar
    // manipulation" is on; otherwise the order is given at once (spec 06 §7 Q46).
    void ask(UiContext& ui, const game::Vehicle& v) {
        if (ui.options().confirmStellarManipulation) {
            confirm_.open("Confirm Action", confirmText(*pending_, size_.name));
            return;
        }
        aim(ui, v);
    }

    static std::string confirmText(game::StellarAction a, const std::string& size) {
        using game::StellarAction;
        switch (a) {
            case StellarAction::CreatePlanet: return "Do you want to create a planet in this sector?";
            case StellarAction::DestroyPlanet: return "Do you want to destroy the planet in this sector?";
            case StellarAction::CreateStar: return "Do you want to create a star in this sector?";
            case StellarAction::DestroyStar: return "Do you want to destroy the star in this sector?";
            case StellarAction::OpenWarpPoint: return "Do you want to open a warp point from this sector?";
            case StellarAction::CloseWarpPoint: return "Do you want to close the warp point in this sector?";
            case StellarAction::CreateStorm: return "Do you want to create a storm in this sector?";
            case StellarAction::DestroyStorm: return "Do you want to destroy the storm in this sector?";
            case StellarAction::CreateNebulae: return "Do you want to turn this system into a nebula?";
            case StellarAction::DestroyNebulae: return "Do you want to clear the nebula from this system?";
            case StellarAction::CreateBlackHole: return "Do you want to turn this system's star into a black hole?";
            case StellarAction::DestroyBlackHole: return "Do you want to collapse the black hole in this system?";
            case StellarAction::CreateConstructedPlanet: return std::format("Do you want to construct a {} around this star?", size);
            case StellarAction::Count: break;
        }
        return "Do you want to give this order?";
    }

    // The target, chosen after the question (spec 06 §1.3, confirmed: binary).
    void aim(UiContext& ui, const game::Vehicle& v) {
        const game::StellarAction a = *pending_;
        switch (stellarAim(a)) {
            case StellarAim::None: give(ui, v, stellarOrder(v, a, {}, size_.id)); return;
            case StellarAim::System: {
                // The Galaxy Map's destination mode gives the system back here.
                ScreenArgs args;
                args.location = v.location;
                args.vehicle = v.id;
                args.pickSystem = [slot = destination_](game::SystemId sys) { *slot = sys; };
                ui.open(ScreenId::GalaxyMap, args);
                return;
            }
            case StellarAim::Object: {
                candidates_ = stellarCandidates(ui.state(), v, a);
                if (candidates_.empty()) {
                    notice_.open(std::format("There is no {} in this sector.", targetWord(a)), "Cannot Perform Action");
                } else if (candidates_.size() == 1) {
                    give(ui, v, stellarOrder(v, a, candidates_.front(), size_.id));
                } else {
                    askObject_ = true;
                }
                return;
            }
        }
    }

    // Appended to the vehicle's own list (spec 06 §1.3): a simultaneous game's
    // window closes; a turn-based game carries the list out at once.
    void give(UiContext& ui, const game::Vehicle& v, const game::Order& order) {
        const game::StellarAction a = *pending_;
        const bool turnBased = ui.session.turnBased();
        if (!status_.issue(ui, stellarCommand(v, order),
                           std::format("{}: ordered.{}", stellarInfo(a).name, turnBased ? "" : " It happens when the turn is processed.")))
            return;
        if (!turnBased) {
            closeNow_ = true;
            return;
        }
        shown_ = a;
        filmStart_ = ui.time;
        lastFilm_.reset();
    }

    // The window's popups, drawn from one place after its buttons.
    void popups(UiContext& ui) {
        if (askSize_) {
            ImGui::OpenPopup(kSizeId);
            askSize_ = false;
        }
        if (auto i = sizePopup(ui))
            if (const game::Vehicle* v = ownVehicle(ui, vehicle_); v && *i < sizes_.size()) {
                size_ = sizes_[*i];
                ask(ui, *v);
            }
        if (confirm_.draw(ui))
            if (const game::Vehicle* v = ownVehicle(ui, vehicle_); v && pending_) aim(ui, *v);
        if (askObject_) {
            ImGui::OpenPopup(kPickId);
            askObject_ = false;
        }
        if (auto o = pickObjectPopup(ui))
            if (const game::Vehicle* v = ownVehicle(ui, vehicle_); v && pending_) give(ui, *v, stellarOrder(*v, *pending_, *o, size_.id));
        notice_.draw(ui);
    }

    // Construct's size (spec 06 §1.3): the constructed worlds the device can build.
    static constexpr const char* kSizeId = "Construct###stellar-size";
    std::optional<size_t> sizePopup(UiContext& ui) {
        std::optional<size_t> chosen;
        ImGui::SetNextWindowPos(ui.at({frameW() * 0.5f, frameH() * 0.5f}), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ui.size({300, 90.0f + 28.0f * static_cast<float>(sizes_.size())}), ImGuiCond_Always);
        if (!ImGui::BeginPopupModal(kSizeId, nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings)) return chosen;
        ImGui::TextColored(kLabelBlue, "Please select the size to construct");
        for (size_t i = 0; i < sizes_.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            if (ImGui::Selectable(sizes_[i].name.c_str(), false, ImGuiSelectableFlags_None, ImVec2(0, ui.px(24)))) chosen = i;
            ImGui::PopID();
        }
        ImGui::SetCursorPosY(std::max(ImGui::GetCursorPosY(), ImGui::GetWindowHeight() - ui.px(34)));
        const bool cancel = ImGui::Button("Cancel", ImVec2(-FLT_MIN, ui.px(24))) || ImGui::IsKeyPressed(ImGuiKey_Escape, false);
        if (chosen || cancel) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return chosen;
    }

    // Pick Object (spec 06 §2.9, §1.3): "Please select" and what, a row per
    // candidate with its system-panel picture and name, Cancel; a click on a
    // row picks it, Cancel or Esc gives no order.
    static constexpr const char* kPickId = "Pick Object###stellar-pick";
    std::optional<game::ObjectId> pickObjectPopup(UiContext& ui) {
        std::optional<game::ObjectId> picked;
        ImGui::SetNextWindowPos(ui.at({frameW() * 0.5f, frameH() * 0.5f}), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ui.size({248, 253}), ImGuiCond_Always);
        ImGui::PushStyleColor(ImGuiCol_PopupBg, IM_COL32_BLACK);
        const bool open = ImGui::BeginPopupModal(kPickId, nullptr,
                                                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
        ImGui::PopStyleColor();
        if (!open) return picked;
        const game::GameState& s = ui.state();
        const std::string title = std::format("Please select the {}", pending_ ? targetWord(*pending_) : "object");
        ImGui::TextColored(kLabelBlue, "%s", title.c_str());
        ImGui::BeginChild("##candidates", ImVec2(0, -ui.px(34)), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
        const float rowH = ui.px(38);
        for (size_t i = 0; i < candidates_.size(); ++i) {
            const game::ObjectId id = candidates_[i];
            if (!id.valid() || id.index() >= s.galaxy.objects.size()) continue;
            const std::string name = objectName(s, id, ui.session.player());
            ImGui::PushID(static_cast<int>(i));
            const ImVec2 rowMin = ImGui::GetCursorScreenPos();
            if (ImGui::Selectable("##row", false, ImGuiSelectableFlags_None, ImVec2(0, rowH))) picked = id;
            script::reportItem(name);   // input scripts pick a row by its name
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddText({rowMin.x + ui.px(40), rowMin.y + (rowH - ImGui::GetTextLineHeight()) * 0.5f}, IM_COL32_WHITE, name.c_str());
            drawSprite(dl, objectSprite(ui, s.galaxy.object(id)), {rowMin.x, rowMin.y + ui.px(1)}, {rowMin.x + ui.px(36), rowMin.y + ui.px(37)});
            ImGui::PopID();
        }
        ImGui::EndChild();
        ui.tagItem("pick-object:list");
        const bool cancel = ImGui::Button("Cancel", ImVec2(-FLT_MIN, ui.px(26))) || ImGui::IsKeyPressed(ImGuiKey_Escape, false);
        if (picked || cancel) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return picked;
    }

    game::VehicleId vehicle_;
    std::optional<game::StellarAction> hovered_, shown_, lastFilm_;
    double filmStart_ = 0.0;
    std::optional<game::StellarAction> pending_;   // the action being given
    std::vector<ConstructSize> sizes_;
    ConstructSize size_;                           // Construct: the size chosen (id 0: none)
    std::vector<game::ObjectId> candidates_;
    // Set by the Galaxy Map's destination mode, which may outlive nothing of ours but this.
    std::shared_ptr<std::optional<game::SystemId>> destination_ = std::make_shared<std::optional<game::SystemId>>();
    bool askSize_ = false;
    bool askObject_ = false;
    bool closeNow_ = false;
    Status status_;
    Confirm confirm_;
    NoticePopup notice_;
};

} // namespace

std::unique_ptr<Screen> makeStellarManipulation(const ScreenArgs& args) { return std::make_unique<StellarScreen>(args); }

} // namespace opense4::client::classic
