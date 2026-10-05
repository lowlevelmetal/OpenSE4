#include "client/classic/session.hpp"

#include "client/app_settings.hpp"
#include "client/classic/screens/setup_model.hpp"
#include "client/classic/settings.hpp"
#include "core/log.hpp"
#include "game/ai.hpp"
#include "game/classic_save.hpp"
#include "game/diplomacy.hpp"
#include "game/serialize.hpp"
#include "game/setup.hpp"
#include "game/turn.hpp"
#include "net/auth.hpp"

#include <algorithm>
#include <exception>
#include <format>
#include <fstream>
#include <random>
#include <string_view>
#include <utility>
#include <vector>

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
    // CR LF line ends on every platform, as the original's Windows text files
    // (the log copy's are confirmed, docs/spec/06 §6.1; the others inferred),
    // written in binary so that Linux and Windows give the same bytes.
    auto write = [&](const std::filesystem::path& file, const std::vector<std::string>& lines, bool fresh) {
        if (lines.empty()) {
            if (fresh) std::filesystem::remove(file, ec);
            return;
        }
        std::ofstream out(file, (fresh ? std::ios::trunc : std::ios::app) | std::ios::binary);
        for (const std::string& line : lines) out << line << "\r\n";
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
    session->multiplayerGameId_ = turn.info.gameId;  // salts the passwords this session makes
    session->pbem_ = std::move(turn);
    session->pbemDrafts_ = std::move(draftsDir);
    session->masterVerifier_ = game.info.masterPasswordVerifier;
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
    return writePbemDraft(*pbem_, pbemDrafts_, orders_);
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
    if (!onIssued && !switchedOwn_) return issueCommand(std::move(c));
    const game::Command copy = c;
    game::CommandResult r = issueCommand(std::move(c));
    if (r.ok && onIssued) onIssued(copy);
    if (r.ok) carryFlags(copy);
    return r;
}

// After a Players-window switch of our own empire on a player's copy, an
// object changed this turn (given orders) carries its minister flag to the
// host with the orders (spec 06 §7 Q84, confirmed: binary); a SetMinister
// (the order panel's minister button) carries its own. Which commands count
// as a change is ours (inferred, spec 06 §7 Q99).
void ClassicSession::carryFlags(const game::Command& c) {
    if (!switchedOwn_ || (kind_ != SessionKind::NetworkClient && kind_ != SessionKind::Pbem)) return;
    std::vector<game::VehicleId> vehicles;
    const auto* o = std::get_if<game::cmd::SetOrders>(&c);
    if (const auto* t = std::get_if<game::cmd::OrderTagged>(&c)) vehicles = game::taggedVehicles(state_, player_, t->vehicles);
    else if (!o) return;
    if (o && o->vehicle.valid()) vehicles.push_back(o->vehicle);
    if (const game::Fleet* f = o ? state_.fleet(o->fleet) : nullptr) vehicles.insert(vehicles.end(), f->members.begin(), f->members.end());
    for (game::VehicleId id : vehicles) {
        const game::Vehicle* v = state_.vehicle(id);
        if (!v || v->owner != player_ || std::find(flagged_.begin(), flagged_.end(), id) != flagged_.end()) continue;
        flagged_.push_back(id);
        issue(game::cmd::SetMinister{id, {}, false, v->minister});
    }
    if (const game::Colony* col = o && o->planet.valid() ? state_.colony(o->planet) : nullptr;
        col && col->owner == player_ && std::find(flaggedPlanets_.begin(), flaggedPlanets_.end(), o->planet) == flaggedPlanets_.end()) {
        flaggedPlanets_.push_back(o->planet);
        issue(game::cmd::SetMinister{{}, o->planet, false, col->minister});
    }
}

