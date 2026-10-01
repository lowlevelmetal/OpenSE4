#pragma once

// A classic-rules game as the client sees it: the rules, the game state, the
// local player and the orders given this turn. Local and hotseat games own
// the authoritative state; a network client holds a copy and hands its
// orders to a TurnTransport (src/net) at End Turn.
//
// Every change the player makes goes through issue(): the command is
// validated and applied to the local state at once (so every window shows
// it immediately) and recorded for the turn.
//
// Turn-based games (GameOptions::simultaneous off) play one player after
// another: issue() carries each order out at once (game::applyLive: ships
// move, battles are fought, messages take effect), End Turn runs the
// player's end-of-turn processing and the computer players' turns, and the
// turn passes to the next human. In a network game the host does this: in
// the player's turn issue() checks the command on the local copy and sends
// it to the host (TurnTransport::playCommand), whose new state replaces the
// copy when it arrives.
//
// Tactical combat in turn-based games (spec 04 §2, §3; game/turn.hpp): a
// battle with human sides stops the engine call before the battle and the
// session holds the question (battleQuestion()). The player answers
// Strategic, or fights it in the Tactical Combat window (startTactical()
// with a combat::TacticalBattle on the question's copy of the game); the
// session then makes the same call again with the answers so far, which
// fights the battle the same way on the real game and carries on. Local and
// hotseat games only: network and PBEM games never ask, and their battles
// are strategic.
//
// Play by e-mail (SessionKind::Pbem, pbem_play.hpp): the game file the host
// sent, played by one empire. Orders are given as in a local game (turn-based:
// carried out at once), and End Turn writes the orders file for the host
// instead of processing the turn; the session then waits for good.

#include "client/classic/pbem_play.hpp"
#include "game/commands.hpp"
#include "game/rules.hpp"
#include "game/state.hpp"
#include "game/tactical.hpp"
#include "game/turn.hpp"

#include <expected>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace opense4::client::classic {

// Moves a player's orders to whoever processes turns, and brings back new
// states. Implemented over TCP by the multiplayer layer.
class TurnTransport {
public:
    virtual ~TurnTransport() = default;
    virtual void submitOrders(const game::EmpireOrders& orders) = 0;
    // Turn-based games: one command for the host to carry out now, and the
    // end of our turn.
    virtual void playCommand(const game::Command& c) { (void)c; }
    virtual void endPlayerTurn() {}
    // Returns a newly processed state when one has arrived (turn-based games:
    // also within a turn).
    virtual std::optional<game::GameState> pollState() = 0;
    virtual std::string status() const = 0;
};

enum class SessionKind { Local, Hotseat, NetworkClient, Pbem };

// A tactical battle being fought in the client: a turn-based game's battle
// (its orders answer the session's battle question) or a combat simulation
// (a sandbox; nothing comes back).
struct TacticalFight {
    enum class Kind { Game, Simulation };
    Kind kind = Kind::Game;
    std::unique_ptr<game::combat::TacticalBattle> battle;
    std::vector<game::EmpireId> players;   // the sides the player drives
    std::string title;                     // "Tactical Combat", "Combat Simulator"
    size_t seen = 0;                       // events of the record already shown (the window plays the rest)
};

class ClassicSession {
public:
    ClassicSession(std::shared_ptr<const game::Rules> rules, game::GameState state, game::EmpireId player,
                   SessionKind kind = SessionKind::Local);
    // Play by e-mail: `game` played by `turn.empire` (from beginPbemTurn).
    // With `draftsDir`, a turn in progress saved there (savePbemDraft) is
    // resumed: its commands are given again.
    static std::unique_ptr<ClassicSession> pbem(std::shared_ptr<const game::Rules> rules, PbemGame game, PbemTurn turn,
                                                std::filesystem::path draftsDir = {});

    const game::Rules& rules() const { return *rules_; }
    std::shared_ptr<const game::Rules> rulesPtr() const { return rules_; }
    const game::GameState& state() const { return state_; }
    game::EmpireId player() const { return player_; }
    const game::Empire& me() const { return state_.empire(player_); }
    SessionKind kind() const { return kind_; }

