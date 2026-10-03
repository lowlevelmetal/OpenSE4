#include "net/host.hpp"

#include "datafile/datafile.hpp"
#include "game/ai_data.hpp"
#include "game/ai.hpp"
#include "game/redact.hpp"
#include "game/serialize.hpp"
#include "game/setup.hpp"
#include "game/turn.hpp"
#include "net/auth.hpp"
#include "net/connection.hpp"
#include "net/protocol.hpp"
#include "net/secure.hpp"

#include <algorithm>
#include <cctype>
#include <exception>
#include <format>
#include <map>
#include <utility>

namespace opense4::net {

using proto::MsgType;

namespace {

constexpr size_t kMaxSlots = 32;
constexpr auto kCloseGrace = std::chrono::seconds(3);
constexpr size_t kMaxRejectionNotices = 10;

bool sameName(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        const auto x = static_cast<unsigned char>(a[i]);
        const auto y = static_cast<unsigned char>(b[i]);
        if (std::tolower(x) != std::tolower(y)) return false;
    }
    return true;
}

// A minister style must be one of the install's (spec 02 §9); its spelling is
// made the folder's.
std::string checkMinisterStyle(const game::Rules& r, std::string& style) {
    if (style.empty()) return {};
    const std::vector<std::string> styles = game::ai::ministerStyles(r);
    const auto known = std::find_if(styles.begin(), styles.end(), [&](const std::string& st) { return datafile::keysEqual(st, style); });
    if (known == styles.end()) return std::format("Unknown minister style '{}'.", style);
    style = *known;
    return {};
}

// Cleans an empire setup received from a player.
std::string checkSetup(const game::Rules& r, const game::GameOptions& options, game::EmpireSetup& s) {
    s.name = proto::sanitize(s.name, 64);
    s.empireType = proto::sanitize(s.empireType, 64);
    s.leaderTitle = proto::sanitize(s.leaderTitle, 64);
    s.leaderName = proto::sanitize(s.leaderName, 64);
    s.preset = proto::sanitize(s.preset, 64);
    s.ministerStyle = proto::sanitize(s.ministerStyle, 64);
    s.passwordHash.clear();
    s.kind = game::PlayerKind::Human;
    s.presetTier = std::clamp(s.presetTier, 0, 2);
    if (!s.preset.empty() && !game::findPreset(r, s.preset)) return std::format("Unknown race '{}'.", s.preset);
    if (std::string problem = checkMinisterStyle(r, s.ministerStyle); !problem.empty()) return problem;
    if (s.customRace) {
        const int cost = game::racialPointCost(r, *s.customRace);
        if (cost > options.racialPoints)
            return std::format("The custom race costs {} racial points; this game allows {}.", cost, options.racialPoints);
    }
    return {};
}

// The checksum in a serializeState() blob's envelope (= stateChecksum).
uint64_t blobChecksum(const std::vector<uint8_t>& blob) {
    uint64_t v = 0;
    if (blob.size() >= game::kEnvelopeSize)
        for (size_t i = 0; i < 8; ++i) v |= static_cast<uint64_t>(blob[24 + i]) << (8 * i);
    return v;
}

std::string listOf(const std::vector<std::string>& parts) {
    if (parts.empty()) return "a part the host cannot name";
    std::string s;
    for (const std::string& p : parts) s += (s.empty() ? "" : ", ") + p;
    return s;
}

// A verifier a player may register: none, or one of the current kind.
bool usableVerifier(std::string_view v) { return v.empty() || verifierKey(v).has_value(); }

} // namespace

struct HostSession::Peer {
    uint64_t id = 0;
    Connection conn;
    std::string address;
    Clock::time_point connectedAt = Clock::now();
    bool keyed = false;           // the handshake is done: everything is sealed from here
    crypto::Key sessionId{};
    uint32_t sentSerial = 0;      // the last State sent, and its blob (desync checks)
    std::shared_ptr<const std::vector<uint8_t>> sentState;
    bool welcomed = false;
    bool admin = false;
    uint32_t slot = kNoSlot;
    std::string player;
    bool closing = false;         // flush, half-close, then drop
    bool shutdownSent = false;
    Clock::time_point closingSince;
    std::string closeReason;

    Peer(Socket s, size_t maxIncoming) : conn(std::move(s), maxIncoming) {}
};

struct HostSession::Slot {
    LobbySlot info;
    std::string verifier;  // the player's password verifier
    uint64_t peer = 0;     // connected peer id, 0 = none
    // Turn-based requests of the player's client (Login::clientId): a request
    // repeated after a reconnect is answered again, not carried out twice.
    uint64_t clientId = 0;
    uint32_t lastRequest = 0;
    std::optional<proto::PlayResult> lastResult;
};

HostSession::HostSession(const game::Rules& rules, HostConfig config) : rules_(rules), config_(std::move(config)) {
    if (config_.dataSet.empty()) config_.dataSet = game::dataSetIdentity(rules_);
    hostKey_ = config_.hostKey ? *config_.hostKey : crypto::newKeyPair();
}

HostSession::~HostSession() { stop(); }

void HostSession::emit(EventType type, std::string text, std::string player, uint32_t slot, game::EmpireId empire, uint32_t turn) {
    events_.push_back(Event{type, std::move(text), std::move(player), slot, empire, turn});
}

// ---- Starting and stopping ------------------------------------------------------------------------

std::expected<void, std::string> HostSession::openPort() {
    auto s = listenTcp(config_.bindAddress, config_.port);
    if (!s) return std::unexpected(s.error());
    listener_ = std::move(*s);
    port_ = listener_.localPort();
    emit(EventType::Listening, std::format("{} is waiting for players on TCP port {}", config_.gameName, port_));
    PortMapperOptions upnp = config_.upnp;
    mapper_.start(port_, upnp);
    if (config_.lanDiscovery) {
        if (discovery_.start(config_.discoveryPort))
            emit(EventType::Info, std::format("Players on the local network can find this game (UDP port {})", config_.discoveryPort));
        else
            emit(EventType::Info, std::format("LAN discovery is off: UDP port {} is not available", config_.discoveryPort));
    }
    return {};
}

LanGame HostSession::lanGame() const {
    LanGame g;
    g.port = port_;
    g.gameName = lobby_.gameName.empty() ? config_.gameName : lobby_.gameName;
    g.version = std::string(appVersion());
    g.dataSet = config_.dataSet.empty() ? game::dataSetIdentity(rules_) : config_.dataSet;
    g.slots = static_cast<uint32_t>(std::max(0, config_.humanSlots));
    for (const LobbySlot& slot : lobby_.slots)
        if (slot.kind == SlotKind::Human && !slot.player.empty()) ++g.players;
    g.started = phase_ != HostPhase::Lobby;
    g.password = !config_.joinPasswordHash.empty();
    g.protocol = kProtocolVersion;
    g.hostKey = hostFingerprint();
    return g;
}

std::expected<void, std::string> HostSession::start() {
    if (phase_ != HostPhase::Stopped) return std::unexpected(std::string("The host is already running."));
    if (config_.humanSlots < 1) return std::unexpected(std::string("A game needs at least one human player slot."));
    if (config_.localPlayer && !proto::validPlayerName(config_.localPlayer->name))
        return std::unexpected(std::string("Choose a player name of 1 to 32 characters."));
    if (auto r = openPort(); !r) return r;
    gameId_ = randomId();
    slots_.clear();
    for (int i = 0; i < config_.humanSlots && slots_.size() < kMaxSlots; ++i) {
        auto s = std::make_unique<Slot>();
        s->info.id = nextSlotId_++;
        s->info.kind = SlotKind::Human;
        if (i == 0 && config_.localPlayer) {
            s->info.player = config_.localPlayer->name;
            s->info.local = true;
            s->info.connected = true;
            s->info.setup = config_.localPlayer->setup;
            s->info.setup.kind = game::PlayerKind::Human;
            s->verifier = passwordVerifier(config_.localPlayer->passwordHash);
        }
        slots_.push_back(std::move(s));
    }
    phase_ = HostPhase::Lobby;
    refreshLobby();
    return {};
}

