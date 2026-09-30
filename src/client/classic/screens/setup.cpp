// Game Setup (docs/spec/06 §1.1, spec 01 §2, spec 02 §9, spec 05 §4-§6):
// eight pages of options plus the empire list; Begin Game builds a
// game::GameSetup (screens/setup_model.hpp) and starts a local or hotseat game.

#include "client/classic/frontend.hpp"
#include "client/classic/screens/setup_empire.hpp"
#include "client/classic/screens/setup_model.hpp"
#include "client/classic/screens/setup_widgets.hpp"
#include "client/classic/widgets.hpp"
#include "datafile/datafile.hpp"

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <format>

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

// The last settings used this session, so returning to Game Setup keeps them.
std::optional<NewGameSettings>& lastSettings() {
    static std::optional<NewGameSettings> settings;
    return settings;
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

// Right-aligned label before a control, in a fixed label column.
void rowLabel(MenuContext& ctx, const char* text, float column) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(kLabelBlue, "%s", text);
    ImGui::SameLine(ctx.px(column));
}

class GameSetupScreen final : public FrontScreen {
public:
    explicit GameSetupScreen(std::string startPage) : startPage_(std::move(startPage)) {}

    void draw(MenuContext& ctx) override {
        if (!rules_) init(ctx);
        if (editor_) {
            drawEditor(ctx);
            return;
        }
        const bool escape = escapePressed();
        const std::string title = std::format("Game Setup - {}###gamesetup", kGamePages[static_cast<size_t>(page_)]);
        SetupFrame frame(ctx, title.c_str());
        if (!frame.open()) return;

        frame.beginContent();
        ImGui::BeginChild("##page", ImVec2(0, -ctx.px(30)));
        switch (page_) {
            case GamePage::Quadrant: pageQuadrant(ctx); break;
            case GamePage::Events: pageEvents(ctx); break;
            case GamePage::Technology: pageTechnology(ctx); break;
            case GamePage::PlayerSettings: pagePlayerSettings(ctx); break;
            case GamePage::Players: pagePlayers(ctx); break;
            case GamePage::Victory: pageVictory(ctx); break;
            case GamePage::GameSettings: pageGameSettings(ctx); break;
            case GamePage::Mechanics: pageMechanics(ctx); break;
            case GamePage::Count: break;
        }
        ImGui::EndChild();
        ImGui::Separator();
        if (!status_.empty()) ImGui::TextColored(statusError_ ? kBad : kGood, "%s", status_.c_str());
        else ImGui::TextColored(kDim, "%s", summary().c_str());

        frame.beginButtons();
        for (size_t i = 0; i < kGamePages.size(); ++i)
            if (frame.pageButton(kGamePages[i], page_ == static_cast<GamePage>(i)) && page_ != static_cast<GamePage>(i)) {
                page_ = static_cast<GamePage>(i);
                status_.clear();
            }
        frame.toBottom(2);
        if (frame.button("Begin Game") && beginGame(ctx)) return;  // this screen is gone once the game starts
        if (frame.button("Cancel") || escape) {
            lastSettings() = s_;
            ctx.go(FrontId::Intro);
        }
    }

private:
    const game::Rules& rules() const { return *rules_; }

