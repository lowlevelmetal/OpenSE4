#pragma once

// Finding games on the local network (docs/MULTIPLAYER.md). A player's client
// broadcasts a short query on UDP port 6716 (the port the classic game used
// for its control traffic); every host on the LAN answers with a one-packet
// description of its game. Nothing here is needed to play: players can always
// type the host's address.

#include "net/socket.hpp"

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace opense4::net {

inline constexpr uint16_t kDiscoveryPort = 6716;

struct LanGame {
    std::string address;       // where the reply came from
    uint16_t port = 0;         // the game's TCP port
    std::string gameName;
    std::string version;       // appVersion() of the host
    std::string dataSet;       // game::dataSetIdentity() of the host's rules
    uint32_t players = 0;      // human slots taken
    uint32_t slots = 0;        // human slots
    bool started = false;
    bool password = false;     // a join password is needed

    bool operator==(const LanGame&) const = default;
};

// The wire format: short "key=value" lines (bounded; unknown keys ignored).
std::string encodeQuery();
bool isQuery(std::string_view packet);
std::string encodeGame(const LanGame& g);
std::optional<LanGame> decodeGame(std::string_view packet);

// Host side: answers queries. start() fails quietly (discovery off) when the
// port cannot be opened.
class DiscoveryResponder {
public:
    bool start(uint16_t udpPort = kDiscoveryPort);
    bool running() const { return socket_.valid(); }
    // Answers every pending query with `game` (address is filled by the client).
    void poll(const LanGame& game);
    void stop() { socket_.close(); }
    NativeSocket native() const { return socket_.native(); }

private:
    Socket socket_;
};

// Client side: broadcasts queries and gathers the answers.
class DiscoveryBrowser {
public:
    // Opens the socket and sends the first query.
    bool start(uint16_t udpPort = kDiscoveryPort);
    // Forgets the list and asks again.
    void refresh();
    // Collects answers; asks again every few seconds while open.
    void poll();
    const std::vector<LanGame>& games() const { return games_; }
    bool running() const { return socket_.valid(); }

private:
    void sendQuery();
    Socket socket_;
    uint16_t port_ = kDiscoveryPort;
    std::vector<LanGame> games_;
    std::chrono::steady_clock::time_point lastQuery_{};
};

} // namespace opense4::net