std::expected<void, std::string> HostSession::resume(game::GameState state, const game::SaveInfo& info) {
    if (phase_ != HostPhase::Stopped) return std::unexpected(std::string("The host is already running."));
    if (!info.masterPasswordVerifier.empty() && !checkPassword(info.masterPasswordVerifier, config_.masterPasswordHash))
        return std::unexpected(std::string("This saved game is protected by a master password, and the one given does not match."));
    if (!info.dataSet.empty() && !game::sameDataSet(info.dataSet, config_.dataSet))
        return std::unexpected(std::format("This game was saved with data set {}, but the host has {}.", info.dataSet, config_.dataSet));
    if (state.empires.empty() || state.empires.size() > kMaxSlots) return std::unexpected(std::string("The saved game has no usable empires."));
    if (std::string problem = game::validateState(state, &rules_); !problem.empty())
        return std::unexpected("The saved game does not fit this data set: " + problem);
    if (!info.gameName.empty()) config_.gameName = info.gameName;
    if (auto r = openPort(); !r) return r;
    gameId_ = info.gameId ? info.gameId : randomId();

    slots_.clear();
    int humans = 0;
    for (size_t i = 0; i < state.empires.size(); ++i) {
        const game::Empire& e = state.empires[i];
        auto s = std::make_unique<Slot>();
        s->info.id = nextSlotId_++;
        s->info.kind = e.kind == game::PlayerKind::Human ? SlotKind::Human : SlotKind::Computer;
        s->info.setup.name = e.name;
        s->info.setup.kind = e.kind;
        s->info.ready = true;
        if (s->info.kind == SlotKind::Human) {
            ++humans;
            s->info.player = i < info.players.size() && !info.players[i].empty() ? info.players[i] : e.name;
            s->verifier = e.passwordHash;
            if (config_.localPlayer && sameName(config_.localPlayer->name, s->info.player)) {
                s->info.local = true;
                s->info.connected = true;
            }
        }
        slots_.push_back(std::move(s));
    }
    config_.humanSlots = humans;
    state_ = std::move(state);
    orders_.assign(state_->empires.size(), std::nullopt);
    phase_ = state_->gameOver ? HostPhase::GameOver : HostPhase::Playing;
    refreshLobby();
    // A turn-based game saved between player turns plays on to the next player who plays.
    if (phase_ == HostPhase::Playing && turnBased() && !state_->playerTurn.started && anyHumanToPlay()) {
        try {
            game::resumeTurnBased(rules_, *state_, liveOptions());
        } catch (const std::exception& e) {
            return std::unexpected(std::format("The saved game could not go on: {}", e.what()));
        }
        if (state_->gameOver) phase_ = HostPhase::GameOver;
    }
    stateCache_ = redactedState();
    beginTurn();
    emit(EventType::GameStarted, std::format("resumed at turn {}", state_->turn), {}, kNoSlot, {}, state_->turn);
    if (turnBased()) emit(EventType::PlayerTurn, {}, playerName(activeEmpire()), kNoSlot, activeEmpire(), state_->turn);
    return {};
}

void HostSession::stop(std::string_view reason) {
    if (phase_ == HostPhase::Stopped) return;
    for (auto& p : peers_) {
        if (!p->closing) p->conn.send(MsgType::Bye, proto::Bye{std::string(reason)});
        p->conn.flush();
    }
    // Give the goodbyes a moment to leave.
    const auto until = Clock::now() + std::chrono::milliseconds(300);
    while (Clock::now() < until) {
        std::vector<PollItem> items;
        for (auto& p : peers_)
            if (p->conn.wantsWrite() && !p->conn.failed()) items.push_back({p->conn.socket().native(), false, true});
        if (items.empty()) break;
        pollSockets(items, 50);
        for (auto& p : peers_) p->conn.flush();
    }
    for (auto& p : peers_) p->conn.socket().shutdownSend();
    peers_.clear();
    listener_.close();
    discovery_.stop();
    mapper_.stop();
    if (auto u = mapper_.takeUpdate()) emit(EventType::PortMapping, u->message);
    phase_ = HostPhase::Stopped;
    emit(EventType::Info, "The host stopped.");
}

// ---- The event loop -----------------------------------------------------------------------------------

std::vector<Event> HostSession::poll(int timeoutMs) {
    if (phase_ != HostPhase::Stopped) {
        std::vector<PollItem> items;
        items.push_back({listener_.native(), true, false});
        for (auto& p : peers_) items.push_back({p->conn.socket().native(), true, p->conn.wantsWrite()});
        // Last, so the listener and peer entries keep their positions.
        if (discovery_.running()) items.push_back({discoveryNative(), true, false});
        int wait = std::max(0, timeoutMs);
        if (deadline_) {
            const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(*deadline_ - Clock::now()).count();
            wait = static_cast<int>(std::clamp<int64_t>(left, 0, wait));
        }
        pollSockets(items, wait);
        discovery_.poll(lanGame());

        const size_t known = peers_.size();
        if (items[0].readable) acceptPeers();
        for (size_t i = 0; i < known; ++i) {
            Peer& p = *peers_[i];
            const PollItem& it = items[i + 1];
            if (it.readable) p.conn.receive();
            while (auto f = p.conn.nextFrame()) {
                if (p.closing) continue;  // draining
                handleFrame(p, static_cast<uint8_t>(f->type), f->payload, f->sealed);
            }
            if (p.conn.keysDiffer() && !p.closing) {
                // The client's first sealed message did not open: it derived
                // other keys, which a wrong join password does. Said in the
                // clear, as it cannot read anything sealed.
                const std::string why = config_.joinPasswordHash.empty()
                                            ? std::string("The connection could not be secured (the handshake was changed on its way).")
                                            : std::string("Wrong game password.");
                p.conn.sendPlain(MsgType::Reject, proto::Reject{proto::RejectReason::Password, why});
                emit(EventType::Info, std::format("Refused {}: {}", p.address, why));
                dropPeer(p, why, false);
            }
            if (it.writable) p.conn.flush();
        }
        runTimers();
        if (phase_ == HostPhase::Playing && allOrdersIn()) {
            if (auto r = processTurnNow(); !r) emit(EventType::Error, r.error());
        }
        if (phase_ == HostPhase::Lobby && config_.autoStart) {
            const bool full = std::none_of(slots_.begin(), slots_.end(), [](const auto& s) { return s->info.open(); });
            if (full && startProblem(false).empty()) {
                if (auto r = startGame(false); !r) {
                    // Don't retry every poll: everyone has to confirm again.
                    for (auto& s : slots_)
                        if (s->info.kind == SlotKind::Human && !s->info.local) s->info.ready = false;
                    for (auto& p : peers_)
                        if (p->welcomed && !p->closing) p->conn.send(MsgType::Notice, proto::Notice{"The game could not be created: " + r.error()});
                    broadcastLobby();
                }
            }
        }
        while (auto u = mapper_.takeUpdate()) emit(EventType::PortMapping, u->message);
        flushAll();

        // Reap finished connections.
        const auto now = Clock::now();
        for (size_t i = 0; i < peers_.size();) {
            Peer& p = *peers_[i];
            if (p.closing && !p.conn.wantsWrite() && !p.shutdownSent) {
                p.conn.socket().shutdownSend();
                p.shutdownSent = true;
            }
            const bool done = p.conn.failed() || p.conn.peerClosed() || (p.closing && now - p.closingSince > kCloseGrace);
            if (!done) {
                ++i;
                continue;
            }
            std::string why = !p.closeReason.empty() ? p.closeReason : p.conn.failed() ? p.conn.error() : std::string("disconnected");
            std::unique_ptr<Peer> gone = std::move(peers_[i]);
            peers_.erase(peers_.begin() + static_cast<std::ptrdiff_t>(i));
            if (gone->welcomed) peerGone(*gone, why);
        }
        if (deadline_ && phase_ == HostPhase::Playing) {
            const auto left = std::chrono::duration_cast<std::chrono::seconds>(*deadline_ - Clock::now()).count();
            turnStatus_.secondsLeft = static_cast<int32_t>(std::max<int64_t>(0, left));
        }
    }
    std::vector<Event> out;
    out.swap(events_);
    return out;
}

void HostSession::acceptPeers() {
    for (;;) {
        Socket s = acceptConnection(listener_);
        if (!s.valid()) return;
        if (peers_.size() >= config_.maxConnections) {
            emit(EventType::Warning, std::format("Refused a connection from {}: too many connections", s.peerAddress()));
            continue;  // closes the socket
        }
        auto p = std::make_unique<Peer>(std::move(s), proto::kMaxHandshakeBytes);
        p->id = nextPeerId_++;
        p->address = p->conn.socket().peerAddress();
        peers_.push_back(std::move(p));
    }
}

void HostSession::runTimers() {
    const auto now = Clock::now();
    for (auto& ptr : peers_) {
        Peer& p = *ptr;
        if (p.closing || p.conn.failed()) continue;
        if (!p.welcomed) {
            if (now - p.connectedAt > std::chrono::seconds(config_.handshakeTimeoutSeconds)) dropPeer(p, "no greeting received in time", false);
            continue;
        }
        if (now - p.conn.lastReceive() > std::chrono::seconds(config_.timeoutSeconds)) {
            dropPeer(p, std::format("timed out (nothing heard for {} seconds)", config_.timeoutSeconds), true);
            continue;
        }
        if (!p.conn.wantsWrite() && now - p.conn.lastSend() > std::chrono::seconds(config_.keepaliveSeconds))
            p.conn.send(MsgType::Ping, proto::Ping{randomId()});
    }
    if (phase_ == HostPhase::Playing && deadline_ && now >= *deadline_) {
        emit(EventType::Info, turnBased() ? "Turn time is up: the game goes on without the player." : "Turn time is up: processing without the missing orders.");
        if (auto r = processTurnNow(); !r) emit(EventType::Error, r.error());
    }
}

