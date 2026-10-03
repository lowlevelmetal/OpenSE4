#include "client/classic/net_transport.hpp"

#include "core/log.hpp"
#include "game/redact.hpp"

#include <format>
#include <utility>

namespace opense4::client::classic {

namespace {

constexpr size_t kLogLines = 200;

// Events worth showing to the player in the game (chat, arrivals, problems).
bool worthShowing(net::EventType t) {
    using net::EventType;
    switch (t) {
        case EventType::Chat:
        case EventType::Warning:
        case EventType::Error:
        case EventType::PlayerJoined:
        case EventType::PlayerReconnected:
        case EventType::PlayerLeft:
        case EventType::Disconnected:
        case EventType::OrdersRejected:
        case EventType::PortMapping:
        case EventType::PlayerTurn:
        case EventType::Desync:
        case EventType::GameOver: return true;
        default: return false;
    }
}

} // namespace

void NetLog::add(std::string line) {
    lines_.push_back(std::move(line));
    while (lines_.size() > kLogLines) lines_.pop_front();
}

std::string waitingFor(const net::TurnStatus& t, game::EmpireId me) {
    if (t.turnBased) {
        if (t.processing) return "The other empires are moving...";
        const net::EmpireTurnStatus* a = t.activeStatus();
        if (!a) return "Waiting: every player is played by the computer";
        if (a->empire == me) return {};
        return std::format("It is {}'s turn{}", a->player.empty() ? a->empireName : a->player, a->connected ? "" : " (away)");
    }
    std::string names;
    for (const auto& e : t.empires)
        if (e.awaited()) names += (names.empty() ? "" : ", ") + (e.player.empty() ? e.empireName : e.player);
    return names.empty() ? std::string{} : "Waiting for: " + names;
}

// ---- Host -----------------------------------------------------------------------------------

HostTransport::HostTransport(std::shared_ptr<const game::Rules> rules, std::unique_ptr<net::HostSession> host)
    : rules_(std::move(rules)), host_(std::move(host)) {}

HostTransport::~HostTransport() {
    if (host_) host_->stop();
}

void HostTransport::submitOrders(const game::EmpireOrders& orders) {
    if (auto r = host_->submitOrders(orders); !r) log_.add("Orders not accepted: " + r.error());
}

void HostTransport::playCommand(const game::Command& c) {
    auto r = host_->playCommands(host_->localEmpire(), {c});
    if (!r) log_.add("Not carried out: " + r.error());
    else
        for (const auto& [empire, why] : r->rejected)
            if (empire == host_->localEmpire()) log_.add("Refused: " + why);
    fresh_ = true;
}

void HostTransport::endPlayerTurn() {
    if (auto r = host_->endPlayerTurn(host_->localEmpire()); !r) log_.add("Could not end the turn: " + r.error());
    fresh_ = true;
}

std::optional<game::GameState> HostTransport::pollState() {
    bool fresh = std::exchange(fresh_, false);
    for (const net::Event& e : host_->poll(0)) {
        if (worthShowing(e.type)) log_.add(net::describe(e));
        if (e.type == net::EventType::Desync) log::warn("Network game: {}", e.text);
        fresh = fresh || e.type == net::EventType::NewTurn || e.type == net::EventType::PlayerTurn ||
                (e.type == net::EventType::StateUpdated && e.empire == host_->localEmpire());
    }
    // The hosting player sees the same fog of war as everyone else.
    if (fresh && host_->state()) return game::redactForEmpire(*rules_, *host_->state(), host_->localEmpire());
    return std::nullopt;
}

std::string HostTransport::status() const {
    if (host_->phase() == net::HostPhase::GameOver) return "The game is over.";
    const auto& t = host_->turnStatus();
    if (t.processing && !t.turnBased) return "Processing the turn...";
    std::string s = waitingFor(t, host_->localEmpire());
    if (t.secondsLeft >= 0) s += std::format(" ({}s left)", t.secondsLeft);
    return s;
}

void HostTransport::chat(std::string_view text) {
    host_->chat(text);
    log_.add(std::format("[chat] you: {}", text));
}

// ---- Client ---------------------------------------------------------------------------------

ClientTransport::ClientTransport(std::unique_ptr<net::ClientSession> client) : client_(std::move(client)) {
    if (client_->state()) handedTurn_ = client_->state()->turn;
}

ClientTransport::~ClientTransport() {
    if (client_) client_->disconnect();
}

void ClientTransport::submitOrders(const game::EmpireOrders& orders) {
    if (auto r = client_->submitOrders(orders); !r) log_.add("Orders not sent: " + r.error());
}

void ClientTransport::playCommand(const game::Command& c) {
    if (auto r = client_->play(c); !r) log_.add("Not sent: " + r.error());
}

void ClientTransport::endPlayerTurn() {
    if (auto r = client_->endTurn(); !r) log_.add("Could not end the turn: " + r.error());
}

std::optional<game::GameState> ClientTransport::pollState() {
    std::optional<game::GameState> fresh;
    for (const net::Event& e : client_->poll(0)) {
        // The host's notices to us (refused commands, Reset Passwords answers) show too.
        if (worthShowing(e.type) || e.type == net::EventType::Info) log_.add(net::describe(e));
        // A desync is never silent: the in-game log above, and opense4.log.
        if (e.type == net::EventType::Desync) log::warn("Network game: {}", e.text);
        // Back after a reconnect: the host resent the game (turn-based: always
        // news; simultaneous: when a turn was processed meanwhile).
        const bool rejoined = e.type == net::EventType::GameStarted && client_->state() &&
                              (client_->turnBased() || client_->state()->turn != handedTurn_);
        const bool newState = e.type == net::EventType::NewTurn || e.type == net::EventType::StateUpdated || rejoined;
        if (newState && client_->state()) {
            fresh = *client_->state();
            handedTurn_ = fresh->turn;
        }
        if (e.type == net::EventType::CommandsDone && !e.text.empty()) log_.add("Refused: " + e.text);
        if (e.type == net::EventType::Rejected) rejected_ = true;
    }
    // Lost the host: try to get back in every few seconds (the host resends
    // the current turn). Not after the host refused us.
    if (client_->phase() == net::ClientPhase::Disconnected && !rejected_) {
        const auto now = std::chrono::steady_clock::now();
        if (now - lastAttempt_ > std::chrono::seconds(5)) {
            lastAttempt_ = now;
            log_.add("Reconnecting to the host...");
            if (auto r = client_->connect(); !r) log_.add("Reconnect failed: " + r.error());
        }
    }
    return fresh;
}

std::string ClientTransport::status() const {
    if (client_->phase() != net::ClientPhase::Playing) return "Not connected to the host";
    if (client_->state() && client_->state()->gameOver) return "The game is over.";
    const auto& t = client_->turnStatus();
    if (t.processing && !t.turnBased) return "The host is processing the turn...";
    std::string s = waitingFor(t, client_->empire());
    if (t.secondsLeft >= 0) s += std::format(" ({}s left)", t.secondsLeft);
    return s;
}

} // namespace opense4::client::classic
