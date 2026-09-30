// Stellar Manipulation (order B, docs/spec/06 §1.3, spec 01 §9): a view of
// the vehicle's sector and one button per manipulation. A button is enabled
// when the vehicle has the ability and the sector allows it; hovering one
// plays its film strip from Pictures/Stellar. A click gives a Stellar
// Manipulation order (amount = the action) carried out here, first thing when
// the turn is processed; Open Warp Point asks for the destination on the map.

#include "client/classic/screens/screens.hpp"
#include "client/classic/screens/ships_common.hpp"

#include "game/design.hpp"

#include <algorithm>
#include <cmath>
#include <format>

namespace opense4::client::classic {

namespace {

using namespace shipui;

void drawSprite(ImDrawList* dl, const Sprite& s, ImVec2 a, ImVec2 b) {
    if (!s) return;
    dl->AddImage(ImTextureRef(static_cast<ImTextureID>(s.tex.value)), a, b, ImVec2(s.uv.min.x, s.uv.min.y), ImVec2(s.uv.max.x, s.uv.max.y));
}

class StellarScreen final : public Screen {
public:
    explicit StellarScreen(const ScreenArgs& args) : vehicle_(args.vehicle) {}

    bool draw(UiContext& ui) override {
        if (!ownVehicle(ui, vehicle_)) vehicle_ = defaultVehicle(ui);
        Dialog d(ui, screenTitle(ScreenId::StellarManipulation), DialogSize::Large);
        if (!d.open()) return d.keepOpen();
        const game::Vehicle* v = ownVehicle(ui, vehicle_);

        d.beginContent();
        if (!v) ImGui::TextColored(kDim, "You have no ships.");
        else content(ui, *v);

        d.beginButtons();
        hovered_.reset();
        for (int i = 0; i < static_cast<int>(game::StellarAction::Count); ++i) {
            const auto a = static_cast<game::StellarAction>(i);
            const StellarCheck check = v ? checkStellar(ui.rules(), ui.state(), *v, a) : StellarCheck{};
            if (d.button(stellarInfo(a).name, check.possible)) act(ui, *v, a, check);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) hovered_ = a;
        }
        if (d.close()) return false;
        if (confirm_.draw(ui))
            if (const game::Vehicle* cv = ownVehicle(ui, vehicle_)) give(ui, *cv, pending_, checkStellar(ui.rules(), ui.state(), *cv, pending_));
        if (closeForPick_) return false;
        return d.keepOpen();
    }

private:
    // The first own ship with any manipulation ability, else the first ship.
    static game::VehicleId defaultVehicle(const UiContext& ui) {
        const game::GameState& s = ui.state();
        for (const game::Vehicle& v : s.vehicles) {
            if (v.owner != ui.session.player()) continue;
            for (int i = 0; i < static_cast<int>(game::StellarAction::Count); ++i)
                if (checkStellar(ui.rules(), s, v, static_cast<game::StellarAction>(i)).hasAbility) return v.id;
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
        drawSprite(dl, ui.art.systemBackground(r.data().systemTypes[sys.type.index()].backgroundBitmap), p0, p1);

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

        // The film strip of the hovered action, or of the one just ordered.
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
        if (const game::Fleet* f = s.fleet(v.fleet)) ImGui::TextColored(kDim, "In fleet %s (orders go to the fleet)", f->name.c_str());
        ImGui::Dummy(ImVec2(0, ui.px(10)));
        if (hovered_) {
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

    void act(UiContext& ui, const game::Vehicle& v, game::StellarAction a, const StellarCheck& check) {
        if (check.needsDestination) {
            pickLocationForOrder(ui, orderOwner(ui.state(), v.id), stellarOrder(v, a, {}),
                                 "Open Warp Point: pick a sector of the destination system", true);
            closeForPick_ = true;
            return;
        }
        if (check.destroysSystem || a == game::StellarAction::DestroyPlanet) {
            pending_ = a;
            confirm_.open(stellarInfo(a).name, check.reason + " Give the order?");
            return;
        }
        give(ui, v, a, check);
    }

    void give(UiContext& ui, const game::Vehicle& v, game::StellarAction a, const StellarCheck& check) {
        if (!check.possible) {
            status_.error(check.reason);
            return;
        }
        if (status_.issue(ui, withImmediate(ui.state(), orderOwner(ui.state(), v.id), stellarOrder(v, a, check.target)),
                          std::format("{}: ordered. It happens when the turn is processed.", stellarInfo(a).name))) {
            shown_ = a;
            filmStart_ = ui.time;
            lastFilm_.reset();
        }
    }

    game::VehicleId vehicle_;
    std::optional<game::StellarAction> hovered_, shown_, lastFilm_;
    double filmStart_ = 0.0;
    game::StellarAction pending_ = game::StellarAction::CreatePlanet;
    bool closeForPick_ = false;
    Status status_;
    Confirm confirm_;
};

} // namespace

std::unique_ptr<Screen> makeStellarManipulation(const ScreenArgs& args) { return std::make_unique<StellarScreen>(args); }

} // namespace opense4::client::classic