void HostSession::flushAll() {
    for (auto& p : peers_) p->conn.flush();
}

// ---- Messages -------------------------------------------------------------------------------------------

void HostSession::handleFrame(Peer& peer, uint8_t type, std::span<const uint8_t> payload, bool sealed) {
    std::string error;
    const auto msg = static_cast<MsgType>(type);
    if (!peer.keyed) {
        if (msg == MsgType::ClientHello) handleClientHello(peer, payload);
        else dropPeer(peer, "protocol error (expected a greeting)", false);
        return;
    }
    if (!sealed) {
        dropPeer(peer, "protocol error (a message in the clear on an encrypted connection)", false);
        return;
    }
    if (!peer.welcomed) {
        if (msg == MsgType::Login) handleLogin(peer, payload);
        else dropPeer(peer, "protocol error (expected a login)", false);
        return;
    }
    Slot* slot = slotOfPeer(peer);
    switch (msg) {
        case MsgType::SubmitSetup: {
            proto::SubmitSetup m;
            if (!proto::decode(payload, m, error)) break;
            if (phase_ != HostPhase::Lobby || !slot) {
                peer.conn.send(MsgType::Notice, proto::Notice{"The game has already started."});
                return;
            }
            if (std::string problem = checkSetup(rules_, config_.setup.options, m.setup); !problem.empty()) {
                peer.conn.send(MsgType::Notice, proto::Notice{problem});
                return;
            }
            slot->info.setup = std::move(m.setup);
            broadcastLobby();
            return;
        }
        case MsgType::SetReady: {
            proto::SetReady m;
            if (!proto::decode(payload, m, error)) break;
            if (phase_ == HostPhase::Lobby && slot && slot->info.ready != m.ready) {
                slot->info.ready = m.ready;
                broadcastLobby();
            }
            return;
        }
        case MsgType::SubmitOrders: handleOrders(peer, payload); return;
        case MsgType::PlayCommands: handlePlay(peer, payload); return;
        case MsgType::EndTurn: handleEndTurn(peer, payload); return;
        case MsgType::ChatSend: {
            proto::ChatSend m;
            if (!proto::decode(payload, m, error)) break;
            const std::string text = proto::sanitize(m.text, kMaxChatLength);
            if (text.empty()) return;
            for (auto& p : peers_)
                if (p->welcomed && !p->closing) p->conn.send(MsgType::Chat, proto::Chat{peer.player, text});
            emit(EventType::Chat, text, peer.player, peer.slot);
            return;
        }
        case MsgType::Admin: handleAdmin(peer, payload); return;
        case MsgType::Ping: {
            proto::Ping m;
            if (!proto::decode(payload, m, error)) break;
            peer.conn.send(MsgType::Pong, m);
            return;
        }
        case MsgType::Pong: return;
        case MsgType::Bye: {
            proto::Bye m;
            proto::decode(payload, m, error);
            dropPeer(peer, m.reason.empty() ? std::string("left the game") : "left: " + proto::sanitize(m.reason, 200), false);
            return;
        }
        default: error = std::format("unknown message type {}", type); break;
    }
    dropPeer(peer, "protocol error: " + error, true);
}

void HostSession::reject(Peer& peer, uint8_t reason, std::string text) {
    peer.conn.send(MsgType::Reject, proto::Reject{static_cast<proto::RejectReason>(reason), text});
    emit(EventType::Info, std::format("Refused {}: {}", peer.address, text));
    dropPeer(peer, text, false);
}

bool HostSession::anyLegacyVerifier() const {
    if (state_)
        for (const game::Empire& e : state_->empires)
            if (isLegacyVerifier(e.passwordHash)) return true;
    return std::any_of(slots_.begin(), slots_.end(), [](const auto& s) { return isLegacyVerifier(s->verifier); });
}

void HostSession::handleClientHello(Peer& peer, std::span<const uint8_t> payload) {
    using RR = proto::RejectReason;
    auto code = [](RR r) { return static_cast<uint8_t>(r); };
    // The version first, whatever the rest is: older and newer clients get a
    // refusal they can read (an OpenSE4 0.6 client handles this Reject).
    proto::VersionProbe probe;
    if (!proto::probeVersion(payload, probe) || probe.magic != proto::kMagic) {
        dropPeer(peer, "not an OpenSE4 client", false);
        return;
    }
    if (probe.protocol != kProtocolVersion)
        return reject(peer, code(RR::Protocol),
                      std::format("The host runs {} (network protocol {}); you have {} (protocol {}). Both need the same version.",
                                  appVersion(), kProtocolVersion, proto::sanitize(probe.app, 40), probe.protocol));
    proto::ClientHello hello;
    std::string error;
    if (!proto::decode(payload, hello, error) || hello.ephemeralKey.size() != crypto::Key{}.size()) {
        dropPeer(peer, "protocol error: a broken greeting", false);
        return;
    }
    crypto::Key clientKey{};
    std::copy_n(reinterpret_cast<const uint8_t*>(hello.ephemeralKey.data()), clientKey.size(), clientKey.begin());
    const crypto::KeyPair ephemeral = crypto::newKeyPair();
    proto::ServerHello answer;
    answer.app = std::string(appVersion());
    answer.ephemeralKey = ephemeral.publicKey;
    answer.hostKey = hostKey_.publicKey;
    answer.joinPassword = !config_.joinPasswordHash.empty();
    answer.legacyPasswords = anyLegacyVerifier();
    const std::vector<uint8_t> answerBytes = proto::encode(answer);
    auto keys = secure::hostKeys(ephemeral, hostKey_, clientKey, secure::joinKey(config_.joinPasswordHash), payload, answerBytes);
    if (!keys) {
        dropPeer(peer, keys.error(), false);
        return;
    }
    peer.conn.sendRaw(MsgType::ServerHello, answerBytes);
    peer.conn.startEncryption(keys->send, keys->receive);
    peer.sessionId = keys->sessionId;
    peer.keyed = true;
}

