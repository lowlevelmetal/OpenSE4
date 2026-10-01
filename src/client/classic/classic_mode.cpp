#include "client/classic/classic_mode.hpp"

#include "client/app_settings.hpp"
#include "client/audio.hpp"
#include "client/classic/net_transport.hpp"
#include "client/classic/reports.hpp"
#include "client/classic/screens/screens.hpp"
#include "client/classic/settings.hpp"
#include "game/setup.hpp"
#include "game/tactical.hpp"

#include "core/log.hpp"

#include <imgui.h>

#include <cstdio>
#include <format>

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
    log::info("Classic data set: {} ({} components, {} race presets)", dataDir->string(), mode->rules_->data().components.size(),
              mode->rules_->racePresets().size());
    audio().setInstall(&mode->art_->files());
    mode->fonts_ = loadClassicFonts(*platform.fonts, mode->art_->files());
    mode->playlists_ = readPlaylists(mode->rules_->data().settings);
    applyClassicStyle();

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
    } else if (auto front = frontScreenByName(options.openWindow)) {
        mode->front_ = std::move(front);  // automation: --open=<front-end screen>
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
        auto session = startLocalGame(mode->rules_, setup);
        if (!session) {
            error = session.error();
            return nullptr;
        }
        mode->startGame(std::move(*session));
        // Automation: the computer plays every empire for a while.
        mode->session_->simulateTurns(options.autoTurns);
        if (!options.openWindow.empty()) {
            mode->openLogOnTurn_ = false;  // the requested window stays in front
            const auto id = screenFromName(options.openWindow);
            if (!id) {
                error = std::format("Unknown window '{}'", options.openWindow);
                return nullptr;
            }
            if (*id == ScreenId::TacticalCombat || *id == ScreenId::TacticalOrders || *id == ScreenId::TacticalOptions ||
                *id == ScreenId::StrategicCombat) {
                // A sample battle to show: the player's warships against copies of them
                // (fought by the strategies for Strategic Combat).
                if (!startDemoSimulation(*mode->ui_, *id != ScreenId::StrategicCombat)) {
                    error = "No armed ship design to fight a sample battle with.";
                    return nullptr;
                }
                if (*id != ScreenId::TacticalCombat && *id != ScreenId::StrategicCombat) mode->ui_->open(*id);
            } else {
                ScreenArgs args;
                if (*id == ScreenId::CombatSimulator) args.text = "demo";
                mode->openScreen(*id, std::move(args));
            }
        }
    } else {
        mode->front_ = makeFrontScreen(FrontId::Intro);
    }
    return mode;
}

ClassicMode::~ClassicMode() {
    screens_.clear();
    ui_.reset();
    session_.reset();
    art_.reset();
}

void ClassicMode::startGame(std::unique_ptr<ClassicSession> session) {
    screens_.clear();
    session_ = std::move(session);
    ui_ = std::make_unique<UiContext>(*session_, *art_, fonts_);
    ui_->app = platform_.app;
    ui_->opener = [this](ScreenId id, ScreenArgs args) { pendingOpen_.emplace_back(id, std::move(args)); };
    session_->onNewTurn = [this] {
        openLogOnTurn_ = true;
        strategicQueue_.clear();   // battles of the turn before: GameState::combats holds the new ones
    };
    main_ = MainWindow{};
    main_.reset(*ui_);
    handoffPlayer_ = {};
    handoff_ = false;
    front_.reset();
}

void ClassicMode::openScreen(ScreenId id, ScreenArgs args) {
    // A window that is already open comes to the front instead of opening twice
    // (windows with arguments are replaced so they show the new target).
    for (auto it = screens_.begin(); it != screens_.end(); ++it)
        if (it->first == id) {
            screens_.erase(it);
            break;
        }
    if (auto screen = makeScreen(id, args)) screens_.emplace_back(id, std::move(screen));
}

void ClassicMode::endTurn() {
    if (!session_ || session_->waitingForOthers()) return;
    audio().play("endturn");
    screens_.clear();
    session_->endTurn();
}

void ClassicMode::updateAudio() {
    const ClassicSettings& prefs = settings();
    audio().setOptions(AudioOptions{prefs.soundOn, prefs.musicOn, prefs.soundVolume, float(prefs.musicVolume) / 100.0f, !prefs.classicSoundEffects});
    // Intro music in the front end, battle music while a replay is open, background music otherwise.
    bool combat = false;
    for (const auto& [id, screen] : screens_)
        combat = combat || id == ScreenId::CombatReplay || id == ScreenId::TacticalCombat || id == ScreenId::StrategicCombat ||
                 id == ScreenId::GroundCombat;
    const std::vector<std::string>& list = !session_ ? playlists_.intro : combat ? playlists_.combat : playlists_.background;
    if (prefs.musicOn && !list.empty()) audio().playMusic(list);
    else audio().stopMusic();
}

