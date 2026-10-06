// Game Setup (docs/spec/06 §1.1, spec 01 §2, spec 02 §9, spec 05 §4-§6) in
// the original's layout (spec 07 session 5): eight pages of options plus the
// empire list; Begin Game builds a game::GameSetup (screens/setup_model.hpp)
// and starts a local or hotseat game. OpenSE4's own additions (the seed, the
// table of possible events, Allow All and Remove All, moving empires in the
// list, Restore Defaults) sit where the original's pages leave room.

#include "client/classic/frontend.hpp"
#include "client/classic/mods_model.hpp"
#include "client/classic/screens/file_dialog.hpp"
#include "client/classic/screens/list_widgets.hpp"
#include "client/classic/screens/setup_empire.hpp"
#include "client/classic/screens/setup_model.hpp"
#include "client/classic/screens/setup_widgets.hpp"
#include "client/classic/settings.hpp"
#include "client/classic/widgets.hpp"
#include "client/script/items.hpp"
#include "datafile/datafile.hpp"
#include "game/economy.hpp"

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <format>
#include <memory>

namespace opense4::client::classic {

namespace {

using namespace setup;
using datafile::keysEqual;

enum class GamePage { Quadrant, Events, Technology, PlayerSettings, Players, Victory, GameSettings, Mechanics, Count };
constexpr std::array<const char*, static_cast<size_t>(GamePage::Count)> kGamePages{
    "Quadrant", "Events", "Technology", "Player Settings", "Players", "Victory Conditions", "Game Settings", "Mechanics"};

std::string squash(std::string_view s) {
    std::string out;
    for (char c : s) {
        if (c >= 'A' && c <= 'Z') out += static_cast<char>(c - 'A' + 'a');
        else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) out += c;
    }
    return out;
}

std::optional<GamePage> gamePageFromName(std::string_view name) {
    const std::string want = squash(name);
    if (want.empty()) return std::nullopt;
    for (size_t i = 0; i < kGamePages.size(); ++i)
        if (squash(kGamePages[i]) == want) return static_cast<GamePage>(i);
    for (size_t i = 0; i < kGamePages.size(); ++i)
        if (squash(kGamePages[i]).starts_with(want)) return static_cast<GamePage>(i);
    return std::nullopt;
}

// The settings of a Game Setup left with Cancel (or for the Mods window), so
// returning to it keeps them (ours). A game begun forgets them: the next New
// Game starts from the defaults, as the original's does (spec 07 session 5).
// Settings made for another data set (the mods changed) are forgotten too.
struct KeptSettings {
    NewGameSettings settings;
    uint64_t generation = 0;   // LoadedMods::generation they were made with
};
std::optional<KeptSettings>& keptSettings() {
    static std::optional<KeptSettings> settings;
    return settings;
}
void keepSettings(const NewGameSettings& s) { keptSettings() = KeptSettings{s, loadedMods().generation}; }
std::optional<NewGameSettings> lastSettings() {
    const auto& kept = keptSettings();
    if (!kept || kept->generation != loadedMods().generation) return std::nullopt;
    return kept->settings;
}

struct PreviewKey {
    uint64_t seed = 0;
    std::string quadrantType;
    int systemCount = 0;
    int quadrantSize = 1;
    bool connected = false, noWarps = false, anywhere = false, noRuins = false, finite = false;
    bool operator==(const PreviewKey&) const = default;
};

PreviewKey previewKey(const NewGameSettings& s) {
    const game::GameOptions& o = s.options;
    return {s.seed, o.quadrantType, o.systemCount, o.quadrantSize, o.allWarpPointsConnected, o.noWarpPoints, o.warpPointsAnywhere, o.noRuins,
            o.finiteResources};
}

ImU32 starColor(std::string_view color) {
    if (keysEqual(color, "Yellow")) return IM_COL32(255, 228, 120, 255);
    if (keysEqual(color, "Orange")) return IM_COL32(255, 165, 80, 255);
    if (keysEqual(color, "Red")) return IM_COL32(255, 95, 75, 255);
    if (keysEqual(color, "Blue")) return IM_COL32(125, 165, 255, 255);
    if (keysEqual(color, "White")) return IM_COL32(235, 238, 255, 255);
    return IM_COL32(210, 210, 210, 255);
}

std::string yearsText(int64_t years) { return std::format("{}.0", years); }

class GameSetupScreen final : public FrontScreen {
public:
    explicit GameSetupScreen(std::string startPage) : startPage_(std::move(startPage)) {}

    void draw(MenuContext& ctx) override {
        if (!rules_) init(ctx);
        if (editor_) {
            drawEditor(ctx);
            return;
        }
        SetupArea a(ctx, "Game Setup###gamesetup", "Game Setup", Decoration::GameSetup);
        if (!a.open()) return;
        // Load Empire (Add Existing) holds the input while it is open.
        const bool modal = loadEmpire_.has_value();
        ImGui::BeginDisabled(modal);
        switch (page_) {
            case GamePage::Quadrant: pageQuadrant(a); break;
            case GamePage::Events: pageEvents(a); break;
            case GamePage::Technology: pageTechnology(a); break;
            case GamePage::PlayerSettings: pagePlayerSettings(a); break;
            case GamePage::Players: pagePlayers(a); break;
            case GamePage::Victory: pageVictory(a); break;
            case GamePage::GameSettings: pageGameSettings(a); break;
            case GamePage::Mechanics: pageMechanics(a); break;
            case GamePage::Count: break;
        }
        for (size_t i = 0; i < kGamePages.size(); ++i)
            if (a.pageButton(static_cast<int>(i), kGamePages[i], page_ == static_cast<GamePage>(i)) && page_ != static_cast<GamePage>(i)) {
                page_ = static_cast<GamePage>(i);
                status_.clear();
            }
        if (!status_.empty()) a.status(status_, statusError_ ? kBad : kGood);
        else a.status(summary(), kDim);
        // OpenSE4's own: the mods the game will use, and the Mods window, under
        // the page buttons' pictures. The settings so far are kept for the way back.
        if (modsLine(ctx, a.at({4, 540}), a.at({2, 562}), a.size({203, 26}), a.px(203)) && !modal) {
            keepSettings(s_);
            ctx.goTo(makeModsScreen([] { return makeGameSetupScreen(); }));
            ImGui::EndDisabled();
            return;
        }
        const bool begin = a.beginButton("Begin Game");
        const bool cancel = a.cancelButton() && !modal;
        ImGui::EndDisabled();
        if (loadEmpire_) drawLoadEmpire(a);
        if (begin && !modal && beginGame(ctx)) return;  // this screen is gone once the game starts
        if (cancel) {
            keepSettings(s_);
            ctx.go(FrontId::Intro);
        }
    }

private:
    const game::Rules& rules() const { return *rules_; }