void HostSession::handleLogin(Peer& peer, std::span<const uint8_t> payload) {
    using RR = proto::RejectReason;
    auto code = [](RR r) { return static_cast<uint8_t>(r); };
    proto::Login h;
    std::string error;
    if (!proto::decode(payload, h, error)) {
        dropPeer(peer, "protocol error: " + error, false);
        return;
    }
    if (!game::sameDataSet(h.dataSet, config_.dataSet))
        return reject(peer, code(RR::DataSet),
                      std::format("The host plays with data set {}; yours is {}. Both need the same game data and mods.", config_.dataSet,
                                  proto::sanitize(h.dataSet, 200)));
    const std::string name = h.player;
    if (!proto::validPlayerName(name)) return reject(peer, code(RR::Name), "Choose a player name of 1 to 32 characters.");
    for (const std::string& b : banned_)
        if (sameName(b, name)) return reject(peer, code(RR::Banned), "The host removed you from this game.");
    // The proofs sign this very session (secure::loginDigest): seen by a
    // man in the middle, they are worth nothing on another connection.
    bool admin = false;
    if (h.master && !config_.masterPasswordHash.empty()) {
        const crypto::Key master = passwordKey(config_.masterPasswordHash).publicKey;
        admin = crypto::verify(h.masterProof, master, secure::loginDigest(peer.sessionId, "master", name));
    }
    const crypto::Key digest = secure::loginDigest(peer.sessionId, "player", name);

    Slot* slot = nullptr;
    bool reconnect = false;
    if (phase_ == HostPhase::Lobby) {
        for (auto& s : slots_)
            if (!s->info.open() && s->info.kind == SlotKind::Human && sameName(s->info.player, name)) slot = s.get();
        if (slot) {
            if (slot->info.local) return reject(peer, code(RR::Name), "That name belongs to the host.");
            if (!checkPasswordSignature(slot->verifier, digest, h.passwordProof))
                return reject(peer, code(RR::Password), "That player name is taken (wrong password).");
            reconnect = true;
        } else {
            if (!usableVerifier(h.passwordVerifier) || !checkPasswordSignature(h.passwordVerifier, digest, h.passwordProof))
                return reject(peer, code(RR::Password), "Your client sent a password the host cannot use.");
            for (auto& s : slots_)
                if (s->info.open()) {
                    slot = s.get();
                    break;
                }
            if (!slot) return reject(peer, code(RR::Full), "The game is full.");
            slot->info.player = name;
            slot->info.ready = false;
            slot->info.setup = {};
            slot->verifier = h.passwordVerifier;
        }
    } else {
        for (size_t i = 0; i < slots_.size() && !slot; ++i) {
            Slot& s = *slots_[i];
            if (s.info.kind != SlotKind::Human) continue;
            if (sameName(s.info.player, name) || (state_ && sameName(state_->empires[i].name, name))) slot = &s;
        }
        if (!slot)
            return reject(peer, code(RR::NotInGame), std::format("This game is under way and has no empire for player {}.", name));
        if (slot->info.local) return reject(peer, code(RR::Name), "That empire belongs to the host.");
        std::string& verifier = state_ ? state_->empires[slotIndex(*slot)].passwordHash : slot->verifier;
        if (isLegacyVerifier(verifier)) {
            // A game of OpenSE4 0.6: the password hash came with the login, sealed.
            if (!checkPassword(verifier, h.legacyPasswordHash))
                return reject(peer, code(RR::Password), std::format("Wrong password for {}.", name));
            verifier = passwordVerifier(h.legacyPasswordHash);
            slot->verifier = verifier;
            emit(EventType::Info, std::format("{}'s password is now kept in the current form.", name));
        } else if (!checkPasswordSignature(verifier, digest, h.passwordProof)) {
            return reject(peer, code(RR::Password), std::format("Wrong password for {}.", name));
        }
        reconnect = true;
    }
    if (slot->clientId != h.clientId) {
        slot->clientId = h.clientId;
        slot->lastRequest = 0;
        slot->lastResult.reset();
    }

    if (Peer* old = peerOfSlot(*slot); old && old != &peer) {
        slot->peer = 0;
        dropPeer(*old, "logged in again from another connection", true);
    }
    peer.welcomed = true;
    peer.admin = admin;
    peer.slot = slot->info.id;
    peer.player = slot->info.player;
    slot->peer = peer.id;
    slot->info.connected = true;
    peer.conn.setMaxIncoming(config_.maxOrdersBytes);
    peer.conn.send(MsgType::Welcome, proto::Welcome{kProtocolVersion, std::string(appVersion()), config_.gameName, gameId_,
                                                     slot->info.id, admin, config_.dataSet});
    emit(reconnect ? EventType::PlayerReconnected : EventType::PlayerJoined, admin ? std::format("from {}, admin", peer.address) : "from " + peer.address,
         peer.player, peer.slot);
    broadcastLobby();
    if (state_) {
        sendState(peer, false);
        refreshTurnStatus();
        broadcastTurnStatus();
    }
}

void HostSession::checkBase(Peer& peer, const proto::BaseState& base) {
    // A copy based on an older State (commands still in flight, an earlier
    // connection) cannot be compared.
    if (base.serial == 0 || base.serial != peer.sentSerial || !peer.sentState) return;
    if (base.checksum == blobChecksum(*peer.sentState)) return;
    std::vector<std::string> parts;
    if (auto sent = game::deserializeState(*peer.sentState)) parts = game::differingStateParts(base.parts, game::statePartHashes(*sent));
    const uint32_t turn = state_ ? state_->turn : 0;
    const std::string list = listOf(parts);
    emit(EventType::Desync, std::format("Turn {}: {}'s copy of the game differs from the host's ({}); the host sends it again.", turn, peer.player, list),
         peer.player, peer.slot, empireOfSlot(peer.slot), turn);
    peer.conn.send(MsgType::Desync,
                   proto::Desync{turn, parts,
                                 std::format("Turn {}: your copy of the game differed from the host's ({}). The host sent its game again.", turn, list)});
    sendState(peer, false, true);
}

void HostSession::handleOrders(Peer& peer, std::span<const uint8_t> payload) {
    proto::SubmitOrders m;
    std::string error;
    if (!proto::decode(payload, m, error)) {
        dropPeer(peer, "protocol error: " + error, true);
        return;
    }
    auto ack = [&](bool ok, std::string text) { peer.conn.send(MsgType::OrdersAck, proto::OrdersAck{m.turn, ok, std::move(text)}); };
    if (phase_ != HostPhase::Playing || !state_) return ack(false, "No turn is open.");
    checkBase(peer, m.base);
    if (turnBased()) return ack(false, "This game is turn-based: commands are carried out as they are given, in your turn.");
    if (m.turn != state_->turn) return ack(false, std::format("These orders are for turn {}, but the game is at turn {}.", m.turn, state_->turn));
    const game::EmpireId e = empireOfSlot(peer.slot);
    if (!e.valid()) return ack(false, "You have no empire in this game.");
    auto orders = game::deserializeOrders(m.orders);
    if (!orders) return ack(false, "Unreadable orders: " + orders.error());
    if (orders->empire != e) return ack(false, "These orders are for another empire.");
    if (orders->turn != state_->turn) return ack(false, "The orders' turn does not match.");
    if (!state_->empire(e).alive) return ack(false, "Your empire is no more.");
    const size_t count = orders->commands.size();
    orders_[e.index()] = std::move(*orders);
    ack(true, std::format("Orders for turn {} received ({} commands).", m.turn, count));
    emit(EventType::OrdersReceived, {}, peer.player, peer.slot, e, m.turn);
    refreshTurnStatus();
    broadcastTurnStatus();
}

void HostSession::handlePlay(Peer& peer, std::span<const uint8_t> payload) {
    proto::PlayCommands m;
    std::string error;
    if (!proto::decode(payload, m, error)) {
        dropPeer(peer, "protocol error: " + error, true);
        return;
    }
    proto::PlayResult res;
    res.request = m.request;
    res.turn = state_ ? state_->turn : 0;
    Slot* slot = slotOfPeer(peer);
    // Every request is answered once: a repeat (the client resends what it
    // had no answer to after a reconnect) gets the answer again.
    if (slot && slot->clientId != 0 && m.request <= slot->lastRequest) {
        if (slot->lastResult && slot->lastResult->request == m.request) {
            res = *slot->lastResult;
        } else {
            res.ok = true;
            res.text = "already carried out";
        }
        peer.conn.send(MsgType::PlayResult, res);
        return;
    }
    auto answer = [&] {
        if (slot) {
            slot->lastRequest = m.request;
            slot->lastResult = res;
        }
        peer.conn.send(MsgType::PlayResult, res);
    };
    auto refuse = [&](std::string text) {
        res.text = std::move(text);
        answer();
    };
    if (phase_ != HostPhase::Playing || !state_) return refuse("No turn is open.");
    if (!turnBased()) return refuse("This game is simultaneous: send your orders for the turn instead.");
    const game::EmpireId e = empireOfSlot(peer.slot);
    if (!e.valid()) return refuse("You have no empire in this game.");
    if (m.turn != state_->turn) return refuse(std::format("These commands are for turn {}, but the game is at turn {}.", m.turn, state_->turn));
    if (activeEmpire() != e) return refuse("It is not your turn.");
    auto orders = game::deserializeOrders(m.orders);
    if (!orders) return refuse("Unreadable commands: " + orders.error());
    if (orders->empire != e || orders->turn != state_->turn) return refuse("These commands are for another empire or turn.");
    checkBase(peer, m.base);
    const size_t count = orders->commands.size();
    emit(EventType::OrdersReceived, std::format("{} command{}", count, count == 1 ? "" : "s"), peer.player, peer.slot, e, state_->turn);
    game::TurnResult result;
    try {
        result = runLive(e, orders->commands);
    } catch (const std::exception& ex) {
        return refuse(std::format("The host could not carry out these commands ({}); nothing was changed.", ex.what()));
    }
    res.ok = true;
    for (const auto& [who, why] : result.rejected)
        if (who == e) res.refused.push_back(why);
    answer();
}

void HostSession::handleEndTurn(Peer& peer, std::span<const uint8_t> payload) {
    proto::EndTurn m;
    std::string error;
    if (!proto::decode(payload, m, error)) {
        dropPeer(peer, "protocol error: " + error, true);
        return;
    }
    proto::PlayResult res;
    res.request = m.request;
    res.turn = state_ ? state_->turn : 0;
    Slot* slot = slotOfPeer(peer);
    if (slot && slot->clientId != 0 && m.request <= slot->lastRequest) {
        if (slot->lastResult && slot->lastResult->request == m.request) {
            res = *slot->lastResult;
        } else {
            res.ok = true;
            res.text = "already done";
        }
        peer.conn.send(MsgType::PlayResult, res);
        return;
    }
    auto answer = [&] {
        if (slot) {
            slot->lastRequest = m.request;
            slot->lastResult = res;
        }
        peer.conn.send(MsgType::PlayResult, res);
    };
    const game::EmpireId e = empireOfSlot(peer.slot);
    std::expected<void, std::string> done = std::unexpected(std::string("No turn is open."));
    if (phase_ == HostPhase::Playing && state_ && turnBased()) {
        if (m.turn != state_->turn) done = std::unexpected(std::format("The game is at turn {}, not {}.", state_->turn, m.turn));
        else if (!e.valid() || activeEmpire() != e) done = std::unexpected(std::string("It is not your turn."));
        else {
            // Answer first: the states of the turns that follow come after.
            res.ok = true;
            answer();
            if (auto r = endPlayerTurn(e); !r) notifyPlayer(e, r.error());
            return;
        }
    } else if (state_ && !turnBased()) {
        done = std::unexpected(std::string("This game is simultaneous: send your orders for the turn instead."));
    }
    res.text = done.error();
    answer();
}

