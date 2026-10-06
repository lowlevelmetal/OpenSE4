#include "sdk/players.hpp"

#include "core/hash.hpp"
#include "core/log.hpp"
#include "game/ai.hpp"
#include "game/turn.hpp"
#include "mods/data_set.hpp"
#include "mods/mod_set.hpp"
#include "script/json.hpp"
#include "sdk/codec.hpp"
#include "sdk/names.hpp"
#include "sdk/player_values.hpp"
#include "sdk/queries.hpp"
#include "sdk/rules_view.hpp"
#include "sdk/value_io.hpp"
#include "sdk/view.hpp"
#include "sdk/worker.hpp"

#include <algorithm>
#include <chrono>
#include <deque>
#include <format>
#include <fstream>
#include <map>
#include <mutex>
#include <optional>
#include <sstream>
#include <thread>
#include <utility>

#if defined(__SANITIZE_ADDRESS__)
#define OPENSE4_SDK_SANITIZED 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define OPENSE4_SDK_SANITIZED 1
#endif
#endif

namespace opense4::sdk {

using detail::Map;
using game::EmpireId;
using script::Value;
using script::ValueList;
using script::ValueMap;

ExternalBot::~ExternalBot() = default;

game::Controller PlayerChoice::controller() const {
    game::Controller c;
    c.kind = game::Controller::Kind::Script;
    c.mod = mod;
    c.player = name;
    return c;
}

std::vector<PlayerChoice> availablePlayers(std::span<const mods::Package> packages) {
    std::vector<PlayerChoice> out;
    for (const mods::Package& p : packages)
        for (const mods::AiPlayer& a : p.manifest.aiPlayers) out.push_back({p.id(), a.name, a.description});
    return out;
}

std::vector<PlayerChoice> availablePlayers(const mods::ModSet& mods) { return availablePlayers(std::span<const mods::Package>(mods.packages)); }

std::span<const mods::Package> gamePackages(const game::Rules& r) {
    if (const auto* data = dynamic_cast<const mods::GameData*>(r.files())) return data->mods().packages;
    return {};
}

std::vector<PlayerChoice> availablePlayers(const game::Rules& r) { return availablePlayers(gamePackages(r)); }

std::vector<std::string> checkControllers(std::span<const game::EmpireSetup> empires, std::span<const mods::Package> packages) {
    std::vector<std::string> out;
    for (size_t i = 0; i < empires.size(); ++i) {
        const game::Controller& c = empires[i].controller;
        if (c.kind != game::Controller::Kind::Script) continue;
        const std::string named = empires[i].name.empty() ? std::format("empire {}", i + 1) : empires[i].name;
        const auto mod = std::find_if(packages.begin(), packages.end(), [&](const mods::Package& p) { return p.id() == c.mod; });
        if (mod == packages.end())
            out.push_back(std::format("{}: the computer player {} needs the mod {}, which the game does not use", named, game::controllerText(c), c.mod));
        else if (!mod->manifest.aiPlayer(c.player))
            out.push_back(std::format("{}: the mod {} has no computer player named '{}'", named, c.mod, c.player));
    }
    return out;
}

namespace {

// ---- Limits -------------------------------------------------------------------------------------------

// The C stack the interpreter may use below each call: the runtime's default,
// and more for the sanitizers' larger frames (docs/sdk/runtime.md, "Limits").
#if defined(OPENSE4_SDK_SANITIZED)
constexpr size_t kCStackBytes = size_t{1} << 20;
#else
constexpr size_t kCStackBytes = size_t{256} << 10;
#endif
// The interpreter's own budget: the requests are limited one by one (§7).
constexpr int64_t kInterpreterBudget = int64_t{1} << 60;

// What the services charge, in bytecodes (docs/sdk/ai-protocol.md §6).
constexpr int64_t kQueryCost = 20'000;
constexpr int64_t kBuiltinCost = 2'000'000;
constexpr int64_t kAnswerCost = 20'000;
constexpr int64_t kApplyCost = 50'000;
constexpr int64_t kNodeCost = 20;       // per value in a result
constexpr int64_t kViewNodeCost = 5;    // per value of a view the engine builds

int64_t nodeCost(const Value& v, int64_t each) { return static_cast<int64_t>(detail::valueNodes(v)) * each; }

// The interpreter is one per process: sessions take turns with it.
std::timed_mutex& interpreterSlot() {
    static std::timed_mutex m;
    return m;
}

Value errorValue(std::string type, std::string message, std::string traceback = {}) {
    ValueMap e;
    e.emplace_back("type", Value(std::move(type)));
    e.emplace_back("message", Value(std::move(message)));
    e.emplace_back("traceback", Value(std::move(traceback)));
    ValueMap r;
    r.emplace_back("error", Value(std::move(e)));
    return Value(std::move(r));
}

std::string jsonText(const Value& v) {
    auto j = script::toJson(v);
    return j ? std::move(*j) : std::string("null");
}

std::string readText(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::string who(const game::GameState& s, EmpireId e) {
    const game::Empire& emp = s.empire(e);
    return std::format("{} ({})", emp.name, game::controllerText(emp.controller));
}

// ---- The session --------------------------------------------------------------------------------------

class Session final : public game::Players {
public:
    Session(const game::Rules& r, game::GameState& s, std::shared_ptr<const PlayerSetup> setup) : rules_(r), setup_(std::move(setup)) {
        // Answers waiting to be given again (a call made again, a turn played again).
        replay_.assign(std::make_move_iterator(s.journal.replay.begin()), std::make_move_iterator(s.journal.replay.end()));
        s.journal.replay.clear();
        for (const game::Empire& e : s.empires)
            if (e.controller.kind == game::Controller::Kind::Script &&
                std::find(modsUsed_.begin(), modsUsed_.end(), e.controller.mod) == modsUsed_.end())
                modsUsed_.push_back(e.controller.mod);
    }

    ~Session() override {
        if (worker_ && interp_) {
            try {
                worker_->run([&] { interp_.reset(); });
            } catch (...) {
            }
        }
        interp_.reset();
        worker_.reset();
    }

    bool plan(game::TurnContext& ctx, EmpireId e, game::PlanCall call, const game::CommandSink& sink) override {
        applied_ = 0;
        const std::optional<Value> response = ask(ctx, e, game::callName(call), Value::emptyMap(), &sink);
        if (!response) {
            if (applied_ > 0) sink.settle();   // what its `apply` carried out stands
            return false;
        }
        Slot& slot = slotOf(e);
        ValueList refusals;
        if (const Value* cmds = response->find("commands"); cmds && cmds->isList()) {
            for (size_t i = 0; i < cmds->asList().size(); ++i) {
                const Value& item = cmds->asList()[i];
                auto decoded = decodeCommand(item);
                std::string why;
                if (!decoded) why = decoded.error().text();
                else if (game::CommandResult res = sink.apply(*decoded); !res.ok) why = res.error;
                if (why.empty()) continue;
                log::info("Computer player {}: {} refused: {}", who(ctx.state, e), script::describe(item, 120), why);
                refusals.push_back(refusal(i, item, why));
            }
        }
        sink.settle();
        slot.refused = Value(std::move(refusals));
        return true;
    }

    std::optional<std::string> colonyType(game::TurnContext& ctx, EmpireId e, game::ObjectId planet, game::VehicleId ship) override {
        const game::GameState& s = ctx.state;
        ValueList choices;
        for (const std::string& t : s.empire(e).colonyTypes) choices.push_back(Value(t));
        ValueMap args;
        args.emplace_back("colony", Value(static_cast<int64_t>(planet.value)));   // a colony is named by its planet
        args.emplace_back("planet", Value(static_cast<int64_t>(planet.value)));
        args.emplace_back("vehicle", ship.valid() ? Value(static_cast<int64_t>(ship.value)) : Value());
        args.emplace_back("choices", Value(std::move(choices)));
        const std::optional<Value> response = ask(ctx, e, "colony_type", Value(std::move(args)), nullptr);
        const Value* answer = response ? response->find("answer") : nullptr;
        if (!answer || answer->isNull()) return std::nullopt;
        const std::vector<std::string>& types = s.empire(e).colonyTypes;
        if (answer->isString() && std::find(types.begin(), types.end(), answer->asString()) != types.end()) return answer->asString();
        fail(ctx, e, std::format("colony_type: {} is not one of the empire's colony types", script::describe(*answer, 80)));
        return std::nullopt;
    }

    std::optional<bool> enterSector(game::TurnContext& ctx, EmpireId e, std::span<const game::VehicleId> vehicles, game::Location where,
                                    std::span<const EmpireId> enemies) override {
        ValueList ids, empires;
        for (game::VehicleId v : vehicles) ids.push_back(Value(static_cast<int64_t>(v.value)));
        for (EmpireId x : enemies) empires.push_back(Value(static_cast<int64_t>(x.value)));
        ValueMap args;
        args.emplace_back("vehicles", Value(std::move(ids)));
        args.emplace_back("sector", Map(3)("system", Value(static_cast<int64_t>(where.system.value)))("x", Value(int64_t{where.sector.x}))(
                                        "y", Value(int64_t{where.sector.y}))
                                        .done());
        args.emplace_back("enemies", Value(std::move(empires)));
        return boolAnswer(ctx, e, "enter_sector", Value(std::move(args)));
    }

    std::optional<bool> decloak(game::TurnContext& ctx, EmpireId e, game::VehicleId vehicle, game::ObjectId planet, game::DecloakReason reason) override {
        ValueMap args;
        args.emplace_back("object", Value(static_cast<int64_t>(vehicle.valid() ? vehicle.value : planet.value)));
        args.emplace_back("vehicle", vehicle.valid() ? Value(static_cast<int64_t>(vehicle.value)) : Value());
        args.emplace_back("planet", planet.valid() ? Value(static_cast<int64_t>(planet.value)) : Value());
        args.emplace_back("reason", Value(reason == game::DecloakReason::Order ? "order" : "attack"));
        return boolAnswer(ctx, e, "decloak", Value(std::move(args)));
    }

    std::optional<std::vector<game::combat::TacticalOrder>> battleRound(game::TurnContext& ctx, EmpireId e, const game::BattleRound& battle) override {
        Slot& slot = slotOf(e);
        slot.battleOrders.clear();
        ValueMap args;
        args.emplace_back("battle", detail::battleValue(rules_, ctx.state, battle));
        const std::optional<Value> response = ask(ctx, e, "battle_round", Value(std::move(args)), nullptr);
        const Value* answer = response ? response->find("answer") : nullptr;
        if (!answer || answer->isNull()) return std::nullopt;
        const Value* orders = answer->find("orders");
        if (!answer->isMap() || !orders || !orders->isList()) {
            fail(ctx, e, "battle_round: the answer should be {orders: [tactical orders]} or null");
            return std::nullopt;
        }
        std::vector<game::combat::TacticalOrder> out;
        ValueList refusals;
        for (size_t i = 0; i < orders->asList().size(); ++i) {
            const Value& item = orders->asList()[i];
            auto decoded = decodeTacticalOrder(item);
            if (!decoded) {
                refusals.push_back(refusal(i, item, decoded.error().text()));
                continue;
            }
            slot.battleOrders.emplace_back(i, item);
            out.push_back(std::move(*decoded));
        }
        slot.refused = Value(std::move(refusals));
        return out;
    }

    void refused(EmpireId e, std::vector<std::pair<size_t, std::string>> refusals) override {
        Slot& slot = slotOf(e);
        ValueList list = slot.refused.isList() ? slot.refused.asList() : ValueList{};
        for (const auto& [index, why] : refusals) {
            if (index >= slot.battleOrders.size()) continue;
            const auto& [original, item] = slot.battleOrders[index];
            list.push_back(refusal(original, item, why));
        }
        slot.refused = Value(std::move(list));
    }

    void replay(std::span<const game::JournalEntry> entries) override { replay_.insert(replay_.end(), entries.begin(), entries.end()); }

    void endSession(game::TurnContext& ctx) override {
        for (size_t i = 0; i < slots_.size(); ++i) {
            const EmpireId e{i};
            if (!slots_[i].asked || !game::playedByController(ctx, e) || outForTurn(ctx.state, e)) continue;
            ask(ctx, e, "end_session", Value::emptyMap(), nullptr);
        }
    }

private:
    struct Slot {
        bool created = false;   // its first live request went out, with `player` and `memory`
        bool asked = false;     // asked in this session: it gets end_session
        Value refused = Value::emptyList();                   // for the next request's args.refused
        std::vector<std::pair<size_t, Value>> battleOrders;   // the last battle answer's orders: index in the answer, the order
        std::map<uint64_t, int64_t> seen;                     // requests per digest so far (the seed)
    };

    // The request being handled live: what the services work on.
    struct Active {
        game::TurnContext* ctx = nullptr;
        EmpireId empire;
        const game::CommandSink* sink = nullptr;
        std::optional<Perspective> perspective;
        std::optional<Queries> queries;
        Value view;               // the view as the player last saw it (the request's, or after an apply)
        ValueList applied;        // the commands its `apply` carried out
        bool external = false;
    };

    Slot& slotOf(EmpireId e) {
        if (slots_.size() <= e.index()) slots_.resize(e.index() + 1);
        return slots_[e.index()];
    }

    static Value refusal(size_t index, const Value& command, const std::string& why) {
        return Map(3)("index", Value(static_cast<int64_t>(index)))("command", command)("reason", Value(why)).done();
    }

    static bool outForTurn(const game::GameState& s, EmpireId e) {
        const game::ScriptPlayerState& st = s.empire(e).script;
        return st.failureTurn == s.turn && st.failures >= game::kPlayerFailuresPerTurn;
    }

    std::optional<bool> boolAnswer(game::TurnContext& ctx, EmpireId e, std::string_view call, Value args) {
        const std::optional<Value> response = ask(ctx, e, call, std::move(args), nullptr);
        const Value* answer = response ? response->find("answer") : nullptr;
        if (!answer || answer->isNull()) return std::nullopt;
        if (answer->isBool()) return answer->asBool();
        fail(ctx, e, std::format("{}: the answer should be true, false or null, not {}", call, script::describe(*answer, 80)));
        return std::nullopt;
    }

    // A failed request (docs/sdk/ai-protocol.md §7): logged and counted; the
    // classic answer stands. After three in a game turn the classic answers
    // stand for the rest of it.
    void fail(game::TurnContext& ctx, EmpireId e, const std::string& what, const std::string& traceback = {}) {
        game::GameState& s = ctx.state;
        game::ScriptPlayerState& st = s.empire(e).script;
        if (st.failureTurn != s.turn) {
            st.failureTurn = s.turn;
            st.failures = 0;
        }
        ++st.failures;
        log::warn("Computer player {}: {}; the built-in AI answers{}", who(s, e), what,
                  st.failures >= game::kPlayerFailuresPerTurn ? " for the rest of the turn" : " this request");
        if (!traceback.empty()) log::warn("{}", traceback);
    }

    // One request (docs/sdk/ai-protocol.md §3, §4): from the journal when an
    // answer waits there, else live. Nothing when it failed (the caller
    // falls back to the classic answer) or the player is out for the turn.
    std::optional<Value> ask(game::TurnContext& ctx, EmpireId e, std::string_view call, Value args, const game::CommandSink* sink) {
        game::GameState& s = ctx.state;
        if (outForTurn(s, e)) return std::nullopt;
        Slot& slot = slotOf(e);
        slot.asked = true;
        // The digest: the call, empire, turn and arguments, without what was
        // refused before (a battle a window showed was asked without it).
        Hasher digestOf;
        digestOf.add(call).add(e.value).add(s.turn);
        detail::hashValue(digestOf, args);
        const uint64_t digest = digestOf.value();
        if (slot.refused.isList() && !slot.refused.asList().empty()) args.set("refused", std::exchange(slot.refused, Value::emptyList()));
        const int64_t occurrence = slot.seen[digest]++;
        Hasher seedOf;
        seedOf.add(s.seed).add(s.turn).add(e.value).add(call).add(digest).add(occurrence);
        const int64_t seed = static_cast<int64_t>(seedOf.value() & 0x7fffffffffffffffull);

        Value response;
        bool replayed = false;
        if (!replay_.empty()) {
            const game::JournalEntry& next = replay_.front();
            if (next.turn == s.turn && next.empire == e && next.call == call && next.digest == digest) {
                auto parsed = script::parseJson(next.response);
                response = parsed ? std::move(*parsed) : errorValue("ValueError", "the journal's answer could not be read");
                replay_.pop_front();
                replayed = true;
                // What its `apply` carried out during the request is carried out again.
                if (const Value* applied = response.find("applied"); applied && applied->isList() && sink)
                    for (const Value& c : applied->asList())
                        if (auto decoded = decodeCommand(c)) {
                            sink->apply(*decoded);
                            ++applied_;
                        }
            } else {
                log::warn("Computer players: the journal's next answer ({} of empire {} on turn {}) is not for this request ({} of empire {}); "
                          "asking the players again",
                          next.call, next.empire.value, next.turn, call, e.value);
                replay_.clear();
            }
        }
        if (!replayed) response = live(ctx, e, call, args, seed, sink);

        // The journal keeps the response, with the memory only when it changed.
        game::Empire& emp = s.empire(e);
        std::optional<std::string> memory;
        if (const Value* m = response.find("memory")) {
            memory = jsonText(*m);
            if (*memory == (emp.script.memory.empty() ? std::string("null") : emp.script.memory)) {
                memory.reset();
                response.editMap().erase(std::find_if(response.editMap().begin(), response.editMap().end(),
                                                      [](const auto& kv) { return kv.first == "memory"; }));
            }
        }
        s.journal.entries.push_back({s.turn, e, std::string(call), digest, jsonText(response)});

        if (const Value* lines = response.find("log"); lines && lines->isList())
            for (const Value& line : lines->asList())
                if (line.isString()) log::info("Computer player {}: {}", who(s, e), line.asString());
        if (const Value* error = response.find("error"); error && error->isMap()) {
            const Value* type = error->find("type");
            const Value* message = error->find("message");
            const Value* traceback = error->find("traceback");
            fail(ctx, e,
                 std::format("{} failed: {}{}{}", call, type && type->isString() ? type->asString() : std::string("Error"),
                             message && message->isString() && !message->asString().empty() ? ": " : "",
                             message && message->isString() ? message->asString() : std::string()),
                 traceback && traceback->isString() ? traceback->asString() : std::string());
            return std::nullopt;
        }
        if (memory) emp.script.memory = *memory == "null" ? std::string() : std::move(*memory);
        if (const Value* notes = response.find("notes"); notes && notes->isList()) keepNotes(s, emp, *notes);
        return response;
    }

    // A note replaces the earlier note on the same thing; notes last for the
    // turn they were given and the next (§4).
    static void keepNotes(const game::GameState& s, game::Empire& emp, const Value& notes) {
        std::erase_if(emp.aiNotes, [&](const game::PlayerNote& n) { return n.turn + 1 < s.turn; });
        for (const Value& n : notes.asList()) {
            game::PlayerNote note;
            note.turn = s.turn;
            const Value* kind = n.find("kind");
            note.kind = kind && kind->isString() ? kind->asString() : std::string("object");
            const Value* object = n.find("object");
            note.object = object && object->isInt() ? object->asInt() : -1;
            note.text = n.find("text")->asString();
            std::erase_if(emp.aiNotes, [&](const game::PlayerNote& o) { return o.kind == note.kind && o.object == note.object; });
            if (!note.text.empty()) emp.aiNotes.push_back(std::move(note));
        }
    }

    // ---- Live requests ----------------------------------------------------------------------------

    Value live(game::TurnContext& ctx, EmpireId e, std::string_view call, const Value& args, int64_t seed, const game::CommandSink* sink) {
        const auto started = std::chrono::steady_clock::now();
        int64_t bytecodes = 0;
        Value response = liveResponse(ctx, e, call, args, seed, sink, bytecodes);
        if (setup_->observe) {
            RequestCost cost;
            cost.empire = e;
            cost.turn = ctx.state.turn;
            cost.call = std::string(call);
            cost.bytecodes = bytecodes;
            cost.time = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - started);
            cost.failed = response.find("error") != nullptr;
            setup_->observe(cost);
        }
        return response;
    }

    Value liveResponse(game::TurnContext& ctx, EmpireId e, std::string_view call, const Value& args, int64_t seed, const game::CommandSink* sink,
                       int64_t& bytecodes) {
        game::GameState& s = ctx.state;
        const game::Empire& emp = s.empire(e);
        const bool planning = sink != nullptr;
        Slot& slot = slotOf(e);

        Active active;
        active.ctx = &ctx;
        active.empire = e;
        active.sink = sink;
        active.external = emp.controller.kind == game::Controller::Kind::External;

        ValueMap request;
        request.emplace_back("api", Value(kApiVersion));
        request.emplace_back("call", Value(call));
        request.emplace_back("empire", Value(static_cast<int64_t>(e.value)));
        request.emplace_back("turn", Value(static_cast<int64_t>(s.turn)));
        request.emplace_back("seed", Value(seed));
        if (planning) {
            active.perspective.emplace(rules_, s, e, ViewOptions{s.options.aiSeesEverything});
            active.view = buildView(*active.perspective);
            request.emplace_back("view", active.view);
        } else {
            request.emplace_back("view", Value());
        }
        std::string problem;
        if (!slot.created) {
            Value player = playerSpec(emp, problem);
            if (!problem.empty()) return errorValue("LookupError", problem);
            request.emplace_back("player", std::move(player));
            auto memory = emp.script.memory.empty() ? std::expected<Value, script::JsonError>(Value()) : script::parseJson(emp.script.memory);
            request.emplace_back("memory", memory ? std::move(*memory) : Value());
        }
        request.emplace_back("args", args);

        const int64_t budget = planning ? s.options.aiPlanningBudget : s.options.aiCallBudget;
        bool delivered = false;
        Value response;
        {
            // The services answer for this request while it is handled.
            struct Handling {
                Active*& slot;
                Handling(Active*& at, Active& a) : slot(at) { slot = &a; }
                ~Handling() { slot = nullptr; }
                Handling(const Handling&) = delete;
                Handling& operator=(const Handling&) = delete;
            } handling(active_, active);
            response = active.external ? callBot(emp, Value(std::move(request)), delivered) : callScript(Value(std::move(request)), budget, delivered);
            if (!active.external && delivered && interp_) bytecodes = interp_->lastCallBudget();
        }
        // `player` and `memory` go with the first request the player gets in the session.
        if (delivered) slot.created = true;

        if (!response.find("error")) {
            if (std::string why = detail::responseProblem(response, planning); !why.empty()) response = errorValue("ValueError", why);
        }
        if (const Value* m = response.find("memory"); m && !response.find("error")) {
            const std::string text = jsonText(*m);
            if (static_cast<int64_t>(text.size()) > s.options.aiMemoryLimit)
                response = errorValue("MemoryError", std::format("the memory takes {} bytes as JSON; the game allows {}", text.size(),
                                                                 s.options.aiMemoryLimit));
        }
        if (!active.applied.empty()) response.set("applied", Value(std::move(active.applied)));
        return response;
    }

    // The player to make at the session's first request: {mod, name, module, class}, or {slot}.
    Value playerSpec(const game::Empire& emp, std::string& problem) {
        const game::Controller& c = emp.controller;
        if (c.kind == game::Controller::Kind::External) return Map(1)("slot", Value(static_cast<int64_t>(c.slot))).done();
        const mods::Package* mod = findMod(c.mod);
        if (!mod) {
            problem = std::format("the mod {} is not here", c.mod);
            return Value();
        }
        const mods::AiPlayer* p = mod->manifest.aiPlayer(c.player);
        if (!p) {
            problem = std::format("the mod {} declares no computer player named '{}'", c.mod, c.player);
            return Value();
        }
        return Map(4)("mod", Value(c.mod))("name", Value(p->name))("module", Value(p->module))("class", Value(p->className)).done();
    }

    const mods::Package* findMod(std::string_view id) const {
        for (const mods::Package& p : setup_->mods)
            if (p.id() == id) return &p;
        if (const auto* data = dynamic_cast<const mods::GameData*>(rules_.files()))
            for (const mods::Package& p : data->mods().packages)
                if (p.id() == id) return &p;
        return nullptr;
    }

    Value callScript(Value request, int64_t budget, bool& delivered) {
        if (!startInterpreter()) return errorValue("RuntimeError", startError_);
        delivered = true;
        script::Result<Value> r = std::unexpected(script::Error{});
        const std::vector<Value> args{std::move(request)};
        worker_->run([&] { r = interp_->call("opense4._engine", "dispatch", args, script::CallOptions{budget}); });
        if (const std::string& out = interp_->output(); out.size() > outputSeen_) {
            for (std::string_view rest = std::string_view(out).substr(outputSeen_); !rest.empty();) {
                const size_t nl = rest.find('\n');
                log::info("Computer player output: {}", rest.substr(0, nl));
                rest = nl == std::string_view::npos ? std::string_view{} : rest.substr(nl + 1);
            }
            outputSeen_ = out.size();
        }
        if (!r) return errorValue(r.error().type.empty() ? std::string(script::errorKindName(r.error().kind)) : r.error().type, r.error().message,
                                  r.error().traceback);
        return std::move(*r);
    }

    Value callBot(const game::Empire& emp, const Value& request, bool& delivered) {
        ExternalBot* bot = setup_->externals ? setup_->externals(emp.controller.slot) : nullptr;
        if (!bot) return errorValue("ConnectionError", std::format("no bot is connected to external slot {}", emp.controller.slot));
        delivered = true;
        const ServiceCall services = [this](std::string_view name, const Value& arg) -> std::expected<Value, std::string> {
            try {
                return service(name, arg);
            } catch (const script::NativeError& e) {
                return std::unexpected(std::format("{}: {}", e.type(), e.what()));
            } catch (const std::exception& e) {
                return std::unexpected(std::string(e.what()));
            }
        };
        auto r = bot->request(request, services);
        if (!r) return errorValue("ConnectionError", r.error());
        return std::move(*r);
    }

    // ---- The interpreter (on the players' thread) ----------------------------------------------------------

    bool startInterpreter() {
        if (interp_) return true;
        if (!startError_.empty()) return false;
        try {
            if (!worker_) worker_ = std::make_unique<Worker>(kPlayerStackBytes);
        } catch (const std::exception& e) {
            startError_ = e.what();
            return false;
        }
        slotLock_ = std::unique_lock(interpreterSlot(), std::defer_lock);
        if (!slotLock_.try_lock_for(std::chrono::seconds(60))) {
            startError_ = "the script runtime is busy with another game";
            log::warn("Computer players: {}", startError_);
            return false;
        }
        worker_->run([&] { startError_ = makeInterpreter(); });
        if (!startError_.empty()) {
            log::warn("Computer players: {}", startError_);
            worker_->run([&] { interp_.reset(); });
            slotLock_.unlock();
        }
        return interp_ != nullptr;
    }

    std::string makeInterpreter() {
        script::Limits limits;
        limits.heapBytes = setup_->heapBytes;
        limits.budget = kInterpreterBudget;
        limits.cStackBytes = kCStackBytes;
        // Another part of the program may hold the interpreter for a moment.
        const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(60);
        for (;;) {
            auto made = script::Interpreter::create(limits);
            if (made) {
                interp_ = std::move(*made);
                break;
            }
            if (made.error().kind != script::ErrorKind::Usage || !script::Interpreter::active() || std::chrono::steady_clock::now() > until)
                return "the script runtime could not start: " + made.error().describe();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        // The opense4 package, then each used mod's ai/ files at the root.
        std::vector<std::string> added;
        auto add = [&](const std::string& path, std::string text, std::string_view from) {
            if (std::find(added.begin(), added.end(), path) != added.end()) {
                log::warn("Computer players: {} has {}, which another mod's computer player already has: left out", from, path);
                return;
            }
            if (auto r = interp_->addFile(path, std::move(text)); !r) {
                log::warn("Computer players: {}: {}: {}", from, path, r.error().describe());
                return;
            }
            added.push_back(path);
        };
        if (!setup_->package.empty())
            for (const auto& [path, text] : setup_->package) add(path, text, "the opense4 package");
        else
            for (const script::LibraryFile& f : packageFiles()) add(std::string(f.path), std::string(f.text), "the opense4 package");
        for (const std::string& id : modsUsed_) {
            const mods::Package* mod = findMod(id);
            if (!mod) continue;
            for (const mods::PackageFile& f : mod->files) {
                if (!f.path.starts_with("ai/") || !f.path.ends_with(".py")) continue;
                add(f.path.substr(3), readText(f.real), std::format("the mod {}", id));
            }
        }
        // The engine's services (§6), each with one argument: a map.
        for (const char* name : {"query", "rules", "builtin", "builtin_answer", "apply"}) {
            const std::string serviceName(name);
            auto r = interp_->addNativeFunction("_opense4", serviceName, [this, serviceName](std::span<const Value> args) -> Value {
                if (args.size() > 1) throw script::NativeError("TypeError", std::format("_opense4.{} takes one argument", serviceName));
                return service(serviceName, args.empty() ? Value::emptyMap() : args[0]);
            });
            if (!r) return "the engine's services could not be added: " + r.error().describe();
        }
        return {};
    }

    // ---- Services (docs/sdk/ai-protocol.md §6) ---------------------------------------------------------------

    void charge(int64_t units) {
        if (interp_ && active_ && !active_->external) interp_->charge(units);
    }

    static const Value& field(const Value& arg, std::string_view key, std::string_view service) {
        static const Value none;
        if (!arg.isMap()) throw script::NativeError("TypeError", std::format("_opense4.{} takes a map", service));
        const Value* v = arg.find(key);
        return v ? *v : none;
    }

    Value service(std::string_view name, const Value& arg) {
        if (!active_) throw script::NativeError("RuntimeError", "the engine answers services only while a request is handled");
        Active& a = *active_;
        const game::Rules& r = rules_;
        game::GameState& s = a.ctx->state;
        if (name == "query") {
            const Value& q = field(arg, "name", name);
            if (!q.isString()) throw script::NativeError("TypeError", "query: 'name' should be the query's name");
            if (!a.perspective) a.perspective.emplace(r, s, a.empire, ViewOptions{s.options.aiSeesEverything});
            if (!a.queries) a.queries.emplace(*a.perspective);
            const Value& qargs = field(arg, "args", name);
            auto result = a.queries->call(q.asString(), qargs.isNull() ? Value::emptyMap() : qargs);
            if (!result) throw script::NativeError("ValueError", result.error().text());
            charge(kQueryCost + nodeCost(*result, kNodeCost));
            return std::move(*result);
        }
        if (name == "rules") {
            if (rulesView_.isNull()) rulesView_ = buildRulesView(r);
            charge(nodeCost(rulesView_, kViewNodeCost));
            return rulesView_;
        }
        if (name == "builtin") return builtin(a, arg);
        if (name == "builtin_answer") return builtinAnswer(a, arg);
        if (name == "apply") return applyNow(a, arg);
        throw script::NativeError("ValueError", std::format("'{}' is not a service", name));
    }

    // The classic planners for the empire now, some ministers only.
    Value builtin(Active& a, const Value& arg) {
        const game::GameState& s = a.ctx->state;
        const Value& call = field(arg, "call", "builtin");
        uint32_t mask = game::kAllMinisters;
        auto ministers = [&](std::string_view key) -> std::optional<uint32_t> {
            const Value& list = field(arg, key, "builtin");
            if (list.isNull()) return std::nullopt;
            if (!list.isList()) throw script::NativeError("TypeError", std::format("builtin: '{}' should be a list of minister names", key));
            uint32_t bits = 0;
            for (const Value& m : list.asList()) {
                const auto minister = m.isString() ? parseEnum<game::Minister>(m.asString()) : std::nullopt;
                if (!minister) throw script::NativeError("ValueError", std::format("builtin: {} is not a minister", script::describe(m, 60)));
                bits |= game::ministerBit(*minister);
            }
            return bits;
        };
        if (auto only = ministers("ministers")) mask = *only;
        if (auto skip = ministers("skip")) mask &= ~*skip;
        std::vector<game::Command> commands;
        const std::string what = call.isString() ? call.asString() : std::string();
        if (what == "politics") {
            commands = game::ai::planPoliticsOrders(rules_, s, a.empire, mask);
        } else if (what == "orders") {
            const std::vector<game::SystemId> territory = s.empire(a.empire).claimedSystems;
            commands = game::ai::planOrdersAfterPolitics(rules_, s, a.empire, &territory, nullptr, nullptr, mask);
        } else if (what == "economy") {
            commands = game::ai::planEconomyStep(rules_, s, a.empire, 0, nullptr, nullptr, mask);
        } else {
            throw script::NativeError("ValueError", "builtin: 'call' should be \"politics\", \"orders\" or \"economy\"");
        }
        Value out = encodeCommands(commands);
        charge(kBuiltinCost + nodeCost(out, kNodeCost));
        return out;
    }

    // The classic answer to a mid-turn call.
    Value builtinAnswer(Active& a, const Value& arg) {
        const game::GameState& s = a.ctx->state;
        const Value& call = field(arg, "call", "builtin_answer");
        const Value& args = field(arg, "args", "builtin_answer");
        charge(kAnswerCost);
        const std::string what = call.isString() ? call.asString() : std::string();
        if (what == "colony_type") {
            const Value* planet = args.find("planet");
            const Value* id = planet && planet->isMap() ? planet->find("id") : planet;
            if (!id || !id->isInt() || id->asInt() < 0 || static_cast<uint64_t>(id->asInt()) >= s.galaxy.objects.size())
                throw script::NativeError("ValueError", "builtin_answer: colony_type needs args.planet, the planet's id or record");
            return Value(game::ai::colonyTypeAtColonization(rules_, s, a.empire, game::ObjectId{static_cast<uint32_t>(id->asInt())}));
        }
        // A computer player's groups always enter, and its Ship Cloaking minister decloaks.
        if (what == "enter_sector" || what == "decloak") return Value(true);
        if (what == "battle_round") return Value();   // the strategies
        throw script::NativeError("ValueError", "builtin_answer: 'call' should be colony_type, enter_sector, decloak or battle_round");
    }

    // One command now, as the planning call's own commands are carried out.
    Value applyNow(Active& a, const Value& arg) {
        if (!a.sink) throw script::NativeError("RuntimeError", "apply: commands are given only in the planning calls (politics, orders, economy)");
        const Value& command = field(arg, "command", "apply");
        auto decoded = decodeCommand(command);
        if (!decoded) throw script::NativeError("ValueError", decoded.error().text());
        const game::CommandResult res = a.sink->apply(*decoded);
        a.applied.push_back(command);
        ++applied_;
        game::GameState& s = a.ctx->state;
        a.perspective.emplace(rules_, s, a.empire, ViewOptions{s.options.aiSeesEverything});
        a.queries.reset();
        Value now = buildView(*a.perspective);
        Value changes = detail::viewChanges(a.view, now);
        charge(kApplyCost + nodeCost(now, kViewNodeCost) + nodeCost(changes, kNodeCost));
        a.view = std::move(now);
        return Map(4)("ok", Value(res.ok))("reason", Value(res.error))("changed", *changes.find("changed"))("removed", *changes.find("removed"))
            .done();
    }

    const game::Rules& rules_;
    std::shared_ptr<const PlayerSetup> setup_;
    std::vector<std::string> modsUsed_;
    std::vector<Slot> slots_;
    std::deque<game::JournalEntry> replay_;
    Active* active_ = nullptr;
    size_t applied_ = 0;             // commands `apply` carried out during the current planning call
    Value rulesView_;
    std::unique_ptr<Worker> worker_;
    std::unique_ptr<script::Interpreter> interp_;
    std::unique_lock<std::timed_mutex> slotLock_;
    std::string startError_;
    size_t outputSeen_ = 0;
};

std::shared_ptr<const PlayerSetup>& installed() {
    static std::shared_ptr<const PlayerSetup> setup;
    return setup;
}

} // namespace

std::unique_ptr<game::Players> makeSession(const game::Rules& r, game::GameState& s, std::shared_ptr<const PlayerSetup> setup) {
    if (!setup) setup = std::make_shared<const PlayerSetup>();
    return std::make_unique<Session>(r, s, std::move(setup));
}

void installPlayers(PlayerSetup setup) {
    auto shared = std::make_shared<const PlayerSetup>(std::move(setup));
    installed() = shared;
    game::setPlayersFactory([shared](const game::Rules& r, game::GameState& s) { return makeSession(r, s, shared); });
}

void uninstallPlayers() {
    installed().reset();
    game::setPlayersFactory({});
}

} // namespace opense4::sdk