    // Bumped on every state change; windows use it to refresh cached views.
    uint64_t revision() const { return revision_; }

    // Validates and applies a command for the local player (turn-based games:
    // and carries it out).
    game::CommandResult issue(game::Command c);
    // Called with every command issue() accepted (the lesson runner counts them).
    std::function<void(const game::Command&)> onIssued;
    const std::vector<game::Command>& ordersThisTurn() const { return orders_; }

    bool turnBased() const { return game::turnBased(state_); }
    // Turn-based games: the local player's turn is in progress.
    bool myTurn() const;
    // Turn-based games: moves of the player's ships that stopped before a
    // sector with enemies, oldest first; answer() gives the Attack Sector
    // answer to the first (spec 03 §6.2).
    const std::vector<game::EntryQuestion>& questions() const;
    void answer(bool enter);
    // Battles for the local player to watch in the Strategic Combat window
    // (spec 06 §1.6, spec 04 §2), as indices into GameState::combats, oldest
    // first, then forgotten: turn-based games, those the player's orders
    // started and those in which the player answered Strategic (never one
    // fought in the Tactical Combat window); simultaneous games, when the
    // Settings flag `Simultaneous Games Show Strategic Combat` is on, every
    // battle of the processed turn the player fought in. Hotseat: battles
    // listed for another player than the one now playing are dropped.
    std::vector<size_t> takeStrategicBattles();

    // Tactical combat (see the file comment). The battle that waits for its
    // answer, if any: while it waits the game is as before the call.
    const std::optional<game::BattleQuestion>& battleQuestion() const { return battle_; }
    // Answers it and carries on (the next battle of the same call may ask next).
    void answerBattle(game::BattleAnswer answer);
    // The tactical battle in the Tactical Combat window, if any.
    TacticalFight* tactical() { return tactical_.get(); }
    void startTactical(TacticalFight fight);
    // Closes it: a game battle is finished (the strategies play what is left)
    // and its orders answer the battle question; a simulation is dropped.
    void endTactical();

    // Ends the local player's turn. Local: every computer empire plays and the
    // turn is processed at once. Hotseat: moves to the next human who has not
    // ended the turn, processing when all have. Network: sends the orders.
    void endTurn();
    bool waitingForOthers() const { return waiting_; }
    // Call every frame: picks up new states from the transport.
    void poll();
    void setTransport(std::unique_ptr<TurnTransport> transport) { transport_ = std::move(transport); }
    const TurnTransport* transport() const { return transport_.get(); }
    TurnTransport* transport() { return transport_.get(); }

    // Play by e-mail: the turn being played (nullptr in other games), the
    // orders file End Turn wrote (empty until then), and why writing failed.
    const PbemTurn* pbemTurn() const { return pbem_ ? &*pbem_ : nullptr; }
    const std::filesystem::path& ordersFile() const { return ordersFile_; }
    const std::string& pbemError() const { return pbemError_; }
    // Play by e-mail: saves the turn so far to finish later (in the drafts
    // folder given to pbem()); End Turn removes it. The commands a resumed
    // draft gave again.
    std::expected<std::filesystem::path, std::string> savePbemDraft() const;
    size_t pbemResumed() const { return pbemResumed_; }

    // The value cmd::SetEmpireOptions::passwordHash takes for a new password
    // (empty: none). Local and hotseat games keep game::hashPassword();
    // network and PBEM games keep the verifier the host checks logins and
    // .plr files against (net::passwordVerifier of net::hashPassword), and so
    // does a network or PBEM game file opened with Load Game.
    std::string empirePasswordValue(std::string_view password) const;
    // Whether `password` opens the empire's turn (hotseat hand-over), by the
    // same scheme as empirePasswordValue. True when it has no password.
    bool passwordMatches(const game::Empire& e, std::string_view password) const;

    // Messages the engine produced for the player on the last turn (rejected orders).
    const std::vector<std::string>& notices() const { return notices_; }
    // Called after a new turn begins (for the Log window auto-open etc.).
    std::function<void()> onNewTurn;