    void init(MenuContext& ctx) {
        rules_ = ctx.rules;
        const auto kept = lastSettings();
        s_ = kept ? *kept : defaultSettings(rules(), ctx.seed);
        std::string_view start = startPage_;
        if (start.starts_with("empire")) {
            page_ = GamePage::Players;
            openEditor(-1);
            if (const auto colon = start.find(':'); colon != std::string_view::npos && editor_)
                if (auto p = empirePageFromName(start.substr(colon + 1))) editor_->setPage(*p);
        } else if (auto p = gamePageFromName(start)) {
            page_ = *p;
        }
    }

    // Our own line under the frame while nothing else is said there.
    std::string summary() const {
        int humans = 0, computers = 0;
        for (const auto& e : s_.players) (e.kind == game::PlayerKind::Human ? humans : computers)++;
        std::string text = std::format("{} human and {} computer empire{} listed", humans, computers, computers == 1 ? "" : "s");
        for (const bool neutral : {false, true}) {
            const RandomPlayers& rp = neutral ? s_.neutrals : s_.computers;
            if (!rp.enabled) continue;
            const auto [lo, hi] = randomPlayerRange(rules(), neutral, rp.level);
            text += lo == hi ? std::format(", {} random {}", lo, neutral ? "neutral" : "computer")
                             : std::format(", {}-{} random {}", lo, hi, neutral ? "neutral" : "computer");
        }
        if (s_.map) {
            text += std::format("; the map {} ({} systems).", s_.map->name, s_.map->galaxy.systems.size());
        } else if (s_.options.systemCount > 0) {
            text += std::format("; {} systems.", s_.options.systemCount);
        } else {
            const auto [lo, hi] = quadrantSizeRange(rules(), s_.options.quadrantSize);
            text += std::format("; {} to {} systems.", lo, hi);
        }
        if (humans > 1) text += " Humans take turns on this computer (hotseat).";
        return text;
    }

    bool beginGame(MenuContext& ctx) {
        auto setup = buildGameSetup(rules(), s_);
        if (!setup) {
            setStatus(setup.error(), true);
            return false;
        }
        auto session = startLocalGame(ctx.rules, *setup);
        if (!session) {
            setStatus(session.error(), true);
            return false;
        }
        keptSettings().reset();
        newGameStarted(setup->options.simultaneous);
        ctx.startGame(std::move(*session));
        return true;
    }

    void setStatus(std::string text, bool error) {
        status_ = std::move(text);
        statusError_ = error;
    }

    // ---- Empire Setup ---------------------------------------------------------------------------

    void openEditor(int index) {
        EmpireDraft d;
        if (index >= 0 && static_cast<size_t>(index) < s_.players.size()) {
            d = draftFromSetup(rules(), s_.players[static_cast<size_t>(index)]);
        } else {
            index = -1;
            // Add New starts empty, with the first race style (spec 07 session 5).
            d = blankDraft(rules(), nullptr);
        }
        editIndex_ = index;
        editor_.emplace(rules_, std::move(d), s_.options.racialPoints, index < 0);
    }

    void drawEditor(MenuContext& ctx) {
        const EmpireEditor::Result r = editor_->draw(ctx);
        if (r == EmpireEditor::Result::Created) {
            const game::EmpireSetup e = editor_->result();
            if (editIndex_ >= 0 && static_cast<size_t>(editIndex_) < s_.players.size()) {
                s_.players[static_cast<size_t>(editIndex_)] = e;
                setStatus(std::format("{} updated.", e.name), false);
            } else {
                s_.players.push_back(e);
                selected_ = static_cast<int>(s_.players.size()) - 1;
                setStatus(std::format("{} added.", e.name), false);
            }
            editor_.reset();
        } else if (r == EmpireEditor::Result::Cancelled) {
            editor_.reset();
        }
    }

    // ---- Quadrant ---------------------------------------------------------------------------------

    void updatePreview() {
        if (s_.map) {
            // A loaded map is shown as it is.
            if (!previewIsMap_) {
                preview_ = game::Generated{s_.map->galaxy, mapWarnings_};
                previewError_.clear();
                previewIsMap_ = true;
                previewKey_.reset();
            }
            return;
        }
        if (previewIsMap_) {
            previewIsMap_ = false;
            previewKey_.reset();
        }
        // The box stays empty until Generate Map Now (spec 07 session 5); once
        // drawn, the map follows the options, as the game will make it.
        if (!mapShown_) return;
        const PreviewKey key = previewKey(s_);
        if (previewKey_ && *previewKey_ == key) return;
        previewKey_ = key;
        auto g = previewQuadrant(rules(), s_.seed, s_.options);
        if (g) {
            preview_ = std::move(*g);
            previewError_.clear();
        } else {
            preview_.reset();
            previewError_ = g.error();
        }
    }

