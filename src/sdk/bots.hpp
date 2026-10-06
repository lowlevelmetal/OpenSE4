#pragma once

// External bots' connection to the game (docs/sdk/ai-protocol.md §10,
// docs/sdk/bots-and-arena.md): a computer player that runs as a program of
// its own, on CPython with any library, connects over TCP and plays the
// empires whose controller is `external:<slot>` for its slot.
//
// The host listens on this computer only unless told otherwise
// (BotHostOptions::bind), and every bot must present the host's token, a
// random secret the host gives its bots on their command line or in their
// environment. Each message is one line of strict JSON (script/json.hpp):
//
//   bot  -> host  {"hello": {"api": 1, "token": "...", "slot": 0, "name": "..."}}
//   host -> bot   {"welcome": {"api": 1, "slot": 0, "game": "...", "timeout_ms": 60000}}
//                 or {"refused": {"message": "..."}}, and the host closes the connection
//   host -> bot   {"request": <request, §3>, "id": 7}
//   bot  -> host  {"service": {"name": "query", "args": {...}}, "id": 7}   (any number, §6)
//   host -> bot   {"result": <value>, "id": 7}
//                 or {"service_error": {"type": "ValueError", "message": "..."}, "id": 7}
//   bot  -> host  {"response": <response, §4>, "id": 7}
//   either way    {"bye": {"reason": "..."}}, then the connection closes
//
// A request the bot does not answer within the host's time fails (§7): the
// classic AI answers it, and the bot's late messages about it are recognised
// by their id and dropped. A connection that breaks, or a bot that says bye,
// fails the request in progress; the bot may connect again.

#include "script/value.hpp"
#include "sdk/players.hpp"

#include <chrono>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace opense4::sdk {

// Where bots connect when the host names no port (opense4-server, the client).
inline constexpr uint16_t kDefaultBotPort = 6722;

struct BotHostOptions {
    // The address to listen on: this computer only by default. Any other
    // (such as "0.0.0.0") lets bots on other computers connect, without
    // encryption: the token is then all that keeps others out.
    std::string bind = "127.0.0.1";
    uint16_t port = 0;                 // 0: any free port (BotHost::port)
    std::string token;                 // the token bots present; empty: a new random one
    std::string gameName;              // told to bots in `welcome`
    // How long a bot may take over one request (the host's turn timer).
    std::chrono::milliseconds requestTimeout{60'000};
    // How long a new connection may take to say hello.
    std::chrono::milliseconds handshakeTimeout{10'000};
    // The longest message a bot may send.
    size_t maxMessageBytes = size_t{64} << 20;
    // The slots the game has, for a bot that asks for none (lowest free
    // first; empty: any number).
    std::vector<uint32_t> slots;
    // Where connections, refusals and drops are told (default: the log).
    std::function<void(const std::string&)> report;
};

class BotHost {
public:
    // Starts listening. The host accepts bots on a thread of its own until
    // it is destroyed; requests run on the caller's thread.
    static std::expected<std::unique_ptr<BotHost>, std::string> open(BotHostOptions options);
    ~BotHost();
    BotHost(const BotHost&) = delete;
    BotHost& operator=(const BotHost&) = delete;

    uint16_t port() const;
    const std::string& token() const;
    // "127.0.0.1:6722": the address bots connect to.
    std::string address() const;

    // The bot connected to a slot, as the sessions ask it (null: none).
    ExternalBot* bot(uint32_t slot);
    bool connected(uint32_t slot) const;
    std::vector<uint32_t> connectedSlots() const;
    // Waits until a bot is connected to each slot; false when the time ran out.
    bool waitFor(std::span<const uint32_t> slots, std::chrono::milliseconds timeout) const;
    void setRequestTimeout(std::chrono::milliseconds timeout);
    std::chrono::milliseconds requestTimeout() const;
    // Says goodbye to every bot ({"bye": details}, details a map) and closes
    // their connections. Bots that connect later are welcome as before.
    void sayGoodbye(const script::Value& details);

    // `base` with this host's bots as the external slots' players.
    PlayerSetup playerSetup(PlayerSetup base = {});

    struct Impl;

private:
    explicit BotHost(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

// A new token: 32 hexadecimal digits from the system's cryptographic source.
std::string newBotToken();

// Reads the slots out of controllers: the external ones' slot numbers, sorted, each once.
std::vector<uint32_t> externalSlots(std::span<const game::EmpireSetup> empires);

} // namespace opense4::sdk
