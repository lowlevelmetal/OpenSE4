#include "net/discovery.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <format>
#include <string_view>

namespace opense4::net {

namespace {

constexpr std::string_view kQuery = "OPENSE4-DISCOVER 1";
constexpr std::string_view kReply = "OPENSE4-GAME 1";
constexpr size_t kMaxPacket = 1024;
constexpr size_t kMaxGames = 64;

std::span<const uint8_t> bytes(const std::string& s) { return {reinterpret_cast<const uint8_t*>(s.data()), s.size()}; }

// Values may not contain line breaks; long names are cut.
std::string clean(std::string_view v, size_t max = 64) {
    std::string out;
    for (char c : v.substr(0, max))
        if (c != '\n' && c != '\r') out += c;
    return out;
}

template <class T>
bool number(std::string_view v, T& out) {
    const auto [p, ec] = std::from_chars(v.data(), v.data() + v.size(), out);
    return ec == std::errc{} && p == v.data() + v.size();
}

} // namespace

std::string encodeQuery() { return std::string(kQuery); }

bool isQuery(std::string_view packet) { return packet.substr(0, kQuery.size()) == kQuery; }

std::string encodeGame(const LanGame& g) {
    return std::format("{}\nname={}\nport={}\nversion={}\ndata={}\nplayers={}\nslots={}\nstarted={}\npassword={}\n", kReply, clean(g.gameName),
                       g.port, clean(g.version), clean(g.dataSet, 96), g.players, g.slots, g.started ? 1 : 0, g.password ? 1 : 0);
}

std::optional<LanGame> decodeGame(std::string_view packet) {
    if (packet.size() > kMaxPacket || packet.substr(0, kReply.size()) != kReply) return std::nullopt;
    LanGame g;
    bool havePort = false;
    size_t pos = kReply.size();
    while (pos < packet.size()) {
        size_t end = packet.find('\n', pos);
        if (end == std::string_view::npos) end = packet.size();
        const std::string_view line = packet.substr(pos, end - pos);
        pos = end + 1;
        const size_t eq = line.find('=');
        if (eq == std::string_view::npos) continue;
        const std::string_view key = line.substr(0, eq), value = line.substr(eq + 1);
        if (key == "name") g.gameName = clean(value);
        else if (key == "port") havePort = number(value, g.port) && g.port != 0;
        else if (key == "version") g.version = clean(value);
        else if (key == "data") g.dataSet = clean(value, 96);
        else if (key == "players") number(value, g.players);
        else if (key == "slots") number(value, g.slots);
        else if (key == "started") g.started = value == "1";
        else if (key == "password") g.password = value == "1";
    }
    if (!havePort) return std::nullopt;
    return g;
}

// ---- Host ------------------------------------------------------------------------------------

bool DiscoveryResponder::start(uint16_t udpPort) {
    auto s = openUdp(udpPort, /*shared=*/true);
    if (!s) return false;
    socket_ = std::move(*s);
    return true;
}

void DiscoveryResponder::poll(const LanGame& game) {
    if (!socket_.valid()) return;
    std::array<uint8_t, kMaxPacket> buffer{};
    const std::string reply = encodeGame(game);
    // A bounded number per call: a flood of queries cannot stall the host.
    for (int i = 0; i < 32; ++i) {
        const auto d = receiveDatagram(socket_, buffer);
        if (!d) break;
        if (isQuery(std::string_view(reinterpret_cast<const char*>(buffer.data()), d->bytes))) sendDatagram(socket_, d->address, d->port, bytes(reply));
    }
}

// ---- Client ----------------------------------------------------------------------------------

bool DiscoveryBrowser::start(uint16_t udpPort) {
    port_ = udpPort;
    auto s = openUdp(0, false);
    if (!s) return false;
    socket_ = std::move(*s);
    sendQuery();
    return true;
}

void DiscoveryBrowser::sendQuery() {
    if (!socket_.valid()) return;
    lastQuery_ = std::chrono::steady_clock::now();
    const std::string q = encodeQuery();
    sendDatagram(socket_, "255.255.255.255", port_, bytes(q));
    // Also ask this machine directly (broadcasts may not loop back everywhere).
    sendDatagram(socket_, "127.0.0.1", port_, bytes(q));
}

void DiscoveryBrowser::refresh() {
    games_.clear();
    sendQuery();
}

void DiscoveryBrowser::poll() {
    if (!socket_.valid()) return;
    std::array<uint8_t, kMaxPacket> buffer{};
    for (int i = 0; i < 64; ++i) {
        const auto d = receiveDatagram(socket_, buffer);
        if (!d) break;
        auto g = decodeGame(std::string_view(reinterpret_cast<const char*>(buffer.data()), d->bytes));
        if (!g) continue;
        g->address = d->address;
        // A game on this machine answers both the broadcast (from its LAN
        // address) and the loopback query: keep one entry, the LAN address.
        const bool loopback = g->address == "127.0.0.1";
        auto sameHost = [&](const LanGame& o) { return o.address == g->address && o.port == g->port; };
        auto localTwin = [&](const LanGame& o) {
            return o.port == g->port && o.gameName == g->gameName && o.dataSet == g->dataSet && (loopback || o.address == "127.0.0.1");
        };
        if (auto it = std::find_if(games_.begin(), games_.end(), sameHost); it != games_.end()) {
            *it = *g;
        } else if (auto twin = std::find_if(games_.begin(), games_.end(), localTwin); twin != games_.end()) {
            if (!loopback) *twin = *g;  // replace the loopback entry with the LAN address
        } else if (games_.size() < kMaxGames) {
            games_.push_back(*g);
        }
    }
    if (std::chrono::steady_clock::now() - lastQuery_ > std::chrono::seconds(5)) sendQuery();
}

} // namespace opense4::net
