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
// Battles shown as they happen (spec 04 §2, §3; spec 06 §1.10.5, §1.10.6;
// game/turn.hpp): in local and hotseat games a battle with human sides stops
// the engine call once it is set up, before combat turn 1, and the session
// holds the question (battleQuestion()): in a turn-based game Tactical or
// Strategic (with "No Tactical Combat" on, or in a simultaneous game whose
// Settings show battles, the Strategic Combat window with Begin and Close).
// The battle is fought in its window, by hand in the Tactical Combat window
// or by the strategies, phase by phase, in the Strategic Combat window
// (startTactical() with a combat::TacticalBattle on the question's copy of
// the game); when the window closes the session makes the same call again
// with the answers so far, which fights the battle the same way on the real
// game and carries on, so the reports, log entries and everything after the
// battle come only then. The moves the stopped call made before the battle
// are handed on at once (takeMoves, marked provisional) and shown before it;
// the call made again hands on only the rest. A turn-based game also stops at the colony owner's
// end-of-turn ground combat when one of the two empires is human: the
// question holds the fight, shown in the Ground Combat window after a notice.
// Network and PBEM games never stop (their host fights every battle at once);
// their battles are shown afterwards (takeStrategicBattles()), and so is
// nothing for an automated run (simulateTurns).
//
// Play by e-mail (SessionKind::Pbem, pbem_play.hpp): the turn file the host
// sent, the game as one empire knows it, played by that empire. Orders are
// given as in a local game (turn-based: carried out at once, as a preview of
// what the host will carry out on the whole game), and End Turn writes the
// orders file for the host instead of processing the turn; the session then
// waits for good.

#include "client/classic/pbem_play.hpp"
#include "game/commands.hpp"
#include "game/players.hpp"
#include "game/rules.hpp"
#include "game/serialize.hpp"
#include "game/state.hpp"
#include "game/tactical.hpp"
#include "game/turn.hpp"
#include "net/types.hpp"

#include <expected>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
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
    // Turn-based games: the player's own moves that came with the states
    // pollState() returned (protocol 10), in order. Taking them forgets them.
    virtual net::OwnMoves takeOwnMoves() { return {}; }
    virtual std::string status() const = 0;
};

enum class SessionKind { Local, Hotseat, NetworkClient, Pbem };

// A tactical battle being fought in the client: a turn-based game's battle
// (its orders answer the session's battle question) or a combat simulation
// (a sandbox; nothing comes back).
struct TacticalFight {
    enum class Kind { Game, Simulation };
    Kind kind = Kind::Game;
    // A game battle's script and external players (game::makePlayers on the
    // battle's copy of the game, TacticalBattle::Setup::scriptPlayers): they
    // fight their sides as in the engine's battles, and their answers go with
    // the battle's (BattleAnswer::decisions). Null: none in the game. Declared
    // before the battle, which uses it, so that it outlives the battle.
    std::unique_ptr<game::Players> scriptPlayers;
    std::unique_ptr<game::combat::TacticalBattle> battle;
    std::vector<game::EmpireId> players;   // the sides the player drives
    std::string title;                     // "Tactical Combat", "Combat Simulator"
    size_t seen = 0;                       // events of the record already shown (the window plays the rest)
};

class ClassicSession {
public:
    ClassicSession(std::shared_ptr<const game::Rules> rules, game::GameState state, game::EmpireId player,
                   SessionKind kind = SessionKind::Local);
    // Waits for a turn being processed in the background (setBackground).
    ~ClassicSession();
    ClassicSession(const ClassicSession&) = delete;
    ClassicSession& operator=(const ClassicSession&) = delete;
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
    // How many engine calls that play the game (an order carried out, End
    // Turn, the computer players' turns, a battle's answer) have run.
    uint64_t engineCalls() const { return engineCalls_; }
    // A number no other session of this run has (caches about one game tell games apart).
    uint64_t serial() const { return serial_; }

