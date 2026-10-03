#include "client/classic/classic_mode.hpp"

#include "client/app_settings.hpp"
#include "client/audio.hpp"
#include "client/classic/net_transport.hpp"
#include "client/classic/pointers.hpp"
#include "client/classic/reports.hpp"
#include "client/classic/screens/screens.hpp"
#include "client/classic/settings.hpp"
#include "client/script/items.hpp"
#include "client/ui/theme.hpp"
#include "game/setup.hpp"
#include "game/tactical.hpp"
#include "learn/markdown.hpp"

#include "core/log.hpp"

#include <imgui.h>
#include <imgui_internal.h>
#include <SDL3/SDL.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// The SDL3 backend's key mapping (imgui_impl_sdl3.cpp), for the tutorial lock's key filter.
ImGuiKey ImGui_ImplSDL3_KeyEventToImGuiKey(SDL_Keycode keycode, SDL_Scancode scancode);

namespace opense4::client {

using namespace classic;

std::string missingInstallMessage(const std::string& installDir) {
    constexpr const char* kIntro =
        "OpenSE4 is an engine for Space Empires IV Deluxe. It plays with your own installed copy of the game, "
        "reading its data, art and sound in place; nothing from the game ships with OpenSE4.";
    constexpr const char* kHelp = "docs/SETUP.md explains how to get the game files, also on Linux and macOS, and how to point OpenSE4 at them.";
    if (installDir.empty())
        return std::format("{}\n\n"
                           "No copy of the game was found. OpenSE4 looks in every Steam library on this computer for "
                           "steamapps/common/Space Empires IV Deluxe (Steam app 1610).\n\n"
                           "If your copy is somewhere else, start OpenSE4 with --classic-dir=<game directory>: the folder that "
                           "holds Data, Pictures and Sounds, or its Data folder.\n\n{}",
                           kIntro, kHelp);
    return std::format("{}\n\n"
                       "No copy of the game was found at {}.\n\n"
                       "--classic-dir takes the game directory (the folder that holds Data, Pictures and Sounds), its se4 "
                       "folder, or its Data folder.\n\n{}",
                       kIntro, installDir, kHelp);
}

std::unique_ptr<ClassicMode> ClassicMode::create(const Platform& platform, const ClassicOptions& options, std::string& error) {
    const auto dataDir = ruleset::findInstalledDataDir(options.installDir);
    if (!dataDir) {
        error = missingInstallMessage(options.installDir);
        return nullptr;
    }
    auto loaded = ruleset::loadRuleset(*dataDir);
    if (!loaded.ruleset || !loaded.diagnostics.errors.empty()) {
        error = std::format("The data set at {} has errors:\n", dataDir->string());
        for (size_t i = 0; i < std::min<size_t>(loaded.diagnostics.errors.size(), 15); ++i) error += "\n" + loaded.diagnostics.errors[i];
        return nullptr;
    }
    const std::filesystem::path gameRoot = dataDir->parent_path();

    std::unique_ptr<ClassicMode> mode(new ClassicMode(platform));
    mode->options_ = options;
    mode->rules_ = std::make_shared<const game::Rules>(std::move(*loaded.ruleset), gameRoot);
    mode->art_ = std::make_unique<Art>(*platform.device, assets::InstallFiles(gameRoot));
    Art::setColorSource(mode->art_.get());  // empire colours come from the race art (docs/spec/06 §5.3)
    log::info("Classic data set: {} ({} components, {} race presets)", dataDir->string(), mode->rules_->data().components.size(),
              mode->rules_->racePresets().size());
    audio().setInstall(&mode->art_->files());
    mode->fonts_ = loadClassicFonts(*platform.fonts, mode->art_->files());
    // The pointers (docs/spec/06 §5.8): with the install's Normal pointer the
    // classic pointers replace ImGui's (no text beam, no resize arrows).
    pointers().load(mode->art_->files());
    if (pointers().loaded()) ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    // The layout the original would pick: from the desktop width alone (§2.1.1),
    // in logical units, as the original (which knows nothing of display
    // scaling) sees it on a scaled Windows desktop. SDL gives Wayland's desktop
    // in logical points already, Windows' and X11's in pixels with a content
    // scale; dividing by that scale gives the same width everywhere.
    int desktopWidth = 1024;
    if (platform.window) {
        const SDL_DisplayID display = SDL_GetDisplayForWindow(platform.window);
        if (const SDL_DisplayMode* desktop = SDL_GetDesktopDisplayMode(display)) {
            const float scale = SDL_GetDisplayContentScale(display);
            desktopWidth = scale > 0.0f ? static_cast<int>(std::lround(static_cast<float>(desktop->w) / scale)) : desktop->w;
        }
    }
    mode->desktopLayout_ = layoutForDesktop(desktopWidth);
    mode->applyLayout();
    mode->playlists_ = readPlaylists(mode->rules_->data().settings);
    mode->restyle();
    mode->learn_ = loadLearnContent(platform.assetsDir, options.learnDir, mode->art_->files());

    if (!options.pbemFile.empty()) {
        // --pbem: play a play-by-e-mail game file at once.
        auto game = loadPbemGame(*mode->rules_, options.pbemFile);
        if (!game) {
            error = std::format("{}: {}", options.pbemFile, game.error());
            return nullptr;
        }
        game::EmpireId empire;
        if (options.pbemEmpire > 0) {
            empire = game::EmpireId{static_cast<uint32_t>(options.pbemEmpire - 1)};
        } else {
            int playable = 0;
            for (const PbemEmpireChoice& c : pbemEmpires(*game))
                if (c.playable && c.yourTurn) {
                    ++playable;
                    empire = c.id;
                }
            if (playable != 1) {
                error = "Several empires can play this turn: choose yours with --pbem-empire=N.";
                return nullptr;
            }
        }
        auto turn = beginPbemTurn(*game, empire, options.pbemPassword, options.pbemOrdersDir);
        if (!turn) {
            error = turn.error();
            return nullptr;
        }
        mode->startGame(ClassicSession::pbem(mode->rules_, std::move(*game), std::move(*turn), pbemDraftsDir()));
        if (options.pbemEndTurn) {
            mode->session_->endTurn();
            if (!mode->session_->pbemError().empty()) {
                error = mode->session_->pbemError();
                return nullptr;
            }
            std::printf("Orders saved to %s\n", mode->session_->ordersFile().string().c_str());
            if (options.pbemExit) mode->ui_->requests.quitGame = true;
        }
        if (!options.openWindow.empty() && !frontScreenByName(options.openWindow)) {
            mode->openLogOnTurn_ = false;
            const auto id = screenFromName(options.openWindow);
            if (!id) {
                error = std::format("Unknown window '{}'", options.openWindow);
                return nullptr;
            }
            mode->openScreen(*id, {});
        }
    } else if (!options.tutorial.empty() || !options.training.empty()) {
        // --tutorial / --training: the lesson's game at once; "<slug>:<step>"
        // starts a tutorial at that step (1-based), for checking content.
        const bool training = options.tutorial.empty();
        std::string slug = training ? options.training : options.tutorial;
        size_t step = 0;
        if (const size_t colon = slug.find(':'); colon != std::string::npos && !training) {
            step = static_cast<size_t>(std::max(1, std::atoi(slug.c_str() + colon + 1))) - 1;
            slug.resize(colon);
        }
        if (auto problem = mode->startLesson(training ? learn::LessonKind::Training : learn::LessonKind::Tutorial, slug)) {
            error = *problem;
            return nullptr;
        }
        // Automation, as with a quick start: the computer plays every empire
        // for a while, then a window (or a sample battle) opens.
        mode->session_->simulateTurns(options.autoTurns);
        if (step > 0) mode->lesson_->jumpTo(*mode->ui_, step);
        if (auto problem = mode->openAutomationWindow(options.openWindow)) {
            error = *problem;
            return nullptr;
        }
        if (options.lessonCheck) mode->prepareLessonCheck();
    } else if (options.manual) {
        // --manual[=slug]: the manual on its own.
        const learn::Link at = learn::parseLink(*options.manual);
        if (!options.manual->empty() && (at.kind != learn::Link::Kind::Page || !mode->learn_->library.page(at.target))) {
            error = std::format("No manual page '{}'", *options.manual);
            return nullptr;
        }
        mode->front_ = makeLearnFrontScreen("manual:" + *options.manual);
    } else if (auto front = frontScreenByName(options.openWindow)) {
        mode->front_ = std::move(front);  // automation: --open=<front-end screen>
        // --select: once the game joined or started there has such a vehicle.
        mode->pendingSelect_ = options.select;
        mode->keepLogClosed_ = !options.select.empty();
    } else if (options.skipIntro) {
        std::string race = options.race;
        if (race.empty())
            for (const auto& p : mode->rules_->racePresets())
                if (!p.neutral) {
                    race = p.folder;
                    break;
                }
        game::GameSetup setup = quickStartSetup(*mode->rules_, race, options.seed, std::max(0, options.empireCount - 1));
        if (options.systemCount > 0) setup.options.systemCount = options.systemCount;
        setup.options.quadrantType = options.quadrantType;
        setup.options.simultaneous = !options.turnBased;
        auto session = startLocalGame(mode->rules_, setup, quickStartExtras());
        if (!session) {
            error = session.error();
            return nullptr;
        }
        newGameStarted(setup.options.simultaneous);  // a quick start's
        mode->startGame(std::move(*session));
        // Automation: the computer plays every empire for a while.
        mode->session_->simulateTurns(options.autoTurns);
        if (auto problem = mode->selectForAutomation(options.select)) {
            error = *problem;
            return nullptr;
        }
        if (auto problem = mode->openAutomationWindow(options.openWindow)) {
            error = *problem;
            return nullptr;
        }
    } else {
        mode->front_ = makeFrontScreen(FrontId::Intro);
    }
    return mode;
}

EventVerdict ClassicMode::filterEvent(const SDL_Event& e) {
    if (!session_ || !lock_.active()) return EventVerdict::Pass;
    auto verdict = [](InputVerdict v) {
        switch (v) {
            case InputVerdict::Pass: return EventVerdict::Pass;
            case InputVerdict::Drop: return EventVerdict::Drop;
            case InputVerdict::PointerAway: return EventVerdict::PointerAway;
        }
        return EventVerdict::Pass;
    };
    switch (e.type) {
        case SDL_EVENT_MOUSE_MOTION: return verdict(lock_.mouseMove({e.motion.x, e.motion.y}));
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
            return verdict(lock_.mouseButton({e.button.x, e.button.y}, int(e.button.button), e.type == SDL_EVENT_MOUSE_BUTTON_DOWN));
        case SDL_EVENT_MOUSE_WHEEL: return verdict(lock_.wheel({e.wheel.mouse_x, e.wheel.mouse_y}));
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP: {
            const KeyChord chord{ImGui_ImplSDL3_KeyEventToImGuiKey(e.key.key, e.key.scancode), (e.key.mod & SDL_KMOD_CTRL) != 0,
                                 (e.key.mod & SDL_KMOD_SHIFT) != 0, (e.key.mod & SDL_KMOD_ALT) != 0};
            return verdict(lock_.key(chord, e.type == SDL_EVENT_KEY_DOWN));
        }
        case SDL_EVENT_TEXT_INPUT:
        case SDL_EVENT_TEXT_EDITING: return verdict(lock_.text());
        default: return EventVerdict::Pass;
    }
}

void ClassicMode::updateLock(UiContext& ui) {
    const learn::Step* step = lesson_ && lesson_->locking() ? lesson_->activeStep() : nullptr;
    if (!step) {
        lock_.set({});
        return;
    }
    std::vector<TaggedArea> tags;
    tags.reserve(ui.tags.size());
    for (const UiTag& t : ui.tags) tags.push_back({t.name, {t.min, t.max}});
    std::vector<std::string> open;
    for (const auto& [id, screen] : screens_) open.emplace_back(windowId(id));
    // The game's prompts, and every ImGui popup (the prompts windows raise, combo lists).
    std::vector<LockArea> prompts;
    for (const auto& [a, b] : ui.promptAreas) prompts.push_back({a, b});
    for (const ImGuiPopupData& p : ImGui::GetCurrentContext()->OpenPopupStack)
        if (p.Window && (p.Window->Active || p.Window->WasActive))
            prompts.push_back({p.Window->Pos, ImVec2(p.Window->Pos.x + p.Window->Size.x, p.Window->Pos.y + p.Window->Size.y)});
    lock_.set(makeLockState(*step, tags, open, prompts, ImGui::GetIO().WantTextInput, appSettings().controls.bindings));
}

void ClassicMode::prepareLessonCheck() {
    // The windows the step works in, as the steps before it would have left them.
    const learn::Step* step = lesson_ ? lesson_->activeStep() : nullptr;
    if (!step) return;
    std::vector<std::string> tags = step->highlight;
    tags.insert(tags.end(), step->allow.begin(), step->allow.end());
    bool battle = false;
    for (const std::string& tag : tags) {
        const auto window = tagWindowId(tag);
        if (!window) continue;
        const auto id = screenFromWindowId(*window);
        if (!id) continue;
        if (*id == ScreenId::TacticalCombat || *id == ScreenId::TacticalOrders || *id == ScreenId::TacticalOptions || *id == ScreenId::StrategicCombat) {
            // A sample battle: the player's warships against copies of them.
            if (!battle) battle = startDemoSimulation(*ui_, *id != ScreenId::StrategicCombat);
            if (*id == ScreenId::TacticalOrders || *id == ScreenId::TacticalOptions) ui_->open(*id, ScreenArgs{.index = 0});
            continue;
        }
        if (std::none_of(screens_.begin(), screens_.end(), [&](const auto& s) { return s.first == *id; })) openScreen(*id, {});
    }
    openLogOnTurn_ = false;
    lessonCheckFrame_ = 0;
}

void ClassicMode::lessonCheckReport(UiContext& ui) {
    // A few frames in, so that the windows have drawn: every tag the step
    // highlights or allows must be on screen. A few depend on the moment
    // (a piece selected in battle, an empire to talk to) and only warn, as
    // does an outline whose middle the lesson panel covers.
    if (lessonCheckFrame_ < 0 || ++lessonCheckFrame_ != 6 || !lesson_) return;
    const learn::Step* step = lesson_->activeStep();
    if (!step) return;
    static constexpr std::string_view kSituational[] = {"tactical-combat:weapons", "communicate:message-type", "communicate:treaty",
                                                        "communicate:send"};
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    auto onScreen = [&](const UiTag& t) {
        return t.max.x > t.min.x && t.max.y > t.min.y && t.max.x > 0 && t.max.y > 0 && t.min.x < display.x && t.min.y < display.y;
    };
    const auto panel = std::find_if(ui.tags.begin(), ui.tags.end(), [](const UiTag& t) { return t.name == "lesson:panel"; });
    auto covered = [&](const UiTag& t) {
        if (panel == ui.tags.end() || t.name.starts_with("lesson:")) return false;
        const ImVec2 mid((t.min.x + t.max.x) * 0.5f, (t.min.y + t.max.y) * 0.5f);
        return mid.x >= panel->min.x && mid.x < panel->max.x && mid.y >= panel->min.y && mid.y < panel->max.y;
    };
    std::vector<std::string> tags = step->highlight;
    tags.insert(tags.end(), step->allow.begin(), step->allow.end());
    std::string missing, situational, under;
    for (const std::string& tag : tags) {
        const auto seen = std::find_if(ui.tags.begin(), ui.tags.end(), [&](const UiTag& t) { return t.name == tag && onScreen(t); });
        if (seen != ui.tags.end()) {
            if (covered(*seen) && under.find(" " + tag) == std::string::npos) under += " " + tag;
            continue;
        }
        const bool moment = std::find(std::begin(kSituational), std::end(kSituational), tag) != std::end(kSituational);
        (moment ? situational : missing) += " " + tag;
    }
    const size_t areas = lock_.state().areas.size();
    std::printf("lesson-check %s:%zu areas=%zu %s%s%s%s%s%s\n", lesson_->lesson().slug.c_str(), lesson_->progress().active() + 1, areas,
                missing.empty() ? "ok" : "missing:", missing.c_str(), situational.empty() ? "" : " situational:", situational.c_str(),
                under.empty() ? "" : " under-panel:", under.c_str());
    std::fflush(stdout);
    if (!missing.empty() || !lock_.active()) exitCode_ = 1;
    if (options_.lessonCheckQuits) ui.requests.quitGame = true;
}

std::optional<std::string> ClassicMode::selectForAutomation(const std::string& what) {
    if (what.empty()) return std::nullopt;
    const game::GameState& s = session_->state();
    const game::EmpireId me = session_->player();
    auto moves = [](const std::vector<game::Order>& list) {
        return std::any_of(list.begin(), list.end(), [](const game::Order& o) {
            return o.kind == game::OrderKind::MoveTo || o.kind == game::OrderKind::MoveToWaypoint;
        });
    };
    std::optional<game::VehicleId> pick;
    if (what == "moving") {
        for (const game::Vehicle& v : s.vehicles)
            if (v.owner == me && !v.fleet.valid() && moves(v.orders)) {
                pick = v.id;
                break;
            }
    } else if (what == "fleet") {
        for (const game::Fleet& f : s.fleets)
            if (f.owner == me && !game::fleetOrders(s, f).empty() && !game::fleetMembersAt(s, f).empty()) {
                pick = game::fleetMembersAt(s, f).front();
                break;
            }
    } else {
        uint32_t id = 0;
        const auto [end, ec] = std::from_chars(what.data(), what.data() + what.size(), id);
        if (ec == std::errc{} && end == what.data() + what.size() && s.vehicle(game::VehicleId{id})) pick = game::VehicleId{id};
    }
    if (!pick) return std::format("Nothing to select for --select={}", what);
    ui_->requests.selectVehicle = *pick;
    return std::nullopt;
}

std::optional<std::string> ClassicMode::openAutomationWindow(const std::string& name) {
    if (name.empty()) return std::nullopt;
    openLogOnTurn_ = false;   // the requested window (or none) stays in front
    if (name == "none") return std::nullopt;   // just the main window
    // "window:text" passes the text as the window's argument (e.g. help:hotkeys).
    const size_t colon = name.find(':');
    const auto id = screenFromName(name.substr(0, colon));
    if (!id) return std::format("Unknown window '{}'", name);
    if (*id == ScreenId::TacticalCombat || *id == ScreenId::TacticalOrders || *id == ScreenId::TacticalOptions || *id == ScreenId::TacticalLaunch ||
        *id == ScreenId::CombatPieceReport || *id == ScreenId::StrategicCombat) {
        // A sample battle to show: the player's warships against copies of them
        // (fought by the strategies for Strategic Combat).
        if (!startDemoSimulation(*ui_, *id != ScreenId::StrategicCombat)) return std::string("No armed ship design to fight a sample battle with.");
        if (*id != ScreenId::TacticalCombat && *id != ScreenId::StrategicCombat) ui_->open(*id, ScreenArgs{.index = 0});
        return std::nullopt;
    }
    if (*id == ScreenId::GroundCombat) {
        // A sample ground combat: troops land on the homeworld in a strategic simulation.
        if (std::string problem = startDemoGroundCombat(*ui_); !problem.empty()) return problem;
        return std::nullopt;
    }
    ScreenArgs args;
    if (*id == ScreenId::CombatSimulator) args.text = "demo";
    if (colon != std::string::npos) args.text = name.substr(colon + 1);
    openScreen(*id, std::move(args));
    return std::nullopt;
}

ClassicMode::~ClassicMode() {
    screens_.clear();
    lesson_.reset();
    ui_.reset();
    session_.reset();
    art_.reset();
    pointers().release();
    ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NoMouseCursorChange;
}

void ClassicMode::applyLayout() {
    // The setting or --layout forces one; Auto follows the desktop as the original does.
    switch (appSettings().graphics.layout) {
        case LayoutChoice::Auto: setScreenLayout(desktopLayout_); break;
        case LayoutChoice::Small800: setScreenLayout(ScreenLayout::Small); break;
        case LayoutChoice::Large1024: setScreenLayout(ScreenLayout::Large); break;
    }
}

void ClassicMode::startGame(std::unique_ptr<ClassicSession> session) {
    screens_.clear();
    lesson_.reset();
    lock_.set({});
    session_ = std::move(session);
    ui_ = std::make_unique<UiContext>(*session_, *art_, fonts_);
    ui_->app = platform_.app;
    ui_->learn = learn_.get();
    session_->onIssued = [this](const game::Command& c) {
        if (lesson_) lesson_->issued(c);
        if (options_.scripted) scriptTracker_.issued(c);
    };
    ui_->opener = [this](ScreenId id, ScreenArgs args) { pendingOpen_.emplace_back(id, std::move(args)); };
    session_->onNewTurn = [this] {
        cueMusic(MusicCue::TurnProcessed);  // a new background track every 5 turns
        openLogOnTurn_ = true;
        strategicQueue_.clear();   // battles of the turn before: GameState::combats holds the new ones
    };
    main_ = MainWindow{};
    main_.reset(*ui_);
    if (options_.scripted) {
        // Input scripts count from the start of this game.
        scriptTracker_ = {};
        lastFacts_ = {};
        scriptTracker_.observe(session_->state(), session_->player());
        trackedRevision_ = session_->revision();
        gameMark_ = learn::markNow(session_->rules(), session_->state(), session_->player(), scriptTracker_);
    }
    logSeen_ = session_->me().log.size();  // what the game brought is not news
    handoffPlayer_ = {};
    handoff_ = false;
    front_.reset();
}

std::optional<std::string> ClassicMode::startLesson(learn::LessonKind kind, const std::string& slug) {
    const learn::Library& lib = learn_->library;
    const learn::Lesson* lesson = lib.lesson(kind, slug);
    const char* what = kind == learn::LessonKind::Tutorial ? "tutorial" : "training game";
    if (!lesson) {
        std::string known;
        for (const learn::Lesson& l : lib.lessons(kind)) known += (known.empty() ? "" : ", ") + l.slug;
        return std::format("No {} named '{}' ({}).", what, slug, known.empty() ? "none are installed" : "there are: " + known);
    }
    // The lesson's game: a quick start for its race, then its options.
    std::string race;
    if (!lesson->setup.race.empty()) {
        const ruleset::RacePreset* preset = game::findPreset(*rules_, lesson->setup.race);
        if (!preset) return std::format("The {} '{}' plays the race '{}', which this data set does not have.", what, slug, lesson->setup.race);
        race = preset->folder;
    } else {
        for (const auto& p : rules_->racePresets())
            if (!p.neutral) {
                race = p.folder;
                break;
            }
    }
    game::GameSetup setup = quickStartSetup(*rules_, race, lesson->setup.seed.value_or(options_.seed), lesson->setup.computerPlayers);
    game::StartExtras extras = quickStartExtras();
    learn::applySetup(lesson->setup, setup, extras);
    auto session = startLocalGame(rules_, setup, extras);
    if (!session) return std::format("The {} '{}' could not start its game: {}", what, slug, session.error());
    startGame(std::move(*session));
    lesson_ = std::make_unique<LessonRunner>(*lesson, *session_);
    openLogOnTurn_ = false;
    log::info("Started the {} '{}'", what, slug);
    return std::nullopt;
}

void ClassicMode::quitToLearn(learn::LessonKind kind) {
    screens_.clear();
    lesson_.reset();
    lock_.set({});
    ui_.reset();
    session_.reset();
    front_ = makeLearnFrontScreen(kind == learn::LessonKind::Tutorial ? "tutorials" : "training");
}

void ClassicMode::contextHelp() {
    // The page that explains the window in front (the main window when none is open).
    const std::string_view id = screens_.empty() ? std::string_view("main") : windowId(screens_.back().first);
    if (id == "manual") return;
    const learn::ManualPage* page = learn_->library.pageForWindow(id);
    ScreenArgs args;
    if (page) args.text = page->slug;
    openScreen(ScreenId::Manual, std::move(args));
}

void ClassicMode::updateLesson(UiContext& ui) {
    const Bindings& keys = appSettings().controls.bindings;
    if (keys.pressed(Action::ContextHelp)) contextHelp();
    const bool toggle = ui.requests.toggleLessonPanel;
    ui.requests.toggleLessonPanel = false;
    if (!lesson_ && !options_.scripted) return;
    learn::ClientFacts facts = std::move(ui.facts);   // what the windows told this frame
    for (const auto& [id, screen] : screens_) facts.openWindows.emplace_back(windowId(id));
    facts.selected = main_.selectionKinds(ui);
    facts.selections = main_.selections();
    facts.battleOrders = tacticalOrderLog();
    if (options_.scripted) lastFacts_ = facts;   // for input scripts' conditions
    if (!lesson_) return;
    if (toggle || keys.pressed(Action::LessonText)) lesson_->togglePanel();
    const script::ItemScope scope("lesson");
    lesson_->frame(ui, facts, lock_.state());
}

void ClassicMode::openScreen(ScreenId id, ScreenArgs args) {
    // A window that is already open comes to the front instead of opening twice
    // (windows with arguments are replaced so they show the new target).
    for (auto it = screens_.begin(); it != screens_.end(); ++it)
        if (it->first == id) {
            screens_.erase(it);
            break;
        }
    if (auto screen = makeScreen(id, args)) {
        // Tactical Combat and a Combat Replay start a combat track; nothing switches back after tactical combat.
        if (id == ScreenId::TacticalCombat || id == ScreenId::CombatReplay) cueMusic(MusicCue::CombatOpened);
        screens_.emplace_back(id, std::move(screen));
    }
}

void ClassicMode::endTurn() {
    if (!session_ || session_->waitingForOthers()) return;
    audio().play("endturn");
    screens_.clear();
    const BusyPointer busy;  // the Hourglass while the turn is processed (§5.8)
    session_->endTurn();
}

void ClassicMode::cueMusic(MusicCue cue) {
    if (!settings().musicOn) return;
    const std::string track = music_.cue(cue, playlists_, session_ ? session_->state().turn : 0, session_ != nullptr);
    if (!track.empty()) audio().playTrack(track);
}

void ClassicMode::updateAudio() {
    const ClassicSettings& prefs = settings();
    audio().setOptions(AudioOptions{prefs.soundOn, prefs.musicOn, prefs.soundVolume, float(prefs.musicVolume) / 100.0f, !prefs.classicSoundEffects});
    // The music rules (docs/spec/06 §5.5): the cues come from the intro, loading,
    // new turns and the combat windows; here music off stops it, and music on
    // with nothing playing starts the intro or background list.
    if (!prefs.musicOn) {
        audio().stopMusic();
    } else if (!audio().musicPlaying()) {
        cueMusic(MusicCue::NothingPlaying);
    }
    // A stellar manipulation the player sees plays its sound when its log entry appears.
    if (session_) {
        const auto& log = session_->me().log;
        if (log.size() < logSeen_) logSeen_ = 0;
        for (size_t i = logSeen_; i < log.size(); ++i)
            if (const std::string_view sound = stellarSound(log[i].title); !sound.empty()) {
                audio().play(sound);
                break;
            }
        logSeen_ = log.size();
    }
    // Closing a Combat Replay starts a new background track.
    bool replayOpen = false;
    for (const auto& [id, screen] : screens_) replayOpen = replayOpen || id == ScreenId::CombatReplay;
    if (replayWasOpen_ && !replayOpen) cueMusic(MusicCue::ReplayClosed);
    replayWasOpen_ = replayOpen;
}

bool ClassicMode::update(const FrameState& fs) {
    updateAudio();
    applyLayout();
    mapping_ = frameMappingFor(float(fs.frame.width), float(fs.frame.height));
    fbScale_ = fs.fbScale;
    // Every classic window defaults to the game's text font at its native size.
    ImGui::PushFont(fonts_.regular, kTextSize * mapping_.scale / fs.fbScale * appSettings().graphics.textScale);
    const bool keepRunning = updateFrame(fs);
    ImGui::PopFont();
    // The frame's pointer, grown with the classic screens by whole multiples.
    if (pointers().loaded()) pointers().apply(int(std::lround(mapping_.scale / std::max(0.01f, fs.fbScale))));
    return keepRunning;
}

void ClassicMode::restyle() {
    // The classic frame is scaled to the window and every size is in its
    // pixels (UiContext::k, fontPx), so the desktop's scale must not scale the
    // style again: from the theme at scale 1, then the classic look. Without
    // this a Windows desktop at 125 % or 150 % (display scale over pixel
    // density) made every classic text and style size that much larger than
    // on a Linux desktop, and a move to a display with another scale left the
    // plain theme in place of the classic look.
    applyTheme(1.0f);
    applyClassicStyle();
}

void ClassicMode::background() {
    // Minimized: the game's network traffic goes on (new states, the host's
    // own clients); nothing is drawn.
    if (session_) session_->poll();
}

bool ClassicMode::updateFrame(const FrameState& fs) {
    art_->setFilter(appSettings().graphics.sharpPixels ? gfx::Filter::Nearest : gfx::Filter::Linear);

    if (!session_) {
        MenuContext ctx{rules_, *art_, fonts_, mapping_, fs.fbScale, fs.time, options_.seed, platform_.app, {}, {}, {}, {}, frontError_};
        // The game starts once the screen has drawn: starting it replaces the screen.
        std::unique_ptr<ClassicSession> started;
        ctx.startGame = [&started](std::unique_ptr<ClassicSession> s) { started = std::move(s); };
        ctx.go = [this](FrontId id) { nextFront_ = id; };
        ctx.quit = [this] { quit_ = true; };
        ctx.learn = learn_.get();
        ctx.startLesson = [this](learn::LessonKind kind, const std::string& slug) { pendingLesson_ = {kind, slug}; };
        ctx.loadedFromIntro = [this] { loadedFromIntro_ = true; };
        if (front_) {
            const script::ItemScope scope("front");
            front_->draw(ctx);
        }
        if (started) startGame(std::move(started));
        if (pendingLesson_ && !session_) {
            // Started after the screen drew: starting replaces it.
            const auto [kind, slug] = *pendingLesson_;
            pendingLesson_.reset();
            if (auto problem = startLesson(kind, slug)) {
                frontError_ = *problem;
                front_ = makeFrontScreen(FrontId::Intro);
            } else {
                cueMusic(MusicCue::GameLoaded);  // Tutorial and Scenario: background music
            }
            return !quit_;
        }
        if (loadedFromIntro_ && session_) cueMusic(MusicCue::GameLoaded);  // Resume Game, Load Game
        loadedFromIntro_ = false;
        if (nextFront_ && !session_) {
            front_ = makeFrontScreen(*nextFront_);
            nextFront_.reset();
        }
        return !quit_;
    }

    UiContext& ui = *ui_;
    ui.map = mapping_;
    ui.textScale = appSettings().graphics.textScale;
    ui.fbScale = fs.fbScale;
    ui.time = fs.time;
    ui.dt = fs.dt;
    ui.tags.clear();
    ui.facts = {};
    ui.promptAreas.clear();
    ui.lessonLocked = lock_.active();
    // A click the tutorial's lock refused: the lesson says why.
    if (const auto refused = lock_.takeRefused(); refused && lesson_) lesson_->refused(*refused, fs.time);
    ui.lessonRunning = lesson_ != nullptr;
    session_->poll();
    if (options_.scripted) trackForScripts();
    const script::ItemScope mainScope("main");
    if (!pendingSelect_.empty() && !selectForAutomation(pendingSelect_)) pendingSelect_.clear();

    // Hotseat: when the turn passes to another human, hide the map until that
    // player starts their turn (with their password, if they set one).
    if (session_->kind() == SessionKind::Hotseat && session_->player() != handoffPlayer_) {
        handoffPlayer_ = session_->player();
        handoff_ = true;
        handoffPassword_.clear();
        handoffError_.clear();
        screens_.clear();
        // Battles the previous player was to watch stay theirs (the Log's Combat Replay keeps them).
        strategicQueue_.clear();
    }
    if (handoff_) {
        lock_.set({});
        drawHandoff(ui);
        return !ui.requests.quitGame;
    }

    auto isOpen = [&](ScreenId id) { return std::any_of(screens_.begin(), screens_.end(), [&](const auto& s) { return s.first == id; }); };
    // A battle fought in the client stays in its window until it is done: the
    // Tactical Combat window, or Strategic Combat for a simulation the
    // strategies fight (spec 06 §1.6).
    if (session_->tactical() && !isOpen(ScreenId::TacticalCombat) && !isOpen(ScreenId::StrategicCombat))
        openScreen(session_->tactical()->players.empty() ? ScreenId::StrategicCombat : ScreenId::TacticalCombat, {});
    const bool battleAsking = !session_->tactical() && session_->battleQuestion().has_value();
    // Battles to watch in the Strategic Combat window, one after another.
    for (size_t i : session_->takeStrategicBattles()) strategicQueue_.push_back(i);
    if (!session_->tactical() && !battleAsking && !isOpen(ScreenId::StrategicCombat) && !isOpen(ScreenId::GroundCombat) &&
        !strategicQueue_.empty()) {
        const size_t i = strategicQueue_.front();
        strategicQueue_.pop_front();
        if (i < ui.state().combats.size()) {
            ScreenArgs args;
            args.index = int(i);
            openScreen(ScreenId::StrategicCombat, std::move(args));
        }
    }
    const bool asking = screens_.empty() && !session_->questions().empty() && !battleAsking;
    const std::optional<game::ObjectId> choosing = screens_.empty() && !asking && !battleAsking ? colonyTypeChoice(ui) : std::nullopt;

    // Classic windows are modal: while one is open the main window takes no input.
    main_.update(ui, !screens_.empty() || asking || battleAsking || choosing.has_value() || confirmEndTurn_);
    drawNetwork(ui);
    drawPbem(ui);
    if (asking) drawEntryQuestion(ui);
    if (choosing) drawColonyTypeChoice(ui, *choosing);

    // Windows, oldest first; the newest draws on top.
    for (size_t i = 0; i < screens_.size();) {
        ImGui::PushID(int(i));
        ui.drawing = screens_[i].first;   // its Dialog registers window:<id>
        ui.windowTagged = false;
        ui.drawingWindow = 0;
        const script::ItemScope scope(windowId(screens_[i].first));
        const bool keep = screens_[i].second->draw(ui);
        ui.drawing.reset();
        ImGui::PopID();
        if (keep) {
            frontWindow_ = ui.drawingWindow;
            ++i;
        } else {
            screens_.erase(screens_.begin() + std::ptrdiff_t(i));
        }
    }
    if (screens_.empty()) frontWindow_ = 0;
    if (battleAsking) drawBattleQuestion(ui);
    updateLesson(ui);
    for (auto& [id, args] : pendingOpen_) openScreen(id, std::move(args));
    pendingOpen_.clear();
    main_.applyRequests(ui);

    if (ui.requests.endTurn) {
        ui.requests.endTurn = false;
        // The Empire Options' "confirm ending the turn" (spec 06 §1.9).
        if (ui.options().confirmEndTurn) confirmEndTurn_ = true;
        else endTurn();
    }
    if (confirmEndTurn_) {
        // A Yes/No message box: Y means Yes; N, Esc and Enter mean No (spec 06
        // §3.4). The key that asked for the end of the turn does not answer it.
        ImGui::SetNextWindowPos(ui.at({std::floor((frameW() - 300) * 0.5f), std::floor((frameH() - 110) * 0.5f)}));
        ImGui::SetNextWindowSize(ui.size({300, 110}));
        ImGui::PushFont(fonts_.regular, ui.fontPx(kTextSize));
        ImGui::Begin("End Turn", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | kPromptFlags);
        ui.promptWindow();   // never covered by a tutorial's input lock
        if (ImGui::IsWindowAppearing()) ImGui::SetWindowFocus();
        ImGui::TextUnformatted("End the turn now?");
        const std::optional<bool> key = yesNoKey();
        const bool yes = ImGui::Button("Yes", ui.size({120, 28})) || key == true;
        ImGui::SameLine();
        const bool no = ImGui::Button("No", ui.size({120, 28})) || key == false;
        ImGui::End();
        ImGui::PopFont();
        if (yes) {
            confirmEndTurn_ = false;
            endTurn();
        } else if (no) {
            confirmEndTurn_ = false;
        }
    }
    if (openLogOnTurn_ && !keepLogClosed_ && !battleAsking && !session_->tactical() && strategicQueue_.empty() && !isOpen(ScreenId::StrategicCombat)) {
        openLogOnTurn_ = false;
        if (ui.options().showLogAtTurnStart && !ui.me().log.empty() && ui.me().log.back().turn + 1 >= ui.state().turn)
            openScreen(ScreenId::Log, {});
    }
    if (ui.requests.loadGame) {
        const std::filesystem::path file = *ui.requests.loadGame;
        ui.requests.loadGame.reset();
        const BusyPointer busy;
        auto loaded = ClassicSession::load(rules_, file);
        if (loaded) {
            restoreHistoryFrom(file);
            startGame(std::move(*loaded));
            return true;
        }
        ScreenArgs args;
        args.text = loaded.error();
        openScreen(ScreenId::LoadGame, std::move(args));
    }
    if (ui.requests.quitToIntro) {
        ui.requests.quitToIntro = false;
        screens_.clear();
        lesson_.reset();
        lock_.set({});
        ui_.reset();
        session_.reset();
        front_ = makeFrontScreen(FrontId::Intro);
        cueMusic(MusicCue::IntroOpened);
        return true;
    }
    // The learning system: a lesson chosen in the Learn window, or what the
    // player chose in the lesson panel or its result. Each replaces the game.
    if (ui.requests.startLesson) {
        const auto [kind, slug] = *ui.requests.startLesson;
        ui.requests.startLesson.reset();
        if (auto problem = startLesson(kind, slug)) lessonError_ = *problem;
        return true;
    }
    if (lesson_) {
        const learn::LessonKind kind = lesson_->lesson().kind;
        const std::string slug = lesson_->lesson().slug;
        switch (lesson_->takeRequest()) {
            case LessonRunner::Request::None: break;
            case LessonRunner::Request::Leave: quitToLearn(kind); return true;
            case LessonRunner::Request::Restart:
                if (auto problem = startLesson(kind, slug)) lessonError_ = *problem;
                return true;
            case LessonRunner::Request::Next:
                if (const learn::Lesson* next = learn_->library.next(kind, slug))
                    if (auto problem = startLesson(kind, next->slug)) lessonError_ = *problem;
                return true;
        }
    }
    if (!lessonError_.empty()) {
        ImGui::SetNextWindowPos(ui.at({std::floor((frameW() - 400) * 0.5f), 320 * frameH() / kFrameH}));
        ImGui::SetNextWindowSize(ui.size({400, 0}));
        ImGui::PushFont(fonts_.regular, ui.fontPx(kTextSize));
        ImGui::Begin("Lesson", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize | kPromptFlags);
        ui.promptWindow();   // never covered by a tutorial's input lock
        ImGui::TextWrapped("%s", lessonError_.c_str());
        if (ImGui::Button("OK", ui.size({120, 26})) || okKey()) lessonError_.clear();   // a message box: Esc or Enter is OK
        ImGui::End();
        ImGui::PopFont();
    }
    keepFocusOnFrontWindow();
    updateLock(ui);
    if (options_.lessonCheck) lessonCheckReport(ui);
    return !ui.requests.quitGame;
}

void ClassicMode::keepFocusOnFrontWindow() {
    // Classic windows are modal (docs/spec/06 §1): while one is open the
    // keyboard belongs to the one in front, so that Esc and Enter close it
    // (Dialog::close). Dear ImGui gives the focus to whatever was clicked or
    // focused before, which can be one of the main window's own panels (a
    // command button that opened the window above, which has closed since).
    ImGuiContext& g = *ImGui::GetCurrentContext();
    if (frontWindow_ == 0 || g.ActiveId != 0 || ImGui::IsAnyMouseDown() || ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel))
        return;
    ImGuiWindow* front = ImGui::FindWindowByID(frontWindow_);
    const ImGuiWindow* nav = g.NavWindow ? g.NavWindow->RootWindow : nullptr;
    if (!front || nav == front) return;
    if (nav && nav->WasActive && !MainWindow::ownsWindow(nav->ID)) return;   // a prompt, the lesson panel, the chat
    ImGui::FocusWindow(front);
}

