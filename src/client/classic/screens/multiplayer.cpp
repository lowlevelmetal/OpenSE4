// Multiplayer front end (docs/spec/06 §1.7, docs/MULTIPLAYER.md): host a
// network game from the client (with UPnP port mapping) or join one, then a
// lobby with the player slots, empire choice, ready flags and chat. When the
// host starts the game, the running session hands its network connection to
// the game (net_transport.hpp).

#include "client/classic/frontend.hpp"
#include "client/classic/net_transport.hpp"
#include "game/redact.hpp"
#include "game/serialize.hpp"
#include "net/auth.hpp"
#include "net/discovery.hpp"
#include "net/socket.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <utility>
#include <format>

namespace opense4::client::classic {

namespace {

enum class Mode { Choose, Host, Join, Lobby };

void textField(const char* label, std::string& value, float width, ImGuiInputTextFlags flags = 0) {
    char buffer[256] = {};
    std::snprintf(buffer, sizeof buffer, "%s", value.c_str());
    ImGui::SetNextItemWidth(width);
    if (ImGui::InputText(label, buffer, sizeof buffer, flags)) value = buffer;
}

class MultiplayerScreen final : public FrontScreen {
public:
    explicit MultiplayerScreen(std::string_view automation) : automation_(automation) {}

    void draw(MenuContext& ctx) override {
        if (!automation_.empty()) {
            const std::string a = std::exchange(automation_, {});
            if (a == "browse") {
                mode_ = Mode::Join;
            } else if (a == "host") {
                upnp_ = false;
                openLobby(ctx);
            } else if (a.starts_with("join=")) {
                std::string addr = a.substr(5);
                if (addr.ends_with(",ready")) {
                    addr.resize(addr.size() - 6);
                    autoReady_ = true;
                }
                if (const size_t colon = addr.rfind(':'); colon != std::string::npos) {
                    port_ = std::atoi(addr.c_str() + colon + 1);
                    addr.resize(colon);
                }
                address_ = addr;
                connect(ctx);
            }
        }
        if (presets_.empty())
            for (size_t i = 0; i < ctx.rules->racePresets().size(); ++i)
                if (!ctx.rules->racePresets()[i].neutral) presets_.push_back(i);

        ImGui::SetNextWindowPos(ctx.at({62, 50}));
        ImGui::SetNextWindowSize(ctx.size({900, 668}));
        ImGui::PushFont(ctx.fonts.regular, kTextSize * ctx.k());
        ImGui::Begin(mode_ == Mode::Lobby ? "Multiplayer Lobby" : "Multiplayer", nullptr,
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
        switch (mode_) {
            case Mode::Choose: chooser(ctx); break;
            case Mode::Host: hostForm(ctx); break;
            case Mode::Join: joinForm(ctx); break;
            case Mode::Lobby: lobby(ctx); break;
        }
        if (!error_.empty()) ImGui::TextColored(ImVec4(1, 0.5f, 0.4f, 1), "%s", error_.c_str());
        ImGui::End();
        ImGui::PopFont();
    }

private:
    // ---- Pages ------------------------------------------------------------------------------

    void chooser(MenuContext& ctx) {
        ImGui::TextWrapped("Play with other people over the network, with simultaneous turns or one player after another. One "
                           "player hosts; the others join with the host's address. The host's computer runs the game.");
        ImGui::Spacing();
        if (ImGui::Button("Host a Game", ctx.size({200, 40}))) mode_ = Mode::Host;
        ImGui::SameLine();
        if (ImGui::Button("Join a Game", ctx.size({200, 40}))) mode_ = Mode::Join;
        ImGui::SameLine();
        if (ImGui::Button("Play by E-mail", ctx.size({200, 40}))) ctx.go(FrontId::Pbem);
        ImGui::SameLine();
        if (ImGui::Button("Back", ctx.size({200, 40}))) ctx.go(FrontId::Intro);
        ImGui::Spacing();
        ImGui::TextDisabled("To host a game without a player on the host machine, or to host a play-by-e-mail game, use opense4-server.");
    }

    void common(MenuContext& ctx) {
        const float w = ctx.px(300);
        textField("Your name", name_, w);
        textField("Your password (optional)", password_, w, ImGuiInputTextFlags_Password);
        racePicker(ctx);
    }

    void racePicker(MenuContext& ctx) {
        if (presets_.empty()) return;
        race_ = std::clamp(race_, 0, int(presets_.size()) - 1);
        const auto& preset = ctx.rules->racePresets()[presets_[size_t(race_)]];
        ImGui::SetNextItemWidth(ctx.px(300));
        if (ImGui::BeginCombo("Your race", preset.name.c_str())) {
            for (int i = 0; i < int(presets_.size()); ++i)
                if (ImGui::Selectable(ctx.rules->racePresets()[presets_[size_t(i)]].name.c_str(), i == race_)) race_ = i;
            ImGui::EndCombo();
        }
    }

    game::EmpireSetup mySetup(MenuContext& ctx) const {
        game::EmpireSetup e;
        if (!presets_.empty()) e.preset = ctx.rules->racePresets()[presets_[size_t(race_)]].folder;
        e.kind = game::PlayerKind::Human;
        return e;
    }

    void hostForm(MenuContext& ctx) {
        const float w = ctx.px(300);
        ImGui::SeparatorText("Game");
        textField("Game name", gameName_, w);
        ImGui::SetNextItemWidth(w);
        ImGui::InputInt("Port (TCP)", &port_);
        port_ = std::clamp(port_, 1, 65535);
        ImGui::SetNextItemWidth(w);
        ImGui::SliderInt("Human players", &humans_, 2, 20);
        ImGui::SetNextItemWidth(w);
        ImGui::SliderInt("Computer players", &computers_, 0, 19);
        ImGui::SetNextItemWidth(w);
        // The number of systems is rolled from the quadrant size, as in the original (spec 01 §2.2).
        static constexpr std::array<const char*, 3> kSizes{"Small", "Medium", "Large"};
        ImGui::Combo("Quadrant size", &quadrantSize_, kSizes.data(), static_cast<int>(kSizes.size()));
        quadrantSize_ = std::clamp(quadrantSize_, 0, 2);
        ImGui::SetNextItemWidth(w);
        // Turn style (spec 01 §2.2, spec 05 §8): simultaneous is OpenSE4's default (inferred).
        static constexpr std::array<const char*, 2> kStyles{"Simultaneous", "Turn-based (one player after another)"};
        ImGui::Combo("Turn style", &turnStyle_, kStyles.data(), static_cast<int>(kStyles.size()));
        turnStyle_ = std::clamp(turnStyle_, 0, 1);
        ImGui::SetNextItemWidth(w);
        ImGui::SliderInt("Turn time limit (s, 0 = none)", &timeout_, 0, 3600);
        textField("Join password (optional)", joinPassword_, w, ImGuiInputTextFlags_Password);
        ImGui::BeginDisabled(!net::PortMapper::supported());
        ImGui::Checkbox("Forward the port on my router automatically (UPnP)", &upnp_);
        ImGui::EndDisabled();
        if (!net::PortMapper::supported()) ImGui::TextDisabled("This build has no UPnP support; forward TCP %d by hand.", port_);
        ImGui::SeparatorText("You");
        common(ctx);
        ImGui::Spacing();
        if (ImGui::Button("Open Lobby", ctx.size({200, 36}))) openLobby(ctx);
        ImGui::SameLine();
        if (ImGui::Button("Back", ctx.size({200, 36}))) mode_ = Mode::Choose;
    }

    void lanGames(MenuContext& ctx) {
        if (!browser_.running()) browser_.start();
        browser_.poll();
        ImGui::SeparatorText("Games on your local network");
        if (!browser_.running()) {
            ImGui::TextDisabled("LAN discovery is not available on this machine; enter the host's address below.");
            return;
        }
        const std::string mine = game::dataSetIdentity(*ctx.rules);
        if (ImGui::BeginTable("##lan", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY,
                              ImVec2(0, ctx.px(140)))) {
            ImGui::TableSetupColumn("Game");
            ImGui::TableSetupColumn("Address", ImGuiTableColumnFlags_WidthFixed, ctx.px(170));
            ImGui::TableSetupColumn("Players", ImGuiTableColumnFlags_WidthFixed, ctx.px(70));
            ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed, ctx.px(90));
            ImGui::TableSetupColumn("Note", ImGuiTableColumnFlags_WidthFixed, ctx.px(190));
            ImGui::TableHeadersRow();
            for (size_t i = 0; i < browser_.games().size(); ++i) {
                const net::LanGame& g = browser_.games()[i];
                ImGui::PushID(int(i));
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                const bool chosen = address_ == g.address && port_ == g.port;
                if (ImGui::Selectable(g.gameName.c_str(), chosen, ImGuiSelectableFlags_SpanAllColumns)) {
                    address_ = g.address;
                    port_ = g.port;
                }
                ImGui::TableNextColumn();
                ImGui::Text("%s:%u", g.address.c_str(), unsigned(g.port));
                ImGui::TableNextColumn();
                ImGui::Text("%u / %u", g.players, g.slots);
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(g.started ? "In progress" : "Lobby");
                ImGui::TableNextColumn();
                if (g.dataSet != mine) ImGui::TextColored(ImVec4(1, 0.6f, 0.4f, 1), "Different game data");
                else if (g.password) ImGui::TextUnformatted("Password needed");
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        if (browser_.games().empty()) ImGui::TextDisabled("Looking for games...");
        if (ImGui::SmallButton("Refresh")) browser_.refresh();
    }

    void joinForm(MenuContext& ctx) {
        const float w = ctx.px(300);
        lanGames(ctx);
        ImGui::SeparatorText("Host");
        textField("Address", address_, w);
        ImGui::SetNextItemWidth(w);
        ImGui::InputInt("Port (TCP)", &port_);
        port_ = std::clamp(port_, 1, 65535);
        textField("Join password (if the host set one)", joinPassword_, w, ImGuiInputTextFlags_Password);
        ImGui::SeparatorText("You");
        common(ctx);
        ImGui::TextDisabled("To rejoin a game in progress, use the same name and password as before.");
        ImGui::Spacing();
        if (ImGui::Button("Connect", ctx.size({200, 36}))) connect(ctx);
        ImGui::SameLine();
        if (ImGui::Button("Back", ctx.size({200, 36}))) mode_ = Mode::Choose;
    }

    // ---- Starting ---------------------------------------------------------------------------

    void openLobby(MenuContext& ctx) {
        error_.clear();
        if (name_.empty()) {
            error_ = "Enter your name.";
            return;
        }
        net::HostConfig cfg;
        cfg.gameName = gameName_;
        cfg.port = static_cast<uint16_t>(port_);
        cfg.humanSlots = humans_;
        cfg.localPlayer = net::LocalPlayer{name_, net::hashPassword(password_), mySetup(ctx)};
        cfg.setup.seed = ctx.seed;
        cfg.setup.options.systemCount = 0;  // rolled from the quadrant size
        cfg.setup.options.quadrantSize = quadrantSize_;
        cfg.setup.options.simultaneous = turnStyle_ == 0;
        cfg.joinPasswordHash = net::hashPassword(joinPassword_);
        cfg.turnTimeoutSeconds = timeout_;
        cfg.upnp.enabled = upnp_ && net::PortMapper::supported();
        host_ = std::make_unique<net::HostSession>(*ctx.rules, cfg);
        rules_ = ctx.rules;
        if (auto r = host_->start(); !r) {
            error_ = r.error();
            host_.reset();
            return;
        }
        for (int i = 0; i < computers_; ++i) {
            game::EmpireSetup ai;
            ai.kind = game::PlayerKind::Computer;
            if (!presets_.empty()) ai.preset = ctx.rules->racePresets()[presets_[size_t((race_ + 1 + i) % int(presets_.size()))]].folder;
            if (auto r = host_->addComputerEmpire(ai); !r) log_.add("Could not add a computer player: " + r.error());
        }
        mode_ = Mode::Lobby;
    }

    void connect(MenuContext& ctx) {
        error_.clear();
        if (name_.empty() || address_.empty()) {
            error_ = "Enter your name and the host's address.";
            return;
        }
        net::ClientConfig cfg;
        cfg.host = address_;
        cfg.port = static_cast<uint16_t>(port_);
        cfg.playerName = name_;
        cfg.passwordHash = net::hashPassword(password_);
        cfg.joinPasswordHash = net::hashPassword(joinPassword_);
        cfg.dataSet = game::dataSetIdentity(*ctx.rules);
        client_ = std::make_unique<net::ClientSession>(cfg);
        rules_ = ctx.rules;
        if (auto r = client_->connect(); !r) {
            error_ = r.error();
            client_.reset();
            return;
        }
        setupSent_ = false;
        mode_ = Mode::Lobby;
    }

    void leave() {
        if (host_) host_->stop();
        if (client_) client_->disconnect();
        host_.reset();
        client_.reset();
        mode_ = Mode::Choose;
    }

    // ---- Lobby ------------------------------------------------------------------------------

    void pollNetwork(MenuContext& ctx) {
        std::vector<net::Event> events = host_ ? host_->poll(0) : client_ ? client_->poll(0) : std::vector<net::Event>{};
        for (const net::Event& e : events) {
            if (e.type != net::EventType::LobbyChanged && e.type != net::EventType::TurnStatusChanged) log_.add(net::describe(e));
            if (e.type == net::EventType::Joined && client_ && !setupSent_) {
                client_->submitSetup(mySetup(ctx));
                setupSent_ = true;
                if (autoReady_) client_->setReady(true);
            }
            if (e.type == net::EventType::Rejected) error_ = e.text;
            if (e.type == net::EventType::GameStarted) {
                startPlaying(ctx);
                return;
            }
        }
    }

    void startPlaying(MenuContext& ctx) {
        if (host_ && host_->state()) {
            const game::EmpireId me = host_->localEmpire();
            game::GameState state = game::redactForEmpire(*rules_, *host_->state(), me);
            auto session = std::make_unique<ClassicSession>(rules_, std::move(state), me, SessionKind::NetworkClient);
            session->setTransport(std::make_unique<HostTransport>(rules_, std::move(host_)));
            ctx.startGame(std::move(session));
        } else if (client_ && client_->state()) {
            game::GameState state = *client_->state();
            const game::EmpireId me = client_->empire();
            auto session = std::make_unique<ClassicSession>(rules_, std::move(state), me, SessionKind::NetworkClient);
            session->setTransport(std::make_unique<ClientTransport>(std::move(client_)));
            ctx.startGame(std::move(session));
        }
    }

    void lobby(MenuContext& ctx) {
        pollNetwork(ctx);
        if (!host_ && !client_) return;  // the game started
        const net::LobbyInfo& info = host_ ? host_->lobby() : client_->lobby();

        // Connection line: where others connect, and the router mapping.
        if (host_) {
            const net::PortMapStatus pm = host_->portMapping();
            ImGui::Text("Hosting \"%s\" on TCP port %u. Local address: %s", info.gameName.c_str(), unsigned(host_->port()),
                        net::localAddressGuess().c_str());
            const ImVec4 col = pm.state == net::PortMapState::Mapped      ? ImVec4(0.5f, 1, 0.5f, 1)
                               : pm.state == net::PortMapState::Discovering ? ImVec4(1, 0.85f, 0.4f, 1)
                                                                             : ImVec4(0.8f, 0.8f, 0.8f, 1);
            if (pm.state == net::PortMapState::Mapped)
                ImGui::TextColored(col, "Router: forwarded. Players on the internet connect to %s:%u", pm.externalAddress.c_str(),
                                   unsigned(pm.externalPort));
            else if (pm.state == net::PortMapState::Discovering)
                ImGui::TextColored(col, "Router: looking for a UPnP router...");
            ImGui::PushTextWrapPos(0);
            if (pm.state != net::PortMapState::Mapped && !pm.message.empty()) ImGui::TextColored(col, "%s", pm.message.c_str());
            ImGui::PopTextWrapPos();
            if (host_->lanDiscoveryRunning()) ImGui::TextDisabled("Players on your local network see this game in their Join list.");
        } else {
            const char* phase = client_->phase() == net::ClientPhase::Lobby ? "In the lobby" : client_->phase() == net::ClientPhase::Playing
                                                                                                    ? "Playing"
                                                                                                    : "Connecting...";
            ImGui::Text("%s - %s:%u", phase, client_->config().host.c_str(), unsigned(client_->config().port));
        }
        ImGui::TextDisabled("%s", info.options.simultaneous ? "Simultaneous turns: everyone gives orders, then the host runs the turn."
                                                             : "Turn-based: players take their turns one after another.");
        ImGui::Separator();

        // Slots.
        const uint32_t mine = host_ ? host_->localSlot() : client_->slot();
        if (ImGui::BeginTable("##slots", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY,
                              ImVec2(0, ctx.px(250)))) {
            ImGui::TableSetupColumn("Player");
            ImGui::TableSetupColumn("Race");
            ImGui::TableSetupColumn("Connected", ImGuiTableColumnFlags_WidthFixed, ctx.px(90));
            ImGui::TableSetupColumn("Ready", ImGuiTableColumnFlags_WidthFixed, ctx.px(70));
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, ctx.px(170));
            ImGui::TableHeadersRow();
            for (const net::LobbySlot& slot : info.slots) {
                ImGui::PushID(int(slot.id));
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                const std::string who = slot.kind == net::SlotKind::Computer ? "Computer" : slot.open() ? "(open)" : slot.player;
                if (slot.id == mine) ImGui::TextColored(ImVec4(1, 1, 0.6f, 1), "%s (you)", who.c_str());
                else ImGui::TextUnformatted(who.c_str());
                ImGui::TableNextColumn();
                const auto* preset = game::findPreset(*ctx.rules, slot.setup.preset);
                ImGui::TextUnformatted(preset ? preset->name.c_str() : slot.setup.customRace ? slot.setup.customRace->name.c_str() : "-");
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(slot.kind == net::SlotKind::Computer || slot.connected || slot.local ? "yes" : "no");
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(slot.kind == net::SlotKind::Computer || slot.ready ? "yes" : "-");
                ImGui::TableNextColumn();
                if (host_ && slot.id != mine) {
                    if (slot.kind == net::SlotKind::Computer) {
                        if (ImGui::SmallButton("Remove")) (void)host_->removeSlot(slot.id);
                    } else if (!slot.open()) {
                        if (ImGui::SmallButton("Kick")) (void)host_->kick(slot.id, "Removed by the host.");
                    }
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }

        // Own empire and readiness.
        const net::LobbySlot* me = info.slot(mine);
        const int before = race_;
        racePicker(ctx);
        if (race_ != before) {
            if (host_) (void)host_->setSlotSetup(mine, mySetup(ctx));
            else client_->submitSetup(mySetup(ctx));
        }
        const bool ready = me && me->ready;
        if (ImGui::Button(ready ? "Not Ready" : "Ready", ctx.size({150, 30}))) {
            if (host_) (void)host_->setLocalReady(!ready);
            else client_->setReady(!ready);
        }
        if (host_) {
            ImGui::SameLine();
            if (ImGui::Button("Add Computer", ctx.size({150, 30}))) {
                game::EmpireSetup ai;
                ai.kind = game::PlayerKind::Computer;
                if (!presets_.empty())
                    ai.preset = ctx.rules->racePresets()[presets_[size_t(info.slots.size() % presets_.size())]].folder;
                if (auto r = host_->addComputerEmpire(ai); !r) error_ = r.error();
            }
            ImGui::SameLine();
            const std::string problem = host_->startProblem(false);
            ImGui::BeginDisabled(!problem.empty() && host_->startProblem(true).size() > 0);
            if (ImGui::Button(problem.empty() ? "Start Game" : "Start Anyway", ctx.size({150, 30}))) {
                if (auto r = host_->startGame(!problem.empty()); !r) error_ = r.error();
            }
            ImGui::EndDisabled();
            if (!problem.empty()) {
                ImGui::SameLine();
                ImGui::TextDisabled("%s", problem.c_str());
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Leave", ctx.size({120, 30}))) {
            leave();
            return;
        }

        // Chat and log.
        ImGui::SeparatorText("Chat");
        ImGui::BeginChild("##log", ImVec2(0, -ctx.px(40)), ImGuiChildFlags_Borders);
        for (const std::string& line : log_.lines()) ImGui::TextWrapped("%s", line.c_str());
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4) ImGui::SetScrollHereY(1.0f);
        ImGui::EndChild();
        ImGui::SetNextItemWidth(-ctx.px(90));
        char buffer[512] = {};
        std::snprintf(buffer, sizeof buffer, "%s", chat_.c_str());
        const bool send = ImGui::InputText("##chat", buffer, sizeof buffer, ImGuiInputTextFlags_EnterReturnsTrue);
        chat_ = buffer;
        ImGui::SameLine();
        if ((ImGui::Button("Send", ctx.size({80, 26})) || send) && !chat_.empty()) {
            if (host_) {
                host_->chat(chat_);
                log_.add(std::format("[chat] {}: {}", name_, chat_));
            } else {
                client_->chat(chat_);
            }
            chat_.clear();
            ImGui::SetKeyboardFocusHere(-1);
        }
    }

    std::string automation_;
    Mode mode_ = Mode::Choose;
    std::vector<size_t> presets_;
    int race_ = 0;
    std::string name_ = "Player";
    std::string password_;
    std::string gameName_ = "OpenSE4 game";
    std::string address_ = "127.0.0.1";
    std::string joinPassword_;
    int port_ = net::kDefaultPort;
    int humans_ = 2;
    int computers_ = 2;
    int quadrantSize_ = 1;  // Medium, the default
    int timeout_ = 0;
    int turnStyle_ = 0;     // 0 simultaneous, 1 turn-based
    bool upnp_ = true;
    std::string chat_;
    std::string error_;
    bool setupSent_ = false;
    bool autoReady_ = false;  // automation: ready as soon as we joined
    NetLog log_;
    std::shared_ptr<const game::Rules> rules_;
    std::unique_ptr<net::HostSession> host_;
    std::unique_ptr<net::ClientSession> client_;
    net::DiscoveryBrowser browser_;
};

} // namespace

std::unique_ptr<FrontScreen> makeMultiplayerScreen(std::string_view automation) { return std::make_unique<MultiplayerScreen>(automation); }

} // namespace opense4::client::classic
