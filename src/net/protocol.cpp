#include "net/protocol.hpp"

#include <format>

#ifndef OPENSE4_VERSION
#define OPENSE4_VERSION "0.0.0"
#endif

namespace opense4::net {

std::string_view appVersion() { return "OpenSE4 " OPENSE4_VERSION; }

std::string_view displayName(EventType t) {
    switch (t) {
        case EventType::Info: return "info";
        case EventType::Warning: return "warning";
        case EventType::Error: return "error";
        case EventType::Listening: return "listening";
        case EventType::PortMapping: return "upnp";
        case EventType::Connected: return "connected";
        case EventType::Joined: return "joined";
        case EventType::Rejected: return "rejected";
        case EventType::Disconnected: return "disconnected";
        case EventType::PlayerJoined: return "player joined";
        case EventType::PlayerReconnected: return "player reconnected";
        case EventType::PlayerLeft: return "player left";
        case EventType::LobbyChanged: return "lobby";
        case EventType::Chat: return "chat";
        case EventType::GameStarted: return "game started";
        case EventType::TurnStatusChanged: return "turn status";
        case EventType::OrdersReceived: return "orders received";
        case EventType::OrdersAccepted: return "orders accepted";
        case EventType::OrdersRejected: return "orders rejected";
        case EventType::TurnProcessing: return "processing";
        case EventType::NewTurn: return "new turn";
        case EventType::GameOver: return "game over";
        case EventType::PlayerTurn: return "player turn";
        case EventType::StateUpdated: return "state updated";
        case EventType::CommandsDone: return "commands done";
        case EventType::Desync: return "desync";
    }
    return "?";
}

std::string describe(const Event& e) {
    const std::string tag = std::format("[{}]", displayName(e.type));
    switch (e.type) {
        case EventType::Chat: return std::format("{} {}: {}", tag, e.player, e.text);
        case EventType::PlayerJoined:
        case EventType::PlayerReconnected:
            return std::format("{} {} (slot {}){}{}", tag, e.player, e.slot, e.text.empty() ? "" : ": ", e.text);
        case EventType::PlayerLeft: return std::format("{} {}{}{}", tag, e.player, e.text.empty() ? "" : ": ", e.text);
        case EventType::OrdersReceived:
            return std::format("{} turn {} from {} (empire {}){}{}", tag, e.turn, e.player, e.empire.value, e.text.empty() ? "" : ": ", e.text);
        case EventType::PlayerTurn:
            if (!e.empire.valid()) return std::format("{} turn {}: nobody's turn (waiting)", tag, e.turn);
            return std::format("{} turn {}: {} (empire {}){}{}", tag, e.turn, e.player.empty() ? std::string("computer") : e.player,
                               e.empire.value, e.text.empty() ? "" : ", ", e.text);
        case EventType::CommandsDone:
            return std::format("{} request {}{}{}", tag, e.request, e.text.empty() ? "" : ": ", e.text);
        case EventType::Desync: return std::format("{} {}", tag, e.text);
        case EventType::GameStarted:
        case EventType::NewTurn:
        case EventType::TurnProcessing:
        case EventType::OrdersAccepted:
            return std::format("{} turn {}{}{}", tag, e.turn, e.text.empty() ? "" : ": ", e.text);
        default: return e.text.empty() ? tag : std::format("{} {}", tag, e.text);
    }
}

} // namespace opense4::net

namespace opense4::net::proto {

bool probeVersion(std::span<const uint8_t> payload, VersionProbe& out) {
    game::serial::Reader r(payload, kArchiveVersion);
    io(r, out);
    return r.ok();
}

std::string sanitize(std::string_view text, size_t maxLength) {
    std::string out;
    out.reserve(std::min(text.size(), maxLength));
    for (char c : text) {
        if (out.size() >= maxLength) break;
        const auto u = static_cast<unsigned char>(c);
        if (u < 0x20 || u == 0x7f) out.push_back(' ');  // no control characters in logs and UIs
        else out.push_back(c);
    }
    return out;
}

bool validPlayerName(std::string_view name) {
    if (name.empty() || name.size() > kMaxPlayerNameLength) return false;
    if (name.front() == ' ' || name.back() == ' ') return false;
    for (char c : name) {
        const auto u = static_cast<unsigned char>(c);
        if (u < 0x20 || u == 0x7f) return false;
    }
    return true;
}

} // namespace opense4::net::proto