    // Validates and applies a command for the local player (turn-based games:
    // and carries it out).
    game::CommandResult issue(game::Command c);
    // Called with every command issue() accepted (the lesson runner counts them).
    std::function<void(const game::Command&)> onIssued;
    // Objects changed during the turn (spec 06 §7 Q99(d), confirmed: binary):
    // the main window marks the selected object, or each tagged one, whenever
    // an order button or key runs for it (arming a pick or opening a window
    // counts); the commands that change an object's orders, fleet, name,
    // queue, cloak, cargo or colony type mark it themselves (issue()). On a
    // player's copy after a Players-window switch of our own empire, each
    // marked object carries its minister flag to the host, once.
    void markChanged(std::span<const game::VehicleId> vehicles, std::span<const game::ObjectId> planets = {});
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
    // after the engine fought them (spec 06 §1.10.5, spec 04 §2), as indices
    // into GameState::combats, oldest first, then forgotten. Only games played
    // on several machines, whose host never stops: a network game's battles
    // the player fought in (simultaneous games only when the Settings flag
    // `Simultaneous Games Show Strategic Combat` is on), and in a PBEM game
    // the battles the player's orders started. Local and hotseat games show
    // every battle as it happens instead (battleQuestion()).
    std::vector<size_t> takeStrategicBattles();

    // Battles shown as they happen (see the file comment). The battle (or
    // ground fight) that waits to be shown, if any: while it waits the game is
    // as before the call.
    const std::optional<game::BattleQuestion>& battleQuestion() const { return battle_; }
    // Answers it and carries on (the next stop of the same call may come next).
    void answerBattle(game::BattleAnswer answer);
    // The tactical battle in the Tactical Combat window, if any.
    TacticalFight* tactical() { return tactical_.get(); }
    void startTactical(TacticalFight fight);
    // Closes it: a game battle is finished (the strategies play what is left)
    // and its orders answer the battle question; a simulation is dropped. A
    // game battle without player sides is one fought by the strategies in the
    // Strategic Combat window (spec 06 §1.10.5).
    void endTactical();

    // Ends the local player's turn. Local: every computer empire plays and the
    // turn is processed at once. Hotseat: moves to the next human who has not
    // ended the turn, processing when all have. Network: sends the orders.
    void endTurn();
    bool waitingForOthers() const { return waiting_; }
    // Call every frame: picks up new states from the transport, and the turn
    // processed in the background when it is done.
    void poll();

    // A local or hotseat simultaneous game's turn processed in the
    // background (the interactive client's): End Turn (and a battle's answer)
    // starts the processing on a thread of its own, on a copy of the game,
    // and returns at once; the windows meanwhile show the game as it was,
    // and poll() takes the processed game when it is done. Off (the default:
    // tests, automation, input scripts), the processing is done at once.
    // The game comes out the same either way.
    void setBackground(bool on) { background_ = on; }
    // A turn is being processed in the background; and how far it has got
    // (TurnOptions::progress: steps done, of the total), for the Progress
    // window (spec 06 §1.7).
    bool processing() const { return job_ != nullptr; }
    std::pair<int, int> progress() const;
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
    // .plr files against (net::passwordVerifier, salted with the game's id),
    // and so does a network or PBEM game file opened with Load Game. Fails
    // (in words) when the computer lacks the memory a verifier's Argon2id
    // work needs (net::PasswordWorkError).
    std::expected<std::string, std::string> empirePasswordValue(std::string_view password) const;
    // Whether `password` opens the empire's turn in the Next Player window,
    // by the same scheme as empirePasswordValue. True when it has no
    // password. Spaces before and after are ignored and letter case counts
    // (spec 06 §1.2.1, confirmed: binary); the password as typed is tried
    // too, for one set with spaces around it (only its hash is kept). Fails
    // as empirePasswordValue does.
    std::expected<bool, std::string> passwordMatches(const game::Empire& e, std::string_view password) const;
    // Whether the Next Player window asks for the empire's password: it has
    // one, other than "master" in any letter case, which counts as none (a
    // random computer player's, handed to a human; spec 06 §1.2.1, confirmed:
    // binary). A verifier of a game on several machines cannot be told so.
    bool passwordAsked(const game::Empire& e) const;
    // The same for Login To Game (spec 06 §1.1, confirmed: binary): spaces
    // before and after are ignored and so is letter case, as far as a hash
    // allows: the trimmed password in small letters is tried first, then as
    // typed (trimmed, and untrimmed), so a password set with capitals opens
    // with those capitals only (OpenSE4 keeps no password itself).
    std::expected<bool, std::string> loginPasswordMatches(const game::Empire& e, std::string_view password) const;
    // Login To Game's Game Master row: the game's master password compared
    // as above; a game without one takes an empty entry.
    std::expected<bool, std::string> masterLoginMatches(std::string_view password) const;