void HostSession::handleAdmin(Peer& peer, std::span<const uint8_t> payload) {
    proto::Admin a;
    std::string error;
    if (!proto::decode(payload, a, error)) {
        dropPeer(peer, "protocol error: " + error, true);
        return;
    }
    if (!peer.admin) {
        peer.conn.send(MsgType::Notice, proto::Notice{"Only the host, or a player who gave the master password, can do that."});
        return;
    }
    std::expected<void, std::string> result;
    std::string what;
    switch (a.action) {
        case proto::AdminAction::StartGame: what = "start the game"; result = startGame(a.value != 0); break;
        case proto::AdminAction::AddComputer: {
            what = "add a computer empire";
            auto r = addComputerEmpire(a.setup);
            if (!r) result = std::unexpected(r.error());
            break;
        }
        case proto::AdminAction::RemoveSlot: what = "remove a slot"; result = removeSlot(a.slot); break;
        case proto::AdminAction::Kick: what = "kick a player"; result = kick(a.slot, proto::sanitize(a.text, 200)); break;
        case proto::AdminAction::ProcessTurn: what = "process the turn"; result = processTurnNow(); break;
        case proto::AdminAction::SetAiControl:
            what = "change computer control";
            result = setAiControl(game::EmpireId{a.slot}, a.value != 0);
            break;
        case proto::AdminAction::SetTurnTimeout:
            what = "set the turn timeout";
            setTurnTimeout(a.value);
            break;
        case proto::AdminAction::ResetPasswords: {
            // A player with the master password stands in for the host of a
            // headless server: the new passwords go to that player only (inferred).
            what = "reset passwords";
            std::vector<game::EmpireId> empires;
            for (uint32_t i = 0; i < 31; ++i)
                if ((static_cast<uint32_t>(a.value) >> i) & 1u) empires.push_back(game::EmpireId{i});
            auto r = resetPasswords(empires);
            if (!r) {
                result = std::unexpected(r.error());
                break;
            }
            for (const PasswordReset& p : *r)
                peer.conn.send(MsgType::Notice, proto::Notice{std::format("Password Reset: player {} ({}): {}", p.empire.value + 1,
                                                                           state_->empire(p.empire).name, p.password)});
            break;
        }
        default: result = std::unexpected(std::string("Unknown admin action.")); break;
    }
    emit(EventType::Info, std::format("{} (admin) asked to {}: {}", peer.player, what, result ? "done" : result.error()));
    peer.conn.send(MsgType::Notice, proto::Notice{result ? std::format("Done: {}.", what) : result.error()});
}

void HostSession::dropPeer(Peer& peer, std::string reason, bool sayBye) {
    if (peer.closing) return;
    if (sayBye) peer.conn.send(MsgType::Bye, proto::Bye{reason});
    peer.closing = true;
    peer.closingSince = Clock::now();
    peer.closeReason = std::move(reason);
}

void HostSession::peerGone(Peer& peer, const std::string& reason) {
    Slot* s = findSlot(peer.slot);
    if (!s || s->peer != peer.id) return;
    s->peer = 0;
    s->info.connected = false;
    if (phase_ == HostPhase::Lobby) {  // the slot opens up again
        s->info.player.clear();
        s->info.ready = false;
        s->info.setup = {};
        s->verifier.clear();
    }
    emit(EventType::PlayerLeft, reason, peer.player, peer.slot);
    broadcastLobby();
    if (state_) {
        refreshTurnStatus();
        broadcastTurnStatus();
    }
}

// ---- Lobby ---------------------------------------------------------------------------------------------------

HostSession::Slot* HostSession::findSlot(uint32_t id) {
    for (auto& s : slots_)
        if (s->info.id == id) return s.get();
    return nullptr;
}

const HostSession::Slot* HostSession::findSlot(uint32_t id) const {
    for (const auto& s : slots_)
        if (s->info.id == id) return s.get();
    return nullptr;
}

HostSession::Slot* HostSession::slotOfPeer(const Peer& peer) {
    Slot* s = findSlot(peer.slot);
    return s && s->peer == peer.id ? s : nullptr;
}

HostSession::Peer* HostSession::peerOfSlot(const Slot& slot) {
    if (slot.peer == 0) return nullptr;
    for (auto& p : peers_)
        if (p->id == slot.peer) return p.get();
    return nullptr;
}

size_t HostSession::slotIndex(const Slot& slot) const {
    for (size_t i = 0; i < slots_.size(); ++i)
        if (slots_[i].get() == &slot) return i;
    return slots_.size();
}

void HostSession::refreshLobby() {
    lobby_.gameName = config_.gameName;
    lobby_.gameId = gameId_;
    lobby_.humanSlots = static_cast<uint32_t>(std::count_if(slots_.begin(), slots_.end(), [](const auto& s) { return s->info.kind == SlotKind::Human; }));
    lobby_.started = phase_ == HostPhase::Playing || phase_ == HostPhase::GameOver;
    lobby_.turnTimeoutSeconds = config_.turnTimeoutSeconds;
    lobby_.seed = config_.setup.seed;
    lobby_.options = state_ ? state_->options : config_.setup.options;
    lobby_.slots.clear();
    for (const auto& s : slots_) {
        LobbySlot info = s->info;
        info.setup.passwordHash.clear();
        lobby_.slots.push_back(std::move(info));
    }
}

void HostSession::broadcastLobby() {
    refreshLobby();
    for (auto& p : peers_)
        if (p->welcomed && !p->closing) p->conn.send(MsgType::Lobby, lobby_);
    emit(EventType::LobbyChanged);
}

std::expected<uint32_t, std::string> HostSession::addComputerEmpire(game::EmpireSetup setup) {
    if (phase_ != HostPhase::Lobby) return std::unexpected(std::string("Empires can only be added before the game starts."));
    if (slots_.size() >= kMaxSlots) return std::unexpected(std::format("A game has at most {} empires.", kMaxSlots));
    if (!setup.preset.empty() && !game::findPreset(rules_, setup.preset)) return std::unexpected(std::format("Unknown race '{}'.", setup.preset));
    if (std::string problem = checkMinisterStyle(rules_, setup.ministerStyle); !problem.empty()) return std::unexpected(problem);
    auto s = std::make_unique<Slot>();
    s->info.id = nextSlotId_++;
    s->info.kind = SlotKind::Computer;
    s->info.ready = true;
    s->info.setup = std::move(setup);
    s->info.setup.kind = game::PlayerKind::Computer;
    s->info.setup.passwordHash.clear();
    const uint32_t id = s->info.id;
    slots_.push_back(std::move(s));
    broadcastLobby();
    return id;
}

std::expected<void, std::string> HostSession::removeSlot(uint32_t id) {
    if (phase_ != HostPhase::Lobby) return std::unexpected(std::string("Slots can only be removed before the game starts."));
    Slot* s = findSlot(id);
    if (!s) return std::unexpected(std::string("No such slot."));
    if (s->info.local) return std::unexpected(std::string("The host's own slot cannot be removed."));
    if (Peer* p = peerOfSlot(*s)) {
        s->peer = 0;
        dropPeer(*p, "The host removed your slot.", true);
        emit(EventType::PlayerLeft, "slot removed", p->player, id);
    }
    std::erase_if(slots_, [&](const auto& x) { return x.get() == s; });
    broadcastLobby();
    return {};
}

std::expected<void, std::string> HostSession::kick(uint32_t id, std::string reason) {
    Slot* s = findSlot(id);
    if (!s || s->info.kind != SlotKind::Human || s->info.player.empty()) return std::unexpected(std::string("No player in that slot."));
    if (s->info.local) return std::unexpected(std::string("The host cannot kick itself."));
    const std::string player = s->info.player;
    banned_.push_back(player);
    if (Peer* p = peerOfSlot(*s)) {
        s->peer = 0;
        dropPeer(*p, reason.empty() ? std::string("The host removed you from the game.") : "The host removed you from the game: " + reason, true);
    }
    s->info.connected = false;
    if (phase_ == HostPhase::Lobby) {
        s->info.player.clear();
        s->info.ready = false;
        s->info.setup = {};
        s->verifier.clear();
    } else {
        s->info.aiControl = true;
    }
    emit(EventType::PlayerLeft, reason.empty() ? std::string("kicked") : "kicked: " + reason, player, id);
    broadcastLobby();
    if (state_) {
        refreshTurnStatus();
        broadcastTurnStatus();
        // Turn-based: the computer plays the rest of a kicked player's turn.
        if (const game::EmpireId gone = empireOfSlot(id); phase_ == HostPhase::Playing && turnBased() && gone.valid() && activeEmpire() == gone)
            if (auto r = skipPlayerTurn(); !r) emit(EventType::Error, r.error());
    }
    return {};
}