    // Saves and loads (the .gam equivalent).
    std::expected<void, std::string> save(const std::filesystem::path& file, const std::string& gameName) const;
    // The game's Autosave choice (spec 01 §2.2), applied after a game turn has
    // been processed here (local and hotseat games, either turn style): when
    // the turns since 2400.0 are a multiple of N, into the saves folder's file
    // named after that number's last digit (setup::autosaveName). Returns what
    // was written, if anything.
    std::optional<std::filesystem::path> autosave();
    // Where the last autosave went, or why it failed (for the status line).
    const std::string& autosaveNote() const { return autosaveNote_; }
    // The Autosave choice can be changed during the game (spec 01 §2.2): in a
    // local or hotseat game it is kept with this computer's copy of the game
    // (GameOptions::autosaveTurns, saved with it). False, changing nothing,
    // in network and PBEM games (their host keeps the game) and for a value
    // that is not one of the choices (setup::kAutosaveTurns).
    bool setAutosaveTurns(int everyTurns);
    static std::expected<std::unique_ptr<ClassicSession>, std::string> load(std::shared_ptr<const game::Rules> rules,
                                                                            const std::filesystem::path& file);

    // Hotseat: switches the local player (after a password check by the UI).
    void setPlayer(game::EmpireId e);
    // Replaces the state (e.g. after loading or a network update).
    void replaceState(game::GameState s);
    // Automation: the computer plays every empire (humans too) for n turns.
    void simulateTurns(int n);

private:
    game::CommandResult issueCommand(game::Command c);
    void beginTurn();
    // Turn-based games: plays up to a human player's turn and hands the
    // session to that player.
    void resumeTurnBased();
    void takeResult(const game::TurnResult& result);
    // Simultaneous games: the processed turn's battles of the player, when the Settings flag asks for them.
    void queueTurnBattles();
    // Turn-based games: the engine call in progress, made again with the
    // battle answers until no battle asks; then its results are taken.
    enum class Call { None, Issue, EndTurn, Resume };
    void beginCall(Call call, std::optional<game::Command> command = std::nullopt);
    void runCall();
    // Whether this session's battles may ask (turn-based, local or hotseat,
    // "No Tactical Combat" off).
    bool offersTactical() const;

    std::shared_ptr<const game::Rules> rules_;
    game::GameState state_;
    game::EmpireId player_;
    SessionKind kind_;
    // A network or PBEM game file opened with Load Game (SaveInfo::gameId set):
    // its empire passwords are verifiers, and saving keeps the game id.
    uint64_t multiplayerGameId_ = 0;
    uint64_t revision_ = 1;
    std::vector<game::Command> orders_;
    std::vector<uint8_t> ended_;  // hotseat: humans who ended this turn
    bool waiting_ = false;
    std::unique_ptr<TurnTransport> transport_;
    std::vector<std::string> notices_;
    std::string autosaveNote_;
    std::vector<std::pair<game::EmpireId, size_t>> strategic_;   // battles to watch, and for whom (takeStrategicBattles)
    std::vector<game::Location> answeredStrategic_, answeredTactical_;   // this call's answers of the local player
    Call call_ = Call::None;
    std::optional<game::Command> callCommand_;
    size_t callBattles_ = 0;                  // GameState::combats before the call
    uint32_t callTurn_ = 0;                   // GameState::turn before the call (a turn-based game turn ended: autosave)
    std::vector<game::BattleAnswer> answers_;
    game::CommandResult issued_;              // the result of the last Issue call that finished
    std::optional<game::BattleQuestion> battle_;
    std::unique_ptr<TacticalFight> tactical_;
    // Tactical battles answered in this call, as fought in the window: the
    // game's copy must come out the same (a check on determinism, logged).
    std::vector<game::CombatRecord> fought_;
    std::optional<PbemTurn> pbem_;
    std::filesystem::path ordersFile_;
    std::string pbemError_;
    std::filesystem::path pbemDrafts_;
    size_t pbemResumed_ = 0;
};

// Where OpenSE4 keeps saves and settings (created on demand).
std::filesystem::path userDataDir();
std::filesystem::path savesDir();

} // namespace opense4::client::classic
