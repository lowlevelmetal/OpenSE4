#pragma once

// Binary save format for GameState and order lists (the `.gam` and `.plr`
// equivalents, docs/spec/05 §9.5). Little-endian, versioned, checksummed.
//
// Every blob starts with a 32-byte envelope:
//
//     offset  size  field
//          0     8  magic ("OSE4STAT" state, "OSE4ORDR" orders, "OSE4SAVE" save file)
//          8     4  u32 format version (kSaveVersion)
//         12     4  u32 flags (0)
//         16     8  u64 payload size
//         24     8  u64 FNV-1a 64 of the payload
//         32     n  payload (serialize_io.hpp)
//
// Readers reject wrong magic, unsupported versions, truncation, checksum
// mismatches and malformed payloads with a readable message, and never crash
// on hostile input. See docs/MULTIPLAYER.md.

#include "game/commands.hpp"
#include "game/state.hpp"

#include <cstdint>
#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace opense4::game {

class Rules;

// Current and oldest readable version of the binary format (save files,
// order files and network messages share it). Bump kSaveVersion when a
// change to the serialized structs makes older files unreadable.
// Version 2: colony anger in whole percent (was tenths), no riot counter,
// atmosphere counter counts up (spec 02 §2, §4); research and intelligence
// pools, the trade counter, the Research - Steal target area, and the
// Technology Cost and Score Display options (spec 05); vehicle and fleet
// experience tenths, the sector a vehicle came from, colony militia and
// obstacle combat pieces (spec 04); the computer players' memory, relation
// counters and flags, difficulty, ministers, and the random-players option
// (spec 05 §7); planet orders (Colony::orders, spec 03 §12); the Quadrant
// Size and All Planets Same Size options (spec 01 §2); mood events waiting
// for an empire's next happiness update (GameState::pendingMood, spec 05 §8).
// Version 3: one object slot order for every object (SpaceObject::slot) and
// fleets whose orders are their members' copies, with their own location
// (spec 03 §19 Q62, Q65); the recorded home sector, design "ever built"
// marks, dated messages and the computer players' demand counts (spec 05
// Q42, Q41, Q37, Q49); per-empire interface options and facilities left on
// abandoned planets (spec 06 §1.9, §7).
// Version 4: colony cloaking (Colony::cloaked and its cloak and sensor
// levels), colony order Repeat, Convert Resources amounts on orders, ground
// combat counts per round in battle records, log entry targets and messages,
// combat piece damage, and the windows' sort history (spec 01 §6.9, spec 02
// §5.6, spec 06 §7 Q24, Q33, Q41-Q43).
// Version 5: design Number Scrapped, the Scrap window's actions as orders,
// vehicle headings, combat event flags, the Ships\Units window's tab and
// switches, the minister flag on messages, empire-file strategies; the unused
// vehicle target fields are gone (spec 03 §15, spec 06 §7 Q56, Q62, spec 05 Q52).
// Version 6: ship arrival stamps per system (Vehicle::arrival,
// GameState::arrivals), the computer players' Seek and Join Fleet orders;
// happiness events waiting for the next update are no longer saved (spec 05
// §7.5, spec 02 §4).
// Version 7: each empire's e-mail address (Empire::email, EmpireSetup::email
// and the Change Email command cmd::SetEmail, spec 06 §7 Q95); the neutral mark
// of a neutral empire a human plays (Empire::neutral) and the fleets' flag in a
// player's minister switches (cmd::SetMinisters::fleets) (spec 06 §7 Q84); the
// "Players can see the complete tech tree" option (GameOptions::completeTechTree,
// spec 06 §7 Q92); the designer's To Hit Modifiers and Condensed View options
// (InterfaceOptions::designToHit, designCondensed, spec 06 §7 Q94).
inline constexpr uint32_t kSaveVersion = 7;
inline constexpr uint32_t kMinSaveVersion = 7;

inline constexpr size_t kEnvelopeSize = 32;

