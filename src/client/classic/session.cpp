#include "client/classic/session.hpp"

#include "client/classic/screens/setup_model.hpp"
#include "core/log.hpp"
#include "game/serialize.hpp"
#include "game/setup.hpp"
#include "game/turn.hpp"
#include "net/auth.hpp"

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_stdinc.h>

#include <algorithm>
#include <format>
#include <fstream>

namespace opense4::client::classic {

namespace {

// Human players' statistics, history and log text files (spec 05 §3.4, §5,
// §8 step 2; docs/spec/06 §6.1): the lines the engine made, in
// History/plr_<N>_stats.txt, plr_<N>_events.txt and plr_<N>_log.txt. The
// original keeps History/ in its installation; ours is in the user data
// folder, and saves carry copies (copyHistoryNextTo, restoreHistoryFrom).
// Statistics and history are appended, the history file opened only when
// there is a line; the log copy is rewritten whenever the engine made it. A
// new game's first turn starts the files afresh.
void writePlayerRecords(const std::vector<game::score::PlayerRecords>& records) {
    if (records.empty()) return;
    std::error_code ec;
    const std::filesystem::path dir = historyDir();
    std::filesystem::create_directories(dir, ec);
    if (ec) {
        log::warn("Cannot create {}: {}", dir.string(), ec.message());
        return;
    }
    auto write = [&](const std::filesystem::path& file, const std::vector<std::string>& lines, bool fresh) {
        if (lines.empty()) {
            if (fresh) std::filesystem::remove(file, ec);
            return;
        }
        std::ofstream out(file, fresh ? std::ios::trunc : std::ios::app);
        for (const std::string& line : lines) out << line << '\n';
        if (!out) log::warn("Cannot write {}", file.string());
    };
    for (const game::score::PlayerRecords& rec : records) {
        const bool fresh = rec.turn == 0;
        write(dir / historyFileName(rec.empire, "stats.txt"), rec.statistics, fresh);
        write(dir / historyFileName(rec.empire, "events.txt"), rec.history, fresh);
        if (!rec.log.empty()) write(dir / historyFileName(rec.empire, "log.txt"), rec.log, true);
        else if (fresh) std::filesystem::remove(dir / historyFileName(rec.empire, "log.txt"), ec);
    }
}

} // namespace

ClassicSession::ClassicSession(std::shared_ptr<const game::Rules> rules, game::GameState state, game::EmpireId player, SessionKind kind)
    : rules_(std::move(rules)), state_(std::move(state)), player_(player), kind_(kind) {
    ended_.assign(state_.empires.size(), 0);
    // A PBEM game file already holds the player's turn (loadPbemGame).
    if (turnBased() && kind_ != SessionKind::NetworkClient && kind_ != SessionKind::Pbem) resumeTurnBased();
    if (turnBased() && kind_ == SessionKind::NetworkClient) waiting_ = !myTurn();
}

std::unique_ptr<ClassicSession> ClassicSession::pbem(std::shared_ptr<const game::Rules> rules, PbemGame game, PbemTurn turn,
                                                     std::filesystem::path draftsDir) {
    const game::EmpireId player = turn.empire;
    auto session = std::make_unique<ClassicSession>(std::move(rules), std::move(game.state), player, SessionKind::Pbem);
    session->pbem_ = std::move(turn);
    session->pbemDrafts_ = std::move(draftsDir);
    session->waiting_ = session->turnBased() && !session->myTurn();
    // A turn saved earlier: its commands again, in order (the game is the same, so they play the same).
    if (!session->waiting_ && !session->pbemDrafts_.empty())
        if (auto commands = readPbemDraft(*session->pbem_, session->pbemDrafts_)) {
            for (game::Command& c : *commands) session->issue(std::move(c));
            session->pbemResumed_ = commands->size();
            session->strategic_.clear();  // battles of the replayed commands were seen when they were given
        }
    return session;
}

std::expected<std::filesystem::path, std::string> ClassicSession::savePbemDraft() const {
    if (!pbem_) return std::unexpected(std::string("This is not a play-by-e-mail game."));
    if (!ordersFile_.empty()) return std::unexpected(std::string("The orders of this turn are already saved for the host."));
    if (pbemDrafts_.empty()) return std::unexpected(std::string("No folder to save the turn in."));
    return writePbemDraft(*pbem_, pbemDrafts_, state_, orders_);
}

bool ClassicSession::myTurn() const {
    return turnBased() && !state_.gameOver && state_.playerTurn.started && state_.playerTurn.empire == player_;
}

const std::vector<game::EntryQuestion>& ClassicSession::questions() const {
    static const std::vector<game::EntryQuestion> none;
    return myTurn() ? state_.playerTurn.questions : none;
}

void ClassicSession::record(game::Command c) {
    // The Empire Options are replaced as a whole: only the last change counts.
    if (std::holds_alternative<game::cmd::SetInterfaceOptions>(c))
        std::erase_if(orders_, [](const game::Command& o) { return std::holds_alternative<game::cmd::SetInterfaceOptions>(o); });
    orders_.push_back(std::move(c));
}

game::CommandResult ClassicSession::issue(game::Command c) {
    if (!onIssued) return issueCommand(std::move(c));
    const game::Command copy = c;
    game::CommandResult r = issueCommand(std::move(c));
    if (r.ok) onIssued(copy);
    return r;
}

game::CommandResult ClassicSession::issueCommand(game::Command c) {
    if (waiting_ && kind_ == SessionKind::Pbem)
        return game::CommandResult::fail(ordersFile_.empty() ? "It is not your turn." : "This turn's orders are saved; the turn is over here.");
    if (waiting_) return game::CommandResult::fail("Waiting for the other players");
    if (turnBased() && kind_ == SessionKind::NetworkClient) {
        // The host carries the command out; our copy shows it at once and is
        // replaced by the host's result when that arrives.
        if (!myTurn()) return game::CommandResult::fail("It is not your turn.");
        game::CommandResult r = game::apply(*rules_, state_, player_, c);
        // As the engine does: an answer (even a refused one), or new orders, drop the group's question.
        auto drop = [&](game::VehicleId v, game::FleetId f) {
            std::erase_if(state_.playerTurn.questions, [&](const game::EntryQuestion& q) {
                return f.valid() ? q.fleet == f : !q.fleet.valid() && q.vehicle == v;
            });
            ++revision_;
        };
        if (const auto* a = std::get_if<game::cmd::EnterSector>(&c)) drop(a->vehicle, a->fleet);
        if (!r.ok) return r;
        if (const auto* o = std::get_if<game::cmd::SetOrders>(&c); o && !o->planet.valid()) drop(o->vehicle, o->fleet);
        if (transport_) transport_->playCommand(c);
        record(std::move(c));
        ++revision_;
        return r;
    }
    if (turnBased()) {
        if (kind_ == SessionKind::Pbem && !myTurn()) return game::CommandResult::fail("It is not your turn.");
        if (call_ != Call::None) return game::CommandResult::fail("A battle waits to be fought first.");
        issued_ = {};
        beginCall(Call::Issue, std::move(c));
        // While a battle waits for its answer the order is under way; a refusal shows up as a notice.
        return issued_;
    }
    if (call_ != Call::None) return game::CommandResult::fail("A battle waits to be fought first.");
    game::CommandResult r = game::apply(*rules_, state_, player_, c);
    if (r.ok) {
        record(std::move(c));
        ++revision_;
    }
    return r;
}

std::string ClassicSession::empirePasswordValue(std::string_view password) const {
    if (password.empty()) return {};
    if (kind_ == SessionKind::NetworkClient || kind_ == SessionKind::Pbem || multiplayerGameId_ != 0)
        return net::passwordVerifier(net::hashPassword(password));
    return game::hashPassword(password);
}

bool ClassicSession::passwordMatches(const game::Empire& e, std::string_view password) const {
    if (e.passwordHash.empty()) return true;
    if (kind_ == SessionKind::NetworkClient || kind_ == SessionKind::Pbem || multiplayerGameId_ != 0)
        return net::checkPassword(e.passwordHash, net::hashPassword(password));
    return game::hashPassword(password) == e.passwordHash;
}

void ClassicSession::answer(bool enter) {
    if (questions().empty()) return;
    const game::EntryQuestion q = questions().front();  // the answer drops it (applyLive, or issue() on a network copy)
    issue(game::cmd::EnterSector{q.vehicle, q.fleet, q.where, enter});
}

std::vector<size_t> ClassicSession::takeStrategicBattles() {
    std::vector<size_t> out;
    for (const auto& [who, index] : std::exchange(strategic_, {}))
        if (who == player_) out.push_back(index);
    return out;
}

void ClassicSession::queueTurnBattles() {
    if (turnBased() || !game::simultaneousBattlesShown(*rules_)) return;
    for (size_t i = 0; i < state_.combats.size(); ++i) {
        const auto& who = state_.combats[i].participants;
        if (std::find(who.begin(), who.end(), player_) != who.end()) strategic_.emplace_back(player_, i);
    }
}

void ClassicSession::takeResult(const game::TurnResult& result) {
    notices_.clear();
    for (const auto& [empire, text] : result.rejected)
        if (empire == player_) notices_.push_back(text);
}

void ClassicSession::resumeTurnBased() { beginCall(Call::Resume); }

// Network and PBEM games (the host's own player included, whose session is a
// network client too) never stop: their host fights every battle at once
// (docs/MULTIPLAYER.md).
bool ClassicSession::showsBattles() const { return kind_ == SessionKind::Local || kind_ == SessionKind::Hotseat; }

void ClassicSession::beginCall(Call call, std::optional<game::Command> command) {
    call_ = call;
    callCommand_ = std::move(command);
    callBattles_ = state_.combats.size();
    callTurn_ = state_.turn;
    answers_.clear();
    fought_.clear();
    battle_.reset();
    runCall();
}

void ClassicSession::runCall() {
    const std::vector<game::BattleAnswer>* answers = showsBattles() ? &answers_ : nullptr;
    game::TurnResult res;
    switch (call_) {
        case Call::Issue: res = game::applyLive(*rules_, state_, player_, *callCommand_, answers); break;
        case Call::EndTurn: res = game::endPlayerTurn(*rules_, state_, player_, {}, answers); break;
        case Call::Resume: res = game::resumeTurnBased(*rules_, state_, {}, answers); break;
        case Call::Process: {
            game::TurnOptions options;
            options.battles = answers;
            res = game::processTurn(*rules_, state_, callOrders_, options);
            break;
        }
        case Call::None: return;
    }
    ++revision_;
    if (res.battle) {
        // The call stopped at a battle (or ground fight) to show; the game is as it was.
        battle_ = std::move(res.battle);
        log::info("A battle at system {} ({}, {}) stops the turn to be shown ({} human side(s))", battle_->where.system.value,
                  battle_->where.sector.x, battle_->where.sector.y, battle_->humans.size());
        return;
    }
    const Call call = std::exchange(call_, Call::None);
    if (kind_ == SessionKind::Local || kind_ == SessionKind::Hotseat) writePlayerRecords(res.records);
    // The battles fought in the Tactical Combat window must have come out the same here.
    for (const game::CombatRecord& fought : fought_) {
        const auto same = [&](const game::CombatRecord& r) {
            return r.location == fought.location && r.turn == fought.turn && r.summary == fought.summary && r.pieces.size() == fought.pieces.size() &&
                   r.events.size() == fought.events.size();
        };
        if (std::none_of(state_.combats.begin() + std::ptrdiff_t(std::min(callBattles_, state_.combats.size())), state_.combats.end(), same))
            log::warn("The tactical battle at system {} came out differently in the game", fought.location.system.value);
    }
    fought_.clear();
    const bool answered = !answers_.empty();
    answers_.clear();
    // A PBEM game never stops: the battles the player's order started are
    // shown afterwards (spec 06 §1.10.5, "different machines"). Local and
    // hotseat games showed theirs as they happened.
    if (kind_ == SessionKind::Pbem && call == Call::Issue)
        for (size_t i = std::min(callBattles_, state_.combats.size()); i < state_.combats.size(); ++i) {
            const auto& who = state_.combats[i].participants;
            if (std::find(who.begin(), who.end(), player_) != who.end()) strategic_.emplace_back(player_, i);
        }
    auto nextHuman = [&] {
        // The session belongs to the human whose turn it is (hotseat: the next one).
        if (const game::EmpireId e = game::activePlayer(state_); e.valid() && state_.empire(e).kind == game::PlayerKind::Human) player_ = e;
    };
    switch (call) {
        case Call::Issue: {
            // Attack Sector questions stay in the game (GameState::playerTurn.questions).
            // PBEM: the host replays every command given, refused ones too (a
            // refused answer still settles its question), so all are kept.
            if (kind_ == SessionKind::Pbem) record(*callCommand_);
            if (!res.rejected.empty()) {
                issued_ = game::CommandResult::fail(res.rejected.front().second);
                if (answered) notices_.push_back(res.rejected.front().second);
            } else {
                if (kind_ != SessionKind::Pbem) record(std::move(*callCommand_));
                issued_ = {};
            }
            break;
        }
        case Call::EndTurn:
            nextHuman();
            takeResult(res);
            orders_.clear();
            waiting_ = false;
            if (state_.turn != callTurn_) autosave();  // the game turn was processed (spec 01 §2.2)
            if (onNewTurn) onNewTurn();
            break;
        case Call::Resume:
            nextHuman();
            takeResult(res);
            if (state_.turn != callTurn_) autosave();
            break;
        case Call::Process:
            // A simultaneous turn: the new turn begins for the first human.
            writePlayerRecords(res.records);
            strategic_.clear();
            takeResult(res);
            if (kind_ == SessionKind::Hotseat)
                for (const game::Empire& e : state_.empires)
                    if (e.alive && e.kind == game::PlayerKind::Human) {
                        setPlayer(e.id);
                        break;
                    }
            callOrders_.clear();
            autosave();
            beginTurn();
            break;
        case Call::None: break;
    }
    callCommand_.reset();
}

void ClassicSession::answerBattle(game::BattleAnswer answer) {
    if (!battle_ || call_ == Call::None) return;
    answers_.push_back(std::move(answer));
    battle_.reset();
    runCall();
}

void ClassicSession::startTactical(TacticalFight fight) { tactical_ = std::make_unique<TacticalFight>(std::move(fight)); }

void ClassicSession::endTactical() {
    if (!tactical_) return;
    std::unique_ptr<TacticalFight> fight = std::move(tactical_);
    if (fight->kind != TacticalFight::Kind::Game || !fight->battle) return;
    // Phases left are played by the strategies, as a script that runs out does.
    fight->battle->finish();
    fought_.push_back(fight->battle->record());
    // Fought by hand, or by the strategies while the Strategic Combat window showed it.
    answerBattle(game::BattleAnswer{fight->players, fight->battle->script()});
}

void ClassicSession::endTurn() {
    if (waiting_ || call_ != Call::None) return;
    if (kind_ == SessionKind::Pbem) {
        // The host processes the turn: write the orders file for it and wait.
        if (!pbem_ || (turnBased() && !myTurn())) return;
        auto file = writePbemOrders(*pbem_, state_, orders_);
        if (!file) {
            pbemError_ = file.error();
            log::warn("PBEM: {}", pbemError_);
            return;
        }
        pbemError_.clear();
        ordersFile_ = *file;
        waiting_ = true;
        if (!pbemDrafts_.empty()) removePbemDraft(*pbem_, pbemDrafts_);
        ++revision_;
        return;
    }
    if (turnBased() && kind_ == SessionKind::NetworkClient) {
        if (!myTurn()) return;
        if (transport_) transport_->endPlayerTurn();
        waiting_ = true;
        return;
    }
    if (turnBased()) {
        // The player's end-of-turn processing; the computer players' turns;
        // then the next human's turn starts (after any battles shown).
        beginCall(Call::EndTurn);
        return;
    }
    if (kind_ == SessionKind::NetworkClient) {
        if (transport_) transport_->submitOrders(game::EmpireOrders{player_, state_.turn, orders_});
        waiting_ = true;
        return;
    }
    if (kind_ == SessionKind::Hotseat) {
        ended_[player_.index()] = 1;
        for (const game::Empire& e : state_.empires)
            if (e.alive && e.kind == game::PlayerKind::Human && !ended_[e.id.index()]) {
                setPlayer(e.id);
                orders_.clear();
                ++revision_;
                return;
            }
    }
    // Every human's orders are already applied to this state; an empty list
    // marks them as submitted so the computer does not play for them. The
    // turn stops at each battle the Settings show (game::TurnOptions::battles).
    callOrders_.clear();
    for (const game::Empire& e : state_.empires)
        if (e.alive && e.kind == game::PlayerKind::Human) callOrders_.push_back({e.id, state_.turn, {}});
    beginCall(Call::Process);
}

std::optional<std::filesystem::path> ClassicSession::autosave() {
    if (kind_ == SessionKind::NetworkClient || kind_ == SessionKind::Pbem) return std::nullopt;  // the host keeps the game
    const auto name = setup::autosaveName(state_.options.autosaveTurns, state_.turn);
    if (!name) return std::nullopt;
    const std::filesystem::path file = savesDir() / (*name + ".gam");
    if (auto saved = save(file, *name); !saved) {
        autosaveNote_ = std::format("Autosave failed: {}", saved.error());
        log::warn("{}", autosaveNote_);
        return std::nullopt;
    }
    copyHistoryNextTo(file);
    autosaveNote_ = std::format("Saved as {}", *name);
    return file;
}

void ClassicSession::poll() {
    if (!transport_) return;
    auto s = transport_->pollState();
    if (!s) return;
    if (!game::turnBased(*s)) {
        state_ = std::move(*s);
        strategic_.clear();
        queueTurnBattles();
        beginTurn();
        return;
    }
    // Turn-based: the host's state after our commands, a battle we fought in
    // another player's turn, or the turn passing on.
    const bool wasMine = myTurn();
    const uint32_t oldTurn = state_.turn;
    const size_t oldBattles = state_.combats.size();
    state_ = std::move(*s);
    ++revision_;
    if (state_.turn == oldTurn)
        for (size_t i = oldBattles; i < state_.combats.size(); ++i) {
            const auto& who = state_.combats[i].participants;
            if (std::find(who.begin(), who.end(), player_) != who.end()) strategic_.emplace_back(player_, i);
        }
    const bool mine = myTurn();
    if (mine && (!wasMine || state_.turn != oldTurn)) beginTurn();  // our turn starts
    waiting_ = !mine;
}

void ClassicSession::beginTurn() {
    orders_.clear();
    ended_.assign(state_.empires.size(), 0);
    waiting_ = false;
    ++revision_;
    if (onNewTurn) onNewTurn();
}

bool ClassicSession::setAutosaveTurns(int everyTurns) {
    if (kind_ == SessionKind::NetworkClient || kind_ == SessionKind::Pbem) return false;
    if (std::find(setup::kAutosaveTurns.begin(), setup::kAutosaveTurns.end(), everyTurns) == setup::kAutosaveTurns.end()) return false;
    state_.options.autosaveTurns = everyTurns;
    ++revision_;
    return true;
}

void ClassicSession::setPlayer(game::EmpireId e) {
    player_ = e;
    ++revision_;
}

void ClassicSession::replaceState(game::GameState s) {
    state_ = std::move(s);
    strategic_.clear();
    call_ = Call::None;
    battle_.reset();
    answers_.clear();
    callOrders_.clear();
    tactical_.reset();
    if (turnBased() && kind_ != SessionKind::NetworkClient && kind_ != SessionKind::Pbem) resumeTurnBased();
    beginTurn();
}

void ClassicSession::simulateTurns(int n) {
    if (kind_ == SessionKind::Pbem) return;  // only the host plays PBEM turns
    // A turn-based game plays whole game turns the same way (processTurn).
    for (int i = 0; i < n && !state_.gameOver; ++i) {
        const game::TurnResult result = game::processTurn(*rules_, state_, {});
        if (kind_ != SessionKind::NetworkClient) writePlayerRecords(result.records);
    }
    if (n > 0 && turnBased() && kind_ != SessionKind::NetworkClient) resumeTurnBased();
    if (n > 0) beginTurn();
}

std::expected<void, std::string> ClassicSession::save(const std::filesystem::path& file, const std::string& gameName) const {
    game::SaveInfo info;
    info.gameName = gameName;
    info.gameId = multiplayerGameId_;  // a network or PBEM game stays one: its passwords are verifiers
    info.dataSet = rules_->data().dataDir.parent_path().filename().string();
    info.turn = state_.turn;
    for (const game::Empire& e : state_.empires) info.empires.push_back(e.name);
    return game::saveGame(file, state_, info);
}

std::expected<std::unique_ptr<ClassicSession>, std::string> ClassicSession::load(std::shared_ptr<const game::Rules> rules,
                                                                                 const std::filesystem::path& file) {
    auto loaded = game::loadGame(file);
    if (!loaded) return std::unexpected(loaded.error());
    game::GameState& s = loaded->first;
    game::EmpireId player;
    int humans = 0;
    for (const game::Empire& e : s.empires)
        if (e.alive && e.kind == game::PlayerKind::Human) {
            if (!player.valid()) player = e.id;
            ++humans;
        }
    if (!player.valid()) player = game::EmpireId{0u};
    const SessionKind kind = humans > 1 ? SessionKind::Hotseat : SessionKind::Local;
    auto session = std::make_unique<ClassicSession>(std::move(rules), std::move(s), player, kind);
    // A network host's save or a PBEM game file (they carry a game id) is played
    // on here as a local game; its passwords are the verifiers the host checks.
    session->multiplayerGameId_ = loaded->second.gameId;
    return session;
}

std::filesystem::path userDataDir() {
    std::filesystem::path dir;
    if (char* pref = SDL_GetPrefPath("", "OpenSE4")) {
        dir = pref;
        SDL_free(pref);
    } else {
        dir = std::filesystem::current_path() / "userdata";
    }
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

std::filesystem::path historyDir() { return userDataDir() / "History"; }

std::string historyFileName(game::EmpireId empire, std::string_view kind) { return std::format("plr_{}_{}", empire.value + 1, kind); }

void copyHistoryNextTo(const std::filesystem::path& saveFile, const std::filesystem::path& history) {
    std::error_code ec;
    const std::string prefix = saveFile.stem().string() + "_";
    for (const auto& e : std::filesystem::directory_iterator(history, ec)) {
        if (!e.is_regular_file(ec)) continue;
        std::filesystem::copy_file(e.path(), saveFile.parent_path() / (prefix + e.path().filename().string()),
                                   std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) log::warn("Cannot copy {} next to {}: {}", e.path().string(), saveFile.string(), ec.message());
    }
}

void restoreHistoryFrom(const std::filesystem::path& saveFile, const std::filesystem::path& history) {
    std::error_code ec;
    const std::filesystem::path& dir = history;
    for (const auto& e : std::filesystem::directory_iterator(dir, ec))
        if (e.is_regular_file(ec)) std::filesystem::remove(e.path(), ec);
    std::filesystem::create_directories(dir, ec);
    const std::string prefix = saveFile.stem().string() + "_plr_";
    for (const auto& e : std::filesystem::directory_iterator(saveFile.parent_path(), ec)) {
        const std::string name = e.path().filename().string();
        if (!e.is_regular_file(ec) || !name.starts_with(prefix) || !name.ends_with(".txt")) continue;
        std::filesystem::copy_file(e.path(), dir / name.substr(saveFile.stem().string().size() + 1), std::filesystem::copy_options::overwrite_existing,
                                   ec);
    }
}

std::filesystem::path savesDir() {
    const std::filesystem::path dir = userDataDir() / "saves";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

} // namespace opense4::client::classic