    // The map in the Quadrant Map box (484,32)–(759,225).
    void drawPreview(SetupArea& a) {
        const Vec2 boxMin{484, 32}, boxMax{759, 225};
        a.box(boxMin, boxMax);
        if (!preview_ || preview_->galaxy.systems.empty() || (!mapShown_ && !s_.map)) {
            if (!previewError_.empty()) a.textWrapped(boxMin + Vec2{6, 8}, previewError_, boxMax.x - boxMin.x - 12, 0xff7060);
            return;
        }
        const game::Galaxy& g = preview_->galaxy;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float spanX = static_cast<float>(std::max(1, g.width - 1)), spanY = static_cast<float>(std::max(1, g.height - 1));
        const float cell = std::min((boxMax.x - boxMin.x - 10) / spanX, (boxMax.y - boxMin.y - 10) / spanY);
        const Vec2 origin{boxMin.x + (boxMax.x - boxMin.x - cell * spanX) * 0.5f, boxMin.y + (boxMax.y - boxMin.y - cell * spanY) * 0.5f};
        auto at = [&](game::GalaxyPos p) { return a.at(origin + Vec2{static_cast<float>(p.x) * cell, static_cast<float>(p.y) * cell}); };
        for (const game::SpaceObject& o : g.objects)
            if (o.kind == game::ObjectKind::WarpPoint && o.destination.valid() && o.id.index() < o.destination.index())
                dl->AddLine(at(g.system(o.system).position), at(g.system(g.object(o.destination).system).position), IM_COL32(70, 110, 220, 170),
                            a.px(1.0f));
        const float radius = a.px(1.6f);
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        int hover = -1;
        float best = a.px(5);
        for (size_t i = 0; i < g.systems.size(); ++i) {
            const game::StarSystem& sys = g.systems[i];
            const ImVec2 c = at(sys.position);
            std::string_view color;
            for (game::ObjectId id : sys.objects)
                if (g.object(id).kind == game::ObjectKind::Star) {
                    color = g.object(id).starColor;
                    break;
                }
            if (color.empty() && !keysEqual(sys.physicalType, "Normal")) dl->AddCircle(c, radius, IM_COL32(170, 120, 230, 255));
            else dl->AddCircleFilled(c, radius, starColor(color));
            const float d = std::hypot(mouse.x - c.x, mouse.y - c.y);
            if (ImGui::IsWindowHovered() && d < best) {
                best = d;
                hover = static_cast<int>(i);
            }
        }
        // A loaded map's starting points.
        if (s_.map)
            for (const game::StartingPoint& p : s_.map->startingPoints)
                if (p.system.index() < g.systems.size()) dl->AddCircle(at(g.system(p.system).position), radius + a.px(2), IM_COL32(120, 255, 140, 255));
        if (hover >= 0) {
            // Ours: what a system is, under the pointer.
            const game::StarSystem& sys = g.systems[static_cast<size_t>(hover)];
            dl->AddCircle(at(sys.position), radius + a.px(3), IM_COL32(255, 230, 120, 255));
            int planets = 0;
            for (game::ObjectId id : sys.objects) planets += g.object(id).kind == game::ObjectKind::Planet ? 1 : 0;
            const auto& types = rules().data().systemTypes;
            const ruleset::SystemType* type = sys.type.index() < types.size() ? &types[sys.type.index()] : nullptr;
            ImGui::BeginTooltip();
            ImGui::TextColored(kHighlight, "%s", sys.name.c_str());
            if (type) ImGui::TextUnformatted(type->name.c_str());
            ImGui::TextColored(kDim, "%d planets, %zu warp points", planets, g.warpPoints(sys.id).size());
            if (type && type->empiresCanStartIn) ImGui::TextColored(kGood, "Empires can start here");
            ImGui::EndTooltip();
        }
    }

    void pageQuadrant(SetupArea& a) {
        game::GameOptions& o = s_.options;
        a.heading({231, 21}, "Quadrant Type");
        std::vector<std::string> types;
        int type = 0;
        for (const auto& q : rules().data().quadrantTypes) {
            if (keysEqual(q.name, o.quadrantType)) type = static_cast<int>(types.size());
            types.push_back(q.name);
        }
        if (a.lampList("##qtypes", {232, 32}, {451, 171}, type, std::span<const std::string>(types), true, true))
            o.quadrantType = types[static_cast<size_t>(type)];
        a.heading({484, 21}, "Quadrant Map");
        updatePreview();
        drawPreview(a);
        a.heading({231, 193}, "Quadrant Size");
        if (a.lampList("##qsize", {232, 204}, {451, 263}, o.quadrantSize, {"Small", "Medium", "Large"})) o.systemCount = 0;
        a.heading({231, 285}, "General Options");
        std::array<SetupArea::Check, 7> options{{
            {"All Warp Points connected", &o.allWarpPointsConnected, !o.noWarpPoints},
            {"No Warp Points", &o.noWarpPoints},
            {"Warp Points located anywhere in system", &o.warpPointsAnywhere, !o.noWarpPoints},
            {"All systems seen by all players", &o.allSystemsSeen},
            {"Omnipresent view of all systems", &o.omnipresent},
            {"Finite resources", &o.finiteResources},
            {"All player planets the same size", &o.allPlanetsSameSize},
        }};
        a.checkList("##general", {232, 296}, {531, 455}, options);

        if (a.button({234, 506}, {414, 531}, "Generate Map Now")) {
            // Each press rerolls the quadrant; the game starts with the map shown.
            Rng next(s_.seed);
            s_.seed = next.next() % 1000000000ull + 1;
            if (s_.map) setStatus("Back to a generated quadrant; the loaded map and its starting points are gone.", false);
            clearMap(s_);
            mapShown_ = true;
        }
        if (a.button({419, 506}, {599, 531}, "Load Map")) {
            mapFiles_ = listMapFiles(rules(), mapsDir());
            ImGui::OpenPopup("Load Map");
        }
        if (a.button({604, 506}, {784, 531}, "Save Map", preview_.has_value() && (mapShown_ || s_.map))) {
            if (mapName_.empty()) mapName_ = s_.map && !s_.map->name.empty() ? s_.map->name : std::format("Quadrant {}", s_.seed);
            ImGui::OpenPopup("Save Map");
        }
        loadMapPopup(a);
        saveMapPopup(a);

        // OpenSE4's own: the seed the quadrant and the game are made from, and what the map holds.
        a.heading({548, 245}, "Seed");
        std::string seed = std::to_string(s_.seed);
        if (a.edit("##seed", {590, 240}, {700, 259}, seed, ImGuiInputTextFlags_CharsDecimal)) {
            uint64_t v = 0;
            for (char c : seed)
                if (c >= '0' && c <= '9' && v < 100000000000000000ull) v = v * 10 + static_cast<uint64_t>(c - '0');
            s_.seed = v ? v : 1;
        }
        float y = 270;
        if (o.systemCount > 0) y += a.textWrapped({548, y}, std::format("Exactly {} systems (set on the command line).", o.systemCount), 235) + 4;
        if (s_.map) y += a.textWrapped({548, y}, std::format("Map: {} ({} starting points)", s_.map->name, s_.map->startingPoints.size()), 235, 0x80ff90) + 4;
        if (preview_ && (mapShown_ || s_.map)) {
            size_t links = 0, starts = 0;
            for (const auto& obj : preview_->galaxy.objects)
                if (obj.kind == game::ObjectKind::WarpPoint && obj.destination.valid()) ++links;
            for (const auto& sys : preview_->galaxy.systems)
                if (sys.type.index() < rules().data().systemTypes.size() && rules().data().systemTypes[sys.type.index()].empiresCanStartIn) ++starts;
            y += a.textWrapped({548, y}, std::format("{} systems, {} warp links, {} where empires can start", preview_->galaxy.systems.size(), links / 2, starts),
                               235) + 4;
            for (const auto& w : preview_->warnings) y += a.textWrapped({548, y}, w, 235, 0xff7060) + 2;
        }
    }

