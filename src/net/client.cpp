#include "net/client.hpp"

#include "game/serialize.hpp"
#include "net/auth.hpp"
#include "net/connection.hpp"
#include "net/protocol.hpp"

#include <format>

namespace opense4::net {

using proto::MsgType;

struct ClientSession::Impl {
    std::optional<Connection> conn;
    Clock::time_point connectStarted;
};

ClientSession::ClientSession(ClientConfig config) : config_(std::move(config)), impl_(std::make_unique<Impl>()) {}

ClientSession::~ClientSession() {
    if (phase_ != ClientPhase::Disconnected) disconnect();
}

void ClientSession::emit(EventType type, std::string text, std::string player, uint32_t slot, game::EmpireId empire, uint32_t turn) {
    events_.push_back(Event{type, std::move(text), std::move(player), slot, empire, turn});
}

std::expected<void, std::string> ClientSession::connect() {
    if (phase_ != ClientPhase::Disconnected) closeConnection("reconnecting");
    if (!proto::validPlayerName(config_.playerName)) return std::unexpected(std::string("Choose a player name of 1 to 32 characters."));
    auto s = connectTcp(config_.host, config_.port);
    if (!s) return std::unexpected(s.error());
    impl_->conn.emplace(std::move(*s), config_.maxMessageBytes);
    impl_->connectStarted = Clock::now();
    phase_ = ClientPhase::Connecting;
    admin_ = false;
    slot_ = kNoSlot;
    ordersAccepted_ = false;
    return {};
}

void ClientSession::closeConnection(std::string reason, bool rejected) {
    if (impl_->conn) {
        impl_->conn->flush();
        impl_->conn->socket().shutdownSend();
        impl_->conn.reset();
    }
    const bool wasConnected = phase_ != ClientPhase::Disconnected;
    phase_ = ClientPhase::Disconnected;
    if (wasConnected) emit(rejected ? EventType::Rejected : EventType::Disconnected, std::move(reason));
}

void ClientSession::disconnect(std::string_view reason) {
    if (!impl_->conn || phase_ == ClientPhase::Disconnected) return;
    if (phase_ != ClientPhase::Connecting) impl_->conn->send(MsgType::Bye, proto::Bye{std::string(reason)});
    // Give the goodbye a moment to leave, then close.
    const auto until = Clock::now() + std::chrono::milliseconds(200);
    while (impl_->conn->wantsWrite() && !impl_->conn->failed() && Clock::now() < until) {
        PollItem item{impl_->conn->socket().native(), false, true};
        pollSockets(std::span(&item, 1), 50);
        impl_->conn->flush();
    }
    closeConnection(std::string(reason));
}

std::vector<Event> ClientSession::poll(int timeoutMs) {
    if (impl_->conn && phase_ != ClientPhase::Disconnected) {
        Connection& c = *impl_->conn;
        PollItem item{c.socket().native(), phase_ != ClientPhase::Connecting, phase_ == ClientPhase::Connecting || c.wantsWrite()};
        pollSockets(std::span(&item, 1), std::max(0, timeoutMs));
        const auto now = Clock::now();

        if (phase_ == ClientPhase::Connecting) {
            if (item.writable || item.failed) {
                if (std::string err = c.socket().connectError(); !err.empty()) {
                    closeConnection(std::format("Could not connect to {}:{}: {}", config_.host, config_.port, err));
                } else {
                    phase_ = ClientPhase::Handshaking;
                    proto::Hello h;
                    h.app = std::string(appVersion());
                    h.dataSet = config_.dataSet;
                    h.player = config_.playerName;
                    h.passwordHash = config_.passwordHash;
                    h.joinPasswordHash = config_.joinPasswordHash;
                    h.masterPasswordHash = config_.masterPasswordHash;
                    c.send(MsgType::Hello, h);
                    emit(EventType::Connected, std::format("{}:{}", config_.host, config_.port));
                }
            } else if (now - impl_->connectStarted > std::chrono::seconds(config_.connectTimeoutSeconds)) {
                closeConnection(std::format("Could not connect to {}:{}: no answer", config_.host, config_.port));
            }
        }

        if (impl_->conn && phase_ != ClientPhase::Connecting && phase_ != ClientPhase::Disconnected) {
            if (item.readable) c.receive();
            while (impl_->conn && phase_ != ClientPhase::Disconnected) {
                auto f = c.nextFrame();
                if (!f) break;
                handleFrame(static_cast<uint8_t>(f->type), f->payload);
            }
        }
        if (impl_->conn && phase_ != ClientPhase::Disconnected) {
            Connection& conn = *impl_->conn;
            if (conn.failed()) {
                closeConnection(conn.error());
            } else if (conn.peerClosed()) {
                closeConnection("The host closed the connection.");
            } else if (phase_ != ClientPhase::Connecting) {
                if (now - conn.lastReceive() > std::chrono::seconds(config_.timeoutSeconds))
                    closeConnection(std::format("The host stopped answering ({} seconds).", config_.timeoutSeconds));
                else if (!conn.wantsWrite() && now - conn.lastSend() > std::chrono::seconds(config_.keepaliveSeconds))
                    conn.send(MsgType::Ping, proto::Ping{randomId()});
            }
        }
        if (impl_->conn && phase_ != ClientPhase::Disconnected) {
            impl_->conn->flush();
            if (impl_->conn->failed()) closeConnection(impl_->conn->error());
        }
    }
    std::vector<Event> out;
    out.swap(events_);
    return out;
}

void ClientSession::handleFrame(uint8_t type, std::span<const uint8_t> payload) {
    std::string error;
    Connection& c = *impl_->conn;
    const auto msg = static_cast<MsgType>(type);
    if (phase_ == ClientPhase::Handshaking && msg != MsgType::Welcome && msg != MsgType::Reject && msg != MsgType::Bye &&
        msg != MsgType::Ping) {
        closeConnection("The host broke the protocol (no welcome).");
        return;
    }
    switch (msg) {
        case MsgType::Welcome: {
            proto::Welcome m;
            if (!proto::decode(payload, m, error)) break;
            admin_ = m.admin;
            slot_ = m.slot;
            gameName_ = m.gameName;
            gameId_ = m.gameId;
            phase_ = ClientPhase::Lobby;
            emit(EventType::Joined, std::format("{} ({}){}", m.gameName, m.app, m.admin ? ", admin" : ""), config_.playerName, m.slot);
            return;
        }
        case MsgType::Reject: {
            proto::Reject m;
            if (!proto::decode(payload, m, error)) break;
            closeConnection(m.text, true);
            return;
        }
        case MsgType::Lobby: {
            if (!proto::decode(payload, lobby_, error)) break;
            emit(EventType::LobbyChanged);
            return;
        }
        case MsgType::State: {
            proto::State m;
            if (!proto::decode(payload, m, error)) break;
            auto s = game::deserializeState(m.state);
            if (!s) {
                error = "unreadable game state: " + s.error();
                break;
            }
            const bool first = !state_ || phase_ != ClientPhase::Playing || m.gameStart;
            state_ = std::move(*s);
            empire_ = m.empire;
            phase_ = ClientPhase::Playing;
            ordersAccepted_ = false;
            emit(first ? EventType::GameStarted : EventType::NewTurn, {}, {}, kNoSlot, empire_, state_->turn);
            return;
        }
        case MsgType::TurnStatus: {
            TurnStatus t;
            if (!proto::decode(payload, t, error)) break;
            const bool startedProcessing = t.processing && !turnStatus_.processing;
            turnStatus_ = std::move(t);
            if (startedProcessing) emit(EventType::TurnProcessing, {}, {}, kNoSlot, {}, turnStatus_.turn);
            emit(EventType::TurnStatusChanged, {}, {}, kNoSlot, {}, turnStatus_.turn);
            return;
        }
        case MsgType::OrdersAck: {
            proto::OrdersAck m;
            if (!proto::decode(payload, m, error)) break;
            if (m.ok && state_ && m.turn == state_->turn) ordersAccepted_ = true;
            emit(m.ok ? EventType::OrdersAccepted : EventType::OrdersRejected, m.text, {}, kNoSlot, empire_, m.turn);
            return;
        }
        case MsgType::Chat: {
            proto::Chat m;
            if (!proto::decode(payload, m, error)) break;
            emit(EventType::Chat, proto::sanitize(m.text, kMaxChatLength), proto::sanitize(m.from, kMaxPlayerNameLength));
            return;
        }
        case MsgType::Notice: {
            proto::Notice m;
            if (!proto::decode(payload, m, error)) break;
            emit(EventType::Info, proto::sanitize(m.text, 1000));
            return;
        }
        case MsgType::Ping: {
            proto::Ping m;
            if (!proto::decode(payload, m, error)) break;
            c.send(MsgType::Pong, m);
            return;
        }
        case MsgType::Pong: return;
        case MsgType::Bye: {
            proto::Bye m;
            proto::decode(payload, m, error);
            closeConnection(m.reason.empty() ? std::string("The host closed the connection.") : proto::sanitize(m.reason, 500));
            return;
        }
        default: error = std::format("unknown message type {}", type); break;
    }
    closeConnection("Protocol error: " + error);
}

// ---- Requests -------------------------------------------------------------------------------------------

void ClientSession::submitSetup(const game::EmpireSetup& setup) {
    if (phase_ != ClientPhase::Lobby || !impl_->conn) return;
    impl_->conn->send(MsgType::SubmitSetup, proto::SubmitSetup{setup});
}

void ClientSession::setReady(bool ready) {
    if (phase_ != ClientPhase::Lobby || !impl_->conn) return;
    impl_->conn->send(MsgType::SetReady, proto::SetReady{ready});
}

std::expected<void, std::string> ClientSession::submitOrders(game::EmpireOrders orders) {
    if (phase_ != ClientPhase::Playing || !impl_->conn || !state_) return std::unexpected(std::string("Not in a game."));
    if (!orders.empire.valid()) orders.empire = empire_;
    if (orders.turn == 0) orders.turn = state_->turn;
    if (orders.empire != empire_) return std::unexpected(std::string("Those orders are for another empire."));
    if (orders.turn != state_->turn) return std::unexpected(std::format("The game is at turn {}, not {}.", state_->turn, orders.turn));
    proto::SubmitOrders m;
    m.turn = orders.turn;
    m.orders = game::serializeOrders(orders);
    ordersAccepted_ = false;
    impl_->conn->send(MsgType::SubmitOrders, m);
    return {};
}

void ClientSession::chat(std::string_view text) {
    if ((phase_ != ClientPhase::Lobby && phase_ != ClientPhase::Playing) || !impl_->conn) return;
    impl_->conn->send(MsgType::ChatSend, proto::ChatSend{proto::sanitize(text, kMaxChatLength)});
}

namespace {
proto::Admin adminMessage(proto::AdminAction action, uint32_t slot = kNoSlot, int32_t value = 0) {
    proto::Admin a;
    a.action = action;
    a.slot = slot;
    a.value = value;
    return a;
}
} // namespace

void ClientSession::requestStart(bool force) {
    if (impl_->conn && phase_ == ClientPhase::Lobby)
        impl_->conn->send(MsgType::Admin, adminMessage(proto::AdminAction::StartGame, kNoSlot, force ? 1 : 0));
}

void ClientSession::requestAddComputer(const game::EmpireSetup& setup) {
    if (!impl_->conn || phase_ != ClientPhase::Lobby) return;
    proto::Admin a = adminMessage(proto::AdminAction::AddComputer);
    a.setup = setup;
    impl_->conn->send(MsgType::Admin, a);
}

void ClientSession::requestRemoveSlot(uint32_t slot) {
    if (impl_->conn && phase_ == ClientPhase::Lobby) impl_->conn->send(MsgType::Admin, adminMessage(proto::AdminAction::RemoveSlot, slot));
}

void ClientSession::requestKick(uint32_t slot, std::string_view reason) {
    if (!impl_->conn || phase_ < ClientPhase::Lobby) return;
    proto::Admin a = adminMessage(proto::AdminAction::Kick, slot);
    a.text = std::string(reason);
    impl_->conn->send(MsgType::Admin, a);
}

void ClientSession::requestProcessTurn() {
    if (impl_->conn && phase_ == ClientPhase::Playing) impl_->conn->send(MsgType::Admin, adminMessage(proto::AdminAction::ProcessTurn));
}

void ClientSession::requestAiControl(game::EmpireId empire, bool ai) {
    if (impl_->conn && phase_ == ClientPhase::Playing)
        impl_->conn->send(MsgType::Admin, adminMessage(proto::AdminAction::SetAiControl, empire.value, ai ? 1 : 0));
}

void ClientSession::requestTurnTimeout(int seconds) {
    if (impl_->conn && phase_ >= ClientPhase::Lobby)
        impl_->conn->send(MsgType::Admin, adminMessage(proto::AdminAction::SetTurnTimeout, kNoSlot, seconds));
}

} // namespace opense4::net
