#include "client/classic/computer_players.hpp"

#include "client/classic/net_transport.hpp"
#include "client/classic/session.hpp"
#include "game/players.hpp"

#include <algorithm>
#include <format>
#include <mutex>
#include <utility>

namespace opense4::client::classic {

// ---- Notes ---------------------------------------------------------------------------------------

const game::GameState* wholeGame(const ClassicSession& session) {
    if (session.kind() == SessionKind::Local || session.kind() == SessionKind::Hotseat) return &session.state();
    if (session.kind() == SessionKind::NetworkClient)
        if (const auto* host = dynamic_cast<const HostTransport*>(session.transport())) return host->host().state();
    return nullptr;
}

namespace {

// Whom a note names, and where that is.
void resolve(const game::GameState& s, ShownNote& n) {
    if (n.object < 0 || n.object > int64_t{UINT32_MAX - 1}) return;
    const auto id = static_cast<uint32_t>(n.object);
    if (n.kind == "vehicle") {
        if (const game::Vehicle* v = s.vehicle(game::VehicleId{id})) {
            n.subject = v->name;
            n.system = v->location.system;
            if (v->location.system.valid()) n.sector = v->location.sector;
        }
    } else if (n.kind == "fleet") {
        if (const game::Fleet* f = s.fleet(game::FleetId{id})) {
            n.subject = f->name;
            n.system = f->location.system;
            if (f->location.system.valid()) n.sector = f->location.sector;
        }
    } else if (n.kind == "object") {
        if (id < s.galaxy.objects.size()) {
            const game::SpaceObject& o = s.galaxy.objects[id];
            n.subject = o.name;
            n.system = o.system;
            n.sector = o.sector;
        }
    } else if (n.kind == "system") {
        if (id < s.galaxy.systems.size()) {
            n.subject = s.galaxy.systems[id].name;
            n.system = game::SystemId{id};
        }
    } else if (n.kind == "empire") {
        if (id < s.empires.size()) n.subject = s.empires[id].name;
    } else if (n.kind == "design") {
        if (id < s.designs.size()) n.subject = s.designs[id].name;
    } else if (n.kind == "message") {
        n.subject = std::format("message {}", id);
    }
    if (n.subject.empty()) n.subject = std::format("{} {}", n.kind, id);   // gone, or not known
}

// "Captain" for a mod's player, "external 2" for a bot.
std::string playerShort(const game::Controller& c) {
    if (c.kind == game::Controller::Kind::Script) return c.player;
    if (c.kind == game::Controller::Kind::External) return std::format("external {}", c.slot);
    return "classic AI";
}

} // namespace

std::vector<ShownNote> computerPlayerNotes(const game::GameState& s) {
    std::vector<ShownNote> out;
    for (const game::Empire& e : s.empires) {
        if (e.aiNotes.empty()) continue;
        const std::string author = std::format("{} ({})", e.name, playerShort(e.controller));
        for (const game::PlayerNote& note : e.aiNotes) {
            ShownNote n;
            n.empire = e.id;
            n.author = author;
            n.kind = note.kind.empty() ? std::string("object") : note.kind;
            n.object = note.object;
            n.turn = note.turn;
            n.text = note.text;
            resolve(s, n);
            out.push_back(std::move(n));
        }
    }
    return out;
}

std::vector<ShownNote> notesAbout(std::span<const ShownNote> notes, std::string_view kind, int64_t id) {
    std::vector<ShownNote> out;
    for (const ShownNote& n : notes)
        if (n.kind == kind && n.object == id) out.push_back(n);
    return out;
}

std::string noteLine(const ShownNote& n) { return n.subject.empty() ? n.text : n.subject + ": " + n.text; }

// ---- Failures ------------------------------------------------------------------------------------

namespace {

struct Failures {
    std::mutex mutex;
    std::vector<sdk::PlayerFailure> list;
};
Failures& failures() {
    static Failures f;
    return f;
}

std::string_view callWords(std::string_view call) {
    if (call == "politics") return "its diplomacy";
    if (call == "orders") return "its orders";
    if (call == "economy") return "its economy";
    if (call == "colony_type") return "a new colony's type";
    if (call == "enter_sector") return "entering a sector with enemies";
    if (call == "decloak") return "a decloak question";
    if (call == "battle_round") return "a combat turn";
    if (call == "end_session") return "the end of its session";
    return call;
}

} // namespace

void notePlayerFailure(const sdk::PlayerFailure& f) {
    Failures& all = failures();
    const std::scoped_lock lock(all.mutex);
    all.list.push_back(f);
}

std::vector<sdk::PlayerFailure> playerFailures() {
    Failures& all = failures();
    const std::scoped_lock lock(all.mutex);
    return all.list;
}

size_t playerFailureCount() {
    Failures& all = failures();
    const std::scoped_lock lock(all.mutex);
    return all.list.size();
}

void forgetPlayerFailures() {
    Failures& all = failures();
    const std::scoped_lock lock(all.mutex);
    all.list.clear();
}

std::string failureNotice(const sdk::PlayerFailure& f) {
    std::string_view error = f.error;
    while (error.ends_with('.')) error.remove_suffix(1);
    std::string out = std::format("{}, playing {}, failed in {}: {}.", f.player, f.empireName, callWords(f.call), error);
    out += f.outForTurn ? " The classic AI plays it for the rest of the turn." : " The classic AI answered.";
    return out;
}

} // namespace opense4::client::classic