std::expected<void, std::string> HostSession::setSlotSetup(uint32_t id, game::EmpireSetup setup) {
    if (phase_ != HostPhase::Lobby) return std::unexpected(std::string("The game has already started."));
    Slot* s = findSlot(id);
    if (!s) return std::unexpected(std::string("No such slot."));
    if (!s->info.local && s->info.kind != SlotKind::Computer) return std::unexpected(std::string("Players choose their own empire."));
    if (!setup.preset.empty() && !game::findPreset(rules_, setup.preset)) return std::unexpected(std::format("Unknown race '{}'.", setup.preset));
    setup.kind = s->info.kind == SlotKind::Human ? game::PlayerKind::Human : game::PlayerKind::Computer;
    setup.passwordHash.clear();
    s->info.setup = std::move(setup);
    broadcastLobby();
    return {};
}

std::expected<void, std::string> HostSession::setLocalReady(bool ready) {
    for (auto& s : slots_)
        if (s->info.local) {
            if (s->info.ready != ready) {
                s->info.ready = ready;
                broadcastLobby();
            }
            return {};
        }
    return std::unexpected(std::string("The host is not playing in this game."));
}

uint32_t HostSession::localSlot() const {
    for (const auto& s : slots_)
        if (s->info.local) return s->info.id;
    return kNoSlot;
}

std::string HostSession::startProblem(bool force) const {
    if (phase_ != HostPhase::Lobby) return "The game has already started.";
    size_t empires = 0;
    for (const auto& s : slots_) {
        if (s->info.open()) continue;
        ++empires;
        if (s->info.kind != SlotKind::Human) continue;
        if (!s->info.connected) return std::format("{} is not connected.", s->info.player);
        if (!s->info.ready && !force) return std::format("{} is not ready.", s->info.player);
    }
    if (empires == 0) return "The game needs at least one empire.";
    return {};
}

std::expected<void, std::string> HostSession::startGame(bool force) {
    if (std::string problem = startProblem(force); !problem.empty()) return std::unexpected(problem);
    game::GameSetup setup = config_.setup;
    setup.empires.clear();
    for (const auto& s : slots_) {
        if (s->info.open()) continue;
        game::EmpireSetup es = s->info.setup;
        es.kind = s->info.kind == SlotKind::Human ? game::PlayerKind::Human : game::PlayerKind::Computer;
        es.passwordHash = s->info.kind == SlotKind::Human ? s->verifier : std::string{};
        setup.empires.push_back(std::move(es));
    }
    std::expected<game::GameState, std::string> created = std::unexpected(std::string("unknown error"));
    try {
        created = game::createGame(rules_, setup);
    } catch (const std::exception& e) {
        created = std::unexpected(std::string(e.what()));
    }
    if (!created) {
        emit(EventType::Error, "Could not create the game: " + created.error());
        return std::unexpected(created.error());
    }
    std::erase_if(slots_, [](const auto& s) { return s->info.open(); });
    state_ = std::move(*created);
    if (gameCreated_) gameCreated_(*state_);
    orders_.assign(state_->empires.size(), std::nullopt);
    phase_ = HostPhase::Playing;
    for (size_t i = 0; i < slots_.size(); ++i) slots_[i]->info.setup.name = state_->empires[i].name;
    // Turn-based: computer players before the first human take their turns now.
    game::TurnResult opening;
    if (turnBased()) {
        try {
            opening = game::resumeTurnBased(rules_, *state_, liveOptions());
        } catch (const std::exception& e) {
            emit(EventType::Error, std::format("The first turns could not be played: {}", e.what()));
        }
        if (state_->gameOver) phase_ = HostPhase::GameOver;
    }
    stateCache_ = redactedState();
    broadcastLobby();
    beginTurn();
    broadcastState(true);
    broadcastTurnStatus();
    emit(EventType::GameStarted, std::format("{} empires{}", state_->empires.size(), turnBased() ? ", turn-based" : ""), {}, kNoSlot, {},
         state_->turn);
    if (turnBased()) {
        notifyRejections(opening, state_->turn);
        emit(EventType::PlayerTurn, {}, playerName(activeEmpire()), kNoSlot, activeEmpire(), state_->turn);
    }
    return {};
}

// ---- Turns ----------------------------------------------------------------------------------------------------

game::EmpireId HostSession::localEmpire() const { return empireOfSlot(localSlot()); }

game::EmpireId HostSession::empireOfSlot(uint32_t id) const {
    if (!state_) return {};
    for (size_t i = 0; i < slots_.size() && i < state_->empires.size(); ++i)
        if (slots_[i]->info.id == id) return game::EmpireId{static_cast<uint32_t>(i)};
    return {};
}

std::vector<HostSession::StateBlob> HostSession::redactedState() const {
    // Every empire gets its own view (fog of war, game/redact.hpp); the last
    // entry is the spectator view for peers without an empire. Password
    // verifiers never leave the host.
    std::vector<StateBlob> views;
    for (const game::Empire& e : state_->empires)
        views.push_back(std::make_shared<const std::vector<uint8_t>>(game::serializeState(game::redactForEmpire(rules_, *state_, e.id))));
    views.push_back(std::make_shared<const std::vector<uint8_t>>(game::serializeState(game::redactForEmpire(rules_, *state_, game::EmpireId{}))));
    return views;
}

void HostSession::sendState(Peer& peer, bool gameStart, bool resync) {
    proto::State m;
    m.turn = state_->turn;
    m.empire = empireOfSlot(peer.slot);
    m.gameStart = gameStart;
    const size_t view = m.empire.valid() && m.empire.index() + 1 < stateCache_.size() ? m.empire.index() : stateCache_.size() - 1;
    m.state = *stateCache_[view];
    m.serial = nextSerial_++;
    m.resync = resync;
    peer.sentSerial = m.serial;
    peer.sentState = stateCache_[view];
    peer.conn.send(MsgType::State, m);
}

void HostSession::broadcastState(bool gameStart) {
    for (auto& p : peers_)
        if (p->welcomed && !p->closing) sendState(*p, gameStart);
}

void HostSession::refreshTurnStatus() {
    if (!state_) return;
    turnStatus_.turn = state_->turn;
    turnStatus_.secondsLeft = -1;
    if (deadline_) {
        const auto left = std::chrono::duration_cast<std::chrono::seconds>(*deadline_ - Clock::now()).count();
        turnStatus_.secondsLeft = static_cast<int32_t>(std::max<int64_t>(0, left));
    }
    turnStatus_.turnBased = turnBased();
    turnStatus_.active = activeEmpire();
    turnStatus_.empires.clear();
    for (size_t i = 0; i < state_->empires.size(); ++i) {
        const game::Empire& e = state_->empires[i];
        const Slot* s = i < slots_.size() ? slots_[i].get() : nullptr;
        EmpireTurnStatus st;
        st.empire = e.id;
        st.empireName = e.name;
        st.human = e.kind == game::PlayerKind::Human;
        st.player = st.human && s ? s->info.player : std::string{};
        st.alive = e.alive;
        st.connected = s && (s->info.local || s->peer != 0);
        st.aiControl = s && s->info.aiControl;
        if (turnStatus_.turnBased) {
            st.active = e.id == turnStatus_.active;
            st.submitted = !st.active;  // the host expects nothing from the others now
        } else {
            st.submitted = orders_[i].has_value();
        }
        turnStatus_.empires.push_back(std::move(st));
    }
}

void HostSession::broadcastTurnStatus() {
    refreshTurnStatus();
    for (auto& p : peers_)
        if (p->welcomed && !p->closing) p->conn.send(MsgType::TurnStatus, turnStatus_);
    emit(EventType::TurnStatusChanged, {}, {}, kNoSlot, {}, turnStatus_.turn);
}

void HostSession::beginTurn() {
    if (phase_ == HostPhase::Playing && config_.turnTimeoutSeconds > 0)
        deadline_ = Clock::now() + std::chrono::seconds(config_.turnTimeoutSeconds);
    else
        deadline_.reset();
    turnStatus_.processing = false;
    refreshTurnStatus();
}