    void init(MenuContext& ctx) {
        rules_ = ctx.rules;
        s_ = lastSettings() ? *lastSettings() : defaultSettings(rules(), ctx.seed);
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

    std::string summary() const {
        int humans = 0, computers = 0;
        for (const auto& e : s_.players) (e.kind == game::PlayerKind::Human ? humans : computers)++;
        std::string text = std::format("{} human and {} computer empire{}", humans, computers, computers == 1 ? "" : "s");
        for (const bool neutral : {false, true}) {
            const RandomPlayers& rp = neutral ? s_.neutrals : s_.computers;
            if (!rp.enabled) continue;
            const auto [lo, hi] = randomPlayerRange(rules(), neutral, rp.level);
            text += lo == hi ? std::format(", {} random {} player{}", lo, neutral ? "neutral" : "computer", lo == 1 ? "" : "s")
                             : std::format(", {}-{} random {} players", lo, hi, neutral ? "neutral" : "computer");
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
        lastSettings() = s_;
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
            // A race not in the list yet; the second and later empires start as computer players.
            const ruleset::RacePreset* pick = nullptr;
            for (const auto& p : rules().racePresets()) {
                if (p.neutral) continue;
                const bool used = std::any_of(s_.players.begin(), s_.players.end(), [&](const game::EmpireSetup& e) {
                    return keysEqual(e.preset, p.folder);
                });
                if (!used) {
                    pick = &p;
                    break;
                }
                if (!pick) pick = &p;
            }
            if (pick) {
                d = draftFromPreset(rules(), *pick, bestTierWithin(rules(), *pick, s_.options.racialPoints));
            } else {
                d.setup.name = "New Empire";
                d.race.name = "New Race";
            }
            const bool haveHuman = std::any_of(s_.players.begin(), s_.players.end(),
                                               [](const game::EmpireSetup& e) { return e.kind == game::PlayerKind::Human; });
            d.setup.kind = haveHuman ? game::PlayerKind::Computer : game::PlayerKind::Human;
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
        const PreviewKey key = previewKey(s_);
        if (previewKey_ && *previewKey_ == key) return;
        if (ImGui::IsAnyItemActive() && preview_) return;  // wait until a slider is released
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

    void drawPreview(MenuContext& ctx, float size) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.01f, 0.015f, 0.05f, 1));
        ImGui::BeginChild("##map", ImVec2(size, size), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 p0 = ImGui::GetCursorScreenPos();
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        ImGui::InvisibleButton("##mapArea", avail);
        const bool hovered = ImGui::IsItemHovered();
        if (preview_ && !preview_->galaxy.systems.empty()) {
            const game::Galaxy& g = preview_->galaxy;
            const float spanX = static_cast<float>(std::max(1, g.width - 1)), spanY = static_cast<float>(std::max(1, g.height - 1));
            const float cell = std::min((avail.x - ctx.px(24)) / spanX, (avail.y - ctx.px(24)) / spanY);
            const ImVec2 origin(p0.x + (avail.x - cell * spanX) * 0.5f, p0.y + (avail.y - cell * spanY) * 0.5f);
            auto at = [&](game::GalaxyPos p) { return ImVec2(origin.x + static_cast<float>(p.x) * cell, origin.y + static_cast<float>(p.y) * cell); };
            // Faint grid, one line every few squares.
            const int step = std::max(1, static_cast<int>(std::ceil(ctx.px(22) / cell)));
            for (int x = 0; x < g.width; x += step)
                dl->AddLine(at({x, 0}), at({x, g.height - 1}), IM_COL32(24, 40, 90, 110));
            for (int y = 0; y < g.height; y += step)
                dl->AddLine(at({0, y}), at({g.width - 1, y}), IM_COL32(24, 40, 90, 110));
            // Warp links (each pair once).
            for (const game::SpaceObject& o : g.objects)
                if (o.kind == game::ObjectKind::WarpPoint && o.destination.valid() && o.id.index() < o.destination.index()) {
                    const game::SpaceObject& other = g.object(o.destination);
                    dl->AddLine(at(g.system(o.system).position), at(g.system(other.system).position), IM_COL32(70, 110, 220, 170), ctx.px(1.2f));
                }
            // Systems, coloured by their star.
            const float radius = std::clamp(cell * 0.3f, ctx.px(2.5f), ctx.px(6));
            const ImVec2 mouse = ImGui::GetIO().MousePos;
            int hover = -1;
            float best = std::max(radius * 2.0f, ctx.px(8));
            for (size_t i = 0; i < g.systems.size(); ++i) {
                const game::StarSystem& sys = g.systems[i];
                const ImVec2 c = at(sys.position);
                std::string_view color;
                for (game::ObjectId id : sys.objects)
                    if (g.object(id).kind == game::ObjectKind::Star) {
                        color = g.object(id).starColor;
                        break;
                    }
                if (color.empty() && !keysEqual(sys.physicalType, "Normal")) {
                    dl->AddCircle(c, radius, IM_COL32(170, 120, 230, 255), 0, ctx.px(1.5f));
                } else {
                    dl->AddCircleFilled(c, radius + ctx.px(1.5f), IM_COL32(0, 0, 0, 255));
                    dl->AddCircleFilled(c, radius, starColor(color));
                }
                const float d = std::hypot(mouse.x - c.x, mouse.y - c.y);
                if (hovered && d < best) {
                    best = d;
                    hover = static_cast<int>(i);
                }
            }
            // A loaded map's starting points: numbered for their player, plain for common ones.
            if (s_.map)
                for (const game::StartingPoint& p : s_.map->startingPoints) {
                    if (p.system.index() >= g.systems.size()) continue;
                    const ImVec2 c = at(g.system(p.system).position);
                    dl->AddCircle(c, radius + ctx.px(3), IM_COL32(120, 255, 140, 255), 0, ctx.px(1.5f));
                    if (p.player != game::kCommonStart)
                        dl->AddText(ImVec2(c.x + radius + ctx.px(3), c.y - radius - ctx.px(12)), IM_COL32(120, 255, 140, 255),
                                    std::to_string(p.player + 1).c_str());
                }
            if (hover >= 0) {
                const game::StarSystem& sys = g.systems[static_cast<size_t>(hover)];
                dl->AddCircle(at(sys.position), radius + ctx.px(4), IM_COL32(255, 230, 120, 255), 0, ctx.px(1.5f));
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
        } else {
            const char* text = previewError_.empty() ? "No map yet: press Generate Map Now." : previewError_.c_str();
            ImGui::SetCursorScreenPos(ImVec2(p0.x + ctx.px(12), p0.y + ctx.px(12)));
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + avail.x - ctx.px(24));
            ImGui::TextColored(previewError_.empty() ? kDim : kBad, "%s", text);
            ImGui::PopTextWrapPos();
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    void pageQuadrant(MenuContext& ctx) {
        game::GameOptions& o = s_.options;
        const auto& quadrants = rules().data().quadrantTypes;
        const float leftW = ctx.px(330);
        ImGui::BeginChild("##qleft", ImVec2(leftW, 0));
        heading(ctx, "Quadrant Type");
        const ruleset::QuadrantType* chosen = nullptr;
        if (ImGui::BeginListBox("##qtypes", ImVec2(-FLT_MIN, ctx.px(128)))) {
            for (const auto& q : quadrants) {
                const bool selected = keysEqual(q.name, o.quadrantType);
                if (selected) chosen = &q;
                if (ImGui::Selectable(q.name.c_str(), selected)) o.quadrantType = q.name;
            }
            ImGui::EndListBox();
        }
        if (chosen) {
            if (!chosen->description.empty()) note(chosen->description.c_str());
            // Max Warp Points per Sys is how many nearest systems each one considers for links (spec 01 §3.5).
            ImGui::TextColored(kDim, "%s placement; links to its %d nearest systems considered", chosen->systemPlacement.c_str(),
                               chosen->maxWarpPointsPerSystem);
        }
        ImGui::Dummy(ImVec2(0, ctx.px(6)));
        heading(ctx, "Quadrant Size");
        {
            std::vector<std::string> sizes;
            static constexpr std::array<const char*, 3> kSizes{"Small", "Medium", "Large"};
            for (int i = 0; i < 3; ++i) {
                const auto [lo, hi] = quadrantSizeRange(rules(), i);
                sizes.push_back(std::format("{} ({}-{})", kSizes[static_cast<size_t>(i)], lo, hi));
            }
            if (lampChoice(ctx, "##qsize", o.quadrantSize, std::span<const std::string>(sizes))) o.systemCount = 0;
            if (o.systemCount > 0) note(std::format("Exactly {} systems (set on the command line).", o.systemCount).c_str());
            else note("The number of systems is rolled in this range when the map is made.");
        }
        ImGui::Dummy(ImVec2(0, ctx.px(6)));
        heading(ctx, "Warp Points");
        lamp(ctx, "All warp points connected", o.allWarpPointsConnected, !o.noWarpPoints);
        lamp(ctx, "No warp points", o.noWarpPoints);
        lamp(ctx, "Warp points anywhere in a system", o.warpPointsAnywhere, !o.noWarpPoints);
        ImGui::Dummy(ImVec2(0, ctx.px(6)));
        heading(ctx, "Knowledge and Resources");
        lamp(ctx, "All systems seen by all players", o.allSystemsSeen);
        lamp(ctx, "Omnipresent view of all systems", o.omnipresent);
        lamp(ctx, "Finite planet resources", o.finiteResources);
        lamp(ctx, "All player planets the same size", o.allPlanetsSameSize);
        ImGui::EndChild();

        ImGui::SameLine(0, ctx.px(14));
        ImGui::BeginGroup();
        updatePreview();
        const float mapSize = std::min(ImGui::GetContentRegionAvail().x, ctx.px(470));
        drawPreview(ctx, mapSize);
        rowLabel(ctx, "Seed", 0);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ctx.px(170));
        ImGui::InputScalar("##seed", ImGuiDataType_U64, &s_.seed);
        ImGui::SameLine();
        if (ImGui::Button("Generate Map Now", ImVec2(-FLT_MIN, 0))) {
            Rng next(s_.seed);
            s_.seed = next.next() % 1000000000ull + 1;
            if (s_.map) setStatus("Back to a generated quadrant; the loaded map and its starting points are gone.", false);
            clearMap(s_);
        }
        const ImVec2 half((ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f, 0);
        if (ImGui::Button("Load Map", half)) {
            mapFiles_ = listMapFiles(rules(), mapsDir());
            ImGui::OpenPopup("Load Map");
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(!preview_);
        if (ImGui::Button("Save Map", half)) {
            if (mapName_.empty()) mapName_ = s_.map && !s_.map->name.empty() ? s_.map->name : std::format("Quadrant {}", s_.seed);
            ImGui::OpenPopup("Save Map");
        }
        ImGui::EndDisabled();
        loadMapPopup(ctx);
        saveMapPopup(ctx);
        if (s_.map) {
            ImGui::TextColored(kGood, "Map: %s", s_.map->name.c_str());
            ImGui::SameLine();
            ImGui::TextColored(kDim, "(%zu starting points)", s_.map->startingPoints.size());
        }
        if (preview_) {
            size_t links = 0, starts = 0;
            for (const auto& obj : preview_->galaxy.objects)
                if (obj.kind == game::ObjectKind::WarpPoint && obj.destination.valid()) ++links;
            for (const auto& sys : preview_->galaxy.systems)
                if (sys.type.index() < rules().data().systemTypes.size() && rules().data().systemTypes[sys.type.index()].empiresCanStartIn) ++starts;
            ImGui::TextColored(kDim, "%zu systems, %zu warp links, %zu where empires can start", preview_->galaxy.systems.size(), links / 2, starts);
            for (const auto& w : preview_->warnings) ImGui::TextColored(kBad, "%s", w.c_str());
        }
        note(s_.map ? "The game starts on this loaded map; Generate Map Now returns to a generated quadrant."
                    : "This is the map the game starts with. A new seed makes a new map.");
        ImGui::EndGroup();
    }

    static std::filesystem::path mapsDir() {
        const std::filesystem::path dir = userDataDir() / "maps";
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        return dir;
    }

    void loadMapPopup(MenuContext& ctx) {
        ImGui::SetNextWindowSize(ctx.size({520, 420}), ImGuiCond_Always);
        ImGui::SetNextWindowPos(ctx.at({kFrameW * 0.5f, kFrameH * 0.5f}), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        if (!ImGui::BeginPopupModal("Load Map", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) return;
        ImGui::PushStyleColor(ImGuiCol_Text, kDim);
        ImGui::TextWrapped("Map files in %s", mapsDir().string().c_str());
        ImGui::PopStyleColor();
        ImGui::BeginChild("##maps", ImVec2(0, -ctx.px(38)), ImGuiChildFlags_Borders);
        if (mapFiles_.empty()) note("No saved maps yet. Save Map keeps the quadrant shown here for later games.");
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
        ImGui::EndChild();
        if (ImGui::Button("Cancel", ctx.size({120, 28})) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    void saveMapPopup(MenuContext& ctx) {
        ImGui::SetNextWindowSize(ctx.size({440, 150}), ImGuiCond_Always);
        ImGui::SetNextWindowPos(ctx.at({kFrameW * 0.5f, kFrameH * 0.5f}), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        if (!ImGui::BeginPopupModal("Save Map", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) return;
        ImGui::TextColored(kLabelBlue, "Map name");
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        const bool enter = inputString("##mapname", mapName_, 60, ImGuiInputTextFlags_EnterReturnsTrue);
        const std::filesystem::path file = mapFilePath(mapsDir(), mapName_);
        std::error_code ec;
        if (std::filesystem::exists(file, ec)) ImGui::TextColored(kBad, "%s exists and will be replaced.", file.filename().string().c_str());
        if ((ImGui::Button("Save", ctx.size({120, 28})) || enter) && preview_) {
            auto saved = game::saveMapFile(file, rules().data(), mapToSave(s_, preview_->galaxy, mapName_));
            if (saved) setStatus(std::format("Map saved to {}", file.string()), false);
            else setStatus(saved.error(), true);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ctx.size({120, 28})) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    // ---- Events ----------------------------------------------------------------------------------

    void pageEvents(MenuContext& ctx) {
        game::GameOptions& o = s_.options;
        heading(ctx, "Event Frequency");
        std::vector<std::string> freq{"None"};
        for (const char* level : {"Low", "Medium", "High"}) {
            const int64_t chance = rules().setting(std::format("Event Percent Chance {}", level), 0);
            freq.push_back(chance > 0 ? std::format("{} ({}% a turn)", level, chance) : std::string(level));
        }
        lampChoice(ctx, "##freq", o.eventFrequency, std::span<const std::string>(freq));
        note("How often random events such as plagues, storms or discoveries strike an empire.");
        ImGui::Dummy(ImVec2(0, ctx.px(8)));
        heading(ctx, "Maximum Event Severity");
        static constexpr std::array<const char*, 4> kSeverity{"Low", "Medium", "High", "Catastrophic"};
        lampChoice(ctx, "##sev", o.maxEventSeverity, {kSeverity[0], kSeverity[1], kSeverity[2], kSeverity[3]});
        note("Events worse than this never happen.");
        ImGui::Dummy(ImVec2(0, ctx.px(8)));

        auto severityOf = [](std::string_view s) {
            for (size_t i = 0; i < kSeverity.size(); ++i)
                if (keysEqual(s, kSeverity[i])) return static_cast<int>(i);
            return 0;
        };
        const auto& events = rules().data().eventTypes;
        int eligible = 0;
        for (const auto& e : events) eligible += severityOf(e.severity) <= o.maxEventSeverity ? 1 : 0;
        heading(ctx, "Possible Events");
        ImGui::SameLine();
        if (o.eventFrequency == 0) ImGui::TextColored(kDim, "none: random events are off");
        else ImGui::TextColored(kDim, "%d of %zu event types", eligible, events.size());
        const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_BordersInnerV;
        if (ImGui::BeginTable("##events", 4, flags, ImVec2(0, ImGui::GetContentRegionAvail().y))) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Event", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Effect", ImGuiTableColumnFlags_WidthFixed, ctx.px(250));
            ImGui::TableSetupColumn("Severity", ImGuiTableColumnFlags_WidthFixed, ctx.px(110));
            ImGui::TableSetupColumn("Takes", ImGuiTableColumnFlags_WidthFixed, ctx.px(90));
            ImGui::TableHeadersRow();
            for (const auto& e : events) {
                const bool on = o.eventFrequency > 0 && severityOf(e.severity) <= o.maxEventSeverity;
                const ImVec4 color = on ? ImGui::GetStyle().Colors[ImGuiCol_Text] : ImGui::GetStyle().Colors[ImGuiCol_TextDisabled];
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                const std::string title = !e.messages.empty() && !e.messages.front().title.empty() ? e.messages.front().title : e.type;
                ImGui::TextColored(color, "%s", title.c_str());
                ImGui::TableNextColumn();
                ImGui::TextColored(on ? kDim : color, "%s", e.type.c_str());
                ImGui::TableNextColumn();
                ImGui::TextColored(color, "%s", e.severity.c_str());
                ImGui::TableNextColumn();
                if (e.turnsToComplete > 0) ImGui::TextColored(color, "%d turns", e.turnsToComplete);
                else ImGui::TextColored(color, "at once");
            }
            ImGui::EndTable();
        }
    }

    // ---- Technology ---------------------------------------------------------------------------------

    void pageTechnology(MenuContext& ctx) {
        game::GameOptions& o = s_.options;
        heading(ctx, "Starting Technology");
        lampChoice(ctx, "##start", o.startTechLevel, {"Low", "Medium", "High"});
        static constexpr std::array<const char*, 3> kStartNotes{
            "Every empire starts with the basic levels only.",
            "Many areas start a few levels higher.",
            "Every area starts fully researched.",
        };
        note(kStartNotes[static_cast<size_t>(std::clamp(o.startTechLevel, 0, 2))]);
        ImGui::Dummy(ImVec2(0, ctx.px(8)));

        heading(ctx, "Technology Cost");
        o.techCost = std::clamp(o.techCost, 0, 2);
        lampChoice(ctx, "##techcost", o.techCost, {"Low", "Medium", "High"});
        static constexpr std::array<const char*, 3> kCostNotes{
            "Level L of an area costs L times its level cost: level 5 costs 5x.",
            "Level L costs the larger of L and L squared / 2 times the level cost: level 5 costs 12.5x.",
            "Level L costs L squared times its level cost: level 5 costs 25x.",
        };
        note(kCostNotes[static_cast<size_t>(o.techCost)]);
        ImGui::Dummy(ImVec2(0, ctx.px(8)));

        const auto& areas = rules().data().techAreas;
        auto allowed = [&](size_t i) { return o.techAreasAllowed.empty() || i >= o.techAreasAllowed.size() || o.techAreasAllowed[i] != 0; };
        auto setAll = [&](bool v) {
            o.techAreasAllowed.assign(areas.size(), 1);
            for (size_t i = 0; i < areas.size(); ++i)
                if (areas[i].canBeRemoved) o.techAreasAllowed[i] = v ? 1 : 0;
        };
        size_t removable = 0, on = 0;
        for (size_t i = 0; i < areas.size(); ++i)
            if (areas[i].canBeRemoved) {
                ++removable;
                on += allowed(i) ? 1 : 0;
            }
        heading(ctx, "Technology Areas Allowed");
        ImGui::SameLine();
        ImGui::TextColored(kDim, "%zu of %zu removable areas allowed; %zu more are always in", on, removable, areas.size() - removable);
        if (ImGui::SmallButton("Allow All")) setAll(true);
        ImGui::SameLine();
        if (ImGui::SmallButton("Remove All")) setAll(false);
        ImGui::SameLine();
        ImGui::TextColored(kDim, "Items that need a removed area can never be built.");

        // Grouped by the data's Group, in three columns.
        std::vector<std::string> groups;
        for (const auto& a : areas)
            if (a.canBeRemoved && std::none_of(groups.begin(), groups.end(), [&](const std::string& x) { return keysEqual(x, a.group); }))
                groups.push_back(a.group);
        ImGui::BeginChild("##areas", ImVec2(0, 0), ImGuiChildFlags_Borders);
        for (const std::string& group : groups) {
            heading(ctx, group.empty() ? "Other" : group.c_str());
            if (ImGui::BeginTable(group.c_str(), 3, ImGuiTableFlags_SizingStretchSame)) {
                for (size_t i = 0; i < areas.size(); ++i) {
                    if (!areas[i].canBeRemoved || !keysEqual(areas[i].group, group)) continue;
                    ImGui::TableNextColumn();
                    bool v = allowed(i);
                    ImGui::PushID(static_cast<int>(i));
                    if (lamp(ctx, areas[i].name.c_str(), v)) {
                        if (o.techAreasAllowed.empty()) o.techAreasAllowed.assign(areas.size(), 1);
                        o.techAreasAllowed[i] = v ? 1 : 0;
                    }
                    if (!areas[i].description.empty() && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", areas[i].description.c_str());
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
        }
        ImGui::EndChild();
    }

    // ---- Player Settings --------------------------------------------------------------------------------

    void pagePlayerSettings(MenuContext& ctx) {
        game::GameOptions& o = s_.options;
        heading(ctx, "Starting Resources");
        {
            int level = 1;
            for (size_t i = 0; i < kStartingResources.size(); ++i)
                if (o.startingResources.v[0] == kStartingResources[i]) level = static_cast<int>(i);
            const std::vector<std::string> labels{std::format("Low ({})", kStartingResources[0]), std::format("Medium ({})", kStartingResources[1]),
                                                  std::format("High ({})", kStartingResources[2])};
            if (lampChoice(ctx, "##res", level, std::span<const std::string>(labels))) {
                const int64_t v = kStartingResources[static_cast<size_t>(level)];
                o.startingResources = {v, v, v};
            }
            note("Each empire starts with this much of every resource, plus one turn of its income.");
        }
        ImGui::Dummy(ImVec2(0, ctx.px(8)));

        heading(ctx, "Racial Points");
        {
            int level = 1;
            for (size_t i = 0; i < kRacialPoints.size(); ++i)
                if (o.racialPoints == kRacialPoints[i]) level = static_cast<int>(i);
            const std::vector<std::string> labels{"None (0)", std::format("Low ({})", kRacialPoints[1]), std::format("Medium ({})", kRacialPoints[2]),
                                                  std::format("High ({})", kRacialPoints[3])};
            if (lampChoice(ctx, "##rp", level, std::span<const std::string>(labels))) o.racialPoints = kRacialPoints[static_cast<size_t>(level)];
            note("What each empire may spend on characteristics and advanced traits when it is created.");
        }
        ImGui::Dummy(ImVec2(0, ctx.px(8)));

        heading(ctx, "Home Planet Value");
        std::vector<std::string> values;
        static constexpr std::array<const char*, 3> kValueNames{"Bad", "Average", "Good"};
        static constexpr std::array<const char*, 3> kValueKeys{"Low", "Medium", "High"};
        for (size_t i = 0; i < 3; ++i) {
            const int64_t v = o.finiteResources ? rules().setting(std::format("Plr Planet Value {} Resources", kValueKeys[i]), 0)
                                                : rules().setting(std::format("Plr Planet Value {} Percent", kValueKeys[i]), 0);
            values.push_back(v > 0 ? std::format("{} ({}{})", kValueNames[i], v, o.finiteResources ? "" : "%") : std::string(kValueNames[i]));
        }
        lampChoice(ctx, "##home", o.homePlanetValue, std::span<const std::string>(values));
        note(o.finiteResources ? "The stock of resources on each starting planet, and the homeworld's size: Small, Medium or Large."
                               : "The resource value of each starting planet, and the homeworld's size: Small, Medium or Large.");
        ImGui::Dummy(ImVec2(0, ctx.px(8)));

        heading(ctx, "Starting Planets");
        {
            int choice = 0;
            for (size_t i = 0; i < kStartingPlanets.size(); ++i)
                if (o.startingPlanets == kStartingPlanets[i]) choice = static_cast<int>(i);
            if (lampChoice(ctx, "##planets", choice, {"1", "3", "5", "10"})) o.startingPlanets = kStartingPlanets[static_cast<size_t>(choice)];
            note("Planets beyond the homeworld come from its system and systems a jump or two away; neutral empires get one.");
        }
        ImGui::Dummy(ImVec2(0, ctx.px(8)));

        heading(ctx, "Empire Placement");
        lamp(ctx, "Empires may start in the same system", o.sameSystemAllowed);
        lamp(ctx, "Spread empires evenly across the quadrant", o.evenlyDistributed);
        ImGui::Dummy(ImVec2(0, ctx.px(8)));

        heading(ctx, "Score Display");
        o.scoreDisplay = std::clamp(o.scoreDisplay, 0, 2);
        lampChoice(ctx, "##scores", o.scoreDisplay, {"Own score only", "Own and Non-Aggression or better", "Every empire's score"});
    }

    // ---- Players ------------------------------------------------------------------------------------

    void pagePlayers(MenuContext& ctx) {
        auto& players = s_.players;
        selected_ = players.empty() ? -1 : std::clamp(selected_, 0, static_cast<int>(players.size()) - 1);
        heading(ctx, "Empires");
        const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingFixedFit;
        const float rowH = ctx.px(40);
        int toggle = -1;
        if (ImGui::BeginTable("##players", 6, flags, ImVec2(0, ctx.px(292)))) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, rowH);
            ImGui::TableSetupColumn("Empire", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Race", ImGuiTableColumnFlags_WidthFixed, ctx.px(160));
            ImGui::TableSetupColumn("Leader", ImGuiTableColumnFlags_WidthFixed, ctx.px(150));
            ImGui::TableSetupColumn("Player", ImGuiTableColumnFlags_WidthFixed, ctx.px(90));
            ImGui::TableSetupColumn("Points", ImGuiTableColumnFlags_WidthFixed, ctx.px(56));
            ImGui::TableHeadersRow();
            for (size_t i = 0; i < players.size(); ++i) {
                const game::EmpireSetup& e = players[i];
                const game::Race race = raceOf(rules(), e);
                const int cost = game::racialPointCost(rules(), race);
                ImGui::PushID(static_cast<int>(i));
                ImGui::TableNextRow(ImGuiTableRowFlags_None, rowH);
                ImGui::TableNextColumn();
                sprite(ctx.art.racePortrait(race.style), ImVec2(rowH - ctx.px(4), rowH - ctx.px(4)));
                ImGui::TableNextColumn();
                const bool selected = selected_ == static_cast<int>(i);
                const ImVec2 cell = ImGui::GetCursorPos();
                if (ImGui::Selectable("##row", selected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap,
                                      ImVec2(0, rowH - ctx.px(4))))
                    selected_ = static_cast<int>(i);
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) openEditor(static_cast<int>(i));
                ImGui::SetCursorPos(cell);
                ImGui::BeginGroup();
                sprite(ctx.art.flag(race.style, false), ctx.size({14, 10}));
                ImGui::SameLine();
                ImGui::TextColored(selected ? kHighlight : ImGui::GetStyle().Colors[ImGuiCol_Text], "%s", e.name.c_str());
                ImGui::TextColored(kDim, "%s", e.empireType.c_str());
                ImGui::EndGroup();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(race.name.c_str());
                const ruleset::RacePreset* p = presetOf(rules(), e);
                if (e.customRace) ImGui::TextColored(kDim, "custom race");
                else if (p) ImGui::TextColored(kDim, "preset build %d", e.presetTier + 1);
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(e.leaderTitle.c_str());
                ImGui::TextUnformatted(e.leaderName.c_str());
                ImGui::TableNextColumn();
                const bool human = e.kind == game::PlayerKind::Human;
                ImGui::PushStyleColor(ImGuiCol_Text, human ? kHighlight : kDim);
                if (ImGui::Button(human ? "Human" : "Computer", ImVec2(-FLT_MIN, 0))) toggle = static_cast<int>(i);
                ImGui::PopStyleColor();
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Click to switch between a human and a computer player");
                ImGui::TableNextColumn();
                ImGui::TextColored(cost > s_.options.racialPoints ? kBad : ImGui::GetStyle().Colors[ImGuiCol_Text], "%d", cost);
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        if (toggle >= 0) {
            game::EmpireSetup& e = players[static_cast<size_t>(toggle)];
            e.kind = e.kind == game::PlayerKind::Human ? game::PlayerKind::Computer : game::PlayerKind::Human;
            selected_ = toggle;
        }
        if (players.empty()) note("No empires yet: add one, or random computer players only (a human empire is needed to begin).");

        // List actions.
        const bool has = selected_ >= 0 && static_cast<size_t>(selected_) < players.size();
        const ImVec2 bs = ctx.size({106, 26});
        const bool full = static_cast<int>(players.size()) >= kMaxEmpires;
        ImGui::BeginDisabled(full);
        if (ImGui::Button("Add New", bs)) openEditor(-1);
        ImGui::SameLine();
        if (ImGui::Button("Add Existing", bs)) {
            files_ = listEmpireFiles(rules(), userDataDir() / "empires");
            ImGui::OpenPopup("Add Existing Empire");
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!has);
        if (ImGui::Button("Edit", bs)) openEditor(selected_);
        ImGui::SameLine();
        if (ImGui::Button("Remove", bs)) {
            const std::string name = players[static_cast<size_t>(selected_)].name;
            players.erase(players.begin() + selected_);
            setStatus(std::format("{} removed.", name), false);
        }
        ImGui::SameLine();
        if (ImGui::Button("Save To File", bs)) {
            auto file = saveEmpireFile(rules(), userDataDir() / "empires", players[static_cast<size_t>(selected_)]);
            if (file) setStatus(std::format("Saved to {}", file->string()), false);
            else setStatus(file.error(), true);
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(!has || selected_ == 0);
        if (ImGui::ArrowButton("##up", ImGuiDir_Up)) {
            std::swap(players[static_cast<size_t>(selected_)], players[static_cast<size_t>(selected_ - 1)]);
            --selected_;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!has || static_cast<size_t>(selected_ + 1) >= players.size());
        if (ImGui::ArrowButton("##down", ImGuiDir_Down)) {
            std::swap(players[static_cast<size_t>(selected_)], players[static_cast<size_t>(selected_ + 1)]);
            ++selected_;
        }
        ImGui::EndDisabled();
        ImGui::EndDisabled();
        addExistingPopup(ctx);

        ImGui::Dummy(ImVec2(0, ctx.px(6)));
        ImGui::Separator();
        heading(ctx, "Random Players");
        for (const bool neutral : {false, true}) {
            RandomPlayers& rp = neutral ? s_.neutrals : s_.computers;
            ImGui::PushID(neutral ? 1 : 0);
            lamp(ctx, neutral ? "Random neutral players" : "Random computer players", rp.enabled);
            ImGui::SameLine(ctx.px(250));
            ImGui::BeginDisabled(!rp.enabled);
            std::vector<std::string> levels;
            for (int l = 0; l < 3; ++l) {
                const auto [lo, hi] = randomPlayerRange(rules(), neutral, l);
                static constexpr std::array<const char*, 3> kLevels{"Few", "Some", "Many"};
                levels.push_back(lo == hi ? std::format("{} ({})", kLevels[static_cast<size_t>(l)], lo)
                                          : std::format("{} ({}-{})", kLevels[static_cast<size_t>(l)], lo, hi));
            }
            lampChoice(ctx, "##level", rp.level, std::span<const std::string>(levels));
            ImGui::EndDisabled();
            ImGui::PopID();
        }
        note("Random players get races not yet in the game. Neutral empires never leave their home system.");
        ImGui::Dummy(ImVec2(0, ctx.px(4)));
        heading(ctx, "Computer Players");
        rowLabel(ctx, "Difficulty", 120);
        s_.options.aiDifficulty = std::clamp(s_.options.aiDifficulty, 0, 2);
        lampChoice(ctx, "##diff", s_.options.aiDifficulty, {"Low", "Medium", "High"});
        note("The difficulty applies to the random computer players; the others play at Medium.");
        rowLabel(ctx, "Bonus", 120);
        lampChoice(ctx, "##bonus", s_.options.aiBonus, {"None", "Low", "Medium", "High"});
    }

    void addExistingPopup(MenuContext& ctx) {
        ImGui::SetNextWindowSize(ctx.size({520, 460}), ImGuiCond_Always);
        ImGui::SetNextWindowPos(ctx.at({kFrameW * 0.5f, kFrameH * 0.5f}), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        if (!ImGui::BeginPopupModal("Add Existing Empire", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) return;
        const std::filesystem::path dir = userDataDir() / "empires";
        ImGui::PushStyleColor(ImGuiCol_Text, kDim);
        ImGui::TextWrapped("Empire files in %s", dir.string().c_str());
        ImGui::PopStyleColor();
        ImGui::BeginChild("##files", ImVec2(0, -ctx.px(38)), ImGuiChildFlags_Borders);
        if (files_.empty()) note("No saved empires yet. Use Save To File on an empire to keep it for later games.");
        for (size_t i = 0; i < files_.size(); ++i) {
            const EmpireFileInfo& f = files_[i];
            ImGui::PushID(static_cast<int>(i));
            const float h = ctx.px(40);
            const bool clicked = ImGui::Selectable("##file", false, ImGuiSelectableFlags_AllowOverlap, ImVec2(0, h));
            ImGui::SameLine(ctx.px(6));
            sprite(ctx.art.racePortrait(f.style), ImVec2(h, h));
            ImGui::SameLine();
            ImGui::BeginGroup();
            ImGui::TextUnformatted(f.name.c_str());
            ImGui::TextColored(kDim, "%s - %s", f.race.c_str(), f.path.filename().string().c_str());
            ImGui::EndGroup();
            ImGui::PopID();
            if (clicked) {
                auto loaded = loadEmpireFile(rules(), f.path);
                if (!loaded) {
                    setStatus(loaded.error(), true);
                } else {
                    s_.players.push_back(loaded->empire);
                    selected_ = static_cast<int>(s_.players.size()) - 1;
                    std::string text = std::format("{} added from {}.", loaded->empire.name, f.path.filename().string());
                    for (const auto& w : loaded->warnings) text += " " + w;
                    setStatus(text, !loaded->warnings.empty());
                }
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndChild();
        if (ImGui::Button("Cancel", ctx.size({120, 28})) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    // ---- Victory Conditions ---------------------------------------------------------------------------

    void pageVictory(MenuContext& ctx) {
        game::VictoryConditions& v = s_.options.victory;
        heading(ctx, "Victory Conditions");
        note("Any number of conditions may be on; the first empire to meet one wins. With none, the game goes on until one empire is left.");
        ImGui::Dummy(ImVec2(0, ctx.px(8)));
        constexpr float valueX = 330, unitX = 470;
        auto intRow = [&](const char* label, bool& on, int& value, int lo, int hi, const char* unit) {
            ImGui::PushID(label);
            lamp(ctx, label, on);
            ImGui::SameLine(ctx.px(valueX));
            ImGui::BeginDisabled(!on);
            ImGui::SetNextItemWidth(ctx.px(120));
            if (ImGui::InputInt("##v", &value, 1, 10)) value = std::clamp(value, lo, hi);
            ImGui::SameLine(ctx.px(unitX));
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(kDim, "%s", unit);
            ImGui::EndDisabled();
            ImGui::PopID();
        };
        {
            ImGui::PushID("score");
            lamp(ctx, "Score reaches", v.score);
            ImGui::SameLine(ctx.px(valueX));
            ImGui::BeginDisabled(!v.score);
            ImGui::SetNextItemWidth(ctx.px(120));
            const int64_t step = 1000, fast = 10000;
            if (ImGui::InputScalar("##v", ImGuiDataType_S64, &v.scoreValue, &step, &fast, "%lld"))
                v.scoreValue = std::clamp<int64_t>(v.scoreValue, 1, 1'000'000'000'000);
            ImGui::SameLine(ctx.px(unitX));
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(kDim, "points: the first empire there wins");
            ImGui::EndDisabled();
            ImGui::PopID();
        }
        intRow("Game lasts", v.years, v.yearsValue, 1, 10000, "years: then the highest score wins");
        intRow("Score is at least", v.percentOfSecond, v.percentOfSecondValue, 100, 100000, "% of the second-best score");
        intRow("Technology researched", v.techPercent, v.techPercentValue, 1, 100, "% of all tech levels");
        intRow("Quadrant at peace for", v.peace, v.peaceYears, 1, 10000, "years without war");
        ImGui::Dummy(ImVec2(0, ctx.px(10)));
        heading(ctx, "Qualifier");
        intRow("No victory in the first", v.delay, v.delayYears, 1, 10000, "years of the game");
        note("Not a condition by itself: the conditions above are only checked after this many years.");
    }

    // ---- Game Settings ----------------------------------------------------------------------------------

    void pageGameSettings(MenuContext& ctx) {
        game::GameOptions& o = s_.options;
        heading(ctx, "Diplomacy");
        lamp(ctx, "Allow gifts and tributes", o.allowGifts);
        lamp(ctx, "Allow technology in gifts and trades", o.allowTechTrades);
        lamp(ctx, "Allow intelligence projects", o.allowIntel);
        lamp(ctx, "Team mode: computer players ally against the humans", o.teamMode);
        ImGui::Dummy(ImVec2(0, ctx.px(8)));
        heading(ctx, "Colonization");
        lamp(ctx, "Only planets with a breathable atmosphere (no domes)", o.onlyBreathable);
        lamp(ctx, "Only planets of the home planet type", o.onlyHomeType);
        lamp(ctx, "No ancient ruins", o.noRuins);
        ImGui::Dummy(ImVec2(0, ctx.px(8)));
        heading(ctx, "Maps");
        lamp(ctx, "Players can save the map during the game", o.playersCanSaveMap);
        ImGui::Dummy(ImVec2(0, ctx.px(8)));
        heading(ctx, "Limits");
        rowLabel(ctx, "Ships per player", 190);
        ImGui::SetNextItemWidth(ctx.px(160));
        if (ImGui::InputInt("##ships", &o.maxShipsPerPlayer, 10, 100)) o.maxShipsPerPlayer = std::clamp(o.maxShipsPerPlayer, 1, 100000);
        rowLabel(ctx, "Units per player", 190);
        ImGui::SetNextItemWidth(ctx.px(160));
        if (ImGui::InputInt("##units", &o.maxUnitsPerPlayer, 50, 500)) o.maxUnitsPerPlayer = std::clamp(o.maxUnitsPerPlayer, 1, 1000000);
        note("Units are fighters, troops, mines, satellites and drones.");
    }

    // ---- Mechanics -------------------------------------------------------------------------------------

    void pageMechanics(MenuContext& ctx) {
        game::GameOptions& o = s_.options;
        int humans = 0;
        for (const auto& e : s_.players) humans += e.kind == game::PlayerKind::Human ? 1 : 0;
        heading(ctx, "Play Style");
        bool here = true, network = false;
        lamp(ctx, "Everyone on this computer", here);
        lamp(ctx, "Different computers", network, false);
        note(humans > 1 ? "Several human players: each plays their turn in order on this computer (hotseat)."
                        : "One human player against the computer. Network games are set up from Multiplayer on the main menu.");
        ImGui::Dummy(ImVec2(0, ctx.px(8)));
        heading(ctx, "Turn Style");
        int style = o.simultaneous ? 0 : 1;
        if (lampChoice(ctx, "##turns", style, {"Simultaneous", "One player after another"})) o.simultaneous = style == 0;
        note(o.simultaneous ? "Everyone gives orders, then all of them are carried out together."
                            : "Players move one after another. Orders are carried out as soon as they are given, and a ship "
                              "that meets the enemy fights at once. Network games are always simultaneous.");
        ImGui::Dummy(ImVec2(0, ctx.px(8)));
        heading(ctx, "Combat");
        int combat = o.noTacticalCombat ? 1 : 0;
        if (lampChoice(ctx, "##combat", combat, {"Tactical combat", "Strategic combat only"})) o.noTacticalCombat = combat == 1;
        note(o.simultaneous ? "Simultaneous games resolve every battle automatically; tactical combat needs one player after another."
                            : "Strategic combat is resolved automatically; with tactical combat each player in a battle may steer "
                              "their ships or leave them to their strategies.");
        ImGui::Dummy(ImVec2(0, ctx.px(8)));
        heading(ctx, "Autosave");
        {
            std::vector<std::string> choices;
            int current = 0;
            for (size_t i = 0; i < kAutosaveTurns.size(); ++i) {
                const int n = kAutosaveTurns[i];
                choices.push_back(n == 0 ? std::string("None") : n == 1 ? std::string("Every turn") : std::format("Every {} turns", n));
                if (n == o.autosaveTurns) current = static_cast<int>(i);
            }
            if (lampChoice(ctx, "##autosave", current, std::span<const std::string>(choices)))
                o.autosaveTurns = kAutosaveTurns[static_cast<size_t>(std::clamp(current, 0, static_cast<int>(kAutosaveTurns.size()) - 1))];
            note("Saves the game after the turns are processed, rotating through ten slots named Autosave 1 to Autosave 10.");
        }
        ImGui::Dummy(ImVec2(0, ctx.px(8)));
        heading(ctx, "This Game");
        labelValue(ctx, "Seed", std::to_string(s_.seed));
        ImGui::PushTextWrapPos(0);
        labelValue(ctx, "Data set", rules().data().dataDir.string());
        labelValue(ctx, "Empire files", (userDataDir() / "empires").string());
        ImGui::PopTextWrapPos();
        ImGui::Dummy(ImVec2(0, ctx.px(8)));
        if (ImGui::Button("Restore Defaults", ctx.size({160, 26}))) {
            s_ = defaultSettings(rules(), s_.seed);
            selected_ = 0;
            setStatus("Every setting is back to its default.", false);
        }
        ImGui::SameLine();
        ImGui::TextColored(kDim, "Resets all eight pages, including the empire list.");
    }

    std::string startPage_;
    std::shared_ptr<const game::Rules> rules_;
    NewGameSettings s_;
    GamePage page_ = GamePage::Quadrant;
    std::optional<EmpireEditor> editor_;
    int editIndex_ = -1;
    int selected_ = 0;
    std::optional<PreviewKey> previewKey_;
    std::optional<game::Generated> preview_;
    std::string previewError_;
    bool previewIsMap_ = false;               // preview_ shows s_.map
    std::vector<std::string> mapWarnings_;    // what the loaded map lacked in this data set
    std::vector<MapFileInfo> mapFiles_;
    std::string mapName_;
    std::string status_;
    bool statusError_ = false;
    std::vector<EmpireFileInfo> files_;
};

} // namespace

std::unique_ptr<FrontScreen> makeGameSetupScreen(std::string_view startPage) { return std::make_unique<GameSetupScreen>(std::string(startPage)); }

} // namespace opense4::client::classic