    // What a game opened from a saved game asks before it is played (spec 06
    // §1.1, §1.2.1, spec 08 §5): every simultaneous game asks who signs in
    // (Login To Game); a turn-based game's autosave opens the Next Player
    // window for the player whose turn it is; a plain save asks nothing
    // (confirmed: binary). Set by load(), taken once; None otherwise.
    enum class SignIn { None, NextPlayer, Login };
    SignIn takeSignIn() { return std::exchange(signIn_, SignIn::None); }
    // The session was opened from a saved game (load()), not started anew.
    bool fromSave() const { return fromSave_; }
    // The header of the saved game this session was opened from (load()).
    const game::SaveInfo& loadedInfo() const { return loadedInfo_; }
    // Whether that game was made with this data set, as Login To Game checks
    // before an empire signs in ("Invalid Data Files", confirmed: binary): a
    // header with a data set's identity (a host's or e-mail game file) must
    // name this one; OpenSE4's local saves keep only the game folder's name,
    // and their mods are checked when they load.
    bool dataSetMatches() const;

    // Messages the engine produced for the player on the last turn (rejected orders).
    const std::vector<std::string>& notices() const { return notices_; }
    // Called after a new turn begins (for the Log window auto-open etc.).
    std::function<void()> onNewTurn;

    // What the movement animation shows of the engine calls that played the
    // game (an order carried out, End Turn with the computer players' turns
    // and the next human's turn start, a simultaneous turn processed here, a
    // battle's answer that carried one on), in the order the engine made it
    // (docs/spec/06 §2.4 "Moves as they are made", §2.7): every group's step
    // (`step`), and, in a turn-based game during the local player's turn,
    // the view following the player's own groups (`follow`: a group's run
    // starts there, or it jumped through a warp point), marked `turnStart`
    // when it came with the start of the player's turn (orders carried over)
    // rather than with an order just given. `original`: a step the original
    // animates, one made for the human player whose turn it is in a
    // turn-based game (spec 06 §2.4); the others only with OpenSE4's option.
    // A call a battle stopped made none: it is made again with the answer. A
    // network game's host sends the player's own steps and LiveSteps of a
    // turn-based game with its new state (net::OwnMoves, protocol 10), its
    // commands' and its turn start's; other players' moves, a simultaneous
    // turn's and an e-mail game's new states come without them. Taking them
    // forgets them.
    // Also, in their place among the moves, the messages the player's own
    // orders raised as they were carried out here (`message`: a failed
    // Colonize in a box titled "Colonize", a minefield's "Minefield", spec 03
    // §8, spec 06 §2.4), shown as the moves reach them.
    struct ShownMove {
        std::optional<game::MovementStep> step;
        std::optional<game::LiveStep> follow;
        bool turnStart = false;
        bool original = false;
        std::optional<game::PlayerMessage> message;
        // Made before a battle stopped the call, the game still as before it:
        // shown up to the battle, which waits for them (spec 06 §2.4
        // "Order"); the call made again with the answers hands on the rest.
        bool provisional = false;
    };
    // An engine call is under way: stopped at a battle, or a turn processed
    // on its own thread.
    bool callUnderWay() const { return call_ != Call::None; }
    std::vector<ShownMove> takeMoves() { return std::exchange(moves_, {}); }
    // The player's own vehicles those steps name that the call removed from
    // the game (destroyed on the way: mines, a battle), as they were before
    // it, placed where their last step took them: the animation draws them
    // until their steps are shown (spec 06 §2.4: the square is redrawn
    // without them after the strike). Taking them forgets them.
    std::vector<game::Vehicle> takeGone() { return std::exchange(gone_, {}); }

