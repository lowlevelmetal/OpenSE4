#include "learn/ids.hpp"

#include "game/commands.hpp"

#include <algorithm>
#include <array>
#include <utility>

namespace opense4::learn {

namespace {

// In the order of the client's ScreenId enum (client/classic/screen_id.hpp).
constexpr WindowInfo kWindows[] = {
    {"game-menu"}, {"designs"}, {"create-design"}, {"planets"}, {"colonies"}, {"ships"}, {"queues"}, {"set-queue"},
    {"research"}, {"tech-tree"}, {"empires"}, {"log"}, {"empire-status"}, {"help"}, {"galaxy-map"},
    {"empire-options"}, {"ministers"}, {"systems-to-avoid"}, {"waypoints"}, {"strategies"}, {"repair-priorities"},
    {"fleet-transfer"}, {"cargo-transfer"}, {"launch-recover"}, {"scrap"}, {"view-orders"}, {"select-waypoint"},
    {"stellar-manipulation"}, {"rename"}, {"abandon-planet", false}, {"jettison-cargo", false}, {"convert-resources", false},
    {"communicate"}, {"intelligence"}, {"treaty-grid"}, {"scores"}, {"comparisons"}, {"history"}, {"race-report"},
    {"victory-conditions"},
    {"combat-replay", false}, {"tactical-combat", false}, {"tactical-orders", false}, {"tactical-options", false},
    {"tactical-launch", false}, {"combat-piece-report", false}, {"combat-replay-options", false},
    {"combat-simulator"}, {"strategic-combat", false}, {"ground-combat", false},
    {"save-game"}, {"load-game"}, {"options"}, {"settings"},
    {"learn"}, {"manual"},
};

// The Help window's tabs (screens/help.cpp), and its Weapons Report.
constexpr std::array<std::string_view, 10> kHelpTabs{"components", "facilities", "ship-sizes", "unit-sizes", "tech-areas",
                                                     "treaties",   "intel-projects", "formations", "hotkeys", "weapons"};

constexpr std::array<std::string_view, 10> kSelectionKinds{"planet", "colony", "ship", "base", "unit",
                                                           "fleet",  "star",   "warp-point", "system", "sector"};

constexpr std::array<std::pair<game::OrderKind, std::string_view>, static_cast<size_t>(game::OrderKind::Count)> kOrderKinds{{
    {game::OrderKind::MoveTo, "move-to"},
    {game::OrderKind::Warp, "warp"},
    {game::OrderKind::Attack, "attack"},
    {game::OrderKind::Resupply, "resupply"},
    {game::OrderKind::Repair, "repair"},
    {game::OrderKind::Explore, "explore"},
    {game::OrderKind::Colonize, "colonize"},
    {game::OrderKind::Sentry, "sentry"},
    {game::OrderKind::LoadCargo, "load-cargo"},
    {game::OrderKind::DropCargo, "drop-cargo"},
    {game::OrderKind::LaunchUnits, "launch-units"},
    {game::OrderKind::RecoverUnits, "recover-units"},
    {game::OrderKind::Cloak, "cloak"},
    {game::OrderKind::Decloak, "decloak"},
    {game::OrderKind::SweepMines, "sweep-mines"},
    {game::OrderKind::UseComponent, "use-component"},
    {game::OrderKind::StellarManipulation, "stellar-manipulation"},
    {game::OrderKind::MoveToWaypoint, "move-to-waypoint"},
    {game::OrderKind::SelfDestruct, "self-destruct"},
    {game::OrderKind::UseFacility, "use-facility"},
    {game::OrderKind::ConvertResources, "convert-resources"},
    {game::OrderKind::Scrap, "scrap"},
    {game::OrderKind::Analyze, "analyze"},
    {game::OrderKind::Mothball, "mothball"},
    {game::OrderKind::Unmothball, "unmothball"},
    {game::OrderKind::Retrofit, "retrofit"},
    {game::OrderKind::FireOn, "fire-on"},
}};

// The order strip's slots (main_window.cpp kOrderStrip keys) and their tag ids.
constexpr std::pair<std::string_view, std::string_view> kOrderStrip[] = {
    {"Move", "move-to"},
    {"Warp", "warp"},
    {"Wpt", "move-to-waypoint"},
    {"Colonize", "colonize"},
    {"Attack", "attack"},
    {"Fleet", "fleet-transfer"},
    {"Supply", "resupply"},
    {"Repair", "repair"},
    {"Clear", "clear-orders"},
    {"Queue", "build-queue"},
    {"Cargo", "cargo-transfer"},
    {"Units", "launch-recover"},
    {"Load", "load-cargo"},
    {"Drop", "drop-cargo"},
    {"LaunchRemote", "launch-remote"},
    {"RecoverRemote", "recover-remote"},
    {"Sentry", "sentry"},
    {"Explore", "explore"},
    {"Patrol", "patrol"},
    {"Repeat", "repeat-orders"},
    {"Stellar", "stellar-manipulation"},
    {"Name", "rename"},
    {"Scrap", "scrap"},
    {"Strategy", "strategy"},
    {"Orders", "view-orders"},
    {"Sweep", "sweep-mines"},
    {"ScrapFacilities", "scrap-facilities"},
    {"Jettison", "jettison"},
    {"Cloak", "cloak"},
    {"Decloak", "decloak"},
    {"UseComponent", "use-component"},
    {"UseFacility", "use-facility"},
    {"Abandon", "abandon-planet"},
    {"Convert", "convert-resources"},
    {"Minister", "minister"},
    {"ReplayPlay", "replay-play"},
    {"ReplayShip", "replay-ship"},
    {"ReplayStep", "replay-step"},
    {"ReplayRewind", "replay-rewind"},
};

// Tags besides the order strip's and the windows'.
constexpr std::string_view kOtherTags[] = {
    // The command buttons (their ids are the windows they open) and End Turn.
    "command:game-menu", "command:designs", "command:planets", "command:colonies", "command:ships", "command:queues",
    "command:research", "command:empires", "command:log", "command:empire-status", "command:help", "button:end-turn",
    // The status bar; status:lesson is the T button, shown during a lesson.
    "status:empire", "status:leader", "status:date", "status:resources", "status:minerals", "status:organics",
    "status:radioactives", "status:lesson",
    // The main window's panels, the report's tabs and the selection cycles.
    "panel:system", "panel:report", "panel:galaxy", "panel:commands", "panel:orders", "panel:report-tabs",
    "cycle:ship", "cycle:fleet", "cycle:colony",
    // Widgets inside windows.
    "research:areas", "research:queue", "research:tech-tree",
    "set-queue:available", "set-queue:queue",
    "queues:list",
    "designs:list", "designs:create", "designs:simulator",
    "create-design:hull", "create-design:name", "create-design:on-design", "create-design:components",
    "create-design:warnings", "create-design:save",
    "fleet-transfer:ships", "fleet-transfer:fleets", "fleet-transfer:create-fleet",
    "combat-simulator:vehicles", "combat-simulator:items", "combat-simulator:owners", "combat-simulator:strategies",
    "combat-simulator:begin",
    "tactical-combat:map", "tactical-combat:piece", "tactical-combat:weapons", "tactical-combat:target",
    "tactical-combat:options", "tactical-combat:orders", "tactical-combat:auto", "tactical-combat:end-turn",
    "planets:list", "planets:filters", "planets:no-sys-to-avoid", "planets:send-colony-ship",
    "colonies:list", "colonies:queue",
    "research:divide-evenly", "research:repeat",
    "log:messages", "log:categories", "log:send-reply",
    "empires:list", "empires:intelligence",
    "communicate:message-type", "communicate:treaty", "communicate:send",
    // The lesson panel itself.
    "lesson:panel", "lesson:next", "lesson:read-more",
    "help:tabs",
};

// The tabs and filters windows show (UiContext::tagTab): each is a UI tag and
// what a `tab` condition names.
constexpr std::string_view kWindowTabs[] = {
    "planets:all", "planets:colonizable", "planets:all-colonies", "planets:enemy-colonies", "planets:ally-colonies",
    "planets:colonizable-empty", "planets:colonizable-breathable", "planets:ship-enroute", "planets:asteroids", "planets:special",
    "colonies:general", "colonies:value", "colonies:production", "colonies:facilities", "colonies:cargo", "colonies:construction",
    "colonies:status", "colonies:races", "colonies:orders",
    "ships:general", "ships:orders", "ships:cargo", "ships:fleet", "ships:maintenance",
    "designs:ship-designs", "designs:unit-designs", "designs:enemy-ship-designs", "designs:enemy-unit-designs",
    "queues:rate", "queues:usage", "queues:planet-value", "queues:facilities", "queues:cargo",
    "set-queue:ships", "set-queue:facilities", "set-queue:units", "set-queue:upgrades",
    "tech-tree:tech-areas", "tech-tree:tech-levels",
    "empires:treaty", "empires:trade", "empires:tariff",
    "log:all", "log:construction", "log:research", "log:intelligence", "log:events", "log:politics", "log:combat", "log:misc",
    "combat-simulator:tactical", "combat-simulator:strategic",
};

// On/off settings of the player's empire for `option` conditions.
struct OptionInfo {
    std::string_view id;
    bool (*get)(const game::Empire&);
};
constexpr OptionInfo kOptions[] = {
    // Research and Intelligence windows.
    {"research-evenly", [](const game::Empire& e) { return e.researchEvenly; }},
    {"research-repeat", [](const game::Empire& e) { return e.repeatResearch; }},
    {"intel-evenly", [](const game::Empire& e) { return e.intelEvenly; }},
    {"intel-repeat", [](const game::Empire& e) { return e.repeatIntel; }},
    // Ship Movement and Ship Orders, colonization.
    {"avoid-tagged-minefields", [](const game::Empire& e) { return e.avoidTaggedMinefields; }},
    {"avoid-restricted-systems", [](const game::Empire& e) { return e.avoidRestrictedSystems; }},
    {"choose-colony-type", [](const game::Empire& e) { return e.chooseColonyType; }},
    // Empire Options (game::InterfaceOptions).
    {"show-log-at-turn-start", [](const game::Empire& e) { return e.interfaceOptions.showLogAtTurnStart; }},
    {"confirm-end-turn", [](const game::Empire& e) { return e.interfaceOptions.confirmEndTurn; }},
    {"confirm-scrap", [](const game::Empire& e) { return e.interfaceOptions.confirmScrap; }},
    {"confirm-stellar-manipulation", [](const game::Empire& e) { return e.interfaceOptions.confirmStellarManipulation; }},
    {"confirm-delete-research", [](const game::Empire& e) { return e.interfaceOptions.confirmDeleteResearch; }},
    {"confirm-delete-intel", [](const game::Empire& e) { return e.interfaceOptions.confirmDeleteIntel; }},
    {"confirm-delete-first-queue-item", [](const game::Empire& e) { return e.interfaceOptions.confirmDeleteFirstQueueItem; }},
    {"note-similar-abilities", [](const game::Empire& e) { return e.interfaceOptions.noteSimilarAbilities; }},
    {"skip-under-construction", [](const game::Empire& e) { return e.interfaceOptions.skipUnderConstruction; }},
    {"skip-damaged", [](const game::Empire& e) { return e.interfaceOptions.skipDamaged; }},
    {"stop-once-per-location", [](const game::Empire& e) { return e.interfaceOptions.stopOncePerLocation; }},
    {"skip-in-fleets", [](const game::Empire& e) { return e.interfaceOptions.skipInFleets; }},
    {"warp-point-names", [](const game::Empire& e) { return e.interfaceOptions.warpPointNames; }},
    {"planet-names", [](const game::Empire& e) { return e.interfaceOptions.planetNames; }},
    {"colonizable-markers", [](const game::Empire& e) { return e.interfaceOptions.colonizableMarkers; }},
    {"system-grid", [](const game::Empire& e) { return e.interfaceOptions.systemGrid; }},
    {"coordinate-location", [](const game::Empire& e) { return e.interfaceOptions.coordinateLocation; }},
    {"galaxy-grid-lines", [](const game::Empire& e) { return e.interfaceOptions.galaxyGridLines; }},
    {"galaxy-warp-lines", [](const game::Empire& e) { return e.interfaceOptions.galaxyWarpLines; }},
    {"latest-construction-only", [](const game::Empire& e) { return e.interfaceOptions.latestConstructionOnly; }},
    {"latest-components-only", [](const game::Empire& e) { return e.interfaceOptions.latestComponentsOnly; }},
    {"auto-claim-colonized", [](const game::Empire& e) { return e.interfaceOptions.autoClaimColonized; }},
    // Remembered by windows.
    {"planets-no-sys-to-avoid", [](const game::Empire& e) { return e.interfaceOptions.planetsNoSysToAvoid; }},
    {"simulator-no-obsolete", [](const game::Empire& e) { return e.interfaceOptions.simulatorNoObsolete; }},
    {"replay-animate", [](const game::Empire& e) { return e.interfaceOptions.replayAnimate; }},
    {"replay-fast", [](const game::Empire& e) { return e.interfaceOptions.replayFast; }},
    {"replay-view-rect", [](const game::Empire& e) { return e.interfaceOptions.replayViewRect; }},
    {"replay-grid", [](const game::Empire& e) { return e.interfaceOptions.replayGrid; }},
};

constexpr std::pair<game::Treaty, std::string_view> kTreaties[] = {
    {game::Treaty::War, "war"},
    {game::Treaty::NonIntercourse, "non-intercourse"},
    {game::Treaty::NonAggression, "non-aggression"},
    {game::Treaty::Subjugation, "subjugation"},
    {game::Treaty::Protectorate, "protectorate"},
    {game::Treaty::TradeAlliance, "trade-alliance"},
    {game::Treaty::TradeResearchAlliance, "trade-research-alliance"},
    {game::Treaty::MilitaryAlliance, "military-alliance"},
    {game::Treaty::Partnership, "partnership"},
};

std::vector<std::string_view> buildFixedTags() {
    std::vector<std::string_view> out(std::begin(kOtherTags), std::end(kOtherTags));
    out.insert(out.end(), std::begin(kWindowTabs), std::end(kWindowTabs));
    static std::vector<std::string> orderTags = [] {
        std::vector<std::string> v;
        for (const auto& [key, id] : kOrderStrip) v.push_back("order:" + std::string(id));
        return v;
    }();
    for (const std::string& t : orderTags) out.push_back(t);
    return out;
}

template <size_t... I>
std::array<std::string_view, sizeof...(I)> buildCommandNames(std::index_sequence<I...>) {
    return {game::commandName(game::Command{std::in_place_index<I>})...};
}

} // namespace

std::span<const WindowInfo> windows() { return kWindows; }

const WindowInfo* findWindow(std::string_view id) {
    for (const WindowInfo& w : kWindows)
        if (w.id == id) return &w;
    return nullptr;
}

std::span<const std::string_view> helpTabs() { return kHelpTabs; }
bool isHelpTab(std::string_view tab) { return std::find(kHelpTabs.begin(), kHelpTabs.end(), tab) != kHelpTabs.end(); }

std::span<const std::string_view> selectionKinds() { return kSelectionKinds; }
bool isSelectionKind(std::string_view kind) {
    return std::find(kSelectionKinds.begin(), kSelectionKinds.end(), kind) != kSelectionKinds.end();
}

std::string_view orderKindId(game::OrderKind k) {
    for (const auto& [kind, id] : kOrderKinds)
        if (kind == k) return id;
    return {};
}

std::optional<game::OrderKind> orderKindFromId(std::string_view id) {
    for (const auto& [kind, name] : kOrderKinds)
        if (name == id) return kind;
    return std::nullopt;
}

std::span<const std::string_view> commandNames() {
    static const auto names = buildCommandNames(std::make_index_sequence<std::variant_size_v<game::Command>>{});
    return names;
}

bool isCommandName(std::string_view name) {
    const auto names = commandNames();
    return std::find(names.begin(), names.end(), name) != names.end();
}

std::span<const std::string_view> fixedUiTags() {
    static const std::vector<std::string_view> tags = buildFixedTags();
    return tags;
}

std::string_view orderStripId(std::string_view slotKey) {
    for (const auto& [key, id] : kOrderStrip)
        if (key == slotKey) return id;
    return {};
}

std::span<const std::string_view> windowTabs() { return kWindowTabs; }

bool isWindowTab(std::string_view tab) { return std::find(std::begin(kWindowTabs), std::end(kWindowTabs), tab) != std::end(kWindowTabs); }

std::vector<std::string_view> optionNames() {
    std::vector<std::string_view> out;
    for (const OptionInfo& o : kOptions) out.push_back(o.id);
    return out;
}

std::optional<bool> optionValue(const game::Empire& e, std::string_view id) {
    for (const OptionInfo& o : kOptions)
        if (o.id == id) return o.get(e);
    return std::nullopt;
}

bool isOptionName(std::string_view id) {
    return std::any_of(std::begin(kOptions), std::end(kOptions), [&](const OptionInfo& o) { return o.id == id; });
}

std::vector<std::string_view> treatyKinds() {
    std::vector<std::string_view> out;
    for (const auto& [treaty, id] : kTreaties) out.push_back(id);
    return out;
}

std::optional<game::Treaty> treatyFromId(std::string_view id) {
    for (const auto& [treaty, name] : kTreaties)
        if (name == id) return treaty;
    return std::nullopt;
}

bool isUiTag(std::string_view tag) {
    if (tag.starts_with("window:")) return findWindow(tag.substr(7)) != nullptr;
    const auto tags = fixedUiTags();
    return std::find(tags.begin(), tags.end(), tag) != tags.end();
}

} // namespace opense4::learn