bool HostSession::allOrdersIn() const {
    if (!state_ || turnStatus_.processing || turnBased()) return false;
    bool anyActive = false;
    for (size_t i = 0; i < state_->empires.size(); ++i) {
        const game::Empire& e = state_->empires[i];
        if (e.kind != game::PlayerKind::Human || !e.alive) continue;
        if (i < slots_.size() && slots_[i]->info.aiControl) continue;
        anyActive = true;
        if (!orders_[i]) return false;
    }
    return anyActive;  // with no active human the host waits for a timeout or a forced turn
}

void HostSession::notifyPlayer(game::EmpireId empire, const std::string& text) {
    if (!empire.valid() || empire.index() >= slots_.size()) return;
    if (Peer* p = peerOfSlot(*slots_[empire.index()]); p && !p->closing) p->conn.send(MsgType::Notice, proto::Notice{text});
}

// Tells players which of their commands were refused.
void HostSession::notifyRejections(const game::TurnResult& result, uint32_t turn) {
    std::map<uint32_t, std::vector<std::string>> refused;
    for (const auto& [empire, why] : result.rejected)
        if (empire.valid()) refused[empire.value].push_back(why);
    for (const auto& [empire, list] : refused) {
        const game::EmpireId e{empire};
        if (e.index() >= state_->empires.size() || state_->empire(e).kind != game::PlayerKind::Human) continue;
        for (size_t i = 0; i < list.size() && i < kMaxRejectionNotices; ++i)
            notifyPlayer(e, std::format("Turn {}: a command was refused: {}", turn, list[i]));
        if (list.size() > kMaxRejectionNotices)
            notifyPlayer(e, std::format("Turn {}: {} more commands were refused.", turn, list.size() - kMaxRejectionNotices));
    }
}

std::expected<void, std::string> HostSession::submitOrders(game::EmpireOrders orders) {
    if (phase_ != HostPhase::Playing || !state_) return std::unexpected(std::string("No turn is open."));
    if (turnBased()) return std::unexpected(std::string("This game is turn-based: play the commands in the empire's turn."));
    if (!orders.empire.valid() || orders.empire.index() >= state_->empires.size()) return std::unexpected(std::string("No such empire."));
    if (orders.turn != state_->turn)
        return std::unexpected(std::format("These orders are for turn {}, but the game is at turn {}.", orders.turn, state_->turn));
    const game::EmpireId e = orders.empire;
    const Slot& s = *slots_[e.index()];
    orders_[e.index()] = std::move(orders);
    emit(EventType::OrdersReceived, {}, s.info.player.empty() ? std::string("host") : s.info.player, s.info.id, e, state_->turn);
    broadcastTurnStatus();
    return {};
}

std::expected<void, std::string> HostSession::processTurnNow() {
    if (phase_ != HostPhase::Playing || !state_) return std::unexpected(std::string("No turn is open."));
    if (turnBased()) return skipPlayerTurn();
    const uint32_t turn = state_->turn;
    turnStatus_.processing = true;
    for (auto& p : peers_)
        if (p->welcomed && !p->closing) p->conn.send(MsgType::TurnStatus, turnStatus_);
    flushAll();
    emit(EventType::TurnProcessing, {}, {}, kNoSlot, {}, turn);

    std::vector<game::EmpireOrders> list;
    int submitted = 0;
    for (auto& o : orders_)
        if (o) {
            list.push_back(std::move(*o));
            ++submitted;
        }
    // A failure inside the rules must not take the host down: keep the old turn.
    game::GameState before = *state_;
    game::TurnResult result;
    try {
        result = game::processTurn(rules_, *state_, list);
    } catch (const std::exception& e) {
        *state_ = std::move(before);
        for (size_t i = 0; i < list.size(); ++i) orders_[list[i].empire.index()] = std::move(list[i]);
        turnStatus_.processing = false;
        broadcastTurnStatus();
        const std::string why = std::format("Processing turn {} failed ({}); the turn was not processed.", turn, e.what());
        emit(EventType::Error, why);
        for (auto& p : peers_)
            if (p->welcomed && !p->closing) p->conn.send(MsgType::Notice, proto::Notice{why});
        return std::unexpected(why);
    }

    notifyRejections(result, turn);
    emit(EventType::Info, std::format("Turn {} processed: {} empires sent orders, {} commands refused.", turn, submitted, result.rejected.size()));
    // The passwords Reset Passwords chose take effect now, after the orders
    // (which carry the players' own passwords) were read (spec 06 §1.9).
    for (const PasswordReset& p : std::exchange(resets_, {}))
        if (p.empire.index() < state_->empires.size()) state_->empire(p.empire).passwordHash = passwordVerifier(hashPassword(p.password));

    orders_.assign(state_->empires.size(), std::nullopt);
    stateCache_ = redactedState();
    if (state_->gameOver) {
        phase_ = HostPhase::GameOver;
        emit(EventType::GameOver, state_->winner.valid() ? std::format("{} wins", state_->empire(state_->winner).name) : std::string{}, {},
             kNoSlot, state_->winner, state_->turn);
    }
    beginTurn();
    broadcastState(false);
    broadcastTurnStatus();
    emit(EventType::NewTurn, {}, {}, kNoSlot, {}, state_->turn);
    return {};
}

void HostSession::setTurnTimeout(int seconds) {
    config_.turnTimeoutSeconds = std::max(0, seconds);
    if (phase_ == HostPhase::Playing) {
        beginTurn();
        broadcastTurnStatus();
    }
    if (phase_ != HostPhase::Stopped) broadcastLobby();
}

std::expected<std::vector<HostSession::PasswordReset>, std::string> HostSession::resetPasswords(const std::vector<game::EmpireId>& empires) {
    resets_.clear();  // a new click discards every reset not applied yet
    if (!state_ || phase_ != HostPhase::Playing) return std::unexpected(std::string("No game is running."));
    if (turnBased()) return std::unexpected(std::string("Reset Passwords is for simultaneous games."));
    std::vector<PasswordReset> out;
    for (game::EmpireId e : empires) {
        if (!e.valid() || e.index() >= state_->empires.size()) return std::unexpected(std::string("No such empire."));
        out.push_back({e, resetPassword()});
    }
    resets_ = out;
    for (const PasswordReset& p : out)
        emit(EventType::Info, std::format("Password Reset: player {} ({}) gets the password {} at the next turn processing.", p.empire.value + 1,
                                          state_->empire(p.empire).name, p.password));
    return out;
}

std::expected<void, std::string> HostSession::setAiControl(game::EmpireId empire, bool ai) {
    if (!state_ || phase_ != HostPhase::Playing) return std::unexpected(std::string("No game is running."));
    if (!empire.valid() || empire.index() >= slots_.size()) return std::unexpected(std::string("No such empire."));
    Slot& s = *slots_[empire.index()];
    if (s.info.kind != SlotKind::Human) return std::unexpected(std::string("That empire is always played by the computer."));
    // "Toggle Empire AI On/Off" flips only the empire's computer-controlled
    // mark (spec 05 §9.4, confirmed: binary): its ministers and individual
    // flags stay. Marked, the empire is played by the computer every turn and
    // the host waits for no orders from it; handed back, it waits for its
    // player (or the host's orders) again. A kicked player's stand-in keeps
    // the mark as it is (Slot::aiControl alone).
    if (!game::ai::setComputerMark(*state_, empire, ai)) return std::unexpected(std::string("That empire cannot change hands."));
    s.info.aiControl = ai;
    broadcastLobby();
    broadcastTurnStatus();
    emit(EventType::Info, std::format("{} is now played by {}.", state_->empire(empire).name, ai ? "the computer" : "its player"));
    if (turnBased()) {
        // The computer plays the rest of that empire's turn in progress; a
        // player back at the controls while the host waits starts playing.
        if (ai && activeEmpire() == empire) return skipPlayerTurn();
        if (!ai && !state_->playerTurn.started && anyHumanToPlay()) return turnBasedStep([] { return game::TurnResult{}; });
    }
    return {};
}

// ---- Turn-based games ------------------------------------------------------------------------------------------

bool HostSession::turnBased() const { return state_ && game::turnBased(*state_); }

game::EmpireId HostSession::activeEmpire() const {
    if (!turnBased() || state_->gameOver || !state_->playerTurn.started) return {};
    return state_->playerTurn.empire;
}

// Human empires handed to the computer (kicked, or by an administrator).
game::LiveOptions HostSession::liveOptions() const {
    game::LiveOptions o;
    for (size_t i = 0; i < slots_.size() && state_ && i < state_->empires.size(); ++i)
        if (slots_[i]->info.kind == SlotKind::Human && slots_[i]->info.aiControl) o.computerPlays.push_back(game::EmpireId{static_cast<uint32_t>(i)});
    return o;
}