std::vector<uint8_t> serializeState(const GameState& s);
std::expected<GameState, std::string> deserializeState(std::span<const uint8_t> bytes);

std::vector<uint8_t> serializeOrders(const EmpireOrders& o);
std::expected<EmpireOrders, std::string> deserializeOrders(std::span<const uint8_t> bytes);

// Structural consistency of a state: every id a state refers to exists
// (empires, systems, objects, designs, vehicles sorted by id, damage per
// design entry, ...). With `rules`, also the data-set indices (hulls,
// components, facilities, tech areas). Returns what is wrong, or empty.
// deserializeState() runs the structural part, so a crafted file cannot make
// the engine index out of bounds; callers that know the rules (hosts loading
// a save) should run the full check.
std::string validateState(const GameState& s, const Rules* rules = nullptr);

// Stable hash of the whole state (desync detection): FNV-1a 64 of the
// serialized payload, identical on every platform. Equals the checksum in
// serializeState's envelope.
uint64_t stateChecksum(const GameState& s);

// The state in named parts (the date and options, the galaxy, the colonies,
// the empires, ...), each hashed as stateChecksum() hashes the whole: when two
// copies of a game differ, the parts whose hashes differ say where. Always
// the same parts in the same order (statePartNames()).
std::vector<uint64_t> statePartHashes(const GameState& s);
std::span<const std::string_view> statePartNames();
// The names of the parts whose hashes differ ("everything" when the lists
// do not even have the same length).
std::vector<std::string> differingStateParts(std::span<const uint64_t> a, std::span<const uint64_t> b);

// Save files carry a small header (data set identity, turn, empire names).
struct SaveInfo {
    std::string gameName;
    std::string dataSet;               // dataSetIdentity() of the rules the game was played with
    uint32_t turn = 0;                 // filled from the state when saving
    std::vector<std::string> empires;  // filled from the state when saving
    uint64_t gameId = 0;               // random id chosen at creation (matches order files to games)
    std::vector<std::string> players;  // per empire: the player's login name in network games (may be empty)
    std::string masterPasswordVerifier;  // net::passwordVerifier() of the master password; empty = none
};

std::vector<uint8_t> serializeSave(const GameState& s, const SaveInfo& info);
std::expected<std::pair<GameState, SaveInfo>, std::string> deserializeSave(std::span<const uint8_t> bytes);

// Writes atomically (temporary file, then rename).
std::expected<void, std::string> saveGame(const std::filesystem::path& file, const GameState& s, const SaveInfo& info);
std::expected<std::pair<GameState, SaveInfo>, std::string> loadGame(const std::filesystem::path& file);
// Only the header, e.g. for a load dialog (still verifies the checksum).
std::expected<SaveInfo, std::string> readSaveInfo(const std::filesystem::path& file);

// Identity of a data set: "<label>#<16 hex digits>", where the digits hash the
// data files and the loaded tables. Two machines can play together only when
// the hashes match (sameDataSet); the label is for messages.
std::string dataSetIdentity(const Rules& r);
bool sameDataSet(std::string_view a, std::string_view b);

// File helpers (shared with the PBEM and network code).
std::expected<std::vector<uint8_t>, std::string> readFileBytes(const std::filesystem::path& file, size_t maxBytes = size_t{1} << 30);
std::expected<void, std::string> writeFileAtomic(const std::filesystem::path& file, std::span<const uint8_t> bytes);

// Envelope helpers for other blob kinds (e.g. PBEM order files). `magic` is
// 8 characters; `what` names the blob in error messages ("saved game").
struct Envelope {
    std::span<const uint8_t> payload;
    uint32_t version = kSaveVersion;
};
std::vector<uint8_t> wrapEnvelope(std::string_view magic, std::span<const uint8_t> payload);
std::expected<Envelope, std::string> unwrapEnvelope(std::span<const uint8_t> bytes, std::string_view magic, std::string_view what);

} // namespace opense4::game
