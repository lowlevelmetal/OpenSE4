#pragma once

// Script computer players as the client shows them (OpenSE4's own,
// docs/sdk/python-api.md "Watching a player think"):
//
//   - their notes (a response's `notes`, game::Empire::aiNotes), for the AI
//     notes view: on the system and galaxy maps and in the reports, while the
//     setting is on (Settings, Modding; Ctrl+Shift+N). Notes live in the whole
//     game the computer running the players holds: a local or hotseat game,
//     or the host's game when this computer hosts a network game;
//   - their failures (sdk::PlayerSetup::failures), for the notice the main
//     window shows and the Computer Player Errors window.
//
// No drawing here (tests/sdk/test_sdk_client_players.cpp checks it).

#include "game/state.hpp"
#include "sdk/players.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace opense4::client::classic {

class ClassicSession;

// ---- Notes ---------------------------------------------------------------------------------------

struct ShownNote {
    game::EmpireId empire;              // whose player wrote it
    std::string author;                 // that empire, and its player: "Amon'krie Continuum (Captain)"
    std::string kind;                   // what it is about: vehicle, fleet, object, system, empire, design, message
    int64_t object = -1;                // that thing's id; -1: nothing in particular
    std::string subject;                // its name ("Scout 1", "Sol"); empty for nothing in particular
    game::SystemId system;              // the system it is in, or is (none: an empire, a design...)
    std::optional<game::Sector> sector; // its sector in that system, when it is somewhere in it
    uint32_t turn = 0;                  // the game turn it was given
    std::string text;
};

// The whole game this computer holds, where the players' notes are kept: a
// local or hotseat game's state, or the host's own when this computer hosts a
// network game; null for a network player's or a play-by-e-mail copy.
const game::GameState* wholeGame(const ClassicSession& session);

// Every note of the empires that script or external players play in `s`, the
// empires in order and each one's notes in the order they were given.
std::vector<ShownNote> computerPlayerNotes(const game::GameState& s);
// The notes about one thing: `kind` "vehicle", "fleet" or "object" (a
// stellar object; a colony's notes are its planet's), and its id.
std::vector<ShownNote> notesAbout(std::span<const ShownNote> notes, std::string_view kind, int64_t id);
// "Scout 1: exploring" (or the text alone, for nothing in particular).
std::string noteLine(const ShownNote& n);

// ---- Failures ------------------------------------------------------------------------------------

// A failure the SDK tells of (sdk::PlayerSetup::failures): kept for the game
// being played. Safe from any thread.
void notePlayerFailure(const sdk::PlayerFailure& f);
// The failures of the game being played, oldest first.
std::vector<sdk::PlayerFailure> playerFailures();
size_t playerFailureCount();
// The game ended: its failures are forgotten. Those told afterwards belong to
// the next game (a turn-based game's computer players may play before its
// first human, as it starts).
void forgetPlayerFailures();
// The notice of a failure in one line: who failed, in what, and how.
// "Captain (test.ai-fixture), playing Amon'krie Continuum, failed in its orders: ValueError: no ships. The classic AI answered."
std::string failureNotice(const sdk::PlayerFailure& f);

} // namespace opense4::client::classic
