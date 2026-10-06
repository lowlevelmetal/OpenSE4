// Multiplayer front end (docs/spec/06 §1.7, docs/MULTIPLAYER.md): host a
// network game from the client (with UPnP port mapping) or join one, then a
// lobby with the player slots, empire choice, ready flags and chat. When the
// host starts the game, the running session hands its network connection to
// the game (net_transport.hpp).

#include "client/classic/frontend.hpp"
#include "client/classic/screens/list_widgets.hpp"
#include "client/classic/net_transport.hpp"
#include "client/classic/screens/setup_model.hpp"
#include "client/classic/settings.hpp"
#include "client/script/items.hpp"
#include "game/redact.hpp"
#include "game/serialize.hpp"
#include "net/auth.hpp"
#include "net/discovery.hpp"
#include "net/secure.hpp"
#include "net/socket.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <utility>

namespace opense4::client::classic {

namespace {

namespace crypto = net::crypto;

enum class Mode { Choose, Host, Join, Lobby };

// What the player typed, kept while the front end shows other screens (the
// Mods window after a refusal for mods): the screen is made afresh then.
struct Form {
    int race = 0;
    std::string name = "Player";
    std::string gameName = "OpenSE4 game";
    std::string address = "127.0.0.1";
    int port = net::kDefaultPort;
    int humans = 2;
    int computers = 2;
    int quadrantSize = 1;   // Medium, the default
    int timeout = 0;
    int turnStyle = 0;      // 0 simultaneous, 1 turn-based
    bool upnp = true;
    // Who plays the computer empires, and whether they see everything (when
    // the game's mods offer computer players, docs/sdk/ai-protocol.md §1).
    game::Controller aiPlayer;
    bool aiSeesEverything = false;
};
Form& lastForm() {
    static Form form;
    return form;
}

// "Mods: a 1.0 (changes the game), b 2.0 (pictures and sounds)": the host's, in the lobby.
std::string modsLineText(std::span<const ruleset::ModRecord> mods) {
    if (mods.empty()) return "Mods: none";
    std::string out = "Mods: ";
    for (size_t i = 0; i < mods.size(); ++i)
        out += std::format("{}{} {} ({})", i ? ", " : "", mods[i].id, mods[i].version, mods[i].affectsGame ? "changes the game" : "pictures and sounds");
    return out;
}

// A computer player's choice: the classic AI or a player of the game's mods,
// with its description under the pointer. True when it changed.
bool playerCombo(const game::Rules& r, const char* label, std::string_view preview, game::Controller& player, float width) {
    bool changed = false;
    // Input scripts find the box by its label (Dear ImGui's hooks leave combo boxes out).
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const float w = width < 0 ? ImGui::GetContentRegionAvail().x : width;
    script::reportItem(label, at, ImVec2(at.x + w, at.y + ImGui::GetFrameHeight()));
    ImGui::SetNextItemWidth(width);
    const std::string shown(preview);
    if (ImGui::BeginCombo(label, shown.c_str())) {
        if (ImGui::Selectable("Classic AI", player.builtin()) && !player.builtin()) {
            player = game::Controller{};
            changed = true;
        }
        for (const setup::ComputerPlayerChoice& c : setup::computerPlayerChoices(r)) {
            const std::string name = std::format("{} ({})", c.name, c.modName);
            if (ImGui::Selectable(name.c_str(), player == c.controller) && player != c.controller) {
                player = c.controller;
                changed = true;
            }
            if (ImGui::IsItemHovered() && !c.description.empty()) ImGui::SetTooltip("%s", c.description.c_str());
        }
        ImGui::EndCombo();
    }
    return changed;
}

// A line of text input scripts can find (wait-for item:"...").
void scriptLine(const std::string& text, const ImVec4* color = nullptr) {
    if (color) ImGui::TextColored(*color, "%s", text.c_str());
    else ImGui::TextDisabled("%s", text.c_str());
    script::reportItem(text, ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
}

void textField(const char* label, std::string& value, float width, ImGuiInputTextFlags flags = 0) {
    char buffer[256] = {};
    std::snprintf(buffer, sizeof buffer, "%s", value.c_str());
    ImGui::SetNextItemWidth(width);
    if (ImGui::InputText(label, buffer, sizeof buffer, flags)) value = buffer;
}

class MultiplayerScreen final : public FrontScreen {
public:
    explicit MultiplayerScreen(std::string_view automation) : automation_(automation) {
        const Form& f = lastForm();
        race_ = f.race;
        name_ = f.name;
        gameName_ = f.gameName;
        address_ = f.address;
        port_ = f.port;
        humans_ = f.humans;
        computers_ = f.computers;
        quadrantSize_ = f.quadrantSize;
        timeout_ = f.timeout;
        turnStyle_ = f.turnStyle;
        upnp_ = f.upnp;
        aiPlayer_ = f.aiPlayer;
        aiSeesEverything_ = f.aiSeesEverything;
    }
    ~MultiplayerScreen() override {
        lastForm() = Form{race_,     name_,      gameName_, address_, port_, humans_, computers_, quadrantSize_, timeout_, turnStyle_, upnp_,
                          aiPlayer_, aiSeesEverything_};
    }

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

        // Centred on the frame, and inside it at 800x600 too; our own text takes the Text size setting.
        const Vec2 size{std::min(900.0f, frameW() - 24.0f), std::min(668.0f, frameH() - 24.0f)};
        ImGui::SetNextWindowPos(ctx.at({std::floor((frameW() - size.x) * 0.5f), std::floor((frameH() - size.y) * 0.5f)}));
        ImGui::SetNextWindowSize(ctx.size(size));
        ImGui::PushFont(ctx.fonts.regular, ctx.painter().textPx(kTextSize));
        ImGui::Begin(mode_ == Mode::Lobby ? "Multiplayer Lobby" : "Multiplayer", nullptr,
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
        switch (mode_) {
            case Mode::Choose: chooser(ctx); break;
            case Mode::Host: hostForm(ctx); break;
            case Mode::Join: joinForm(ctx); break;
            case Mode::Lobby: lobby(ctx); break;
        }
        if (!error_.empty() && !(mode_ == Mode::Lobby && client_ && client_->refusedForMods())) {
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextColored(ImVec4(1, 0.5f, 0.4f, 1), "%s", error_.c_str());
            ImGui::PopTextWrapPos();
        }
        ImGui::End();
        ImGui::PopFont();
    }

private:
    // ---- Pages ------------------------------------------------------------------------------

    void chooser(MenuContext& ctx) {
        ImGui::TextWrapped("Play with other people over the network, with simultaneous turns or one player after another. One "
                           "player hosts; the others join with the host's address. The host's computer runs the game.");
        ImGui::Spacing();
        // Four buttons sharing the width (800x600 and larger text too).
        const ImVec2 button(std::min(ctx.px(200), (ImGui::GetContentRegionAvail().x - 3 * ImGui::GetStyle().ItemSpacing.x) / 4),
                            std::max(ctx.px(40), ImGui::GetFrameHeight()));
        if (ImGui::Button("Host a Game", button)) mode_ = Mode::Host;
        ImGui::SameLine();
        if (ImGui::Button("Join a Game", button)) mode_ = Mode::Join;
        ImGui::SameLine();
        if (ImGui::Button("Play by E-mail", button)) ctx.go(FrontId::Pbem);
        ImGui::SameLine();
        if (ImGui::Button("Back", button)) ctx.go(FrontId::Intro);
        ImGui::Spacing();
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("To host a game without a player on the host machine, or to host a play-by-e-mail game, use opense4-server.");
        ImGui::PopTextWrapPos();
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
        // OpenSE4's own, when the game's mods offer computer players: who plays
        // the computer empires (each can be changed in the lobby), and their view.
        if (offersPlayers(ctx)) {
            playerCombo(*ctx.rules, "Computer empires are played by", setup::computerPlayerName(*ctx.rules, aiPlayer_), aiPlayer_, w);
            ImGui::Checkbox("Computer players see everything", &aiSeesEverything_);
        }
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
        if (beginListTable(ctx.painter(), "##lan", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH, ImVec2(0, ctx.px(140)),
                           kListLineStep)) {
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
                if (g.protocol != net::kProtocolVersion) ImGui::TextColored(ImVec4(1, 0.6f, 0.4f, 1), "Other OpenSE4 version");
                else if (g.dataSet != mine) ImGui::TextColored(ImVec4(1, 0.6f, 0.4f, 1), "Different game data");
                else if (g.password) ImGui::TextUnformatted("Password needed");
                ImGui::PopID();
            }
            endListTable(ctx.painter());
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
        cfg.localPlayer = net::LocalPlayer{name_, password_, mySetup(ctx)};
        // The galaxy's seed from the cryptographic random source: players
        // must not guess it (the views they get leave it out). A seed the
        // player gave (--seed) is used as given.
        cfg.setup.seed = ctx.seedGiven ? ctx.seed : net::randomId();
        cfg.setup.options.systemCount = 0;  // rolled from the quadrant size
        cfg.setup.options.quadrantSize = quadrantSize_;
        cfg.setup.options.simultaneous = turnStyle_ == 0;
        cfg.setup.options.aiSeesEverything = aiSeesEverything_ && offersPlayers(ctx);
        cfg.joinPassword = joinPassword_;
        cfg.turnTimeoutSeconds = timeout_;
        cfg.upnp.enabled = upnp_ && net::PortMapper::supported();
        // This computer's identity as a host: players' games remember it.
        if (auto key = net::secure::loadOrCreateHostKey(userDataDir() / net::secure::kHostKeyFileName)) {
            cfg.hostKey = key->keys.network;
            if (!key->warning.empty()) log_.add(key->warning);
        } else {
            log_.add(key.error() + " Players cannot remember this host from one game to the next.");
        }
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
            if (offersPlayers(ctx)) ai.controller = aiPlayer_;
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
        cfg.password = password_;
        cfg.joinPassword = joinPassword_;
        cfg.dataSet = game::dataSetIdentity(*ctx.rules);
        cfg.mods.assign(ctx.rules->mods().begin(), ctx.rules->mods().end());
        // The host's key as trusted before (none: trusted on this first connection).
        cfg.hostKey = knownHosts().find(address_, static_cast<uint16_t>(port_));
        // Agreed by the player in the prompt below, for this connection only.
        cfg.sendOldPassword = std::exchange(sendOldPassword_, false);
        prompt_ = Prompt::None;
        confirming_ = false;
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

    static net::secure::KnownHosts knownHosts() { return net::secure::KnownHosts(userDataDir() / net::secure::kKnownHostsFileName); }

    // After joining: remember the host's key, or say it is the one remembered.
    void noteHostKey() {
        const std::optional<crypto::Key>& seen = client_->seenHostKey();
        if (!seen) return;
        const std::string fp = crypto::fingerprint(*seen);
        auto known = knownHosts();
        const auto port = static_cast<uint16_t>(port_);
        if (known.find(address_, port) == seen) {
            log_.add(std::format("The host's key is the one this computer knows: {}", fp));
        } else if (auto r = known.remember(address_, port, *seen); r) {
            log_.add(std::format("First connection to this host: its key {} is now remembered. Check it against the one the host sees.", fp));
        } else {
            log_.add("Could not remember the host's key: " + r.error());
        }
        hostKey_ = fp;
    }

    // The questions about the host, each with a second confirmation.
    void hostPrompt(MenuContext& ctx) {
        const ImVec4 warn(1, 0.5f, 0.4f, 1);
        const auto port = static_cast<uint16_t>(port_);
        const std::optional<crypto::Key> known = knownHosts().find(address_, port);
        const std::string shown = crypto::fingerprint(promptKey_);
        ImGui::PushTextWrapPos(0);
        if (prompt_ == Prompt::KeyChanged) {
            ImGui::TextColored(warn, "This computer trusts the key %s for this host, but it now shows %s.",
                               known ? crypto::fingerprint(*known).c_str() : "(none)", shown.c_str());
            ImGui::TextWrapped("Trust the new key only if the host says it made one (a new computer, or a deleted key file), and the "
                               "fingerprint the host sees is the new one. Otherwise someone may be in between.");
        } else {
            ImGui::TextColored(warn, "This game was saved by OpenSE4 0.6, and your password is still in that version's form.");
            ImGui::TextWrapped("To move it to the new form, your game must show the host the old form once. Do it only with a host you "
                               "trust: compare its key %s with the one the host sees (its lobby or log). Anyone who recorded your games "
                               "of OpenSE4 0.6 knows the old form: if that worries you, ask the host to reset your password instead.",
                               shown.c_str());
        }
        ImGui::PopTextWrapPos();
        if (!confirming_) {
            const char* label = prompt_ == Prompt::KeyChanged ? "Trust the New Key..." : "The Key Matches: Continue...";
            if (ImGui::Button(label, ctx.size({300, 30}))) confirming_ = true;
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ctx.size({120, 30}))) prompt_ = Prompt::None;
            return;
        }
        ImGui::TextColored(warn, "%s", prompt_ == Prompt::KeyChanged ? std::format("Really trust {} for {}:{}?", shown, address_, port_).c_str()
                                                                     : std::format("Really show your old password's form to the host with the "
                                                                                   "key {}, once?",
                                                                                   shown)
                                                                           .c_str());
        if (ImGui::Button("Yes, and Connect", ctx.size({200, 30}))) {
            if (auto r = knownHosts().remember(address_, port, promptKey_); !r) {
                error_ = r.error();
                return;
            }
            sendOldPassword_ = prompt_ == Prompt::OldPassword;
            connect(ctx);
            return;
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ctx.size({120, 30}))) {
            prompt_ = Prompt::None;
            confirming_ = false;
        }
    }