void ClassicSession::clearOrders() {
    orders_.clear();
    switchedOwn_ = false;
    flagged_.clear();
    flaggedPlanets_.clear();
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
        const size_t open = state_.playerTurn.questions.size();
        game::dropSettledQuestions(state_, c, r.ok);
        if (state_.playerTurn.questions.size() != open) ++revision_;
        if (!r.ok) return r;
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

std::expected<std::string, std::string> ClassicSession::empirePasswordValue(std::string_view password) const {
    if (password.empty()) return std::string{};
    if (kind_ == SessionKind::NetworkClient || kind_ == SessionKind::Pbem || multiplayerGameId_ != 0) {
        try {
            return net::passwordVerifier(password, multiplayerGameId_);
        } catch (const net::PasswordWorkError& e) {
            return std::unexpected(std::string(e.what()));
        }
    }
    return game::hashPassword(password);
}

std::expected<bool, std::string> ClassicSession::passwordMatches(const game::Empire& e, std::string_view password) const {
    if (e.passwordHash.empty()) return true;
    if (kind_ == SessionKind::NetworkClient || kind_ == SessionKind::Pbem || multiplayerGameId_ != 0) {
        try {
            return net::checkPassword(e.passwordHash, password, multiplayerGameId_);
        } catch (const net::PasswordWorkError& error) {
            return std::unexpected(std::string(error.what()));
        }
    }
    return game::hashPassword(password) == e.passwordHash;
}

void ClassicSession::answer(bool enter) {
    if (questions().empty()) return;
    const game::EntryQuestion q = questions().front();  // the answer drops it (applyLive, or issue() on a network copy)
    issue(game::cmd::EnterSector{q.vehicle, q.fleet, q.where, enter, q.tagged});
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

void ClassicSession::takeLive(game::TurnResult& res, bool turnStart) {
    // Only the local player's own, while its turn is in progress (spec 03 §8, spec 06 §2.7).
    if (!myTurn()) return;
    for (game::PlayerMessage& m : res.messages)
        if (m.empire == player_) messages_.push_back(std::move(m));
    std::vector<game::LiveStep> steps;
    for (const game::LiveStep& st : res.liveSteps)
        if (st.empire == player_) steps.push_back(st);
    if (steps.empty()) return;
    liveSteps_ = std::move(steps);
    liveStepsAtTurnStart_ = turnStart;
    ++liveStepsCall_;
}

void ClassicSession::resumeTurnBased() { beginCall(Call::Resume); }

// Network and PBEM games (the host's own player included, whose session is a
// network client too) never stop: their host fights every battle at once
// (docs/MULTIPLAYER.md).
bool ClassicSession::showsBattles() const { return kind_ == SessionKind::Local || kind_ == SessionKind::Hotseat; }

game::LiveOptions ClassicSession::liveOptions() const {
    game::LiveOptions o;
    // Local and hotseat games stop when no human is left (spec 06 §1.2.1).
    o.endWithoutHumans = kind_ == SessionKind::Local || kind_ == SessionKind::Hotseat;
    return o;
}

bool ClassicSession::humansGone() const {
    return (kind_ == SessionKind::Local || kind_ == SessionKind::Hotseat) && !game::ai::anyHumanLeft(state_);
}

std::expected<bool, std::string> ClassicSession::masterPasswordMatches(std::string_view password) const {
    if (masterVerifier_.empty()) return false;
    try {
        return net::checkPassword(masterVerifier_, password, multiplayerGameId_);
    } catch (const net::PasswordWorkError& e) {
        return std::unexpected(std::string(e.what()));
    }
}

void ClassicSession::setComputerControl(const std::vector<std::pair<game::EmpireId, bool>>& rows) {
    // While a battle waits to be shown the game is as before the engine call,
    // which is made again with the answers: who is human must not change
    // meanwhile, or the call would stop at other battles than those answered.
    if (call_ != Call::None) {
        log::warn("Computer control is not changed while a battle waits to be fought");
        return;
    }
    for (const auto& [empire, computer] : rows) {
        if (!game::ai::setComputerControl(state_, empire, computer)) continue;
        // A player's copy of a game on different machines: the orders carry the
        // empire's own data always (its minister switches, its fleets with
        // their flags), and of its ships, units and colonies only those changed
        // this turn, with their flags; never the mark, and nothing about other
        // empires (spec 06 §1.2.1, §7 Q84, confirmed: binary).
        if ((kind_ == SessionKind::NetworkClient || kind_ == SessionKind::Pbem) && empire == player_) {
            game::cmd::SetMinisters m;
            m.areas = computer ? game::kAllMinisters : 0u;
            m.fleets = computer;
            issue(m);
            issue(game::cmd::SetMinister{{}, {}, true, computer});
            switchedOwn_ = true;
            flagged_.clear();
            flaggedPlanets_.clear();
            const std::vector<game::Command> earlier = orders_;
            for (const game::Command& c : earlier) carryFlags(c);
        }
    }
    // A second human makes a local game hotseat: End Turn then passes to the
    // other humans before the turn is processed. A hotseat game stays one, so
    // the humans left are still asked in turn.
    if (kind_ == SessionKind::Local) {
        const auto humans = std::count_if(state_.empires.begin(), state_.empires.end(),
                                          [](const game::Empire& e) { return e.alive && e.kind == game::PlayerKind::Human; });
        if (humans > 1) kind_ = SessionKind::Hotseat;
    }
    ended_.resize(state_.empires.size(), 0);
    ++revision_;
}

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
    if (call_ != Call::None) ++engineCalls_;
    try {
        switch (call_) {
            case Call::Issue: res = game::applyLive(*rules_, state_, player_, *callCommand_, answers); break;
            case Call::EndTurn: res = game::endPlayerTurn(*rules_, state_, player_, liveOptions(), answers); break;
            case Call::Resume: res = game::resumeTurnBased(*rules_, state_, liveOptions(), answers); break;
            case Call::Process: {
                game::TurnOptions options;
                options.battles = answers;
                res = game::processTurn(*rules_, state_, callOrders_, options);
                break;
            }
            case Call::None: return;
        }
    } catch (const std::exception& e) {
        dropCall(e.what());
        throw;
    } catch (...) {
        dropCall("an unknown exception");
        throw;
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
    // The battles fought in the Tactical Combat window must have come out the
    // same here. Searched in the whole list: a call that ends the game turn
    // drops the battles of the turn before (GameState::combats), so the new
    // ones need not start at the old length.
    for (const game::CombatRecord& fought : fought_) {
        const auto same = [&](const game::CombatRecord& r) {
            return r.location == fought.location && r.turn == fought.turn && r.summary == fought.summary && r.pieces.size() == fought.pieces.size() &&
                   r.events.size() == fought.events.size();
        };
        if (std::none_of(state_.combats.begin(), state_.combats.end(), same))
            log::warn("The tactical battle at system {} came out differently in the game", fought.location.system.value);
    }
    fought_.clear();
    const bool answered = !answers_.empty();
    answers_.clear();
    // A PBEM game never stops: the battles the player's order started are
    // shown afterwards (spec 06 §1.10.5, "different machines"). Local and
    // hotseat games showed theirs as they happened. The PBEM copy is the
    // player's own view, so its battles are a preview: the host fights them
    // again with the whole game (docs/MULTIPLAYER.md, "Play by e-mail").
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
            takeLive(res, false);
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
            takeLive(res, true);
            clearOrders();
            waiting_ = false;
            if (state_.turn != callTurn_) autosave();  // the game turn was processed (spec 01 §2.2)
            if (onNewTurn) onNewTurn();
            break;
        case Call::Resume:
            nextHuman();
            takeResult(res);
            takeLive(res, true);
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

void ClassicSession::dropCall(std::string_view why) {
    log::error("The game could not go on ({}): it stays as it was before the {}", why,
               call_ == Call::Issue ? "order" : call_ == Call::Resume ? "computer players' turns" : "turn was ended");
    // processTurn keeps no copy of its own when no battle can stop it.
    if (call_ == Call::Process && turnStart_) state_ = *turnStart_;
    call_ = Call::None;
    callCommand_.reset();
    callOrders_.clear();
    answers_.clear();
    fought_.clear();
    battle_.reset();
    if (tactical_ && tactical_->kind == TacticalFight::Kind::Game) tactical_.reset();
    ++revision_;
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
        auto file = writePbemOrders(*pbem_, orders_);
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
    // The game has ended for lack of humans: no further turn (spec 06 §1.2.1).
    if (humansGone()) return;
    if (kind_ == SessionKind::Hotseat) {
        ended_[player_.index()] = 1;
        for (const game::Empire& e : state_.empires)
            if (e.alive && e.kind == game::PlayerKind::Human && !ended_[e.id.index()]) {
                setPlayer(e.id);
                clearOrders();
                reloadGame();
                ++revision_;
                return;
            }
    }
    // The original reloads the game file between hotseat players and before
    // processing the turn (spec 01 §6.9, §14 Q44).
    reloadGame();
    // Every human's orders are already applied to this state; an empty list
    // marks them as submitted so the computer does not play for them. The
    // turn stops at each battle the Settings show (game::TurnOptions::battles).
    callOrders_.clear();
    for (const game::Empire& e : state_.empires)
        if (e.alive && e.kind == game::PlayerKind::Human) callOrders_.push_back({e.id, state_.turn, {}});
    // Kept for the movement log replay (docs/spec/06 §7 Q51): played again
    // without stops, the turn comes out as it does with the battles shown.
    turnStart_ = std::make_shared<const game::GameState>(state_);
    turnStartOrders_ = callOrders_;
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
        if (s->turn == state_.turn) {
            // The host sent this turn's game again (our copy differed from
            // its, a desync): our orders so far go onto the new copy, and an
            // End Turn already given stays given.
            state_ = std::move(*s);
            std::vector<game::Command> again = std::exchange(orders_, {});
            for (game::Command& c : again)
                if (game::apply(*rules_, state_, player_, c).ok) record(std::move(c));
            ++revision_;
            return;
        }
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
    clearOrders();
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

bool ClassicSession::replayLastTurn(const std::function<void(int day, const game::GameState&)>& day,
                                    const std::function<void(const game::MovementStep&)>& step) const {
    if (!turnStart_) return false;
    game::GameState again = *turnStart_;
    game::TurnOptions options;
    options.movementDay = day;
    options.movementStep = step;
    game::processTurn(*rules_, again, turnStartOrders_, options);
    return true;
}

void ClassicSession::reloadGame() {
    game::diplomacy::recalculateColonies(*rules_, state_);
    state_.pendingMood.clear();
}

void ClassicSession::setPlayer(game::EmpireId e) {
    player_ = e;
    ++revision_;
}

void ClassicSession::replaceState(game::GameState s) {
    state_ = std::move(s);
    turnStart_.reset();
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
    info.masterPasswordVerifier = masterVerifier_;
    info.dataSet = rules_->data().dataDir.parent_path().filename().string();
    info.turn = state_.turn;
    for (const game::Empire& e : state_.empires) info.empires.push_back(e.name);
    auto saved = game::saveGame(file, state_, info);
    // Every game saved, Save Game and every autosave alike, becomes the one
    // Resume Game loads (docs/spec/06 §6.1, §7 Q53).
    if (saved) rememberSavedGame(file.string());
    return saved;
}

namespace {

// Writes what an import or export approximated to the log file.
void logReport(std::string_view what, const std::filesystem::path& file, const game::classic::ConversionReport& report) {
    log::info("{} {}", what, file.string());
    for (const std::string& n : report.notes) log::info("  {}", n);
    for (const std::string& d : report.details) log::info("  detail: {}", d);
}

} // namespace

std::expected<std::vector<std::string>, std::string> ClassicSession::exportClassic(const std::filesystem::path& file) const {
    if (kind_ != SessionKind::Local && kind_ != SessionKind::Hotseat)
        return std::unexpected(std::string("Only a local or hotseat game can be saved for Space Empires IV: this copy of a network or "
                                           "play-by-e-mail game holds only what your empire knows."));
    game::classic::ConversionReport report;
    game::classic::ExportOptions options;
    options.keySeed = std::random_device{}();
    options.gameName = file.stem().string();
    auto written = game::classic::writeClassicGame(*rules_, state_, file, report, options);
    if (!written) return std::unexpected(written.error());
    // The players' History files go beside it, as the original's own saves
    // keep them (spec 08 §1.2): the original's Scores and History windows
    // read them, so the graphs are not lost after all.
    copyHistoryNextTo(file);
    std::erase_if(report.notes, [](const std::string& n) { return n.starts_with("History and score graphs"); });
    logReport("Saved for Space Empires IV:", file, report);
    return report.notes;
}

std::expected<std::unique_ptr<ClassicSession>, std::string> ClassicSession::load(std::shared_ptr<const game::Rules> rules,
                                                                                 const std::filesystem::path& file) {
    std::vector<std::string> importNotes;
    std::expected<std::pair<game::GameState, game::SaveInfo>, std::string> loaded;
    if (auto bytes = game::readFileBytes(file); bytes && game::classic::looksLikeClassicSave(*bytes)) {
        // A saved game of the original: imported (docs/spec/08).
        game::classic::ConversionReport report;
        auto imported = game::classic::readClassicGame(*rules, file, report);
        if (!imported) return std::unexpected(imported.error());
        logReport("Imported the Space Empires IV saved game", file, report);
        importNotes = std::move(report.notes);
        loaded = std::pair<game::GameState, game::SaveInfo>{std::move(*imported), game::SaveInfo{}};
    } else {
        loaded = game::loadGame(file);
    }
    if (!loaded) return std::unexpected(loaded.error());
    game::GameState& s = loaded->first;
    // A game made with mods that change the game plays only with the same
    // ones (asset-only and interface-only mods may differ).
    if (const auto mods = game::modDifferences(s.mods, *rules, "the saved game"); !mods.empty()) {
        std::string why = std::format("{} needs other mods than this session has (start OpenSE4 with the same ones, see --mod):", file.filename().string());
        for (const std::string& m : mods) why += "\n  " + m;
        return std::unexpected(why);
    }
    // Reading a game file recalculates every colony's cloak and sensor levels,
    // a colony that can no longer cloak decloaking as by Decloak (spec 01
    // §6.9, §14 Q44, confirmed: binary).
    game::diplomacy::recalculateColonies(*rules, s);
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
    session->masterVerifier_ = loaded->second.masterPasswordVerifier;
    session->importNotes_ = std::move(importNotes);
    return session;
}

std::filesystem::path userDataDir() { return userDataDirectory(); }

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