void ClassicMode::drawNetwork(UiContext& ui) {
    auto* net = dynamic_cast<NetTransport*>(session_->transport());
    if (!net) return;
    // A status strip at the bottom of the system panel: who we wait for, the
    // latest line. As narrow as the PBEM strip, so the planet panel's buttons
    // stay clear; the full lines show as a tooltip.
    ImGui::SetNextWindowPos(ui.at({8, frameH() - 56}));
    ImGui::SetNextWindowSize(ui.size({478, 50}));
    ImGui::PushFont(fonts_.regular, ui.fontPx(kTextSize));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.02f, 0.035f, 0.09f, 0.75f));
    ImGui::Begin("##netstatus", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                             ImGuiWindowFlags_NoBringToFrontOnFocus);
    if (ImGui::SmallButton(chatOpen_ ? "Hide Chat" : "Chat")) chatOpen_ = !chatOpen_;
    ImGui::SameLine();
    if (net->hosting()) {
        if (ImGui::SmallButton("Empires")) hostEmpiresOpen_ = !hostEmpiresOpen_;
        ImGui::SameLine();
    }
    const std::string status = net->status();
    const std::string first = status.empty() ? (session_->waitingForOthers() ? "Orders sent." : "Your turn.") : status;
    ImGui::TextColored(ImVec4(1, 0.85f, 0.45f, 1), "%s", first.c_str());
    const std::string last = net->log().lines().empty() ? std::string{} : net->log().lines().back();
    if (!last.empty()) ImGui::TextDisabled("%s", last.c_str());
    if (ImGui::IsWindowHovered()) ImGui::SetTooltip("%s", last.empty() ? first.c_str() : std::format("{}\n{}", first, last).c_str());
    ImGui::End();
    ImGui::PopStyleColor();

    if (chatOpen_) {
        ImGui::SetNextWindowPos(ui.at({8, 470}), ImGuiCond_Appearing);
        ImGui::SetNextWindowSize(ui.size({480, 236}), ImGuiCond_Appearing);
        if (ImGui::Begin("Chat", &chatOpen_, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings)) {
            ImGui::BeginChild("##chatlog", ImVec2(0, -ui.px(34)));
            for (const std::string& line : net->log().lines()) ImGui::TextWrapped("%s", line.c_str());
            if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4) ImGui::SetScrollHereY(1.0f);
            ImGui::EndChild();
            char buffer[512] = {};
            std::snprintf(buffer, sizeof buffer, "%s", chatInput_.c_str());
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::InputText("##say", buffer, sizeof buffer, ImGuiInputTextFlags_EnterReturnsTrue)) {
                if (buffer[0]) {
                    audio().play("ordbtn");  // sending a chat line (docs/spec/06 §5.5)
                    net->chat(buffer);
                }
                buffer[0] = 0;
                ImGui::SetKeyboardFocusHere(-1);
            }
            chatInput_ = buffer;
        }
        ImGui::End();
    }
    if (hostEmpiresOpen_) drawHostEmpires(ui);
    ImGui::PopFont();
}

