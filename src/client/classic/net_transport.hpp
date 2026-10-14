#pragma once

// Connects a ClassicSession to a network game (src/net): as the hosting
// player (the HostSession runs in this process) or as a joined player.

#include "client/classic/session.hpp"
#include "net/client.hpp"
#include "net/host.hpp"

#include <chrono>
#include <deque>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace opense4::client::classic {

// Chat and status lines shared by both transports (newest last).
class NetLog {
public:
    void add(std::string line);
    const std::deque<std::string>& lines() const { return lines_; }

private:
    std::deque<std::string> lines_;
};

// Who hears a network event (networkSound): this computer's side of the game.
struct NetSoundSide {
    bool hosting = false;          // a TCP/IP host (players connect to us)
    uint32_t localSlot = net::kNoSlot;   // the host's own slot (its own chat lines)
    std::string localName;         // a player's own name (the host sends our lines back)
    game::EmpireId me;             // the empire this computer plays
    bool turnBased = false;
};

// The sound a network event plays (docs/spec/06 §5.5, confirmed: binary), or
// empty: `openwp` on the host when a player connects (or comes back),
// `closewp` when one leaves; `cloakon` when another's chat line arrives; and
// the turn alert `destsun` when this computer's player has a turn to play: a
// new turn of a simultaneous game, or a turn-based game's turn passing to us.
// Sending a chat line plays `ordbtn` where it is sent.
std::string_view networkSound(const net::Event& e, const NetSoundSide& side);

class NetTransport : public TurnTransport {
public:
    virtual NetLog& log() = 0;
    virtual void chat(std::string_view text) = 0;
    // Who we are waiting for this turn ("Waiting for: Alice, Bob").
    virtual const net::TurnStatus& turnStatus() const = 0;
    virtual bool hosting() const = 0;
    // The sounds the events polled since the last call make (networkSound),
    // in order, for the game to play; and whether the turn alert was among
    // them: the game window is brought back then if it is minimized
    // (docs/spec/06 §5.5).
    std::vector<std::string> takeSounds() { return std::exchange(sounds_, {}); }
    bool takeTurnAlert() { return std::exchange(turnAlert_, false); }

protected:
    // Notes the sound an event makes on this side, if any.
    void sound(const net::Event& e, const NetSoundSide& side);

private:
    std::vector<std::string> sounds_;
    bool turnAlert_ = false;
};

class HostTransport final : public NetTransport {
public:
    HostTransport(std::shared_ptr<const game::Rules> rules, std::unique_ptr<net::HostSession> host);
    ~HostTransport() override;

    void submitOrders(const game::EmpireOrders& orders) override;
    void playCommand(const game::Command& c) override;
    void endPlayerTurn() override;
    std::optional<game::GameState> pollState() override;
    std::string status() const override;
    NetLog& log() override { return log_; }
    void chat(std::string_view text) override;
    const net::TurnStatus& turnStatus() const override { return host_->turnStatus(); }
    bool hosting() const override { return true; }
    net::HostSession& host() { return *host_; }
    const net::HostSession& host() const { return *host_; }

private:
    std::shared_ptr<const game::Rules> rules_;  // outlives host_
    std::unique_ptr<net::HostSession> host_;
    NetLog log_;
    bool fresh_ = false;  // turn-based: our view changed since the last pollState()
};

class ClientTransport final : public NetTransport {
public:
    explicit ClientTransport(std::unique_ptr<net::ClientSession> client);
    ~ClientTransport() override;

    void submitOrders(const game::EmpireOrders& orders) override;
    void playCommand(const game::Command& c) override;
    void endPlayerTurn() override;
    std::optional<game::GameState> pollState() override;
    std::string status() const override;
    NetLog& log() override { return log_; }
    void chat(std::string_view text) override { client_->chat(text); }
    const net::TurnStatus& turnStatus() const override { return client_->turnStatus(); }
    bool hosting() const override { return false; }
    net::ClientSession& client() { return *client_; }

private:
    std::unique_ptr<net::ClientSession> client_;
    NetLog log_;
    bool rejected_ = false;
    uint32_t handedTurn_ = 0;  // the turn of the last state pollState() returned
    std::chrono::steady_clock::time_point lastAttempt_{};
};

// "Waiting for: Alice, Bob" (empty when nobody is awaited). Turn-based
// games: whose turn it is (empty when it is `me`'s).
std::string waitingFor(const net::TurnStatus& t, game::EmpireId me = {});

} // namespace opense4::client::classic
