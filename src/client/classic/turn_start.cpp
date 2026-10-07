#include "client/classic/turn_start.hpp"

namespace opense4::client::classic {

TurnStartGate turnStartGate(const TurnStartFacts& f) {
    TurnStartGate g;
    const bool shown = !f.movesShowing;   // the moves come first
    g.hold = f.movesShowing && (f.turnStarting || f.battleWaiting || f.battlesQueued || f.questionWaiting);
    g.battles = shown;
    g.endings = shown && !f.battleWaiting && !f.battlesQueued;
    g.questions = shown && !f.battleWaiting && !f.battlesQueued && !f.endingOpen;
    g.log = shown && !f.battleWaiting && !f.battlesQueued && !f.endingOpen && !f.questionWaiting;
    return g;
}

} // namespace opense4::client::classic
