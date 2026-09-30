#include "client/classic/net_transport.hpp"

#include "core/log.hpp"

#include <format>

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
        case EventType::GameOver: return true;
        default: return false;
    }
}

} // namespace

void NetLog::add(std::string line) {
    lines_.push_back(std::move(line));
    while (lines_.size() > kLogLines) lines_.pop_front();
}

std::string waitingFor(const net::TurnStatus& t) {
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

std::optional<game::GameState> HostTransport::pollState() {
    std::optional<game::GameState> fresh;
    for (const net::Event& e : host_->poll(0)) {
        if (worthShowing(e.type)) log_.add(net::describe(e));
        if (e.type == net::EventType::NewTurn && host_->state()) fresh = *host_->state();
    }
    return fresh;
}

std::string HostTransport::status() const {
    const auto& t = host_->turnStatus();
    if (t.processing) return "Processing the turn...";
    std::string s = waitingFor(t);
    if (t.secondsLeft >= 0) s += std::format(" ({}s left)", t.secondsLeft);
    return s;
}

void HostTransport::chat(std::string_view text) {
    host_->chat(text);
    log_.add(std::format("[chat] you: {}", text));
}

// ---- Client ---------------------------------------------------------------------------------

ClientTransport::ClientTransport(std::unique_ptr<net::ClientSession> client) : client_(std::move(client)) {}

ClientTransport::~ClientTransport() {
    if (client_) client_->disconnect();
}

void ClientTransport::submitOrders(const game::EmpireOrders& orders) {
    if (auto r = client_->submitOrders(orders); !r) log_.add("Orders not sent: " + r.error());
}

std::optional<game::GameState> ClientTransport::pollState() {
    std::optional<game::GameState> fresh;
    for (const net::Event& e : client_->poll(0)) {
        if (worthShowing(e.type)) log_.add(net::describe(e));
        if (e.type == net::EventType::NewTurn && client_->state()) fresh = *client_->state();
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
    const auto& t = client_->turnStatus();
    if (t.processing) return "The host is processing the turn...";
    std::string s = waitingFor(t);
    if (t.secondsLeft >= 0) s += std::format(" ({}s left)", t.secondsLeft);
    return s;
}

} // namespace opense4::client::classic