bool ClassicMode::update(const FrameState& fs) {
    updateAudio();
    mapping_ = frameMappingFor(float(fs.frame.width), float(fs.frame.height));
    // Every classic window defaults to the game's text font at its native size.
    ImGui::PushFont(fonts_.regular, kTextSize * mapping_.scale / fs.fbScale * appSettings().graphics.textScale);
    const bool keepRunning = updateFrame(fs);
    ImGui::PopFont();
    return keepRunning;
}

bool ClassicMode::updateFrame(const FrameState& fs) {
    art_->setFilter(appSettings().graphics.sharpPixels ? gfx::Filter::Nearest : gfx::Filter::Linear);

    if (!session_) {
        MenuContext ctx{rules_, *art_, fonts_, mapping_, fs.fbScale, fs.time, options_.seed, platform_.app, {}, {}, {}, frontError_};
        ctx.startGame = [this](std::unique_ptr<ClassicSession> s) { startGame(std::move(s)); };
        ctx.go = [this](FrontId id) { nextFront_ = id; };
        ctx.quit = [this] { quit_ = true; };
        if (front_) front_->draw(ctx);
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
    session_->poll();

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
        const bool keep = screens_[i].second->draw(ui);
        ImGui::PopID();
        if (keep) ++i;
        else screens_.erase(screens_.begin() + std::ptrdiff_t(i));
    }
    if (battleAsking) drawBattleQuestion(ui);
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
        ImGui::SetNextWindowPos(ui.at({362, 330}));
        ImGui::SetNextWindowSize(ui.size({300, 110}));
        ImGui::PushFont(fonts_.regular, ui.fontPx(kTextSize));
        ImGui::Begin("End Turn", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | kPromptFlags);
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
    if (openLogOnTurn_ && !battleAsking && !session_->tactical() && strategicQueue_.empty() && !isOpen(ScreenId::StrategicCombat)) {
        openLogOnTurn_ = false;
        if (ui.options().showLogAtTurnStart && !ui.me().log.empty() && ui.me().log.back().turn + 1 >= ui.state().turn)
            openScreen(ScreenId::Log, {});
    }
    if (ui.requests.loadGame) {
        const std::filesystem::path file = *ui.requests.loadGame;
        ui.requests.loadGame.reset();
        auto loaded = ClassicSession::load(rules_, file);
        if (loaded) {
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
        ui_.reset();
        session_.reset();
        front_ = makeFrontScreen(FrontId::Intro);
        return true;
    }
    return !ui.requests.quitGame;
}

void ClassicMode::drawNetwork(UiContext& ui) {
    auto* net = dynamic_cast<NetTransport*>(session_->transport());
    if (!net) return;
    // A status strip at the bottom of the system panel: who we wait for, the
    // latest line. As narrow as the PBEM strip, so the planet panel's buttons
    // stay clear; the full lines show as a tooltip.
    ImGui::SetNextWindowPos(ui.at({8, 712}));
    ImGui::SetNextWindowSize(ui.size({478, 50}));
    ImGui::PushFont(fonts_.regular, ui.fontPx(kTextSize));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.02f, 0.035f, 0.09f, 0.75f));
    ImGui::Begin("##netstatus", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                             ImGuiWindowFlags_NoBringToFrontOnFocus);
    if (ImGui::SmallButton(chatOpen_ ? "Hide Chat" : "Chat")) chatOpen_ = !chatOpen_;
    ImGui::SameLine();
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
                if (buffer[0]) net->chat(buffer);
                buffer[0] = 0;
                ImGui::SetKeyboardFocusHere(-1);
            }
            chatInput_ = buffer;
        }
        ImGui::End();
    }
    ImGui::PopFont();
}