// The host's list of empires: "Toggle Empire AI On/Off" for every empire a
// player joined, which asks "Change Empire Control" and flips only the
// empire's computer-controlled mark (spec 05 §9.4, confirmed: binary). The
// player column shows "[Computer]", or "[Host]" for an empire handed back that
// no player is connected to.
void ClassicMode::drawHostEmpires(UiContext& ui) {
    auto* transport = dynamic_cast<HostTransport*>(session_->transport());
    if (!transport || !transport->host().state()) return;
    net::HostSession& host = transport->host();
    const game::GameState& s = *host.state();
    ImGui::SetNextWindowPos(ui.at({500, 300}), ImGuiCond_Appearing);
    ImGui::SetNextWindowSize(ui.size({420, 0}), ImGuiCond_Appearing);
    if (ImGui::Begin("Empires###hostempires", &hostEmpiresOpen_, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings)) {
        for (const net::EmpireTurnStatus& st : host.turnStatus().empires) {
            if (st.empire.index() >= s.empires.size()) continue;
            const game::Empire& e = s.empire(st.empire);
            const net::LobbySlot* slot = st.empire.index() < host.lobby().slots.size() ? &host.lobby().slots[st.empire.index()] : nullptr;
            if (!slot || slot->kind != net::SlotKind::Human || slot->local) continue;
            ImGui::PushID(int(st.empire.index()));
            const bool computer = e.kind == game::PlayerKind::Computer;
            ImGui::TextUnformatted(e.name.c_str());
            ImGui::SameLine(ui.px(170));
            ImGui::TextDisabled("%s", computer ? "[Computer]" : st.connected ? slot->player.c_str() : "[Host]");
            ImGui::SameLine(ui.px(280));
            if (ImGui::SmallButton(computer ? "AI Off" : "AI On")) {
                toggleAsked_ = st.empire;
                ImGui::OpenPopup("Change Empire Control");
            }
            ImGui::PopID();
        }
        if (ImGui::BeginPopupModal("Change Empire Control", nullptr, ImGuiWindowFlags_AlwaysAutoResize | kPromptFlags)) {
            const bool valid = toggleAsked_.valid() && toggleAsked_.index() < s.empires.size();
            const bool toComputer = valid && s.empire(toggleAsked_).kind != game::PlayerKind::Computer;
            if (valid)
                ImGui::TextWrapped("%s", std::format("Hand the {} to {} control?", s.empire(toggleAsked_).name, toComputer ? "AI" : "human").c_str());
            const std::optional<bool> key = yesNoKey();
            const bool yes = ImGui::Button("Yes", ui.size({120, 26})) || key == true;
            ImGui::SameLine();
            const bool no = ImGui::Button("No", ui.size({120, 26})) || key == false;
            if (yes && valid) {
                if (auto r = host.setAiControl(toggleAsked_, toComputer); !r) transport->log().add(r.error());
            }
            if (yes || no) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }
    ImGui::End();
}

void ClassicMode::drawPbem(UiContext& ui) {
    const PbemTurn* turn = session_->pbemTurn();
    if (!turn) return;
    // A status strip at the bottom of the system panel: where End Turn saves
    // the orders, then where it saved them.
    ImGui::SetNextWindowPos(ui.at({8, frameH() - 56}));
    ImGui::SetNextWindowSize(ui.size({478, 50}));
    ImGui::PushFont(fonts_.regular, ui.fontPx(kTextSize));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.02f, 0.035f, 0.09f, 0.75f));
    ImGui::Begin("##pbemstatus", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                              ImGuiWindowFlags_NoBringToFrontOnFocus);
    const ImVec4 gold(1, 0.85f, 0.45f, 1);
    if (!session_->ordersFile().empty()) {
        if (ImGui::SmallButton("Main Menu")) ui.requests.quitToIntro = true;
        ImGui::SameLine();
        ImGui::TextColored(gold, "%s", std::format("Send {} to the host.", session_->ordersFile().filename().string()).c_str());
        ImGui::TextDisabled("Saved in %s", session_->ordersFile().parent_path().string().c_str());
        if (ImGui::IsWindowHovered()) ImGui::SetTooltip("%s", session_->ordersFile().string().c_str());
    } else if (!session_->pbemError().empty()) {
        ImGui::TextColored(ImVec4(1, 0.5f, 0.4f, 1), "The orders were not saved: %s", session_->pbemError().c_str());
        ImGui::TextDisabled("End Turn tries again.");
    } else {
        std::string line = std::format("Play by e-mail: '{}', turn {}.", turn->info.gameName, turn->turn);
        if (session_->pbemResumed() > 0) line += std::format(" Your saved turn is back ({} orders).", session_->pbemResumed());
        ImGui::TextColored(gold, "%s", line.c_str());
        ImGui::TextDisabled("End Turn saves your orders in %s", turn->ordersDir.string().c_str());
        if (ImGui::IsWindowHovered()) ImGui::SetTooltip("%s", turn->ordersDir.string().c_str());
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

// Turn-based games: a move stopped before a sector with enemy forces; the
// player decides whether to go in and fight (spec 03 §6.2, spec 06 §2.7).
void ClassicMode::drawEntryQuestion(UiContext& ui) {
    const game::GameState& s = ui.state();
    const game::EntryQuestion q = session_->questions().front();
    std::string who;
    if (const game::Fleet* f = s.fleet(q.fleet)) who = f->name;
    else if (const game::Vehicle* v = s.vehicle(q.vehicle)) who = v->name;
    std::string where = "an adjacent sector";
    if (q.where.system.valid() && q.where.system.index() < s.galaxy.systems.size())
        where = std::format("{} ({}, {})", s.galaxy.system(q.where.system).name, q.where.sector.x, q.where.sector.y);
    ImGui::SetNextWindowPos(ui.at({std::floor((frameW() - 400) * 0.5f), 290 * frameH() / kFrameH}));
    ImGui::SetNextWindowSize(ui.size({400, 150}));
    ImGui::PushFont(fonts_.regular, ui.fontPx(kTextSize));
    ImGui::Begin("Attack Sector", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | kPromptFlags);
    ui.promptWindow();   // never covered by a tutorial's input lock
    if (ImGui::IsWindowAppearing()) ImGui::SetWindowFocus();
    ImGui::TextWrapped("%s", std::format("Enemy forces are in {}. Should {} enter the sector and attack?", where, who.empty() ? "the ship" : who).c_str());
    ImGui::TextDisabled("Declining stops the move and cancels its orders.");
    ImGui::Spacing();
    // A Yes/No prompt (spec 06 §1.3): Y means Yes; N, Esc and Enter mean No (§3.4).
    const std::optional<bool> key = yesNoKey();
    const bool yes = ImGui::Button("Yes", ui.size({140, 30})) || key == true;
    ImGui::SameLine();
    const bool no = ImGui::Button("No", ui.size({140, 30})) || key == false;
    ImGui::End();
    if (yes) session_->answer(true);
    else if (no) session_->answer(false);
    ImGui::PopFont();
}

std::optional<game::ObjectId> ClassicMode::colonyTypeChoice(const UiContext& ui) const {
    const game::GameState& s = ui.state();
    if (s.options.simultaneous || !session_->myTurn()) return std::nullopt;
    const game::EmpireId me = session_->player();
    if (!me.valid() || me.index() >= s.empires.size()) return std::nullopt;
    for (game::ObjectId p : s.empire(me).colonyTypeChoices)
        if (const game::Colony* c = s.colony(p); c && c->owner == me) return p;
    return std::nullopt;
}

// Turn-based games: a colony was just founded; the player picks its type
// (spec 03 §8, the empire's "choose the colony type" option).
void ClassicMode::drawColonyTypeChoice(UiContext& ui, game::ObjectId planet) {
    const game::GameState& s = ui.state();
    const game::Empire& me = s.empire(session_->player());
    const game::Colony& c = *s.colony(planet);
    std::vector<std::string> types = me.colonyTypes;
    if (std::find(types.begin(), types.end(), c.colonyType) == types.end()) types.insert(types.begin(), c.colonyType);
    const float h = 110.0f + 30.0f * float(types.size());
    ImGui::SetNextWindowPos(ui.at({std::floor((frameW() - 300) * 0.5f), frameH() * 0.5f - h * 0.5f}));
    ImGui::SetNextWindowSize(ui.size({300, h}));
    ImGui::PushFont(fonts_.regular, ui.fontPx(kTextSize));
    ImGui::Begin("Colony Type", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
    ui.promptWindow();   // never covered by a tutorial's input lock
    ImGui::TextWrapped("%s", std::format("A new colony on {}. What kind of colony should it be?", s.galaxy.object(planet).name).c_str());
    ImGui::Spacing();
    for (const std::string& t : types)
        if (ImGui::Button(std::format("{}{}", t, t == c.colonyType ? " (suggested)" : "").c_str(), ui.size({280, 26})))
            session_->issue(game::cmd::SetColonyType{planet, t});
    ImGui::End();
    ImGui::PopFont();
}

void ClassicMode::drawBattleQuestion(UiContext& ui) {
    // A battle (or ground fight) that stops the engine to be shown (spec 04
    // §3 step 1, spec 06 §1.10.5, §1.10.6). One question per battle, answered
    // at the machine, for every human empire in it, hostile or not; with "No
    // Tactical Combat" on, or in a simultaneous game, the Strategic Combat
    // window with Begin and Close instead. When the player whose turn it is is
    // a computer empire, a notice naming the system and the empires comes
    // first. The colony owner's end-of-turn ground combat always has its
    // notice, then the Ground Combat window.
    const game::BattleQuestion& q = *session_->battleQuestion();
    const game::GameState& s = ui.state();
    const bool ground = q.kind == game::BattleQuestion::Kind::Ground;
    const size_t key = q.index * 100003u + size_t(q.where.system.value) * 1009u + size_t(q.where.sector.x * 13 + q.where.sector.y);
    if (battleChoiceKey_ != key) {
        battleChoiceKey_ = key;
        const game::EmpireId turn = game::activePlayer(s);
        battleNotice_ = ground || (!s.options.simultaneous && turn.valid() && turn.index() < s.empires.size() &&
                                   s.empire(turn).kind != game::PlayerKind::Human);
    }
    if (!battleNotice_) {
        // The question itself: the Strategic Combat window, or Ground Combat for a ground fight.
        const ScreenId id = ground ? ScreenId::GroundCombat : ScreenId::StrategicCombat;
        const bool open = std::any_of(screens_.begin(), screens_.end(), [&](const auto& sc) { return sc.first == id; });
        if (!open) {
            ScreenArgs args;
            args.index = ground ? kGroundQuestion : kStrategicQuestion;
            openScreen(id, std::move(args));
        }
        return;
    }
    // The 253x150 notice: the system, and each empire's flag and name; Begin, Esc or Enter go on.
    const std::string system = q.where.system.index() < s.galaxy.systems.size() ? s.galaxy.system(q.where.system).name : std::string("?");
    constexpr const char* kPopup = "Combat##battlenotice";
    if (!ImGui::IsPopupOpen(kPopup)) ImGui::OpenPopup(kPopup);
    const Vec2 size{253, 150};
    ImGui::SetNextWindowPos(ui.at({(frameW() - size.x) * 0.5f, (frameH() - size.y) * 0.5f}));
    ImGui::SetNextWindowSize(ui.size(size));
    ImGui::PushFont(fonts_.regular, ui.fontPx(kTextSize));
    if (!ImGui::BeginPopupModal(kPopup, nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | kPromptFlags)) {
        ImGui::PopFont();
        return;
    }
    ImGui::TextUnformatted(std::format("{} in the {} system", ground ? "Ground combat" : "Combat", system).c_str());
    for (game::EmpireId e : q.participants) {
        if (!e.valid() || e.index() >= s.empires.size()) continue;
        if (Sprite flag = art_->flag(s.empire(e).race.style, false)) {
            image(ui, flag, {20, 14});
            ImGui::SameLine(0, ui.px(5));
        }
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(empireColor(s, e)), "%s", s.empire(e).name.c_str());
    }
    // A battle notice: Esc and Enter both mean Begin (spec 06 §3.4).
    ImGui::SetCursorPosY(std::max(ImGui::GetCursorPosY(), ImGui::GetWindowHeight() - ui.px(36)));
    if (ImGui::Button("Begin", ImVec2(-FLT_MIN, ui.px(26))) || okKey()) {
        battleNotice_ = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
    ImGui::PopFont();
}

void ClassicMode::drawHandoff(UiContext& ui) {
    const game::Empire& e = ui.me();
    ImGui::SetNextWindowPos(ui.at({std::floor((frameW() - 400) * 0.5f), 250 * frameH() / kFrameH}));
    ImGui::SetNextWindowSize(ui.size({400, 230}));
    ImGui::PushFont(fonts_.regular, ui.fontPx(kTextSize));
    ImGui::Begin("Next Player", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
    image(ui, art_->flag(e.race.style), {39, 27});
    ImGui::SameLine();
    ImGui::TextUnformatted(std::format("{} {}", e.name, e.empireType).c_str());
    ImGui::TextDisabled("Game date %s. Other players, please look away.", formatDate(ui.state().turn).c_str());
    ImGui::Spacing();
    const bool needsPassword = !e.passwordHash.empty();
    bool begin = false;
    if (needsPassword) {
        ImGui::TextUnformatted("Password");
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        char buffer[128] = {};
        std::snprintf(buffer, sizeof buffer, "%s", handoffPassword_.c_str());
        if (ImGui::InputText("##password", buffer, sizeof buffer, ImGuiInputTextFlags_Password | ImGuiInputTextFlags_EnterReturnsTrue))
            begin = true;
        handoffPassword_ = buffer;
    }
    // The Next Player notice: Esc or Enter continue (spec 06 §3.4); with a
    // password, Enter in its field submits it.
    if (ImGui::Button("Begin Turn", ui.size({140, 30})) || (!needsPassword && okKey())) begin = true;
    ImGui::SameLine();
    if (ImGui::Button("Quit Game", ui.size({140, 30}))) ui.requests.quitGame = true;
    if (begin) {
        if (!needsPassword || session_->passwordMatches(e, handoffPassword_)) {
            handoff_ = false;
            handoffPassword_.clear();
        } else {
            handoffError_ = "Wrong password.";
            handoffPassword_.clear();
        }
    }
    if (!handoffError_.empty()) ImGui::TextColored(ImVec4(1, 0.5f, 0.4f, 1), "%s", handoffError_.c_str());
    ImGui::End();
    ImGui::PopFont();
}

void ClassicMode::render(gfx::Renderer2D& r, const FrameState& fs) {
    const float fw = float(fs.frame.width), fh = float(fs.frame.height);
    const Vec2 topLeft = mapping_.fromFb({0, 0});
    const Vec2 bottomRight = mapping_.fromFb({fw, fh});
    r.begin(Mat4::ortho2D(topLeft.x, bottomRight.x, topLeft.y, bottomRight.y), fs.frame, mapping_.scale);
    if (session_ && ui_ && !handoff_) main_.render(r, *ui_);
    else r.rect(Rect{{mapping_.left, 0}, {mapping_.right, frameH()}}, Color::hex(0x000000));
    r.flush();
}

} // namespace opense4::client