bool HostSession::anyHumanToPlay() const {
    if (!state_) return false;
    const game::LiveOptions o = liveOptions();
    return std::any_of(state_->empires.begin(), state_->empires.end(),
                       [&](const game::Empire& e) { return e.alive && e.kind == game::PlayerKind::Human && !o.computerPlaysFor(e.id); });
}

std::string HostSession::playerName(game::EmpireId empire) const {
    if (!empire.valid() || empire.index() >= slots_.size() || slots_[empire.index()]->info.kind != SlotKind::Human) return {};
    return slots_[empire.index()]->info.player;
}

std::expected<game::TurnResult, std::string> HostSession::playCommands(game::EmpireId empire, std::vector<game::Command> commands) {
    if (phase_ != HostPhase::Playing || !state_) return std::unexpected(std::string("No turn is open."));
    if (!turnBased()) return std::unexpected(std::string("This game is simultaneous: submit orders for the turn instead."));
    if (!empire.valid() || empire.index() >= state_->empires.size()) return std::unexpected(std::string("No such empire."));
    if (activeEmpire() != empire) return std::unexpected(std::string("It is not that empire's turn."));
    const std::string who = playerName(empire);
    emit(EventType::OrdersReceived, std::format("{} command{}", commands.size(), commands.size() == 1 ? "" : "s"),
         who.empty() ? std::string("host") : who, empire.index() < slots_.size() ? slots_[empire.index()]->info.id : kNoSlot, empire,
         state_->turn);
    try {
        return runLive(empire, commands);
    } catch (const std::exception& e) {
        return std::unexpected(std::format("The commands could not be carried out ({}); nothing was changed.", e.what()));
    }
}

// Carries the commands out one after another. The player gets its new view,
// and so does everyone who fought in a battle they started (inferred: the
// others see the game when the turn passes on). Throws when the rules fail;
// the state is then as before.
game::TurnResult HostSession::runLive(game::EmpireId empire, const std::vector<game::Command>& commands) {
    const size_t battlesBefore = state_->combats.size();
    game::GameState before = *state_;
    game::TurnResult all;
    try {
        for (const game::Command& c : commands) {
            game::TurnResult r = game::applyLive(rules_, *state_, empire, c);
            for (auto& x : r.rejected) all.rejected.push_back(std::move(x));
            for (auto& q : r.questions) all.questions.push_back(q);
        }
    } catch (const std::exception& e) {
        *state_ = std::move(before);
        emit(EventType::Error, std::format("Turn {}: commands of {} failed: {}", state_->turn, state_->empire(empire).name, e.what()));
        throw;
    }
    std::vector<game::EmpireId> changed{empire};
    for (size_t i = battlesBefore; i < state_->combats.size(); ++i)
        for (game::EmpireId p : state_->combats[i].participants)
            if (p.valid() && p.index() < state_->empires.size() && std::find(changed.begin(), changed.end(), p) == changed.end())
                changed.push_back(p);
    // One view per empire and a spectator's; an empire founded meanwhile (a rebel colony) renews them all.
    if (stateCache_.size() != state_->empires.size() + 1) stateCache_ = redactedState();
    for (game::EmpireId e : changed) {
        stateCache_[e.index()] = std::make_shared<const std::vector<uint8_t>>(game::serializeState(game::redactForEmpire(rules_, *state_, e)));
        emit(EventType::StateUpdated, e == empire ? std::string("its own commands") : std::string("a battle"), playerName(e), kNoSlot, e,
             state_->turn);
        if (e.index() >= slots_.size()) continue;
        if (Peer* p = peerOfSlot(*slots_[e.index()]); p && !p->closing) sendState(*p, false);
    }
    return all;
}

std::expected<void, std::string> HostSession::endPlayerTurn(game::EmpireId empire) {
    if (phase_ != HostPhase::Playing || !state_) return std::unexpected(std::string("No turn is open."));
    if (!turnBased()) return std::unexpected(std::string("This game is simultaneous: submit orders for the turn instead."));
    if (!empire.valid() || activeEmpire() != empire) return std::unexpected(std::string("It is not that empire's turn."));
    return turnBasedStep([&] { return game::endPlayerTurn(rules_, *state_, empire, liveOptions()); });
}

// The player whose turn it is runs out of time, is forced on by the host or
// handed to the computer: the computer plays the rest of that turn, as it
// plays a player whose orders are missing (spec 05 §7.1, §9.2; inferred for
// turn-based games). With no turn in progress, one game turn is played.
std::expected<void, std::string> HostSession::skipPlayerTurn() {
    if (phase_ != HostPhase::Playing || !state_) return std::unexpected(std::string("No turn is open."));
    const game::EmpireId e = activeEmpire();
    if (!e.valid()) {
        emit(EventType::Info, std::format("Turn {}: playing on without the players.", state_->turn));
        return turnBasedStep([&] { return game::resumeTurnBased(rules_, *state_, liveOptions()); });
    }
    emit(EventType::Info, std::format("Turn {}: the computer plays the rest of {}'s turn.", state_->turn, state_->empire(e).name));
    return turnBasedStep([&] {
        game::LiveOptions o = liveOptions();
        if (!o.computerPlaysFor(e)) o.computerPlays.push_back(e);
        return game::endPlayerTurn(rules_, *state_, e, o);
    });
}

// Runs a step that passes the turn on, then plays on to the next player who
// plays; everyone gets their view, and the new turn status.
std::expected<void, std::string> HostSession::turnBasedStep(const std::function<game::TurnResult()>& step) {
    const uint32_t turnBefore = state_->turn;
    const game::EmpireId activeBefore = activeEmpire();
    turnStatus_.processing = true;
    for (auto& p : peers_)
        if (p->welcomed && !p->closing) p->conn.send(MsgType::TurnStatus, turnStatus_);
    flushAll();
    emit(EventType::TurnProcessing, {}, {}, kNoSlot, activeBefore, turnBefore);

    game::GameState before = *state_;
    game::TurnResult result;
    try {
        result = step();
        // A step that stopped between game turns (a stand-in played the last
        // human's turn) goes on to the next player who plays.
        if (!state_->gameOver && !state_->playerTurn.started && anyHumanToPlay()) {
            game::TurnResult more = game::resumeTurnBased(rules_, *state_, liveOptions());
            for (auto& x : more.rejected) result.rejected.push_back(std::move(x));
        }
    } catch (const std::exception& e) {
        *state_ = std::move(before);
        turnStatus_.processing = false;
        broadcastTurnStatus();
        const std::string why = std::format("Turn {} could not go on ({}); nothing was changed.", turnBefore, e.what());
        emit(EventType::Error, why);
        for (auto& p : peers_)
            if (p->welcomed && !p->closing) p->conn.send(MsgType::Notice, proto::Notice{why});
        return std::unexpected(why);
    }
    notifyRejections(result, turnBefore);
    stateCache_ = redactedState();
    if (state_->gameOver) {
        phase_ = HostPhase::GameOver;
        emit(EventType::GameOver, state_->winner.valid() ? std::format("{} wins", state_->empire(state_->winner).name) : std::string{}, {},
             kNoSlot, state_->winner, state_->turn);
    }
    beginTurn();
    broadcastState(false);
    broadcastTurnStatus();
    if (state_->turn != turnBefore) emit(EventType::NewTurn, {}, {}, kNoSlot, {}, state_->turn);
    const game::EmpireId now = activeEmpire();
    if (now != activeBefore || state_->turn != turnBefore)
        emit(EventType::PlayerTurn, {}, playerName(now), kNoSlot, now, state_->turn);
    return {};
}

void HostSession::chat(std::string_view text) {
    const std::string clean = proto::sanitize(text, kMaxChatLength);
    if (clean.empty() || phase_ == HostPhase::Stopped) return;
    const std::string from = config_.localPlayer ? config_.localPlayer->name : std::string("Host");
    for (auto& p : peers_)
        if (p->welcomed && !p->closing) p->conn.send(MsgType::Chat, proto::Chat{from, clean});
    emit(EventType::Chat, clean, from, localSlot());
}

game::SaveInfo HostSession::saveInfo() const {
    game::SaveInfo info;
    info.gameName = config_.gameName;
    info.dataSet = config_.dataSet;
    info.gameId = gameId_;
    info.masterPasswordVerifier = passwordVerifier(config_.masterPasswordHash);
    if (state_)
        for (size_t i = 0; i < state_->empires.size(); ++i)
            info.players.push_back(i < slots_.size() && slots_[i]->info.kind == SlotKind::Human ? slots_[i]->info.player : std::string{});
    return info;
}

std::expected<void, std::string> HostSession::save(const std::filesystem::path& file) const {
    if (!state_) return std::unexpected(std::string("There is no game to save yet."));
    return game::saveGame(file, *state_, saveInfo());
}

} // namespace opense4::net
