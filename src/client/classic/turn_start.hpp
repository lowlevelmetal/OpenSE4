#pragma once

// What the start of a turn shows, and in which order (docs/spec/06 §2.7
// "Turn flow as the player sees it", §2.4 "Moves as they are made", §1.10.5,
// §4.1, §7 Q83). The original carries the turn's orders out with the moves
// in the shown system animated as they are made, and opens nothing until
// they are; so at a turn's start the player sees the moves first, then the
// battles shown in windows, the endings (the destruction check), the
// questions the player's own orders raise as they run (Attack Sector, the
// colony-type picker, a failed Colonize's message box), and the Log last,
// when the player gets the turn. Our client sees the moves only once the
// engine call has made them all, and shows them afterwards (ShipGlides): until
// the moves of the shown system have been shown, nothing of this opens and
// the main window waits as it does under a window. A click or a key shows them
// at once (OpenSE4's convenience; the original's animation cannot be skipped).
// Headless, tested in tests/test_client_logic.cpp; ClassicMode follows it.

namespace opense4::client::classic {

struct TurnStartFacts {
    bool movesShowing = false;     // moves of the shown system are being shown (ShipGlides::active)
    bool turnStarting = false;     // a new turn began, and its moves are still being shown
    bool battleWaiting = false;    // a battle stops the engine call: asked about, or fought in a window
    bool battlesQueued = false;    // battles to watch in the Strategic Combat window (one open included)
    bool endingOpen = false;       // an ending window is open
    bool questionWaiting = false;  // an Attack Sector question, the colony-type picker or a message box waits
};

struct TurnStartGate {
    // The main window takes no input meanwhile; a click or a key ends the
    // moves being shown (while a turn starts, or something waits for them).
    bool hold = false;
    bool battles = false;     // the battle question, and the next battle to watch, may be shown
    bool endings = false;     // the endings due may open
    bool questions = false;   // the questions, the picker and the message boxes may be shown
    bool log = false;         // the Log may open (with new entries and the Empire Option on)
};

TurnStartGate turnStartGate(const TurnStartFacts& f);

} // namespace opense4::client::classic
