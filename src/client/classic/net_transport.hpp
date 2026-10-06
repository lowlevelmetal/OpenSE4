#pragma once

// Connects a ClassicSession to a network game (src/net): as the hosting
// player (the HostSession runs in this process) or as a joined player.

#include "client/classic/session.hpp"
#include "net/client.hpp"
#include "net/host.hpp"

#include <chrono>
#include <deque>
#include <memory>

namespace opense4::client::classic {

// Chat and status lines shared by both transports (newest last).
class NetLog {
public:
    void add(std::string line);
    const std::deque<std::string>& lines() const { return lines_; }

private:
    std::deque<std::string> lines_;
};

class NetTransport : public TurnTransport {
public:
    virtual NetLog& log() = 0;
    virtual void chat(std::string_view text) = 0;
    // Who we are waiting for this turn ("Waiting for: Alice, Bob").
    virtual const net::TurnStatus& turnStatus() const = 0;
    virtual bool hosting() const = 0;
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
