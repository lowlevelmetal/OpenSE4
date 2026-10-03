// opense4-server: the dedicated host for network games, the host side of
// play-by-e-mail games, and a small test client. See docs/MULTIPLAYER.md.

#include "game/diplomacy.hpp"
#include "game/rules.hpp"
#include "game/serialize.hpp"
#include "game/setup.hpp"
#include "game/turn.hpp"
#include "net/auth.hpp"
#include "net/client.hpp"
#include "net/host.hpp"
#include "net/pbem.hpp"
#include "net/secure.hpp"
#include "ruleset/ruleset.hpp"
#include "server/setup_file.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <charconv>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <format>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

using namespace opense4;

namespace {

constexpr std::string_view kUsage = R"(opense4-server: host OpenSE4 network and play-by-e-mail games.

Usage:
  opense4-server [options]                      host a network game (lobby, turns, autosave)
  opense4-server pbem new --setup=FILE.toml --out=GAME.gam [--turn-files=DIR]
  opense4-server pbem process --game=GAME.gam --orders=DIR [--password=PW] [--keep-orders]
                              [--reset-passwords=N,M]  (new passwords, shown here only)
                              [--turn-files=DIR]
  opense4-server pbem turn-files --game=GAME.gam [--out=DIR]
  opense4-server pbem orders --turn=FILE.turn [--password=PW] [--out=DIR]
  opense4-server pbem info --game=GAME.gam|FILE.turn
  opense4-server bot --name=NAME [--connect=HOST[:PORT]] [--turns=N]
  opense4-server hash-password PASSWORD

Network game options:
  --data=DIR             The classic game's Data directory (default: auto-detect)
  --port=N               TCP port (default 6720; 0 = any free port)
  --bind=ADDRESS         Listen on one address only (default: all IPv4 interfaces)
  --upnp / --no-upnp     Forward the port on the router with UPnP (default: on)
  --no-lan-discovery     Do not answer LAN game searches (UDP port 6716)
  --players=N            Human player slots (default 2)
  --ai=N                 Computer empires (default 0)
  --seed=N               Galaxy seed (default: random)
  --systems=N            Exactly N star systems (default 0: rolled from the quadrant size)
  --quadrant-size=N      Quadrant size 0 small, 1 medium (default), 2 large
  --quadrant=NAME        Quadrant type from the data set (default: the first)
  --setup=FILE.toml      Game name, seed, options and computer empires from a setup file
  --turn-based           Players take their turns one after another, and their
                         commands are carried out at once (default: simultaneous)
  --name=NAME            Game name (default "OpenSE4 game")
  --password=PW          Master password: players who give it may administer the game
                         (start, kick, add computer empires, force a turn, hand
                         empires to the computer, reset passwords)
  --join-password=PW     Password every player needs to join
  --host-key=FILE        The host's long-term key, which players' games remember
                         (default: host_key.txt in OpenSE4's user folder; made on
                         first use)
  --turn-timeout=SEC     Process the turn after SEC seconds even if orders are missing
                         (turn-based: end a player's turn after SEC seconds)
  --load=GAME.gam        Continue a saved game (players reconnect with name and password)
  --save-dir=DIR         Where autosaves go (default: the current directory, or the
                         loaded game's file)
  --autosave=N           Save every N turns (default 1; 0 = only when stopping)
  --max-turns=N          Stop once the game reaches turn N (for testing)
  --verbose              Also log lobby changes after the game started

The game starts when every player slot is taken and every player is ready.
Players whose orders are missing when the turn is processed are played by the
computer for that turn. In a turn-based game the server waits for the player
whose turn it is; when the time limit runs out the computer plays the rest of
that turn. Stop the server with Ctrl+C; it saves first.

pbem: the host keeps GAME.gam, the whole game, and never sends it. "new" and
"process" write a turn file per player who plays next (<game>_<NN>.turn, next
to GAME.gam or in --turn-files): the game as that player's empire knows it.
Send each player their own. Players send back one .plr file per turn, signed
with their password. "process" reads every .plr in DIR, checks game, turn,
empire, the turn file it was made from and the password, processes the turn,
rewrites GAME.gam (the previous turn is kept as GAME.gam.bak), writes the new
turn files and deletes the .plr files it used (--keep-orders keeps them). In a
turn-based game (setup file: simultaneous = false) "process" plays one
player's turn from that player's .plr and names the player whose turn file to
send next. "turn-files" writes the current turn files again (a game of OpenSE4
0.6, or a lost file). "orders" writes an empty .plr from a turn file.

bot: a scripted player for tests. It joins, readies up, submits orders for
--turns turns (default 2) and exits 0 once the turn has advanced that often.
In a turn-based game it plays one command in each of its turns and ends them.
Options: --password, --join-password, --master-password (then also --start to
start the game), --race=PRESET, --data=DIR, --timeout=SEC (default 120),
--host-key=HEX (the host's public key: refuse any other host).
)";

std::atomic<bool> gStop{false};

extern "C" void onSignal(int) { gStop = true; }

void say(std::string_view line) {
    const auto now = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
    std::printf("%s %.*s\n", std::format("{:%Y-%m-%d %H:%M:%S}Z", now).c_str(), static_cast<int>(line.size()), line.data());
    std::fflush(stdout);
}

int usage() {
    std::printf("%.*s", static_cast<int>(kUsage.size()), kUsage.data());
    return 0;
}

const char* kindName(game::PlayerKind k) {
    switch (k) {
        case game::PlayerKind::Human: return "human";
        case game::PlayerKind::Computer: return "computer";
        case game::PlayerKind::Neutral: return "neutral";
    }
    return "computer";
}

int fail(std::string_view message, int code = 1) {
    std::fprintf(stderr, "opense4-server: %.*s\n", static_cast<int>(message.size()), message.data());
    return code;
}

// ---- Command line ------------------------------------------------------------------------------

struct Options {
    std::map<std::string, std::string, std::less<>> values;
    std::vector<std::string> positional;

    bool has(std::string_view key) const { return values.contains(key); }
    std::string get(std::string_view key, std::string fallback = {}) const {
        auto it = values.find(key);
        return it == values.end() ? fallback : it->second;
    }
    // An integer option within [min, max]; the fallback when absent.
    std::expected<int64_t, std::string> integer(std::string_view key, int64_t fallback, int64_t min, int64_t max) const {
        auto it = values.find(key);
        if (it == values.end()) return fallback;
        int64_t v = 0;
        const std::string& s = it->second;
        auto [end, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
        if (ec != std::errc{} || end != s.data() + s.size() || v < min || v > max)
            return std::unexpected(std::format("--{} must be a number from {} to {}", key, min, max));
        return v;
    }
};

std::expected<Options, std::string> parseArgs(std::span<char*> args, std::initializer_list<std::string_view> valued,
                                              std::initializer_list<std::string_view> flags) {
    Options o;
    for (size_t i = 0; i < args.size(); ++i) {
        const std::string_view arg = args[i];
        if (!arg.starts_with("--")) {
            o.positional.emplace_back(arg);
            continue;
        }
        const size_t eq = arg.find('=');
        const std::string key(arg.substr(2, eq == std::string_view::npos ? std::string_view::npos : eq - 2));
        const bool isValued = std::find(valued.begin(), valued.end(), key) != valued.end();
        const bool isFlag = std::find(flags.begin(), flags.end(), key) != flags.end();
        if (!isValued && !isFlag) return std::unexpected(std::format("unknown option --{} (see --help)", key));
        if (isFlag) {
            if (eq != std::string_view::npos) return std::unexpected(std::format("--{} takes no value", key));
            o.values[key] = "1";
        } else if (eq != std::string_view::npos) {
            o.values[key] = std::string(arg.substr(eq + 1));
        } else if (i + 1 < args.size()) {
            o.values[key] = args[++i];
        } else {
            return std::unexpected(std::format("--{} needs a value", key));
        }
    }
    return o;
}

// ---- Shared helpers -----------------------------------------------------------------------------

std::expected<std::unique_ptr<game::Rules>, std::string> loadRules(const std::string& dataArg) {
    const auto dir = ruleset::findInstalledDataDir(dataArg);
    if (!dir)
        return std::unexpected(dataArg.empty() ? std::string("No installed data set found; pass --data=<the game's Data directory>.")
                                               : std::format("No data set at {}.", dataArg));
    auto loaded = ruleset::loadRuleset(*dir);
    if (!loaded.ruleset) {
        std::string why = std::format("Could not load the data set at {}", dir->string());
        for (const auto& e : loaded.diagnostics.errors) why += "\n  " + e;
        return std::unexpected(why);
    }
    return std::make_unique<game::Rules>(std::move(*loaded.ruleset), dir->parent_path());
}

std::string fileSafe(std::string_view name) {
    std::string out;
    for (char c : name) {
        const auto u = static_cast<unsigned char>(c);
        out.push_back(std::isalnum(u) || c == '-' || c == '_' ? c : '_');
    }
    return out.empty() ? std::string("game") : out;
}

std::string lobbySummary(const net::LobbyInfo& l) {
    std::string s = std::format("Lobby '{}':", l.gameName);
    for (const net::LobbySlot& slot : l.slots) {
        if (slot.kind == net::SlotKind::Computer) s += std::format(" [computer{}{}]", slot.setup.preset.empty() ? "" : " ", slot.setup.preset);
        else if (slot.open()) s += " [open]";
        else s += std::format(" [{}{}{}]", slot.player, slot.ready ? ", ready" : "", slot.connected ? "" : ", away");
    }
    return s;
}

std::string turnSummary(const net::TurnStatus& t) {
    if (t.turnBased) {
        const net::EmpireTurnStatus* a = t.activeStatus();
        std::string s = !a ? std::format("Turn {}: waiting (every player is played by the computer)", t.turn)
                           : std::format("Turn {}: {}'s turn{}", t.turn, a->player.empty() ? a->empireName : a->player, a->connected ? "" : " (away)");
        if (t.secondsLeft >= 0) s += std::format("; {} s left", t.secondsLeft);
        return s;
    }
    std::string waiting;
    int in = 0;
    int humans = 0;
    for (const auto& e : t.empires) {
        if (!e.human || !e.alive || e.aiControl) continue;
        ++humans;
        if (e.submitted) ++in;
        else waiting += std::format("{}{}{}", waiting.empty() ? "" : ", ", e.player.empty() ? e.empireName : e.player, e.connected ? "" : " (away)");
    }
    std::string s = std::format("Turn {}: orders from {} of {} players", t.turn, in, humans);
    if (!waiting.empty()) s += "; waiting for " + waiting;
    if (t.secondsLeft >= 0) s += std::format("; {} s left", t.secondsLeft);
    return s;
}

// ---- The network server ----------------------------------------------------------------------------

int runServer(std::span<char*> args) {
    auto parsed = parseArgs(args,
                            {"data", "port", "bind", "players", "ai", "seed", "systems", "quadrant-size", "quadrant", "setup", "name", "password",
                             "join-password", "turn-timeout", "load", "save-dir", "autosave", "max-turns", "host-key"},
                            {"upnp", "no-upnp", "no-lan-discovery", "turn-based", "verbose", "help", "version"});
    if (!parsed) return fail(parsed.error(), 2);
    const Options& o = *parsed;
    if (o.has("help")) return usage();
    if (o.has("version")) {
        std::printf("%.*s (network protocol %u, save format %u)\n", static_cast<int>(net::appVersion().size()), net::appVersion().data(),
                    net::kProtocolVersion, game::kSaveVersion);
        return 0;
    }
    if (!o.positional.empty()) return fail(std::format("unexpected argument '{}' (see --help)", o.positional.front()), 2);

    auto port = o.integer("port", net::kDefaultPort, 0, 65535);
    auto players = o.integer("players", 2, 1, 32);
    auto ai = o.integer("ai", 0, 0, 31);
    auto systems = o.integer("systems", 0, 0, 500);
    auto quadrantSize = o.integer("quadrant-size", 1, 0, 2);
    auto timeout = o.integer("turn-timeout", 0, 0, 7 * 24 * 3600);
    auto autosave = o.integer("autosave", 1, 0, 100000);
    auto maxTurns = o.integer("max-turns", 0, 0, 1000000);
    auto seed = o.integer("seed", 0, 0, std::numeric_limits<int64_t>::max());
    for (const auto* v : {&port, &players, &ai, &systems, &quadrantSize, &timeout, &autosave, &maxTurns, &seed})
        if (!*v) return fail(v->error(), 2);

    auto rules = loadRules(o.get("data"));
    if (!rules) return fail(rules.error(), 2);
    say(std::format("{}; data set {}", net::appVersion(), game::dataSetIdentity(**rules)));

    net::HostConfig cfg;
    cfg.gameName = o.get("name", "OpenSE4 game");
    cfg.bindAddress = o.get("bind");
    cfg.port = static_cast<uint16_t>(*port);
    cfg.humanSlots = static_cast<int>(*players);
    cfg.autoStart = true;
    cfg.turnTimeoutSeconds = static_cast<int>(*timeout);
    cfg.masterPasswordHash = net::hashPassword(o.get("password"));
    cfg.joinPasswordHash = net::hashPassword(o.get("join-password"));
    cfg.upnp.enabled = !o.has("no-upnp");
    cfg.lanDiscovery = !o.has("no-lan-discovery");
    // The host's identity: players' games remember its key and refuse a host
    // that shows another (docs/MULTIPLAYER.md, "Security").
    const std::filesystem::path keyFile = o.has("host-key") ? std::filesystem::path(o.get("host-key"))
                                                            : net::secure::userDataDir() / net::secure::kHostKeyFileName;
    auto hostKey = net::secure::loadOrCreateHostKey(keyFile);
    if (!hostKey) return fail(hostKey.error(), 2);
    cfg.hostKey = *hostKey;
    say(std::format("Host key {} ({}); public key {}", net::crypto::fingerprint(hostKey->publicKey), keyFile.string(),
                    net::crypto::hex(hostKey->publicKey)));
    cfg.setup.seed = o.has("seed") ? static_cast<uint64_t>(*seed) : net::randomId();
    cfg.setup.options.systemCount = static_cast<int>(*systems);
    cfg.setup.options.quadrantSize = static_cast<int>(*quadrantSize);
    cfg.setup.options.quadrantType = o.get("quadrant");
    cfg.setup.options.simultaneous = true;  // a network host runs simultaneous turns unless --turn-based (docs/MULTIPLAYER.md)
    std::vector<game::EmpireSetup> computers(static_cast<size_t>(*ai));

    if (o.has("setup")) {
        auto setup = server::loadSetupFile(o.get("setup"), **rules);
        if (!setup) return fail(setup.error(), 2);
        if (!o.has("name")) cfg.gameName = setup->gameName;
        if (setup->seed && !o.has("seed")) cfg.setup.seed = *setup->seed;
        const int count = o.has("systems") ? cfg.setup.options.systemCount : setup->options.systemCount;
        const int size = o.has("quadrant-size") ? cfg.setup.options.quadrantSize : setup->options.quadrantSize;
        const std::string quadrant = o.has("quadrant") ? cfg.setup.options.quadrantType : setup->options.quadrantType;
        cfg.setup.options = setup->options;
        cfg.setup.options.systemCount = count;
        cfg.setup.options.quadrantSize = size;
        cfg.setup.options.quadrantType = quadrant;
        if (!setup->masterPasswordHash.empty() && !o.has("password")) cfg.masterPasswordHash = setup->masterPasswordHash;
        for (const auto& e : setup->empires) {
            if (e.setup.kind == game::PlayerKind::Human) say("Note: human empires in the setup file are ignored; players join the lobby.");
            else computers.push_back(e.setup);
        }
    }

    if (o.has("turn-based")) cfg.setup.options.simultaneous = false;

    if (const std::string& q = cfg.setup.options.quadrantType; !q.empty()) {
        const auto& types = (*rules)->data().quadrantTypes;
        const bool known = std::any_of(types.begin(), types.end(), [&](const auto& t) {
            return std::equal(t.name.begin(), t.name.end(), q.begin(), q.end(),
                              [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); });
        });
        if (!known) {
            std::string names;
            for (const auto& t : types) names += (names.empty() ? "" : ", ") + t.name;
            return fail(std::format("the data set has no quadrant type '{}' (it has: {})", q, names), 2);
        }
    }

    std::filesystem::path saveFile = std::filesystem::path(o.get("save-dir", ".")) / (fileSafe(cfg.gameName) + ".gam");
    net::HostSession host(**rules, cfg);
    if (o.has("load")) {
        auto game = game::loadGame(o.get("load"));
        if (!game) return fail(game.error(), 2);
        // Reading a game file recalculates every colony's cloak and sensor
        // levels, a colony that can no longer cloak decloaking as by Decloak
        // (spec 01 §6.9, §14 Q44).
        game::diplomacy::recalculateColonies(**rules, game->first);
        if (!o.has("save-dir")) saveFile = o.get("load");
        else if (!game->second.gameName.empty()) saveFile = std::filesystem::path(o.get("save-dir")) / (fileSafe(game->second.gameName) + ".gam");
        if (auto r = host.resume(std::move(game->first), game->second); !r) return fail(r.error(), 2);
    } else {
        if (auto r = host.start(); !r) return fail(r.error(), 2);
        for (auto& c : computers)
            if (auto r = host.addComputerEmpire(c); !r) return fail(r.error(), 2);
    }

    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    // What was saved last (turn-based: the game turn and whose turn it was).
    std::optional<std::pair<uint32_t, uint32_t>> lastSaved;
    auto saveNow = [&](std::string_view why) {
        if (!host.state()) return;
        lastSaved = std::pair{host.state()->turn, host.activeEmpire().value};
        if (auto r = host.save(saveFile); r) say(std::format("Saved turn {} to {} ({})", host.state()->turn, saveFile.string(), why));
        else say("Could not save: " + r.error());
    };

    const bool verbose = o.has("verbose");
    std::string lastLobby, lastTurn;
    int exitCode = 0;
    while (!gStop) {
        for (const net::Event& e : host.poll(200)) {
            switch (e.type) {
                case net::EventType::LobbyChanged: {
                    std::string s = lobbySummary(host.lobby());
                    if ((host.phase() == net::HostPhase::Lobby || verbose) && s != lastLobby) say(s);
                    lastLobby = std::move(s);
                    break;
                }
                case net::EventType::StateUpdated:
                case net::EventType::TurnProcessing:
                case net::EventType::OrdersReceived:
                    // Turn-based games: every command and every player turn (the turn summary says whose turn it is).
                    // Simultaneous games: each player's orders and each turn processed.
                    if (verbose || !host.turnBased()) say(net::describe(e));
                    break;
                case net::EventType::TurnStatusChanged: {
                    // Only the open turn's status (events from a turn processed since are stale).
                    if (e.turn != host.turnStatus().turn || host.turnStatus().processing) break;
                    std::string s = turnSummary(host.turnStatus());
                    if (s != lastTurn) say(s);
                    lastTurn = std::move(s);
                    break;
                }
                default: say(net::describe(e)); break;
            }
            if (e.type == net::EventType::GameStarted) saveNow("game start");
            // Turn-based games also save as each player's turn begins.
            const bool autosaveTurn = *autosave > 0 && host.state() && host.state()->turn % *autosave == 0;
            if (e.type == net::EventType::NewTurn && autosaveTurn && !host.turnBased()) saveNow("autosave");
            if (e.type == net::EventType::PlayerTurn && autosaveTurn && lastSaved != std::pair{host.state()->turn, host.activeEmpire().value})
                saveNow("autosave");
        }
        if (*maxTurns > 0 && host.state() && host.state()->turn >= static_cast<uint32_t>(*maxTurns)) {
            say(std::format("Reached turn {}; stopping.", host.state()->turn));
            break;
        }
        if (host.phase() == net::HostPhase::Stopped) {
            exitCode = 1;
            break;
        }
    }
    if (gStop) say("Stopping (signal).");
    saveNow("shutdown");
    host.stop("The server is shutting down.");
    for (const net::Event& e : host.poll(0)) say(net::describe(e));
    return exitCode;
}

// ---- Play by e-mail ---------------------------------------------------------------------------------

// Prints the turn files to send, each to its empire's player.
void listTurnFiles(const std::vector<std::pair<game::EmpireId, std::filesystem::path>>& files, const game::SaveInfo& info) {
    for (const auto& [empire, file] : files) {
        const std::string player = empire.index() < info.players.size() ? info.players[empire.index()] : std::string{};
        std::printf("Send %s to empire %u%s%s.\n", file.string().c_str(), empire.value + 1, player.empty() ? "" : ", player ", player.c_str());
    }
}

int pbemNew(std::span<char*> args) {
    auto o = parseArgs(args, {"setup", "out", "data", "turn-files"}, {"help"});
    if (!o) return fail(o.error(), 2);
    if (o->has("help")) return usage();
    if (!o->has("setup") || !o->has("out")) return fail("pbem new needs --setup=FILE.toml and --out=GAME.gam", 2);
    auto rules = loadRules(o->get("data"));
    if (!rules) return fail(rules.error(), 2);
    auto setup = server::loadSetupFile(o->get("setup"), **rules);
    if (!setup) return fail(setup.error(), 2);
    if (setup->empires.empty()) return fail("the setup file has no [[empire]] entries", 2);

    game::GameSetup gs;
    gs.seed = setup->seed.value_or(net::randomId());
    gs.options = setup->options;
    game::SaveInfo info;
    info.gameName = setup->gameName;
    info.dataSet = game::dataSetIdentity(**rules);
    info.gameId = net::randomId();
    info.masterPasswordVerifier = net::passwordVerifier(setup->masterPasswordHash);
    for (const auto& e : setup->empires) {
        game::EmpireSetup es = e.setup;
        es.passwordHash = net::passwordVerifier(e.setup.passwordHash);  // the game keeps only verifiers
        gs.empires.push_back(std::move(es));
        info.players.push_back(e.player);
    }
    auto state = game::createGame(**rules, gs);
    if (!state) return fail("could not create the game: " + state.error(), 1);
    // Turn-based: computer players before the first human play now, and that human's turn starts.
    if (game::turnBased(*state)) game::resumeTurnBased(**rules, *state);
    const std::filesystem::path out = o->get("out");
    if (auto r = game::saveGame(out, *state, info); !r) return fail(r.error(), 1);
    std::printf("Created '%s' (turn %u) in %s:\n", info.gameName.c_str(), state->turn, out.string().c_str());
    for (const game::Empire& e : state->empires)
        std::printf("  empire %u: %s (%s)%s\n", e.id.value + 1, e.name.c_str(), kindName(e.kind),
                    e.passwordHash.empty() ? "" : ", password set");
    const std::filesystem::path dir = o->has("turn-files") ? std::filesystem::path(o->get("turn-files"))
                                      : out.has_parent_path() ? out.parent_path()
                                                              : std::filesystem::path(".");
    auto files = net::pbem::writeTurnFiles(**rules, out, dir);
    if (!files) return fail(files.error(), 1);
    std::printf("Keep %s to yourself: it holds the whole game.\n", out.string().c_str());
    listTurnFiles(*files, info);
    return 0;
}

int pbemTurnFiles(std::span<char*> args) {
    auto o = parseArgs(args, {"game", "out", "data"}, {"help"});
    if (!o) return fail(o.error(), 2);
    if (o->has("help")) return usage();
    if (!o->has("game")) return fail("pbem turn-files needs --game=GAME.gam", 2);
    auto rules = loadRules(o->get("data"));
    if (!rules) return fail(rules.error(), 2);
    const std::filesystem::path game = o->get("game");
    auto info = game::readSaveInfo(game);
    if (!info) return fail(info.error(), 1);
    const std::filesystem::path dir = o->has("out") ? std::filesystem::path(o->get("out"))
                                      : game.has_parent_path() ? game.parent_path()
                                                               : std::filesystem::path(".");
    auto files = net::pbem::writeTurnFiles(**rules, game, dir);
    if (!files) return fail(files.error(), 1);
    if (files->empty()) std::printf("Nobody plays this turn (the game is over, or no human is left).\n");
    listTurnFiles(*files, *info);
    return 0;
}

int pbemProcess(std::span<char*> args) {
    auto o = parseArgs(args, {"game", "orders", "password", "data", "reset-passwords", "turn-files"}, {"keep-orders", "allow-data-mismatch", "help"});
    if (!o) return fail(o.error(), 2);
    if (o->has("help")) return usage();
    if (!o->has("game") || !o->has("orders")) return fail("pbem process needs --game=GAME.gam and --orders=DIR", 2);
    auto rules = loadRules(o->get("data"));
    if (!rules) return fail(rules.error(), 2);
    net::pbem::ProcessOptions options;
    options.masterPasswordHash = net::hashPassword(o->get("password"));
    options.deleteProcessed = !o->has("keep-orders");
    options.allowDataSetMismatch = o->has("allow-data-mismatch");
    if (o->has("turn-files")) options.turnFilesDir = o->get("turn-files");
    // --reset-passwords=2,5: Reset Passwords for those empires (numbers from 1).
    const std::string resets = o->get("reset-passwords");
    for (std::string_view rest = resets; !rest.empty();) {
        const size_t comma = rest.find(',');
        const std::string item(rest.substr(0, comma));
        rest = comma == std::string_view::npos ? std::string_view{} : rest.substr(comma + 1);
        uint32_t n = 0;
        const auto [end, ec] = std::from_chars(item.data(), item.data() + item.size(), n);
        if (ec != std::errc{} || end != item.data() + item.size() || n == 0) return fail(std::format("--reset-passwords: '{}' is not an empire number", item), 2);
        options.resetPasswords.push_back(game::EmpireId{n - 1});
    }
    auto rep = net::pbem::processGameFile(**rules, o->get("game"), o->get("orders"), options);
    if (!rep) return fail(rep.error(), 1);
    const std::string played = !rep->submitted.empty() ? rep->submitted.front() : !rep->playedByComputer.empty() ? rep->playedByComputer.front() : "";
    if (rep->turnBased && !played.empty())
        std::printf("Played %s's turn of turn %u; the game is now at turn %u.\n", played.c_str(), rep->turnBefore, rep->turnAfter);
    else if (rep->turnBased)
        std::printf("The computer players played on; the game is now at turn %u.\n", rep->turnAfter);
    else
        std::printf("Processed turn %u; the game is now at turn %u.\n", rep->turnBefore, rep->turnAfter);
    for (const auto& s : rep->submitted) std::printf("  orders: %s\n", s.c_str());
    for (const auto& s : rep->playedByComputer) std::printf("  no orders, played by the computer: %s\n", s.c_str());
    for (const auto& s : rep->warnings) std::printf("  warning: %s\n", s.c_str());
    for (const auto& s : rep->rejectedCommands) std::printf("  refused: %s\n", s.c_str());
    if (options.deleteProcessed && !rep->used.empty()) std::printf("  deleted %zu processed .plr files\n", rep->used.size());
    if (!rep->next.empty()) std::printf("Next: empire %u (%s).\n", rep->nextEmpire.value + 1, rep->next.c_str());
    if (auto info = game::readSaveInfo(o->get("game"))) listTurnFiles(rep->turnFiles, *info);
    // Shown to the host only: nothing tells the players (spec 06 §1.9).
    for (const auto& [empire, password] : rep->passwordResets)
        std::printf("Password Reset: empire %u gets the password %s.\n", empire.value + 1, password.c_str());
    return 0;
}

int pbemOrders(std::span<char*> args) {
    auto o = parseArgs(args, {"turn", "game", "password", "out"}, {"help"});
    if (!o) return fail(o.error(), 2);
    if (o->has("help")) return usage();
    if (!o->has("turn") && !o->has("game")) return fail("pbem orders needs --turn=FILE.turn (the player's turn file)", 2);
    const std::filesystem::path path = o->has("turn") ? o->get("turn") : o->get("game");
    auto turnFile = net::pbem::readTurnFile(path);
    if (!turnFile) return fail(turnFile.error() + " (pbem orders needs the player's turn file)", 1);
    auto view = game::deserializeState(turnFile->view);
    if (!view) return fail(view.error(), 1);
    const game::GameState& state = *view;
    const game::EmpireId empire = turnFile->empire;
    const std::string hash = net::hashPassword(o->get("password"));
    if (!net::checkPassword(turnFile->verifier, hash)) return fail("wrong password for this turn file's empire", 1);
    if (game::turnBased(state) && (!state.playerTurn.started || game::activePlayer(state) != empire))
        return fail("it is not this empire's turn in this turn file", 1);
    // An order list without commands: "end turn" (the empire keeps its standing orders).
    const game::EmpireOrders orders{empire, state.turn, {}};
    auto file = net::pbem::writePlayerOrders(o->get("out", "."), turnFile->info, orders, turnFile->viewChecksum, hash,
                                             net::isLegacyVerifier(turnFile->verifier));
    if (!file) return fail(file.error(), 1);
    std::printf("Wrote %s (turn %u, empire %u).\n", file->string().c_str(), orders.turn, empire.value + 1);
    return 0;
}

int pbemInfo(std::span<char*> args) {
    auto o = parseArgs(args, {"game"}, {"help"});
    if (!o) return fail(o.error(), 2);
    if (!o->has("game")) return fail("pbem info needs --game=GAME.gam (or a player's FILE.turn)", 2);
    std::optional<game::EmpireId> viewOf;
    std::expected<std::pair<game::GameState, game::SaveInfo>, std::string> game = game::loadGame(o->get("game"));
    if (!game) {
        // A player's turn file: its empire's view.
        auto turnFile = net::pbem::readTurnFile(o->get("game"));
        if (!turnFile) return fail(game.error(), 1);
        auto view = game::deserializeState(turnFile->view);
        if (!view) return fail(view.error(), 1);
        viewOf = turnFile->empire;
        game = std::pair{std::move(*view), turnFile->info};
    }
    const auto& [state, info] = *game;
    std::printf("Game '%s' (id %016llx), turn %u, year %d.%u\n", info.gameName.c_str(), static_cast<unsigned long long>(info.gameId), state.turn,
                state.year(), state.turn % 10);
    if (viewOf) std::printf("A player's turn file: the game as empire %u knows it.\n", viewOf->value + 1);
    std::printf("Data set: %s\nMaster password: %s\n", info.dataSet.c_str(), info.masterPasswordVerifier.empty() ? "none" : "set");
    for (const game::Empire& e : state.empires) {
        const std::string player = e.id.index() < info.players.size() ? info.players[e.id.index()] : std::string{};
        std::printf("  empire %u: %s, %s%s%s%s\n", e.id.value + 1, e.name.c_str(), kindName(e.kind),
                    player.empty() ? "" : ", player ", player.c_str(), e.alive ? "" : ", destroyed");
    }
    if (game::turnBased(state)) {
        const game::EmpireId active = game::activePlayer(state);
        if (!state.gameOver && active.valid() && state.playerTurn.started)
            std::printf("Turn-based: it is empire %u's turn (%s).\n", active.value + 1, state.empire(active).name.c_str());
        else if (!state.gameOver)
            std::printf("Turn-based: between player turns (\"pbem process\" plays on).\n");
    }
    if (state.gameOver) std::printf("The game is over.\n");
    return 0;
}

// ---- Test client ---------------------------------------------------------------------------------------

int runBot(std::span<char*> args) {
    auto parsed = parseArgs(args,
                            {"connect", "port", "name", "password", "join-password", "master-password", "data", "race", "turns", "timeout", "host-key"},
                            {"start", "help"});
    if (!parsed) return fail(parsed.error(), 2);
    const Options& o = *parsed;
    if (o.has("help")) return usage();
    if (!o.has("name")) return fail("bot needs --name=NAME", 2);
    auto turns = o.integer("turns", 2, 1, 100000);
    auto timeout = o.integer("timeout", 120, 1, 24 * 3600);
    auto port = o.integer("port", net::kDefaultPort, 1, 65535);
    for (const auto* v : {&turns, &timeout, &port})
        if (!*v) return fail(v->error(), 2);
    auto rules = loadRules(o.get("data"));
    if (!rules) return fail(rules.error(), 2);

    net::ClientConfig cfg;
    cfg.host = o.get("connect", "127.0.0.1");
    cfg.port = static_cast<uint16_t>(*port);
    if (const size_t colon = cfg.host.rfind(':'); colon != std::string::npos && cfg.host.find(':') == colon) {
        int p = 0;
        const std::string digits = cfg.host.substr(colon + 1);
        auto [end, ec] = std::from_chars(digits.data(), digits.data() + digits.size(), p);
        if (ec != std::errc{} || end != digits.data() + digits.size() || p < 1 || p > 65535) return fail("bad --connect port", 2);
        cfg.port = static_cast<uint16_t>(p);
        cfg.host.resize(colon);
    }
    cfg.playerName = o.get("name");
    cfg.passwordHash = net::hashPassword(o.get("password"));
    cfg.joinPasswordHash = net::hashPassword(o.get("join-password"));
    cfg.masterPasswordHash = net::hashPassword(o.get("master-password"));
    cfg.dataSet = game::dataSetIdentity(**rules);
    if (o.has("host-key")) {
        cfg.hostKey = net::crypto::keyFromHex(o.get("host-key"));
        if (!cfg.hostKey) return fail("--host-key must be the host's public key (64 hex digits)", 2);
    }
    net::ClientSession client(cfg);
    if (auto r = client.connect(); !r) return fail(r.error(), 1);

    game::EmpireSetup setup;
    setup.preset = o.get("race");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(*timeout);
    int64_t firstTurn = -1;
    std::string expectedNote;
    game::SystemId noteSystem;
    uint32_t playRequest = 0;                                  // turn-based: our command in flight
    int64_t playedTurn = -1;                                   // turn-based: the game turn we last played in
    auto homeSystem = [](const game::GameState& s, game::EmpireId me) {
        game::SystemId sys{0u};
        for (const auto& c : s.colonies)
            if (c && c->owner == me && c->homeworld) sys = s.galaxy.object(c->planet).system;
        return sys;
    };
    auto noteApplied = [&](const game::GameState& s) {
        const game::Empire& me = s.empire(client.empire());
        return noteSystem.index() < me.knowledge.notes.size() && me.knowledge.notes[noteSystem.index()] == expectedNote;
    };
    while (std::chrono::steady_clock::now() < deadline) {
        for (const net::Event& e : client.poll(100)) {
            say("bot " + cfg.playerName + ": " + net::describe(e));
            switch (e.type) {
                case net::EventType::Joined:
                    if (client.seenHostKey()) say(std::format("bot {}: host key {}", cfg.playerName, net::crypto::fingerprint(*client.seenHostKey())));
                    client.submitSetup(setup);
                    client.setReady(true);
                    if (o.has("start")) client.requestStart();
                    break;
                case net::EventType::StateUpdated:
                case net::EventType::PlayerTurn:
                    if (!client.turnBased()) break;
                    [[fallthrough]];
                case net::EventType::GameStarted:
                case net::EventType::NewTurn: {
                    if (!client.state()) break;
                    const game::GameState& s = *client.state();
                    if (firstTurn < 0) firstTurn = s.turn;
                    if (client.turnBased()) {
                        // Turn-based: one command in each of our turns, then End Turn.
                        if (s.turn >= firstTurn + *turns && playRequest == 0) {
                            say(std::format("bot {}: the game advanced from turn {} to {}; done", cfg.playerName, firstTurn, s.turn));
                            client.disconnect("Test finished.");
                            return 0;
                        }
                        if (!client.myTurn() || playedTurn == static_cast<int64_t>(s.turn) || client.pendingRequests() > 0) break;
                        noteSystem = homeSystem(s, client.empire());
                        expectedNote = std::format("bot note, turn {}", s.turn);
                        auto r = client.play(game::cmd::SetSystemNote{noteSystem, expectedNote});
                        if (!r) return fail(r.error(), 1);
                        playRequest = *r;
                        playedTurn = s.turn;
                        break;
                    }
                    const game::Empire& me = s.empire(client.empire());
                    if (!expectedNote.empty()) {
                        const bool applied = noteApplied(s);
                        say(std::format("bot {}: last turn's orders {}", cfg.playerName, applied ? "were applied" : "were NOT applied"));
                        if (!applied) return fail("orders were not applied", 1);
                    }
                    if (s.turn >= firstTurn + *turns) {
                        say(std::format("bot {}: the game advanced from turn {} to {}; done", cfg.playerName, firstTurn, s.turn));
                        client.disconnect("Test finished.");
                        return 0;
                    }
                    noteSystem = homeSystem(s, me.id);
                    expectedNote = std::format("bot note, turn {}", s.turn);
                    game::EmpireOrders orders{me.id, s.turn, {game::cmd::SetSystemNote{noteSystem, expectedNote}}};
                    if (auto r = client.submitOrders(orders); !r) return fail(r.error(), 1);
                    break;
                }
                case net::EventType::CommandsDone: {
                    if (playRequest == 0 || e.request != playRequest) break;
                    playRequest = 0;
                    if (!e.text.empty()) return fail("command refused: " + e.text, 1);
                    const bool applied = client.state() && noteApplied(*client.state());
                    say(std::format("bot {}: the command {} at once", cfg.playerName, applied ? "was carried out" : "was NOT carried out"));
                    if (!applied) return fail("the command was not carried out", 1);
                    if (auto r = client.endTurn(); !r) return fail(r.error(), 1);
                    break;
                }
                case net::EventType::OrdersRejected: return fail("orders rejected: " + e.text, 1);
                case net::EventType::Rejected:
                case net::EventType::Disconnected: return fail(e.text, 1);
                default: break;
            }
        }
    }
    return fail("timed out", 1);
}

} // namespace

int main(int argc, char** argv) {
    std::span<char*> args(argv + 1, static_cast<size_t>(std::max(argc - 1, 0)));
    const std::string_view mode = args.empty() ? std::string_view{} : std::string_view(args[0]);
    if (mode == "pbem") {
        const std::string_view sub = args.size() > 1 ? std::string_view(args[1]) : std::string_view{};
        const auto rest = args.size() > 2 ? args.subspan(2) : std::span<char*>{};
        if (sub == "new") return pbemNew(rest);
        if (sub == "process") return pbemProcess(rest);
        if (sub == "orders") return pbemOrders(rest);
        if (sub == "info") return pbemInfo(rest);
        if (sub == "turn-files") return pbemTurnFiles(rest);
        return fail("pbem needs a command: new, process, turn-files, orders or info (see --help)", 2);
    }
    if (mode == "bot") return runBot(args.subspan(1));
    if (mode == "hash-password") {
        if (args.size() != 2) return fail("usage: opense4-server hash-password PASSWORD", 2);
        std::printf("%s\n", net::hashPassword(args[1]).c_str());
        return 0;
    }
    return runServer(args);
}