    // The game as the last engine call that carried orders out found it, for
    // the movement line drawn while its moves are shown (docs/spec/06 §2.4
    // "Movement lines": the line stored before a move is drawn during its
    // animation). A turn-based order's game has the order given but not yet
    // carried out; End Turn's, and a network game's turn passing to us, are
    // marked `turnStart`: their own vehicles got their movement back before
    // their orders ran. `revision` is revision() right after the call: a later
    // change makes it stale. Kept only while asked for (keepMovesBefore, the
    // client's movement lines on); `state` is null when there is none.
    struct MovesBefore {
        std::shared_ptr<const game::GameState> state;
        bool turnStart = false;
        uint64_t revision = 0;
    };
    const MovesBefore& movesBefore() const { return movesBefore_; }
    void keepMovesBefore(bool keep);

    // Saves and loads (the .gam equivalent).
    std::expected<void, std::string> save(const std::filesystem::path& file, const std::string& gameName) const;
    // The game's Autosave choice (spec 01 §2.2), in local and hotseat games:
    // when the turns since 2400.0 are a multiple of N, into the saves folder's
    // file named after that number's last digit (setup::autosaveName), marked
    // as an autosave (SaveInfo::autosave). A simultaneous game is saved after
    // its turn has been processed here; a turn-based game at the start of the
    // first human player's turn of each new game turn, before that turn's
    // start (its endings, its orders carried over), so that loading it starts
    // that turn again (spec 06 §6.1, confirmed: binary). Returns what was
    // written, if anything.
    std::optional<std::filesystem::path> autosave();
    // Where the last autosave went, or why it failed (for the status line).
    const std::string& autosaveNote() const { return autosaveNote_; }
    // The Autosave choice can be changed during the game (spec 01 §2.2): in a
    // local or hotseat game it is kept with this computer's copy of the game
    // (GameOptions::autosaveTurns, saved with it). False, changing nothing,
    // in network and PBEM games (their host keeps the game) and for a value
    // that is not one of the choices (setup::kAutosaveTurns).
    bool setAutosaveTurns(int everyTurns);
    // An OpenSE4 save, or a saved game of the original, which is imported
    // (docs/spec/08; told apart by the file's first bytes, both are .gam).
    // With `scenario`, the Starting Game of one of the original's scenarios
    // (docs/spec/06 §5.6): the game it holds, read as any other, becomes a
    // game of that scenario, from its first page (takeScenarioStart).
    static std::expected<std::unique_ptr<ClassicSession>, std::string> load(std::shared_ptr<const game::Rules> rules,
                                                                            const std::filesystem::path& file,
                                                                            const game::ClassicScenarioState* scenario = nullptr);
    // The game was just started from one of the original's scenarios (load
    // with `scenario`): its pages come up now. Taken once.
    bool takeScenarioStart() { return std::exchange(scenarioStart_, false); }
    // After loading a saved game of the original: what the import
    // approximated, short lines for the player (the details are in the log
    // file). Taken once.
    std::vector<std::string> takeImportNotes() { return std::exchange(importNotes_, {}); }
    // Writes the game as a saved game of the original (docs/spec/08 §7), with
    // the History files beside it as the original's save does. Only the
    // whole game can be written: local and hotseat games. Returns what the
    // export could not carry, short lines for the player. What the game's
    // mods hold that the original cannot (game::classic::ExportOptions):
    // `scriptedMods`, the ids of mods with scripts, and `installHasPicture`,
    // whether the install has a design's own picture.
    std::expected<std::vector<std::string>, std::string> exportClassic(const std::filesystem::path& file, std::vector<std::string> scriptedMods = {},
                                                                       std::function<bool(std::string_view)> installHasPicture = {}) const;

