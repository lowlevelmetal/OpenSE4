#include "net/client.hpp"

#include "game/serialize.hpp"
#include "net/auth.hpp"
#include "net/connection.hpp"
#include "net/protocol.hpp"
#include "net/secure.hpp"

#include <algorithm>
#include <format>

namespace opense4::net {

using proto::MsgType;

struct ClientSession::Impl {
    std::optional<Connection> conn;
    Clock::time_point connectStarted;
    // The handshake in progress: our key for this connection and our hello as sent.
    std::optional<crypto::KeyPair> ephemeral;
    std::vector<uint8_t> hello;
};

namespace {

// Our copy of the host's state as we hold it now, for the host to compare
// with what it sent (desync detection).
proto::BaseState baseOf(const std::optional<game::GameState>& state, uint32_t serial) {
    proto::BaseState b;
    b.serial = serial;
    if (state) {
        b.checksum = game::stateChecksum(*state);
        b.parts = game::statePartHashes(*state);
    }
    return b;
}

} // namespace

ClientSession::ClientSession(ClientConfig config) : config_(std::move(config)), impl_(std::make_unique<Impl>()), clientId_(randomId()) {}

ClientSession::~ClientSession() {
    if (phase_ != ClientPhase::Disconnected) disconnect();
}

void ClientSession::emit(EventType type, std::string text, std::string player, uint32_t slot, game::EmpireId empire, uint32_t turn) {
    events_.push_back(Event{type, std::move(text), std::move(player), slot, empire, turn});
}

bool ClientSession::connected() const { return impl_->conn && (phase_ == ClientPhase::Lobby || phase_ == ClientPhase::Playing); }

std::expected<void, std::string> ClientSession::connect() {
    if (phase_ != ClientPhase::Disconnected) closeConnection("reconnecting");
    if (!proto::validPlayerName(config_.playerName)) return std::unexpected(std::string("Choose a player name of 1 to 32 characters."));
    auto s = connectTcp(config_.host, config_.port);
    if (!s) return std::unexpected(s.error());
    // Small frames only until the host has shown it holds the session's keys (the Welcome).
    impl_->conn.emplace(std::move(*s), std::min(config_.maxMessageBytes, proto::kMaxHandshakeBytes));
    impl_->connectStarted = Clock::now();
    impl_->ephemeral.reset();
    impl_->hello.clear();
    phase_ = ClientPhase::Connecting;
    admin_ = false;
    slot_ = kNoSlot;
    ordersAccepted_ = false;
    hostKeyChanged_ = false;
    hostKeyUnconfirmed_ = false;
    hostAskedOldPassword_ = false;
    return {};
}

void ClientSession::closeConnection(std::string reason, bool rejected) {
    if (impl_->conn) {
        impl_->conn->flush();
        impl_->conn->socket().shutdownSend();
        impl_->conn.reset();
    }
    impl_->ephemeral.reset();
    const bool wasConnected = phase_ != ClientPhase::Disconnected;
    phase_ = ClientPhase::Disconnected;
    if (wasConnected) emit(rejected ? EventType::Rejected : EventType::Disconnected, std::move(reason));
}

void ClientSession::disconnect(std::string_view reason) {
    if (!impl_->conn || phase_ == ClientPhase::Disconnected) return;
    if (phase_ == ClientPhase::Lobby || phase_ == ClientPhase::Playing) impl_->conn->send(MsgType::Bye, proto::Bye{std::string(reason)});
    // Give the goodbye a moment to leave, then close.
    const auto until = Clock::now() + std::chrono::milliseconds(200);
    while (impl_->conn->wantsWrite() && !impl_->conn->failed() && Clock::now() < until) {
        PollItem item{impl_->conn->socket().native(), false, true};
        pollSockets(std::span(&item, 1), 50);
        impl_->conn->flush();
    }
    closeConnection(std::string(reason));
}

void ClientSession::dropConnection(std::string_view reason) {
    if (!impl_->conn || phase_ == ClientPhase::Disconnected) return;
    impl_->conn.reset();  // closes the socket; nothing queued goes out
    impl_->ephemeral.reset();
    phase_ = ClientPhase::Disconnected;
    emit(EventType::Disconnected, std::string(reason));
}

std::vector<Event> ClientSession::poll(int timeoutMs) {
    if (impl_->conn && phase_ != ClientPhase::Disconnected) {
        Connection& c = *impl_->conn;
        PollItem item{c.socket().native(), phase_ != ClientPhase::Connecting, phase_ == ClientPhase::Connecting || c.wantsWrite()};
        pollSockets(std::span(&item, 1), std::max(0, timeoutMs));
        const auto now = Clock::now();

        if (phase_ == ClientPhase::Connecting) {
            // The attempt has ended when the socket turns writable or fails,
            // or hangs up: macOS reports a refused connection with POLLHUP
            // alone, which only sets `readable` (not asked for while connecting).
            if (item.writable || item.failed || item.readable) {
                if (std::string err = c.socket().connectError(); !err.empty()) {
                    closeConnection(std::format("Could not connect to {}:{}: {}", config_.host, config_.port, err));
                } else {
                    // The handshake opens in the clear with our key for this connection.
                    phase_ = ClientPhase::Handshaking;
                    impl_->ephemeral = crypto::newKeyPair();
                    proto::ClientHello h;
                    h.app = std::string(appVersion());
                    h.ephemeralKey.assign(impl_->ephemeral->publicKey.begin(), impl_->ephemeral->publicKey.end());
                    impl_->hello = proto::encode(h);
                    c.sendRaw(MsgType::ClientHello, impl_->hello);
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
                handleFrame(static_cast<uint8_t>(f->type), f->payload, f->sealed);
            }
        }
        if (impl_->conn && phase_ != ClientPhase::Disconnected) {
            Connection& conn = *impl_->conn;
            if (conn.keysDiffer()) {
                closeConnection("The host's messages could not be decrypted: the connection was changed on its way, or someone is in between.", true);
            } else if (conn.failed()) {
                closeConnection(conn.error());
            } else if (conn.peerClosed()) {
                closeConnection("The host closed the connection.");
            } else if (phase_ != ClientPhase::Connecting) {
                if (now - conn.lastReceive() > std::chrono::seconds(config_.timeoutSeconds))
                    closeConnection(std::format("The host stopped answering ({} seconds).", config_.timeoutSeconds));
                else if (connected() && !conn.wantsWrite() && now - conn.lastSend() > std::chrono::seconds(config_.keepaliveSeconds))
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

void ClientSession::handleServerHello(std::span<const uint8_t> payload) {
    proto::ServerHello h;
    std::string error;
    if (!proto::decode(payload, h, error) || h.protocol != kProtocolVersion) {
        closeConnection("The host broke the protocol (a broken handshake).");
        return;
    }
    seenHostKey_ = h.hostKey;
    if (config_.hostKey && !crypto::equal(*config_.hostKey, h.hostKey)) {
        hostKeyChanged_ = true;
        closeConnection(std::format("The host's key is not the one this computer trusts for {}:{} (trusted {}, shown {}). If the host made a new "
                                    "key, trust the new one; if not, someone may be in between.",
                                    config_.host, config_.port, crypto::fingerprint(*config_.hostKey), crypto::fingerprint(h.hostKey)),
                        true);
        return;
    }
    // The join password is part of the keys. A host that asks for none
    // although we have one is refused too: a man in the middle would claim
    // exactly that to get around it.
    if (h.joinPassword && config_.joinPassword.empty()) {
        closeConnection("This game needs a game password to join.", true);
        return;
    }
    if (!h.joinPassword && !config_.joinPassword.empty()) {
        closeConnection("The host does not ask for a game password, but you gave one. Leave it empty to join an open game.", true);
        return;
    }
    // The old form of a password goes only to a host whose key the player
    // trusted beforehand (not merely seen on an earlier connection of this
    // session), and only once the player agreed.
    const bool trusted = config_.hostKey && !keyPinnedBySession_;
    if (config_.sendOldPassword && !config_.password.empty() && !trusted) {
        hostKeyUnconfirmed_ = true;
        closeConnection(std::format("Your game shows the old form of your password only to a host it knows. Compare the host's key {} "
                                    "with the one the host sees, trust it, and connect again.",
                                    crypto::fingerprint(h.hostKey)),
                        true);
        return;
    }
    // The game's keys (Argon2id: a moment of work, then kept in this process).
    crypto::Key psk{};
    std::optional<PasswordKeys> mine, master;
    try {
        psk = joinKey(config_.joinPassword, h.hostKey, h.gameId);
        mine = passwordKeys(config_.password, h.gameId);
        if (!config_.masterPassword.empty()) master = passwordKeys(config_.masterPassword, h.gameId);
    } catch (const PasswordWorkError& e) {
        closeConnection(e.what(), true);
        return;
    }
    auto keys = secure::clientKeys(*impl_->ephemeral, h.ephemeralKey, h.hostKey, psk, impl_->hello, payload);
    crypto::wipe(psk.data(), psk.size());
    crypto::wipe(impl_->ephemeral->secret.data(), impl_->ephemeral->secret.size());
    impl_->ephemeral.reset();
    if (!keys) {
        closeConnection("Could not secure the connection: " + keys.error());
        return;
    }
    Connection& c = *impl_->conn;
    c.startEncryption(keys->send, keys->receive);
    crypto::wipe(keys->send.data(), keys->send.size());
    crypto::wipe(keys->receive.data(), keys->receive.size());
    // Who we are, sealed. The proofs sign this session only.
    proto::Login login;
    login.dataSet = config_.dataSet;
    login.mods = config_.mods;
    login.player = config_.playerName;
    login.clientId = clientId_;
    login.passwordVerifier = mine ? mine->verifier() : std::string{};
    login.passwordProof = signWith(mine, secure::loginDigest(keys->sessionId, "player", config_.playerName));
    login.master = !config_.masterPassword.empty();
    if (login.master)
        login.masterProof = signWith(master, secure::loginDigest(keys->sessionId, "master", config_.playerName));
    if (config_.sendOldPassword) login.legacyPasswordHash = legacyPasswordHash(config_.password);
    c.send(MsgType::Login, login);
}

void ClientSession::handleFrame(uint8_t type, std::span<const uint8_t> payload, bool sealed) {
    std::string error;
    Connection& c = *impl_->conn;
    const auto msg = static_cast<MsgType>(type);
    if (phase_ == ClientPhase::Handshaking) {
        if (!c.encrypted()) {
            // The host's half of the handshake, or a refusal (an older host's too).
            if (msg == MsgType::ServerHello) {
                handleServerHello(payload);
                return;
            }
            if (msg != MsgType::Reject && msg != MsgType::Bye) {
                closeConnection("The host broke the protocol (no handshake).");
                return;
            }
        } else if (!sealed && msg != MsgType::Reject) {
            closeConnection("The host broke the protocol (a message in the clear).");
            return;
        } else if (msg != MsgType::Welcome && msg != MsgType::Reject && msg != MsgType::Bye && msg != MsgType::Ping) {
            closeConnection("The host broke the protocol (no welcome).");
            return;
        }
    } else if (!sealed) {
        closeConnection("The host broke the protocol (a message in the clear).");
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
            // The host has shown it holds its key: keep trusting that one.
            if (!config_.hostKey) {
                config_.hostKey = seenHostKey_;
                keyPinnedBySession_ = true;
            }
            c.setMaxIncoming(config_.maxMessageBytes);
            config_.sendOldPassword = false;  // agreed once, for this login
            resendAfterState_ = true;
            emit(EventType::Joined, std::format("{} ({}){}", m.gameName, m.app, m.admin ? ", admin" : ""), config_.playerName, m.slot);
            return;
        }
        case MsgType::Reject: {
            proto::Reject m;
            if (!proto::decode(payload, m, error)) break;
            if (m.reason == proto::RejectReason::OldPassword && sealed) hostAskedOldPassword_ = true;
            // A refusal in the clear came before the connection was secured:
            // anyone could have sent it, so it is shown as the host's word only.
            const std::string text = proto::sanitize(m.text, 1000);
            closeConnection(sealed ? text : "The host said, before the connection was secured: " + text, true);
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
            const bool sameTurn = state_ && state_->turn == s->turn;
            state_ = std::move(*s);
            stateSerial_ = m.serial;
            empire_ = m.empire;
            phase_ = ClientPhase::Playing;
            // Turn-based games get the state within a game turn too, and so
            // does a player whose copy the host found different (a resync).
            const EventType what = first                                  ? EventType::GameStarted
                                   : sameTurn && (turnBased() || m.resync) ? EventType::StateUpdated
                                                                           : EventType::NewTurn;
            if (what != EventType::StateUpdated) ordersAccepted_ = false;
            if (lastOrders_ && lastOrders_->turn != state_->turn) lastOrders_.reset();
            emit(what, m.resync ? std::string("the host's game again") : std::string{}, {}, kNoSlot, empire_, state_->turn);
            if (std::exchange(resendAfterState_, false)) {
                // Back after a reconnect: what the host may not have had.
                if (lastOrders_ && !turnBased()) sendOrders();
                for (const Request& r : unanswered_) c.sendRaw(static_cast<MsgType>(r.type), r.payload);
            }
            return;
        }
        case MsgType::Desync: {
            proto::Desync m;
            if (!proto::decode(payload, m, error)) break;
            emit(EventType::Desync, proto::sanitize(m.text, 1000), {}, kNoSlot, empire_, m.turn);
            return;
        }
        case MsgType::TurnStatus: {
            TurnStatus t;
            if (!proto::decode(payload, t, error)) break;
            const bool startedProcessing = t.processing && !turnStatus_.processing;
            const bool passed = t.turnBased && !t.processing &&
                                (!turnStatus_.turnBased || t.active != turnStatus_.active || t.turn != turnStatus_.turn);
            turnStatus_ = std::move(t);
            if (startedProcessing) emit(EventType::TurnProcessing, {}, {}, kNoSlot, {}, turnStatus_.turn);
            emit(EventType::TurnStatusChanged, {}, {}, kNoSlot, {}, turnStatus_.turn);
            if (passed) {
                const EmpireTurnStatus* a = turnStatus_.activeStatus();
                emit(EventType::PlayerTurn, a && a->empire == empire_ ? std::string("your turn") : std::string{}, a ? a->player : std::string{},
                     kNoSlot, turnStatus_.active, turnStatus_.turn);
            }
            return;
        }
        case MsgType::OrdersAck: {
            proto::OrdersAck m;
            if (!proto::decode(payload, m, error)) break;
            if (m.ok && state_ && m.turn == state_->turn) ordersAccepted_ = true;
            emit(m.ok ? EventType::OrdersAccepted : EventType::OrdersRejected, m.text, {}, kNoSlot, empire_, m.turn);
            return;
        }
        case MsgType::PlayResult: {
            proto::PlayResult m;
            if (!proto::decode(payload, m, error)) break;
            std::erase_if(unanswered_, [&](const Request& r) { return r.request == m.request; });
            std::string text = proto::sanitize(m.text, 1000);
            for (const std::string& r : m.refused) text += (text.empty() ? "" : "; ") + proto::sanitize(r, 500);
            Event e{EventType::CommandsDone, std::move(text), {}, kNoSlot, empire_, m.turn};
            e.request = m.request;
            if (!m.ok && e.text.empty()) e.text = "refused";
            events_.push_back(std::move(e));
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
    if (!state_) return std::unexpected(std::string("Not in a game."));
    if (!orders.empire.valid()) orders.empire = empire_;
    if (orders.turn == 0) orders.turn = state_->turn;
    if (orders.empire != empire_) return std::unexpected(std::string("Those orders are for another empire."));
    if (orders.turn != state_->turn) return std::unexpected(std::format("The game is at turn {}, not {}.", state_->turn, orders.turn));
    lastOrders_ = std::move(orders);
    ordersAccepted_ = false;
    if (phase_ == ClientPhase::Playing && impl_->conn) sendOrders();
    else emit(EventType::Info, "Not connected to the host: the orders go out when the connection is back.");
    return {};
}

void ClientSession::sendOrders() {
    if (!lastOrders_ || !impl_->conn) return;
    proto::SubmitOrders m;
    m.turn = lastOrders_->turn;
    m.orders = game::serializeOrders(*lastOrders_);
    m.base = baseOf(state_, stateSerial_);
    impl_->conn->send(MsgType::SubmitOrders, m);
}

bool ClientSession::myTurn() const {
    return phase_ == ClientPhase::Playing && state_ && turnBased() && !state_->gameOver && state_->playerTurn.started &&
           state_->playerTurn.empire == empire_ && turnStatus_.active == empire_ && !turnStatus_.processing;
}

bool ClientSession::myTurnInCopy() const {
    return phase_ != ClientPhase::Playing && state_ && turnBased() && !state_->gameOver && state_->playerTurn.started &&
           state_->playerTurn.empire == empire_;
}

std::expected<uint32_t, std::string> ClientSession::play(std::vector<game::Command> commands) {
    if (!state_) return std::unexpected(std::string("Not in a game."));
    if (!turnBased()) return std::unexpected(std::string("This game is simultaneous: submit orders for the turn."));
    if (!myTurn() && !myTurnInCopy()) return std::unexpected(std::string("It is not your turn."));
    proto::PlayCommands m;
    m.turn = state_->turn;
    m.request = nextRequest_++;
    m.orders = game::serializeOrders(game::EmpireOrders{empire_, state_->turn, std::move(commands)});
    m.base = baseOf(state_, stateSerial_);
    Request r{static_cast<uint8_t>(MsgType::PlayCommands), m.request, proto::encode(m)};
    if (phase_ == ClientPhase::Playing && impl_->conn) impl_->conn->sendRaw(MsgType::PlayCommands, r.payload);
    unanswered_.push_back(std::move(r));
    return m.request;
}

std::expected<uint32_t, std::string> ClientSession::play(game::Command command) {
    std::vector<game::Command> one;
    one.push_back(std::move(command));
    return play(std::move(one));
}

std::expected<uint32_t, std::string> ClientSession::endTurn() {
    if (!state_) return std::unexpected(std::string("Not in a game."));
    if (!turnBased()) return std::unexpected(std::string("This game is simultaneous: submit orders for the turn."));
    if (!myTurn() && !myTurnInCopy()) return std::unexpected(std::string("It is not your turn."));
    proto::EndTurn m{state_->turn, nextRequest_++};
    Request r{static_cast<uint8_t>(MsgType::EndTurn), m.request, proto::encode(m)};
    if (phase_ == ClientPhase::Playing && impl_->conn) impl_->conn->sendRaw(MsgType::EndTurn, r.payload);
    unanswered_.push_back(std::move(r));
    return m.request;
}

std::span<const game::EntryQuestion> ClientSession::questions() const {
    if (!state_ || state_->playerTurn.empire != empire_) return {};
    return state_->playerTurn.questions;
}

void ClientSession::chat(std::string_view text) {
    if (!connected()) return;
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
    if (!connected()) return;
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
    if (connected()) impl_->conn->send(MsgType::Admin, adminMessage(proto::AdminAction::SetTurnTimeout, kNoSlot, seconds));
}

void ClientSession::requestPasswordReset(const std::vector<game::EmpireId>& empires) {
    uint32_t mask = 0;
    for (game::EmpireId e : empires)
        if (e.valid() && e.index() < 31) mask |= 1u << e.index();
    if (connected()) impl_->conn->send(MsgType::Admin, adminMessage(proto::AdminAction::ResetPasswords, kNoSlot, static_cast<int32_t>(mask)));
}

} // namespace opense4::net
