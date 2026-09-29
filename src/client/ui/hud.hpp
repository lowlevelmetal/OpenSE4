#pragma once

#include "client/view_context.hpp"

#include <string>

namespace opense4::client {

// All Dear ImGui panels: top bar, selection details, colony management,
// research, event log, help and the new-game dialog.
class Hud {
public:
    void draw(ViewContext& ctx, NavRequest& nav, bool inSystemView);

    bool showResearch = false;
    bool showLog = false;
    bool showHelp = false;
    bool showDebug = false;
    std::string rendererInfo;
    float fps = 0.0f;
    float topBarHeight = 0.0f;  // UI units, measured each frame

private:
    void topBar(ViewContext& ctx, NavRequest& nav);
    void selectionPanel(ViewContext& ctx, NavRequest& nav, bool inSystemView);
    void systemDetails(ViewContext& ctx, NavRequest& nav, sim::SystemId id, bool inSystemView);
    void planetDetails(ViewContext& ctx, NavRequest& nav, sim::PlanetId id);
    void colonyManagement(ViewContext& ctx, const sim::Planet& planet);
    void shipDetails(ViewContext& ctx, NavRequest& nav, sim::ShipId id);
    void warpPointDetails(ViewContext& ctx, NavRequest& nav, sim::WarpPointId id);
    void researchWindow(ViewContext& ctx);
    void logWindow(ViewContext& ctx, NavRequest& nav);
    void helpWindow();
    void newGameDialog(ViewContext& ctx, NavRequest& nav);
    void statusToast(ViewContext& ctx);
    void debugOverlay(ViewContext& ctx);

    bool openNewGame_ = false;
    sim::GameSetup pendingSetup_;
    int raceChoice_ = 0;
};

} // namespace opense4::client