    // Hotseat: switches the local player (after a password check by the UI).
    void setPlayer(game::EmpireId e);
    // A network game's id (net::HostSession / ClientSession::gameId), which
    // salts the passwords this session makes (empirePasswordValue).
    void setMultiplayerGame(uint64_t gameId) { multiplayerGameId_ = gameId; }

    // ---- Player Computer Control (spec 06 §1.2.1, confirmed: binary) ----
    // Whether the game has a master password the window asks for first: a
    // network or e-mail host's game file opened here keeps its host's
    // (SaveInfo); local games and a network or e-mail player's copy hold none
    // (inferred).
    bool hasMasterPassword() const { return !masterVerifier_.empty(); }
    // The exact comparison the window makes (letter case and spaces count).
    // Fails as empirePasswordValue does.
    std::expected<bool, std::string> masterPasswordMatches(std::string_view password) const;
    // The window's OK: each row clicked at least once, with the lamp's final
    // state (game::ai::setComputerControl). A local or hotseat game changes
    // at once; on a player's copy of a game on different machines only that
    // copy changes, and the orders carry the player's own empire's minister
    // switches and flags (never the mark) to the host. Nothing changes while
    // a battle waits to be shown (battleQuestion()).
    void setComputerControl(const std::vector<std::pair<game::EmpireId, bool>>& rows);
    // A local or hotseat game with no living human-controlled empire has
    // ended: no further turn is played (spec 06 §1.2.1 "No human left").
    bool humansGone() const;
    // Replaces the state (e.g. after loading or a network update).
    void replaceState(game::GameState s);
    // Automation: the computer plays every empire (humans too) for n turns.
    void simulateTurns(int n);