    static std::filesystem::path mapsDir() {
        const std::filesystem::path dir = userDataDir() / "maps";
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        return dir;
    }

    void loadMapPopup(SetupArea& a) {
        MenuContext& ctx = a.ctx();
        ImGui::SetNextWindowSize(ctx.size({520, 420}), ImGuiCond_Always);
        ImGui::SetNextWindowPos(ctx.at({frameW() * 0.5f, frameH() * 0.5f}), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ctx.size({10, 10}));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ctx.size({6, 4}));
        if (ImGui::BeginPopupModal("Load Map", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) {
            ImGui::PushStyleColor(ImGuiCol_Text, kDim);
            ImGui::TextWrapped("Map files in %s", mapsDir().string().c_str());
            ImGui::PopStyleColor();
            beginList(ctx.painter(), "##maps", ImVec2(0, -ctx.px(38)), kListLineStep, ImGuiChildFlags_AlwaysUseWindowPadding);
            if (mapFiles_.empty()) ImGui::TextColored(kDim, "No saved maps yet. Save Map keeps the quadrant shown here for later games.");
            for (size_t i = 0; i < mapFiles_.size(); ++i) {
                const MapFileInfo& f = mapFiles_[i];
                ImGui::PushID(static_cast<int>(i));
                const std::string label = std::format("{}  ({} systems, {} starting points)", f.name, f.systems, f.startingPoints);
                if (ImGui::Selectable(label.c_str())) {
                    auto loaded = game::loadMapFile(f.path, rules().data());
                    if (!loaded) {
                        setStatus(loaded.error(), true);
                    } else {
                        mapWarnings_ = loaded->warnings;
                        useMap(s_, std::move(loaded->map));  // replaces the previous map and its starting points
                        previewIsMap_ = false;
                        std::string text = std::format("Map {} loaded.", s_.map->name);
                        for (const auto& w : mapWarnings_) text += " " + w;
                        setStatus(text, !mapWarnings_.empty());
                    }
                    ImGui::CloseCurrentPopup();
                }
                ImGui::PopID();
            }
            endList(ctx.painter());
            if (classicButton(ctx.painter(), "Cancel##loadmap", {120, 28}) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        ImGui::PopStyleVar(2);
    }

    void saveMapPopup(SetupArea& a) {
        MenuContext& ctx = a.ctx();
        ImGui::SetNextWindowSize(ctx.size({440, 150}), ImGuiCond_Always);
        ImGui::SetNextWindowPos(ctx.at({frameW() * 0.5f, frameH() * 0.5f}), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ctx.size({10, 10}));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ctx.size({6, 4}));
        if (ImGui::BeginPopupModal("Save Map", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) {
            ImGui::TextColored(kLabelBlue, "Map name");
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
            const bool enter = inputText("##mapname", mapName_, -FLT_MIN, ImGuiInputTextFlags_EnterReturnsTrue);
            const std::filesystem::path file = mapFilePath(mapsDir(), mapName_);
            std::error_code ec;
            if (std::filesystem::exists(file, ec)) ImGui::TextColored(kBad, "%s exists and will be replaced.", file.filename().string().c_str());
            if ((classicButton(ctx.painter(), "Save", {120, 28}) || enter) && preview_) {
                auto saved = game::saveMapFile(file, rules().data(), mapToSave(s_, preview_->galaxy, mapName_));
                if (saved) setStatus(std::format("Map saved to {}", file.string()), false);
                else setStatus(saved.error(), true);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (classicButton(ctx.painter(), "Cancel##savemap", {120, 28}) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        ImGui::PopStyleVar(2);
    }

    // ---- Events ----------------------------------------------------------------------------------

    void pageEvents(SetupArea& a) {
        game::GameOptions& o = s_.options;
        a.heading({231, 21}, "Event Frequency");
        a.lampList("##freq", {232, 32}, {451, 111}, o.eventFrequency, {"None", "Low", "Medium", "High"});
        a.heading({231, 133}, "Maximum Event Severity");
        static constexpr std::array<const char*, 4> kSeverity{"Low", "Medium", "High", "Catastrophic"};
        a.lampList("##sev", {232, 144}, {451, 223}, o.maxEventSeverity, {kSeverity[0], kSeverity[1], kSeverity[2], kSeverity[3]});

        // OpenSE4's own: the events these choices allow.
        auto severityOf = [](std::string_view s) {
            for (size_t i = 0; i < kSeverity.size(); ++i)
                if (keysEqual(s, kSeverity[i])) return static_cast<int>(i);
            return 0;
        };
        const auto& events = rules().data().eventTypes;
        int eligible = 0;
        for (const auto& e : events) eligible += severityOf(e.severity) <= o.maxEventSeverity ? 1 : 0;
        a.heading({470, 21}, "Possible Events");
        a.textRight({783, 21}, o.eventFrequency == 0 ? std::string("none: events are off") : std::format("{} of {}", eligible, events.size()), kExplainRgb,
                    Face::Small);
        a.box({470, 32}, {783, 535});
        a.place({471, 33});
        const Painter p = a.painter();
        beginList(p, "##events", a.size({312, 502}), 18.0f, ImGuiChildFlags_None, false);
        const float rowW = ImGui::GetContentRegionAvail().x;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        for (const auto& e : events) {
            const bool on = o.eventFrequency > 0 && severityOf(e.severity) <= o.maxEventSeverity;
            const ImVec2 r0 = ImGui::GetCursorScreenPos();
            ImGui::Dummy(ImVec2(rowW, p.px(18)));
            const std::string title = !e.messages.empty() && !e.messages.front().title.empty() ? e.messages.front().title : e.type;
            const ImU32 c = imColor(on ? kWhite : kDimTextRgb);
            dl->PushClipRect(r0, {r0.x + rowW - p.px(90), r0.y + p.px(18)}, true);
            dl->AddText({r0.x + p.px(4), r0.y + p.px(1)}, c, title.c_str());
            dl->PopClipRect();
            dl->AddText({r0.x + rowW - p.px(86), r0.y + p.px(1)}, imColor(on ? kExplainRgb : kDimTextRgb), e.severity.c_str());
        }
        endList(p);
    }

    // ---- Technology ---------------------------------------------------------------------------------

    void pageTechnology(SetupArea& a) {
        game::GameOptions& o = s_.options;
        a.heading({231, 21}, "Technology Cost");
        o.techCost = std::clamp(o.techCost, 0, 2);
        a.lampList("##techcost", {232, 32}, {451, 91}, o.techCost, {"Low", "Medium", "High"});

        // Every tech area in alphabetical order, all on in a new game.
        a.heading({231, 113}, "Technology Areas Allowed");
        const auto& areas = rules().data().techAreas;
        std::vector<size_t> order(areas.size());
        for (size_t i = 0; i < order.size(); ++i) order[i] = i;
        std::stable_sort(order.begin(), order.end(), [&](size_t x, size_t y) { return squash(areas[x].name) < squash(areas[y].name); });
        // bool storage for the check rows (std::vector<bool> has no references).
        const std::unique_ptr<bool[]> values = std::make_unique<bool[]>(areas.size() + 1);
        for (size_t i = 0; i < areas.size(); ++i)
            values[i] = !areas[i].canBeRemoved || i >= o.techAreasAllowed.size() || o.techAreasAllowed[i] != 0;
        std::vector<SetupArea::Check> rows;
        for (const size_t i : order) rows.push_back({areas[i].name, &values[i], areas[i].canBeRemoved});
        if (a.checkList("##areas", {232, 124}, {531, 323}, rows, true)) {
            o.techAreasAllowed.assign(areas.size(), 1);
            for (size_t i = 0; i < areas.size(); ++i) o.techAreasAllowed[i] = values[i] ? 1 : 0;
        }
        // OpenSE4's own: everything on or off at once.
        auto setAll = [&](bool v) {
            o.techAreasAllowed.assign(areas.size(), 1);
            for (size_t i = 0; i < areas.size(); ++i)
                if (areas[i].canBeRemoved) o.techAreasAllowed[i] = v ? 1 : 0;
        };
        if (a.button({548, 124}, {728, 149}, "Allow All")) setAll(true);
        if (a.button({548, 154}, {728, 179}, "Remove All")) setAll(false);
        a.textWrapped({548, 190}, "Items that need a removed area can never be built. Areas that cannot be removed stay allowed.", 230);
    }

    // ---- Player Settings --------------------------------------------------------------------------------

    void pagePlayerSettings(SetupArea& a) {
        game::GameOptions& o = s_.options;
        a.heading({231, 21}, "Starting Resources for Player");
        {
            int level = 1;
            for (size_t i = 0; i < kStartingResources.size(); ++i)
                if (o.startingResources.v[0] == kStartingResources[i]) level = static_cast<int>(i);
            const std::vector<std::string> labels{std::format("Low ({})", kStartingResources[0]), std::format("Medium ({})", kStartingResources[1]),
                                                  std::format("High ({})", kStartingResources[2])};
            if (a.lampList("##res", {232, 32}, {401, 91}, level, std::span<const std::string>(labels))) {
                const int64_t v = kStartingResources[static_cast<size_t>(level)];
                o.startingResources = {v, v, v};
            }
        }
        a.heading({418, 21}, "Home Planet Value");
        a.lampList("##home", {418, 32}, {587, 91}, o.homePlanetValue, {"Bad", "Average", "Good"});
        a.heading({604, 21}, "Number of Starting Planets");
        {
            int choice = 0;
            for (size_t i = 0; i < kStartingPlanets.size(); ++i)
                if (o.startingPlanets == kStartingPlanets[i]) choice = static_cast<int>(i);
            if (a.lampList("##planets", {604, 32}, {773, 111}, choice, {"1", "3", "5", "10"})) o.startingPlanets = kStartingPlanets[static_cast<size_t>(choice)];
        }
        a.heading({231, 113}, "Empire Placement");
        std::array<SetupArea::Check, 2> placement{{
            {"Allowed to start in the same system", &o.sameSystemAllowed},
            {"Evenly distributed through the quadrant", &o.evenlyDistributed},
        }};
        a.checkList("##placement", {232, 124}, {531, 183}, placement);
        a.heading({231, 205}, "Score Display");
        o.scoreDisplay = std::clamp(o.scoreDisplay, 0, 2);
        a.lampList("##scores", {232, 216}, {531, 275}, o.scoreDisplay, {"Own Score Only", "All Allied Players' Scores", "All Players' Scores"});
        a.heading({231, 297}, "Technology Level for New Player");
        a.lampList("##techlevel", {232, 308}, {451, 367}, o.startTechLevel, {"Low", "Medium", "High"});
        a.heading({231, 389}, "Racial Points for New Players");
        {
            int level = 1;
            for (size_t i = 0; i < kRacialPoints.size(); ++i)
                if (o.racialPoints == kRacialPoints[i]) level = static_cast<int>(i);
            const std::vector<std::string> labels{"None (0)", std::format("Low ({})", kRacialPoints[1]), std::format("Medium ({})", kRacialPoints[2]),
                                                  std::format("High ({})", kRacialPoints[3])};
            if (a.lampList("##rp", {232, 400}, {451, 479}, level, std::span<const std::string>(labels))) o.racialPoints = kRacialPoints[static_cast<size_t>(level)];
        }
    }

    // ---- Players ------------------------------------------------------------------------------------

    void pagePlayers(SetupArea& a) {
        auto& players = s_.players;
        selected_ = players.empty() ? -1 : std::clamp(selected_, 0, static_cast<int>(players.size()) - 1);
        a.heading({231, 21}, "Players in Game");
        a.heading({419, 21}, "Number of Players:");
        a.textRight({591, 21}, std::to_string(players.size()));

        // The list: a 20 px heading row (lamp, Flag, Empire Name, Race Age), then one row per empire.
        const Painter p = a.painter();
        a.box({232, 35}, {591, 324});
        a.text({257, 40}, "Flag", kHeadingRgb);
        a.text({293, 40}, "Empire Name", kHeadingRgb);
        a.text({434, 40}, "Race Age", kHeadingRgb);
        a.box({233, 55}, {590, 55}, kInnerRgb);
        a.place({233, 56});
        beginList(p, "##players", a.size({358, 268}), 20.0f, ImGuiChildFlags_None, false);
        const float rowW = ImGui::GetContentRegionAvail().x;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        for (size_t i = 0; i < players.size(); ++i) {
            const game::EmpireSetup& e = players[i];
            const game::Race race = raceOf(rules(), e);
            const ImVec2 r0 = ImGui::GetCursorScreenPos();
            ImGui::PushID(static_cast<int>(i));
            if (ImGui::InvisibleButton("##empire", ImVec2(rowW, p.px(20)))) selected_ = static_cast<int>(i);
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) openEditor(static_cast<int>(i));
            ImGui::PopID();
            script::reportItem(e.name);   // input scripts find an empire by its name
            const bool on = selected_ == static_cast<int>(i);
            if (on)
                if (Sprite grid = p.art.region("Pictures/Game/Dialogs/Rowgrid.bmp", 0, 0, int(rowW / p.k()), 20, false))
                    dl->AddImage(ImTextureRef(static_cast<ImTextureID>(grid.tex.value)), r0, {r0.x + rowW, r0.y + p.px(20)},
                                 {grid.uv.min.x, grid.uv.min.y}, {grid.uv.max.x, grid.uv.max.y});
            if (Sprite s = p.art.region("Pictures/Game/General.bmp", 178 + 13 * (on ? 1 : 0), 0, 13, 13))
                dl->AddImage(ImTextureRef(static_cast<ImTextureID>(s.tex.value)), {r0.x + p.px(3), r0.y + p.px(3.5f)}, {r0.x + p.px(16), r0.y + p.px(16.5f)},
                             {s.uv.min.x, s.uv.min.y}, {s.uv.max.x, s.uv.max.y});
            if (Sprite flag = p.art.flag(race.style, true))
                dl->AddImage(ImTextureRef(static_cast<ImTextureID>(flag.tex.value)), {r0.x + p.px(24), r0.y + p.px(1)}, {r0.x + p.px(50), r0.y + p.px(19)},
                             {flag.uv.min.x, flag.uv.min.y}, {flag.uv.max.x, flag.uv.max.y});
            const std::string name = e.empireType.empty() ? e.name : e.name + " " + e.empireType;
            dl->PushClipRect(r0, {r0.x + p.px(198), r0.y + p.px(20)}, true);
            dl->AddText({r0.x + p.px(60), r0.y + p.px(2)}, IM_COL32_WHITE, name.c_str());
            dl->PopClipRect();
            const std::string age(game::economy::raceAge(e.experience));
            dl->AddText({r0.x + p.px(201), r0.y + p.px(2)}, IM_COL32_WHITE, age.c_str());
            if (e.kind != game::PlayerKind::Human)  // ours: a listed computer player says so
                dl->AddText({r0.x + p.px(275), r0.y + p.px(2)}, imColor(kExplainRgb), "Computer");
        }
        endList(p);

        const bool has = selected_ >= 0 && static_cast<size_t>(selected_) < players.size();
        const bool full = static_cast<int>(players.size()) >= kMaxEmpires;
        if (a.button({603, 57}, {783, 82}, "Add New", !full)) openEditor(-1);
        if (a.button({603, 87}, {783, 112}, "Add Existing", !full)) {
            loadEmpire_.emplace("Load Empire", "Empire Filename", ".toml", empireDirectory(), userDataDir() / "empires");
            loadEmpire_->rescan();
        }
        if (a.button({603, 117}, {783, 142}, "Edit", has)) openEditor(selected_);
        if (a.button({603, 147}, {783, 172}, "Remove", has)) {
            const std::string name = players[static_cast<size_t>(selected_)].name;
            players.erase(players.begin() + selected_);
            setStatus(std::format("{} removed.", name), false);
        }
        if (a.button({603, 177}, {783, 202}, "Save To File", has)) {
            auto file = saveEmpireFile(rules(), userDataDir() / "empires", players[static_cast<size_t>(selected_)]);
            if (file) setStatus(std::format("Saved to {}", file->string()), false);
            else setStatus(file.error(), true);
        }
        // OpenSE4's own: the order of the empires (player order).
        if (a.button({603, 207}, {783, 232}, "Move Up", has && selected_ > 0)) {
            std::swap(players[static_cast<size_t>(selected_)], players[static_cast<size_t>(selected_ - 1)]);
            --selected_;
        }
        if (a.button({603, 237}, {783, 262}, "Move Down", has && static_cast<size_t>(selected_ + 1) < players.size())) {
            std::swap(players[static_cast<size_t>(selected_)], players[static_cast<size_t>(selected_ + 1)]);
            ++selected_;
        }

        a.heading({231, 346}, "Random Computer Players");
        std::array<SetupArea::Check, 2> random{{
            {"Random computer controlled empires", &s_.computers.enabled},
            {"Random computer controlled neutral empires", &s_.neutrals.enabled},
        }};
        a.checkList("##random", {232, 357}, {591, 396}, random);
        // One choice for both kinds (spec 01 §2.2, observed).
        a.heading({231, 418}, "Number of Computer Players");
        if (a.lampList("##count", {232, 429}, {401, 488}, s_.computers.level, {"Low", "Medium", "High"})) s_.neutrals.level = s_.computers.level;
        a.heading({418, 418}, "Computer Player Difficulty");
        s_.options.aiDifficulty = std::clamp(s_.options.aiDifficulty, 0, 2);
        a.lampList("##diff", {418, 429}, {587, 488}, s_.options.aiDifficulty, {"Low", "Medium", "High"});
        a.heading({604, 418}, "Computer Player Bonus");
        a.lampList("##bonus", {604, 429}, {773, 508}, s_.options.aiBonus, {"None", "Low", "Medium", "High"});
    }

    static std::filesystem::path& empireDirectory() {
        static std::filesystem::path dir;
        if (dir.empty()) dir = userDataDir() / "empires";
        return dir;
    }

    // Add Existing: the Load Game dialog as "Load Empire" (spec 07 session 5).
    void drawLoadEmpire(SetupArea& a) {
        const FileDialog::Result r = loadEmpire_->draw(a.painter(), "Load Empire###loadempire");
        if (r == FileDialog::Result::Cancelled) {
            loadEmpire_.reset();
        } else if (r == FileDialog::Result::Chosen) {
            const FileEntry f = loadEmpire_->chosen();
            auto loaded = loadEmpireFile(rules(), f.path);
            if (!loaded) {
                loadEmpire_->setError(loaded.error());
                return;
            }
            s_.players.push_back(loaded->empire);
            selected_ = static_cast<int>(s_.players.size()) - 1;
            std::string text = std::format("{} added from {}.", loaded->empire.name, f.path.filename().string());
            for (const auto& w : loaded->warnings) text += " " + w;
            setStatus(text, !loaded->warnings.empty());
            loadEmpire_.reset();
        }
    }

    // ---- Victory Conditions ---------------------------------------------------------------------------

    void pageVictory(SetupArea& a) {
        game::VictoryConditions& v = s_.options.victory;
        // Our wording of the original's two-line explanation.
        a.heading({231, 18}, "Check the conditions that end the game. The game ends after the turn in which");
        a.heading({231, 34}, "an empire meets any checked condition; the last one only delays the others.");
        struct Row {
            const char* label;
            bool* on;
            int64_t value;
            int64_t step, lo, hi;
            std::string shown;
        };
        std::array<Row, 6> rows{{
            {"An empire's score reaches this value", &v.score, v.scoreValue, 100000, 1000, 1'000'000'000'000, std::to_string(v.scoreValue)},
            {"This many years have passed", &v.years, v.yearsValue, 1, 1, 10000, yearsText(v.yearsValue)},
            {"An empire's score is this percent of the second place's", &v.percentOfSecond, v.percentOfSecondValue, 10, 100, 100000,
             std::format("{}%", v.percentOfSecondValue)},
            {"An empire has researched this percent of the tech areas", &v.techPercent, v.techPercentValue, 5, 1, 100, std::format("{}%", v.techPercentValue)},
            {"The quadrant has been at peace for this many years", &v.peace, v.peaceYears, 1, 1, 10000, yearsText(v.peaceYears)},
            {"No condition applies before this many years (a qualifier)", &v.delay, v.delayYears, 1, 1, 10000, yearsText(v.delayYears)},
        }};
        for (size_t i = 0; i < rows.size(); ++i) {
            Row& r = rows[i];
            const float y = 66 + 40.0f * float(i);
            ImGui::PushID(static_cast<int>(i));
            a.checkBox("##on", {234, y}, r.label, *r.on, true, 253, 330);
            if (a.spin("##value", {602, y + 3}, 101, r.value, r.step, r.lo, r.hi, r.shown, *r.on)) {
                switch (i) {
                    case 0: v.scoreValue = r.value; break;
                    case 1: v.yearsValue = static_cast<int>(r.value); break;
                    case 2: v.percentOfSecondValue = static_cast<int>(r.value); break;
                    case 3: v.techPercentValue = static_cast<int>(r.value); break;
                    case 4: v.peaceYears = static_cast<int>(r.value); break;
                    default: v.delayYears = static_cast<int>(r.value); break;
                }
            }
            ImGui::PopID();
        }
    }

    // ---- Game Settings ----------------------------------------------------------------------------------

    void pageGameSettings(SetupArea& a) {
        game::GameOptions& o = s_.options;
        // A game on this computer keeps no master password (spec 06 §1.2.1);
        // network games set one in Multiplayer. The box stays dim (ours).
        a.heading({231, 21}, "Game Master Password");
        a.edit("##master", {586, 16}, {735, 35}, masterPassword_, ImGuiInputTextFlags_Password, false);
        a.heading({231, 57}, "Maximum number of units allowed (in space) per player");
        int64_t units = o.maxUnitsPerPlayer, ships = o.maxShipsPerPlayer;
        if (a.spin("##units", {586, 52}, 101, units, 50, 1, 32767, std::to_string(units))) o.maxUnitsPerPlayer = static_cast<int>(units);
        a.heading({231, 93}, "Maximum number of ships allowed per player");
        if (a.spin("##ships", {586, 88}, 101, ships, 10, 1, 32767, std::to_string(ships))) o.maxShipsPerPlayer = static_cast<int>(ships);
        // Twelve check rows in the order of spec 01 §2.2. Cheat codes are not
        // in our game options: dim (PARITY_GAPS).
        bool cheats = false;
        struct Row {
            const char* label;
            bool* value;
            bool enabled;
        };
        const std::array<Row, 12> rows{{
            {"Cheat codes allowed", &cheats, false},
            {"Team Mode", &o.teamMode, true},
            {"No Tactical Combat", &o.noTacticalCombat, true},
            {"Players can see the complete tech tree", &o.completeTechTree, true},
            {"Allow gifts/tributes", &o.allowGifts, true},
            {"Allow technology gifts, tributes and trades", &o.allowTechTrades, true},
            {"Allow surrender", &o.allowSurrender, true},
            {"Allow intelligence projects", &o.allowIntel, true},
            {"No Ruins", &o.noRuins, true},
            {"Only breathable atmosphere", &o.onlyBreathable, true},
            {"Only home planet type", &o.onlyHomeType, true},
            {"Players can save the map during the game", &o.playersCanSaveMap, true},
        }};
        for (size_t i = 0; i < rows.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            a.checkBox("##setting", {234, 126 + 30.0f * float(i)}, rows[i].label, *rows[i].value, rows[i].enabled);
            ImGui::PopID();
        }
    }

    // ---- Mechanics -------------------------------------------------------------------------------------

    // A choice with a description under its label (Play Style, Turn Style).
    bool describedChoice(SetupArea& a, const char* id, float y, const char* label, std::span<const char* const> lines, bool on, bool enabled) {
        a.place({233, y});
        ImGui::PushID(id);
        ImGui::BeginDisabled(!enabled);
        const bool clicked = ImGui::InvisibleButton("##choice", a.size({548, 18 + 16.0f * float(lines.size())}));
        ImGui::EndDisabled();
        ImGui::PopID();
        script::reportItem(label);
        a.lamp({241.5f, y + 9}, on, enabled);
        a.text({253, y + 4}, label, enabled ? kWhite : kDimTextRgb);
        for (size_t i = 0; i < lines.size(); ++i) a.text({266, y + 21 + 16.0f * float(i)}, lines[i], enabled ? kExplainRgb : kDimTextRgb);
        return clicked && enabled;
    }

    void pageMechanics(SetupArea& a) {
        game::GameOptions& o = s_.options;
        int humans = 0;
        for (const auto& e : s_.players) humans += e.kind == game::PlayerKind::Human ? 1 : 0;
        a.heading({231, 21}, "Play Style");
        a.box({232, 32}, {783, 141});
        static constexpr std::array<const char*, 1> kHotseat{"All players take their turns on this computer."};
        static constexpr std::array<const char*, 1> kMachines{"Each player plays on a computer of their own (set up from Multiplayer on the intro)."};
        describedChoice(a, "##hotseat", 37, "Hotseat", kHotseat, true, true);
        describedChoice(a, "##machines", 81, "Different Machines", kMachines, false, false);

        a.heading({231, 163}, "Turn Style");
        a.box({232, 174}, {783, 313});
        static constexpr std::array<const char*, 1> kTurnBased{"Players move one after another; orders are carried out as they are given."};
        static constexpr std::array<const char*, 3> kSimultaneous{"Every player gives orders for the turn, then all of them are carried out",
                                                                   "together, day by day through the month. Battles are resolved without",
                                                                   "tactical combat."};
        if (describedChoice(a, "##turnbased", 179, "Turn Based Movement", kTurnBased, !o.simultaneous, true)) o.simultaneous = false;
        if (describedChoice(a, "##simultaneous", 223, "Simultaneous Movement", kSimultaneous, o.simultaneous, true)) o.simultaneous = true;

        // Network and e-mail games are set up from Multiplayer: these stay dim here.
        a.heading({231, 335}, "Multiplayer Game Filename");
        a.edit("##mpfile", {232, 346}, {431, 365}, multiplayerFile_, 0, false);
        a.heading({484, 335}, "Save Game Directory Path");
        std::string saves = savesDir().string();
        a.edit("##savedir", {484, 346}, {783, 365}, saves, 0, false);

        a.heading({231, 387}, "Autosave Frequency");
        {
            std::vector<std::string> choices;
            int current = 0;
            for (size_t i = 0; i < kAutosaveTurns.size(); ++i) {
                const int n = kAutosaveTurns[i];
                choices.push_back(n == 0 ? std::string("None") : n == 1 ? std::string("Every Turn") : std::format("Every {} Turns", n));
                if (n == o.autosaveTurns) current = static_cast<int>(i);
            }
            if (a.lampList("##autosave", {232, 398}, {431, 517}, current, std::span<const std::string>(choices)))
                o.autosaveTurns = kAutosaveTurns[static_cast<size_t>(std::clamp(current, 0, static_cast<int>(kAutosaveTurns.size()) - 1))];
        }
        a.heading({484, 387}, "Connection Type");
        int connection = 0;
        a.lampList("##connection", {484, 398}, {683, 477}, connection, {"Manual File Moving", "TCP/IP Host", "TCP/IP Player"}, false);
        a.textWrapped({484, 484}, humans > 1 ? "Several human players: each plays their turn in order on this computer."
                                              : "Network and e-mail games: Multiplayer on the intro.",
                      300);

        // OpenSE4's own: every page back to a new game's settings.
        if (a.button({604, 506}, {784, 531}, "Restore Defaults")) {
            s_ = defaultSettings(rules(), s_.seed);
            selected_ = 0;
            mapShown_ = false;
            setStatus("Every setting is back to its default, the empire list included.", false);
        }
    }

    std::string startPage_;
    std::shared_ptr<const game::Rules> rules_;
    NewGameSettings s_;
    GamePage page_ = GamePage::Players;   // the original opens on Players (spec 07 session 5)
    std::optional<EmpireEditor> editor_;
    int editIndex_ = -1;
    int selected_ = 0;
    std::optional<PreviewKey> previewKey_;
    std::optional<game::Generated> preview_;
    std::string previewError_;
    bool previewIsMap_ = false;               // preview_ shows s_.map
    bool mapShown_ = false;                   // Generate Map Now has drawn the map
    std::vector<std::string> mapWarnings_;    // what the loaded map lacked in this data set
    std::vector<MapFileInfo> mapFiles_;
    std::string mapName_;
    std::string status_;
    bool statusError_ = false;
    std::optional<FileDialog> loadEmpire_;
    std::string masterPassword_, multiplayerFile_;
};

} // namespace

std::unique_ptr<FrontScreen> makeGameSetupScreen(std::string_view startPage) { return std::make_unique<GameSetupScreen>(std::string(startPage)); }

} // namespace opense4::client::classic