void ClassicMode::drawPbem(UiContext& ui) {
    const PbemTurn* turn = session_->pbemTurn();
    if (!turn) return;
    // A status strip at the bottom of the system panel: where End Turn saves
    // the orders, then where it saved them.
    ImGui::SetNextWindowPos(ui.at({8, 712}));
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
    ImGui::SetNextWindowPos(ui.at({312, 290}));
    ImGui::SetNextWindowSize(ui.size({400, 150}));
    ImGui::PushFont(fonts_.regular, ui.fontPx(kTextSize));
    ImGui::Begin("Attack Sector", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | kPromptFlags);
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
    ImGui::SetNextWindowPos(ui.at({362, 384 - h * 0.5f}));
    ImGui::SetNextWindowSize(ui.size({300, h}));
    ImGui::PushFont(fonts_.regular, ui.fontPx(kTextSize));
    ImGui::Begin("Colony Type", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
    ImGui::TextWrapped("%s", std::format("A new colony on {}. What kind of colony should it be?", s.galaxy.object(planet).name).c_str());
    ImGui::Spacing();
    for (const std::string& t : types)
        if (ImGui::Button(std::format("{}{}", t, t == c.colonyType ? " (suggested)" : "").c_str(), ui.size({280, 26})))
            session_->issue(game::cmd::SetColonyType{planet, t});
    ImGui::End();
    ImGui::PopFont();
}

void ClassicMode::drawBattleQuestion(UiContext& ui) {
    // One question per battle, answered at the machine, for every human empire
    // in it, hostile or not (spec 04 §3 step 1). When the player whose turn it
    // is is a computer empire, a notice naming the system and the empires
    // comes first.
    const game::BattleQuestion& q = *session_->battleQuestion();
    const game::GameState& s = ui.state();
    const size_t key = q.index * 100003u + size_t(q.where.system.value) * 1009u + size_t(q.where.sector.x * 13 + q.where.sector.y);
    if (battleChoiceKey_ != key) {
        battleChoiceKey_ = key;
        const game::EmpireId turn = game::activePlayer(s);
        battleNotice_ = !s.options.simultaneous && turn.valid() && turn.index() < s.empires.size() && s.empire(turn).kind != game::PlayerKind::Human;
    }
    std::string sides;
    for (size_t i = 0; i < q.participants.size(); ++i)
        sides += (i == 0 ? "" : i + 1 == q.participants.size() ? " and " : ", ") + s.empire(q.participants[i]).name;
    const std::string system = q.where.system.index() < s.galaxy.systems.size() ? s.galaxy.system(q.where.system).name : std::string("?");
    const float h = 150.0f;
    // A modal prompt: nothing else takes input until the battle is answered.
    constexpr const char* kPopup = "Combat##battlequestion";
    if (!ImGui::IsPopupOpen(kPopup)) ImGui::OpenPopup(kPopup);
    ImGui::SetNextWindowPos(ui.at({302, 384 - h * 0.5f}));
    ImGui::SetNextWindowSize(ui.size({420, h}));
    ImGui::PushFont(fonts_.regular, ui.fontPx(kTextSize));
    if (!ImGui::BeginPopupModal(kPopup, nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | kPromptFlags)) {
        ImGui::PopFont();
        return;
    }
    if (battleNotice_) {
        ImGui::TextWrapped("%s", std::format("Combat in the {} system between {}.", system, sides).c_str());
        ImGui::Spacing();
        // A battle notice: Esc and Enter both mean Begin (spec 06 §1.10.5, §3.4).
        if (ImGui::Button("Begin", ui.size({140, 30})) || okKey()) battleNotice_ = false;
        ImGui::EndPopup();
        ImGui::PopFont();
        return;
    }
    ImGui::TextWrapped("%s", std::format("Battle at {} between {}.", sectorName(s, q.where, session_->player()), sides).c_str());
    ImGui::TextDisabled("Tactical: the players give the orders. Strategic: the ships follow their strategies.");
    ImGui::Spacing();
    auto fight = [&](std::vector<game::EmpireId> tactical) {
        ImGui::CloseCurrentPopup();
        if (tactical.empty()) {
            session_->answerBattle(game::BattleAnswer{});
            return;
        }
        auto battle = std::make_unique<game::combat::TacticalBattle>(*rules_, *q.state, game::combat::TacticalBattle::Setup{q.where, q.entering, tactical, std::nullopt, std::nullopt, std::nullopt, q.check});
        if (!battle->started()) {
            session_->answerBattle(game::BattleAnswer{});
            return;
        }
        TacticalFight f;
        f.kind = TacticalFight::Kind::Game;
        f.battle = std::move(battle);
        f.players = std::move(tactical);
        f.title = "Tactical Combat";
        session_->startTactical(std::move(f));
        openScreen(ScreenId::TacticalCombat, {});
    };
    // T and S pick Tactical and Strategic (spec 06 §3.4).
    const std::optional<bool> pick = tacticalStrategicKey();
    const bool strategic = ImGui::Button("Strategic", ui.size({140, 30})) || pick == false;
    ImGui::SameLine();
    const bool tactical = ImGui::Button("Tactical", ui.size({140, 30})) || pick == true;
    if (strategic) fight({});
    else if (tactical) fight(q.humans);
    ImGui::EndPopup();
    ImGui::PopFont();
}

void ClassicMode::drawHandoff(UiContext& ui) {
    const game::Empire& e = ui.me();
    ImGui::SetNextWindowPos(ui.at({312, 250}));
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
    else r.rect(Rect{{mapping_.left, 0}, {mapping_.right, kFrameH}}, Color::hex(0x000000));
    r.flush();
}

} // namespace opense4::client