    // Refused for mods: each difference on a line of its own, and the way to
    // the Mods window (true when it was chosen).
    bool modsRefusal(MenuContext& ctx) {
        const ImVec4 warn(1, 0.6f, 0.4f, 1);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(warn, "The host plays with other mods than yours:");
        std::string_view rest = error_;
        if (const size_t colon = rest.find(": "); colon != std::string_view::npos) rest.remove_prefix(colon + 2);
        if (rest.ends_with('.')) rest.remove_suffix(1);
        while (!rest.empty()) {
            const size_t end = rest.find("; ");
            const std::string line(rest.substr(0, end));
            ImGui::Bullet();
            ImGui::TextColored(warn, "%s", line.c_str());
            script::reportText(line, ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
            if (end == std::string_view::npos) break;
            rest.remove_prefix(end + 2);
        }
        ImGui::TextWrapped("Choose the same mods in the Mods window (pictures and sounds may differ), then join again.");
        ImGui::PopTextWrapPos();
        return ImGui::Button("Mods", ctx.size({150, 30}));
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
            if (e.type == net::EventType::Joined && client_) noteHostKey();
            if (e.type == net::EventType::Rejected && client_ && client_->seenHostKey()) {
                promptKey_ = *client_->seenHostKey();
                confirming_ = false;
                if (client_->hostKeyChanged()) prompt_ = Prompt::KeyChanged;
                else if (client_->hostAskedOldPassword() || client_->hostKeyUnconfirmed()) prompt_ = Prompt::OldPassword;
            }
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
            // The host set the game up: a new network game (always simultaneous
            // between machines) switches the movement lines on, as a new
            // simultaneous game does (spec 06 §1.9); joining one does not.
            newGameStarted(host_->state()->options.simultaneous);
            const game::EmpireId me = host_->localEmpire();
            game::GameState state = game::redactForEmpire(*rules_, *host_->state(), me);
            auto session = std::make_unique<ClassicSession>(rules_, std::move(state), me, SessionKind::NetworkClient);
            session->setMultiplayerGame(host_->gameId());
            session->setTransport(std::make_unique<HostTransport>(rules_, std::move(host_)));
            ctx.startGame(std::move(session));
        } else if (client_ && client_->state()) {
            game::GameState state = *client_->state();
            const game::EmpireId me = client_->empire();
            auto session = std::make_unique<ClassicSession>(rules_, std::move(state), me, SessionKind::NetworkClient);
            session->setMultiplayerGame(client_->gameId());
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
            ImGui::TextDisabled("Host key: %s (players see it when they join)", host_->hostFingerprint().c_str());
        } else {
            const char* phase = client_->phase() == net::ClientPhase::Lobby     ? "In the lobby"
                                : client_->phase() == net::ClientPhase::Playing   ? "Playing"
                                : client_->phase() == net::ClientPhase::Disconnected ? "Not connected"
                                                                                     : "Connecting...";
            ImGui::Text("%s - %s:%u", phase, client_->config().host.c_str(), unsigned(client_->config().port));
            if (!hostKey_.empty()) ImGui::TextDisabled("Encrypted. Host key: %s", hostKey_.c_str());
            if (prompt_ != Prompt::None) hostPrompt(ctx);
        }
        // What the host set up, once it said (a refused player never hears it):
        // the turn style, and the mods every player needs the same of (those that change the game).
        if (host_ || client_->phase() == net::ClientPhase::Lobby || client_->phase() == net::ClientPhase::Playing) {
            ImGui::TextDisabled("%s", info.options.simultaneous ? "Simultaneous turns: everyone gives orders, then the host runs the turn."
                                                                 : "Turn-based: players take their turns one after another.");
            const std::string mods = modsLineText(info.mods);
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextDisabled("%s", mods.c_str());
            ImGui::PopTextWrapPos();
            script::reportText(mods, ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
            // The computer players' view of the game (the host chooses), when
            // any of them can be a script player.
            if (showsPlayers(ctx, info)) {
                if (host_) {
                    bool all = info.options.aiSeesEverything;
                    if (ImGui::Checkbox("Computer players see everything", &all)) {
                        game::GameOptions o = info.options;
                        o.aiSeesEverything = all;
                        if (auto r = host_->setOptions(o); !r) error_ = r.error();
                        else aiSeesEverything_ = all;
                    }
                } else {
                    scriptLine(info.options.aiSeesEverything ? "Computer players see everything: the whole game, not only what their empires know."
                                                             : "Computer players see what their empires know.");
                }
            }
        }
        if (client_ && client_->refusedForMods() && modsRefusal(ctx)) {
            // To the Mods window, and back to the join form (kept) afterwards; nothing of the lobby is drawn after this.
            ctx.goTo(makeModsScreen([] { return makeMultiplayerScreen("browse"); }));
            leave();
            return;
        }
        ImGui::Separator();

        // Slots.
        const uint32_t mine = host_ ? host_->localSlot() : client_->slot();
        if (beginListTable(ctx.painter(), "##slots", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH, ImVec2(0, ctx.px(250)),
                           kListLineStep)) {
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
                const bool computer = slot.kind == net::SlotKind::Computer;
                const std::string who = computer ? "Computer" : slot.open() ? "(open)" : slot.player;
                if (computer && showsPlayers(ctx, info)) {
                    // Who plays it: the host chooses, the others see it.
                    const std::string played = "Computer: " + setup::computerPlayerName(*ctx.rules, slot.setup.controller);
                    if (host_) {
                        game::Controller player = slot.setup.controller;
                        if (playerCombo(*ctx.rules, "##slotplayer", played, player, -FLT_MIN)) {
                            game::EmpireSetup changed = slot.setup;
                            changed.controller = player;
                            if (auto r = host_->setSlotSetup(slot.id, changed); !r) error_ = r.error();
                        }
                        script::reportItem(played);
                    } else {
                        ImGui::TextUnformatted(played.c_str());
                        script::reportItem(played, ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
                    }
                } else if (slot.id == mine) {
                    ImGui::TextColored(ImVec4(1, 1, 0.6f, 1), "%s (you)", who.c_str());
                } else {
                    ImGui::TextUnformatted(who.c_str());
                }
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
            endListTable(ctx.painter());
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
                if (offersPlayers(ctx)) ai.controller = aiPlayer_;
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

    // Whether the game's mods offer computer players (worked out once).
    bool offersPlayers(MenuContext& ctx) {
        if (!offersKnown_) {
            offers_ = ctx.rules && !setup::computerPlayerChoices(*ctx.rules).empty();
            offersKnown_ = true;
        }
        return offers_;
    }
    // The lobby shows who plays each computer empire when they may be script
    // players: the mods offer some, or a slot names one.
    bool showsPlayers(MenuContext& ctx, const net::LobbyInfo& info) {
        return offersPlayers(ctx) || std::any_of(info.slots.begin(), info.slots.end(), [](const net::LobbySlot& s) { return !s.setup.controller.builtin(); });
    }

    std::string automation_;
    Mode mode_ = Mode::Choose;
    std::vector<size_t> presets_;
    int race_ = 0;
    std::string name_;
    std::string password_;
    std::string gameName_;
    std::string address_;
    std::string joinPassword_;
    int port_ = net::kDefaultPort;
    int humans_ = 2;
    int computers_ = 2;
    int quadrantSize_ = 1;  // Medium, the default
    int timeout_ = 0;
    int turnStyle_ = 0;     // 0 simultaneous, 1 turn-based
    bool upnp_ = true;
    game::Controller aiPlayer_;
    bool aiSeesEverything_ = false;
    bool offers_ = false, offersKnown_ = false;
    std::string chat_;
    std::string error_;
    bool setupSent_ = false;
    bool autoReady_ = false;  // automation: ready as soon as we joined
    std::string hostKey_;     // fingerprint of the host we joined
    // A question about the host after a refusal: its key changed, or it asks
    // for the OpenSE4 0.6 form of the password. Each needs a second click.
    enum class Prompt { None, KeyChanged, OldPassword };
    Prompt prompt_ = Prompt::None;
    crypto::Key promptKey_{};       // the key the host showed
    bool confirming_ = false;       // the first click was made: ask once more
    bool sendOldPassword_ = false;  // the next connection shows the old form (the player agreed)
    NetLog log_;
    std::shared_ptr<const game::Rules> rules_;
    std::unique_ptr<net::HostSession> host_;
    std::unique_ptr<net::ClientSession> client_;
    net::DiscoveryBrowser browser_;
};

} // namespace

std::unique_ptr<FrontScreen> makeMultiplayerScreen(std::string_view automation) { return std::make_unique<MultiplayerScreen>(automation); }

} // namespace opense4::client::classic
