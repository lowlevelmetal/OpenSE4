#include "client/classic/classic_mode.hpp"

#include "client/app_settings.hpp"
#include "client/audio.hpp"
#include "client/classic/net_transport.hpp"
#include "client/classic/settings.hpp"
#include "game/setup.hpp"

#include "core/log.hpp"

#include <imgui.h>

#include <cstdio>
#include <format>

namespace opense4::client {

using namespace classic;

std::unique_ptr<ClassicMode> ClassicMode::create(const Platform& platform, const ClassicOptions& options, std::string& error) {
    const auto dataDir = ruleset::findInstalledDataDir(options.installDir);
    if (!dataDir) {
        error = options.installDir.empty()
                    ? "Classic mode needs an installed copy of the classic game, and none was found in your Steam libraries.\n"
                      "Pass --classic-dir=<game directory> to point at it (see docs/SETUP.md)."
                    : std::format("No classic data set found at {}.", options.installDir);
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
            mode->openScreen(*id, {});
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
    session_->onNewTurn = [this] { openLogOnTurn_ = true; };
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
    audio().setOptions(AudioOptions{prefs.soundOn, prefs.musicOn, prefs.soundVolume, prefs.musicVolume, prefs.remasteredSounds});
    // Intro music in the front end, battle music while a replay is open, background music otherwise.
    bool combat = false;
    for (const auto& [id, screen] : screens_) combat = combat || id == ScreenId::CombatReplay;
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
    }
    if (handoff_) {
        drawHandoff(ui);
        return !ui.requests.quitGame;
    }

    // Turn-based games: a battle the player's order started is shown at once.
    if (auto battle = session_->takeNewBattle()) {
        ScreenArgs args;
        args.index = int(*battle);
        openScreen(ScreenId::CombatReplay, std::move(args));
    }
    const bool asking = screens_.empty() && !session_->questions().empty();

    // Classic windows are modal: while one is open the main window takes no input.
    main_.update(ui, !screens_.empty() || asking);
    drawNetwork(ui);
    drawPbem(ui);
    if (asking) drawEntryQuestion(ui);

    // Windows, oldest first; the newest draws on top.
    for (size_t i = 0; i < screens_.size();) {
        ImGui::PushID(int(i));
        const bool keep = screens_[i].second->draw(ui);
        ImGui::PopID();
        if (keep) ++i;
        else screens_.erase(screens_.begin() + std::ptrdiff_t(i));
    }
    for (auto& [id, args] : pendingOpen_) openScreen(id, std::move(args));
    pendingOpen_.clear();
    main_.applyRequests(ui);

    if (ui.requests.endTurn) {
        ui.requests.endTurn = false;
        if (settings().confirmEndTurn) confirmEndTurn_ = true;
        else endTurn();
    }
    if (confirmEndTurn_) {
        ImGui::SetNextWindowPos(ui.at({362, 330}));
        ImGui::SetNextWindowSize(ui.size({300, 110}));
        ImGui::PushFont(fonts_.regular, ui.fontPx(kTextSize));
        ImGui::Begin("End Turn", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
        ImGui::TextUnformatted("End the turn now?");
        if (ImGui::Button("End Turn", ui.size({120, 28})) || ImGui::IsKeyPressed(ImGuiKey_Enter, false)) {
            confirmEndTurn_ = false;
            endTurn();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ui.size({120, 28})) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) confirmEndTurn_ = false;
        ImGui::End();
        ImGui::PopFont();
    }
    if (openLogOnTurn_) {
        openLogOnTurn_ = false;
        if (settings().showLogAtTurnStart && !ui.me().log.empty() && ui.me().log.back().turn + 1 >= ui.state().turn)
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
    // A status strip at the bottom of the system panel: who we wait for, the latest line.
    ImGui::SetNextWindowPos(ui.at({8, 712}));
    ImGui::SetNextWindowSize(ui.size({650, 50}));
    ImGui::PushFont(fonts_.regular, ui.fontPx(kTextSize));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.02f, 0.035f, 0.09f, 0.75f));
    ImGui::Begin("##netstatus", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                             ImGuiWindowFlags_NoBringToFrontOnFocus);
    if (ImGui::SmallButton(chatOpen_ ? "Hide Chat" : "Chat")) chatOpen_ = !chatOpen_;
    ImGui::SameLine();
    const std::string status = net->status();
    ImGui::TextColored(ImVec4(1, 0.85f, 0.45f, 1), "%s", status.empty() ? (session_->waitingForOthers() ? "Orders sent." : "Your turn.") : status.c_str());
    if (!net->log().lines().empty()) ImGui::TextDisabled("%s", net->log().lines().back().c_str());
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
    ImGui::Begin("Attack Sector", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
    ImGui::TextWrapped("%s", std::format("Enemy forces are in {}. Should {} enter the sector and attack?", where, who.empty() ? "the ship" : who).c_str());
    ImGui::TextDisabled("Declining stops the move and cancels its orders.");
    ImGui::Spacing();
    if (ImGui::Button("Attack", ui.size({140, 30})) || ImGui::IsKeyPressed(ImGuiKey_Enter, false)) session_->answer(true);
    ImGui::SameLine();
    if (ImGui::Button("Stay Back", ui.size({140, 30})) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) session_->answer(false);
    ImGui::End();
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
    if (ImGui::Button("Begin Turn", ui.size({140, 30}))) begin = true;
    ImGui::SameLine();
    if (ImGui::Button("Quit Game", ui.size({140, 30}))) ui.requests.quitGame = true;
    if (begin) {
        if (!needsPassword || game::hashPassword(handoffPassword_) == e.passwordHash) {
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
