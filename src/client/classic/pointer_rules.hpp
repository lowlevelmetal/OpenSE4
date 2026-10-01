#pragma once

// The classic game's mouse pointers (docs/spec/06 §5.8, §1.10.1, confirmed:
// binary): the twelve .cur files of Pictures/Game and which one shows when.
// Headless, tested in tests/test_client_layout.cpp; client/classic/pointers.*
// loads the files and shows them.

#include <array>
#include <cstdint>
#include <string_view>

namespace opense4::client::classic {

enum class Pointer : uint8_t {
    Normal,
    Hourglass,
    Select,
    Target,
    ArrowN,
    ArrowNE,
    ArrowE,
    ArrowSE,
    ArrowS,
    ArrowSW,
    ArrowW,
    ArrowNW,
};
inline constexpr size_t kPointerCount = 12;

// The file of each pointer in Pictures/Game (the game asks for lower-case names).
std::string_view pointerFile(Pointer p);

// The move pointer for a square at (dx, dy) columns and rows from the selected
// piece: by the signs of the offsets (x grows right, y down); Normal on the
// piece's own square.
Pointer arrowPointer(int dx, int dy);

// The pointer over the Tactical Combat window (§1.10.1, §5.8).
struct TacticalPointerFacts {
    bool begun = false;          // Begin was pressed
    bool busy = false;           // the battle is being played (animation, computer turns)
    bool overMap = false;        // the pointer is over the battle map
    bool aiming = false;         // Ram or Capture waits for its target
    bool selected = false;       // one of our pieces is selected
    bool selectedIsDrone = false;
    bool overOtherEmpire = false;  // over a piece of another empire
    int dx = 0, dy = 0;          // from the selected piece to the square under the pointer
};
Pointer tacticalPointer(const TacticalPointerFacts& f);

} // namespace opense4::client::classic