    // The movement log replay (docs/spec/06 §7 Q51): a local or hotseat
    // simultaneous game keeps the state the last turn started from and plays
    // that turn again from it, calling `day` after each of the 30 movement
    // days (the engine is deterministic, so it comes out as it did). False
    // when there is no such turn (turn-based, network and PBEM games, a game
    // just loaded).
    bool canReplayLastTurn() const { return turnStart_ != nullptr; }
    // Plays the last turn again from its start: `day` sees the state after
    // each movement day, `step` each vehicle's every step as it is made.
    bool replayLastTurn(const std::function<void(int day, const game::GameState&)>& day,
                        const std::function<void(const game::MovementStep&)>& step = {}) const;
    // The state the last processed turn started from, if kept.
    const game::GameState* turnStart() const { return turnStart_.get(); }

private:
    // Adds a command to this turn's orders.
    void record(game::Command c);
    // The objects a command changes, as markChanged counts them.
    struct Changed {
        std::vector<game::VehicleId> vehicles;
        std::vector<game::ObjectId> planets;
    };
    Changed changedBy(const game::Command& c) const;
    // Whether this session's orders carry the flags of changed objects (a
    // player's copy of a game on different machines).
    bool marksChanges() const { return kind_ == SessionKind::NetworkClient || kind_ == SessionKind::Pbem; }
    // Issues the minister flags of the objects marked changed not carried yet.
    void carryFlags();
    void clearOrders();
    game::CommandResult issueCommand(game::Command c);
    void beginTurn();
    // Turn-based games: plays up to a human player's turn and hands the
    // session to that player.
    void resumeTurnBased();
    void takeResult(const game::TurnResult& result);
    // Local and hotseat simultaneous games stand for the original's reading of
    // the game file between players and before processing: every colony is
    // recalculated (spec 01 §6.9, §14 Q44), and the happiness events waiting
    // for an empire's next update are gone, as no game file keeps them (spec
    // 02 §4).
    void reloadGame();
    // Network simultaneous games: the processed turn's battles of the player, when the Settings flag asks for them.
    void queueTurnBattles();
    // The engine call in progress (a turn-based game's order, End Turn or
    // resume; a simultaneous game's turn), made again with the answers until
    // nothing stops it; then its results are taken.
    enum class Call { None, Issue, EndTurn, Resume, Process };
    void beginCall(Call call, std::optional<game::Command> command = std::nullopt);
    void runCall();
    // The rest of runCall once the engine call has returned: a battle that
    // stopped it waits to be shown; otherwise its results are taken.
    void finishCall(game::TurnResult res);
    // The background processing (setBackground): started by runCall, taken by poll().
    struct Job;
    bool background_ = false;
    std::unique_ptr<Job> job_;
    void startJob(const std::vector<game::BattleAnswer>* answers);
    void pollJob();
    // A fault in the engine call (an exception other than a battle stop): the
    // game is put back as it was before the call (the engine's copy, or the
    // turn's start), and the call is dropped with its question and answers,
    // so that nothing goes on from it: no new turn, no autosave. runCall()
    // then lets the exception go on to the crash report (client/crash_report.hpp).
    void dropCall(std::string_view why);
    // Whether this session's engine calls stop for battles to show (local and hotseat games).
    bool showsBattles() const;
    // Who plays the human empires' turns in this session's turn-based calls,
    // and the autosave before a human player's turn starts.
    game::LiveOptions liveOptions();

    std::shared_ptr<const game::Rules> rules_;
    game::GameState state_;
    game::EmpireId player_;
    SessionKind kind_;
    // A network or PBEM game file opened with Load Game (SaveInfo::gameId set):
    // its empire passwords are verifiers, and saving keeps the game id.
    uint64_t multiplayerGameId_ = 0;
    std::vector<std::string> importNotes_;
    bool scenarioStart_ = false;
    // The master password verifier of a network or PBEM game file opened
    // here (SaveInfo::masterPasswordVerifier); kept when the game is saved.
    std::string masterVerifier_;
    uint64_t revision_ = 1;
    uint64_t engineCalls_ = 0;
    uint64_t serial_ = nextSessionSerial();
    static uint64_t nextSessionSerial();
    std::vector<game::Command> orders_;
    // The Players window switched our own empire this turn (a player's copy):
    // the objects changed since carry their flags (carryFlags), once each.
    bool switchedOwn_ = false;
    std::vector<game::VehicleId> changed_, flagged_;
    std::vector<game::ObjectId> changedPlanets_, flaggedPlanets_;
    std::vector<uint8_t> ended_;  // hotseat: humans who ended this turn
    bool waiting_ = false;
    std::unique_ptr<TurnTransport> transport_;
    std::vector<std::string> notices_;
    // The steps of the engine call under way (runCall), and what takeMoves() gives.
    std::vector<game::MovementStep> callSteps_;
    std::vector<ShownMove> moves_;
    // The human players' vehicles before the call under way, and the
    // player's own it removed that its steps name (takeGone()).
    std::vector<game::Vehicle> callBefore_, gone_;
    void keepOwnVehicles(const game::GameState& before);
    void noteGone(const std::vector<game::MovementStep>& steps);
    // Adds the finished call's steps and the local player's own LiveSteps and
    // messages to moves_, in order; `provisional` for a call a battle
    // stopped, whose part handed on is counted so that the call made again
    // hands on only the rest.
    void queueMoves(const game::TurnResult& res, bool turnStart, bool provisional = false);
    size_t queuedSteps_ = 0, queuedLive_ = 0, queuedMessages_ = 0;
    void forgetQueued() { queuedSteps_ = queuedLive_ = queuedMessages_ = 0; }
    std::string autosaveNote_;
    // Turn-based games: the game turn whose autosave was written (or whose
    // first human turn this session began in): one autosave per game turn.
    uint32_t autosavedTurn_ = 0;
    // Writes `s` as the autosave of its date, if one is due.
    std::optional<std::filesystem::path> autosaveOf(const game::GameState& s);
    std::expected<void, std::string> saveState(const std::filesystem::path& file, const std::string& gameName, const game::GameState& s,
                                               bool autosave) const;
    SignIn signIn_ = SignIn::None;
    bool fromSave_ = false;
    game::SaveInfo loadedInfo_;
    // A stored password hash or verifier against the forms of a typed one (passwordForms).
    std::expected<bool, std::string> storedPasswordMatches(const std::string& stored, std::string_view password, bool foldCase) const;
    std::vector<std::pair<game::EmpireId, size_t>> strategic_;   // battles to watch, and for whom (takeStrategicBattles)
    Call call_ = Call::None;
    std::optional<game::Command> callCommand_;
    std::vector<game::EmpireOrders> callOrders_;   // Process: the humans' (already applied) orders
    size_t callBattles_ = 0;                  // GameState::combats before the call
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
    // The last simultaneous turn processed here: its starting state and orders.
    std::shared_ptr<const game::GameState> turnStart_;
    std::vector<game::EmpireOrders> turnStartOrders_;
    // movesBefore(): kept while keepMoves_; set when a call begins (the
    // revision when it is over).
    bool keepMoves_ = false;
    MovesBefore movesBefore_;
    void noteMovesBefore(Call call);
};

