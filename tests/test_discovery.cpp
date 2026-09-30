// LAN discovery (net/discovery.hpp).

#include "net/discovery.hpp"

#include <doctest/doctest.h>

#include <chrono>
#include <thread>

using namespace opense4;
using namespace opense4::net;

TEST_CASE("discovery: wire format round trip and rejection of bad packets") {
    LanGame g;
    g.port = 6720;
    g.gameName = "Friday night";
    g.version = "OpenSE4 0.1.0";
    g.dataSet = "se4/Data#1234";
    g.players = 2;
    g.slots = 4;
    g.started = true;
    g.password = true;
    const auto back = decodeGame(encodeGame(g));
    REQUIRE(back.has_value());
    CHECK(*back == g);

    CHECK(isQuery(encodeQuery()));
    CHECK_FALSE(isQuery("hello"));
    CHECK_FALSE(decodeGame("OPENSE4-GAME 1\nname=x\n").has_value());      // no port
    CHECK_FALSE(decodeGame("SOMETHING ELSE\nport=6720\n").has_value());  // wrong magic
    CHECK_FALSE(decodeGame(std::string(5000, 'x')).has_value());          // oversized
    // Line breaks in names cannot forge extra fields.
    g.gameName = "evil\nport=1";
    const auto clean = decodeGame(encodeGame(g));
    REQUIRE(clean.has_value());
    CHECK(clean->port == 6720);
}

TEST_CASE("discovery: a browser finds a responder on this machine") {
    // A free UDP port above the classic one, so a real game on this machine is not disturbed.
    DiscoveryResponder responder;
    uint16_t port = 0;
    for (uint16_t p = 46716; p < 46816 && !port; ++p)
        if (responder.start(p)) port = p;
    REQUIRE(port != 0);
    LanGame game;
    game.port = 7000;
    game.gameName = "Test game";
    game.slots = 3;

    DiscoveryBrowser browser;
    REQUIRE(browser.start(port));
    bool found = false;
    for (int i = 0; i < 200 && !found; ++i) {
        responder.poll(game);
        browser.poll();
        for (const LanGame& g : browser.games()) found = found || (g.gameName == "Test game" && g.port == 7000);
        if (!found) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    CHECK(found);
    CHECK(browser.games().size() == 1);  // broadcast and loopback answers are merged
}