// Passwords as the sign-in windows compare them (spec 06 §1.1, §1.2.1,
// confirmed: binary). `trimPassword`: without the spaces (and other control
// characters) before and after. `passwordForms`: the forms of a typed
// password to try against a stored hash, best first, each once (with
// `foldCase` the trimmed one in small letters first), since OpenSE4 keeps
// only the hash of the password as it was set. `storedPasswordOpens`:
// whether one of them opens `stored`, a net::passwordVerifier salted with
// `gameId` when `verifier` (games on several machines, and their files
// played here), else a game::hashPassword; an empty form opens nothing. Fails
// (in words) when the computer lacks the memory a verifier's Argon2id needs.
std::string trimPassword(std::string_view p);
std::vector<std::string> passwordForms(std::string_view typed, bool foldCase);
std::expected<bool, std::string> storedPasswordOpens(const std::string& stored, std::string_view typed, bool foldCase, bool verifier,
                                                    uint64_t gameId);

// Whether a saved game's header names this data set, as Login To Game checks
// it (ClassicSession::dataSetMatches): a header with a data set's identity
// must name this one; OpenSE4's local saves keep only the game folder's name.
bool savedDataSetMatches(const game::SaveInfo& info, const game::Rules& rules);

// Where OpenSE4 keeps saves and settings (created on demand): the user
// folder (client/app_settings.hpp userDataDirectory), and the saves folder,
// "saves" in it unless Settings → Files chose another (FileSettings).
std::filesystem::path userDataDir();
std::filesystem::path savesDir();
// The players' statistics, history and log files (docs/spec/06 §6.1):
// <userDataDir>/History/plr_<N>_<kind> with kind "stats.txt", "events.txt"
// or "log.txt". A save carries copies named <save name>_plr_<N>_<kind>
// beside it; loading a game empties History/ and refills it from them.
std::filesystem::path historyDir();
std::string historyFileName(game::EmpireId empire, std::string_view kind);
void copyHistoryNextTo(const std::filesystem::path& saveFile, const std::filesystem::path& history = historyDir());
void restoreHistoryFrom(const std::filesystem::path& saveFile, const std::filesystem::path& history = historyDir());

} // namespace opense4::client::classic
