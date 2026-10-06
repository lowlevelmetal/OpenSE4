// Helpers shared by opense4-sdk's commands for computer players (sdk_tool.hpp).

#include "sdk_tool.hpp"

#include "core/environment.hpp"
#include "mods/data_set.hpp"
#include "net/secure.hpp"
#include "net/types.hpp"
#include "ruleset/ruleset.hpp"
#include "sdk/players.hpp"
#include "sdk/process.hpp"
#include "server/setup_file.hpp"

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <format>
#include <fstream>

namespace opense4::sdktool {

namespace fs = std::filesystem;

int fail(std::string_view message, int code) {
    std::fprintf(stderr, "opense4-sdk: %.*s\n", static_cast<int>(message.size()), message.data());
    return code;
}

std::string Options::get(std::string_view key, std::string fallback) const {
    auto it = values.find(key);
    return it == values.end() || it->second.empty() ? fallback : it->second.back();
}

std::vector<std::string> Options::all(std::string_view key) const {
    auto it = values.find(key);
    return it == values.end() ? std::vector<std::string>{} : it->second;
}

std::expected<int64_t, std::string> Options::integer(std::string_view key, int64_t fallback, int64_t min, int64_t max) const {
    if (!has(key)) return fallback;
    const std::string text = get(key);
    int64_t v = 0;
    const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), v);
    if (ec != std::errc{} || end != text.data() + text.size() || v < min || v > max)
        return std::unexpected(std::format("--{} must be a whole number from {} to {}", key, min, max));
    return v;
}

std::expected<Options, std::string> parseOptions(const std::vector<std::string>& argv, size_t from, std::initializer_list<std::string_view> valued,
                                                 std::initializer_list<std::string_view> flags) {
    Options o;
    for (size_t i = from; i < argv.size(); ++i) {
        const std::string& arg = argv[i];
        if (arg == "--") {
            o.rest.assign(argv.begin() + static_cast<std::ptrdiff_t>(i) + 1, argv.end());
            break;
        }
        if (!arg.starts_with("--")) {
            o.positional.push_back(arg);
            continue;
        }
        const size_t eq = arg.find('=');
        const std::string key = arg.substr(2, eq == std::string::npos ? std::string::npos : eq - 2);
        const bool isValued = std::find(valued.begin(), valued.end(), key) != valued.end();
        const bool isFlag = std::find(flags.begin(), flags.end(), key) != flags.end();
        if (!isValued && !isFlag) return std::unexpected(std::format("unknown option --{} (see --help)", key));
        if (isFlag) {
            if (eq != std::string::npos) return std::unexpected(std::format("--{} takes no value", key));
            o.values[key].push_back("1");
        } else if (eq != std::string::npos) {
            o.values[key].push_back(arg.substr(eq + 1));
        } else if (i + 1 < argv.size()) {
            o.values[key].push_back(argv[++i]);
        } else {
            return std::unexpected(std::format("--{} needs a value", key));
        }
    }
    return o;
}

std::string dataOption(const Options& o) { return o.has("data") ? o.get("data") : o.get("classic-dir"); }

namespace {
bool gBundledMods = true;   // false with --no-bundled-mods
}

void setBundledMods(bool on) { gBundledMods = on; }

mods::ModFolders modFolders(const std::string& modsDir) {
    mods::ModFolders folders;
    folders.user = modsDir.empty() ? mods::modsFolderIn(net::secure::userDataDir()) : fs::path(modsDir);
    if (gBundledMods) folders.bundled = mods::bundledModsFolder(sdk::executableDir());
    return folders;
}

std::expected<mods::ModSet, std::string> chooseMods(const std::vector<std::string>& list, const std::string& modsDir) {
    const fs::path user = net::secure::userDataDir();
    mods::ModChoice choice;
    choice.mods = list;
    choice.folders = modFolders(modsDir);
    choice.open.cacheDir = mods::modCacheIn(user);
    auto set = mods::selectMods(choice);
    if (!set) {
        std::string why = "the mods could not be loaded:";
        for (const std::string& e : set.error()) why += "\n  " + e;
        return std::unexpected(why);
    }
    return std::move(*set);
}

std::expected<LoadedRules, std::string> loadRules(const std::string& dataArg, mods::ModSet modSet) {
    const auto dir = ruleset::findInstalledDataDir(dataArg);
    if (!dir)
        return std::unexpected(dataArg.empty() ? std::string("no installed game found: give its folder with --data=DIR")
                                               : std::format("no data set at {}", dataArg));
    LoadedRules out;
    out.dataDir = *dir;
    out.root = dir->parent_path();
    auto loaded = mods::loadDataSet(out.root, out.dataDir, modSet);
    if (!loaded.ruleset || (!modSet.empty() && !loaded.diagnostics.errors.empty())) {
        std::string why = std::format("the data set at {} could not be loaded", dir->string());
        for (const std::string& e : loaded.diagnostics.errors) why += "\n  " + e;
        return std::unexpected(why);
    }
    out.rules = std::make_unique<game::Rules>(std::move(*loaded.ruleset), out.root);
    out.mods = std::move(modSet);
    return out;
}

std::expected<size_t, std::string> setupFileEmpires(const game::Rules& r, const fs::path& file) {
    if (file.empty()) return size_t{0};
    auto setup = server::loadSetupFile(file, r);
    if (!setup) return std::unexpected(setup.error());
    return setup->empires.size();
}

namespace {

// SplitMix64: the races a seed draws (not the game's random numbers).
uint64_t mix(uint64_t& x) {
    uint64_t z = (x += 0x9e3779b97f4a7c15ull);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
}

} // namespace

std::expected<game::GameSetup, std::string> makeGameSetup(const game::Rules& r, const GameChoice& c, uint64_t seed, size_t seats) {
    game::GameSetup gs;
    gs.seed = seed;
    gs.options.simultaneous = true;
    gs.options.quadrantSize = c.quadrantSize;
    if (!c.setupFile.empty()) {
        auto file = server::loadSetupFile(c.setupFile, r);
        if (!file) return std::unexpected(file.error());
        gs.options = file->options;
        for (const server::SetupEmpire& e : file->empires) gs.empires.push_back(e.setup);
    }
    // The command line's choices over the file's.
    if (c.systems > 0) gs.options.systemCount = c.systems;
    if (!c.quadrant.empty()) gs.options.quadrantType = c.quadrant;
    if (c.turnBased) gs.options.simultaneous = false;
    if (gs.empires.empty()) {
        std::vector<std::string> races = c.races;
        if (races.empty()) {
            for (const ruleset::RacePreset& p : r.racePresets())
                if (!p.neutral) races.push_back(p.folder);
            std::sort(races.begin(), races.end());
            uint64_t state = seed;
            for (size_t i = races.size(); i > 1; --i) std::swap(races[i - 1], races[static_cast<size_t>(mix(state) % i)]);
        }
        for (size_t i = 0; i < seats; ++i) {
            game::EmpireSetup e;
            e.kind = game::PlayerKind::Computer;
            if (!races.empty()) e.preset = races[i % races.size()];
            gs.empires.push_back(std::move(e));
        }
    }
    for (game::EmpireSetup& e : gs.empires) {
        e.kind = game::PlayerKind::Computer;
        e.passwordHash.clear();
    }
    return gs;
}

fs::path pythonPackageHome() {
    std::string version(net::appVersion());
    for (char& ch : version)
        if (ch == ' ') ch = '-';
    return net::secure::userDataDir() / "python" / version;
}

std::expected<fs::path, std::string> writePythonPackage(const fs::path& dir) {
    std::error_code ec;
    for (const script::LibraryFile& f : sdk::packageFiles()) {
        const fs::path file = dir / fs::path(std::string(f.path));
        fs::create_directories(file.parent_path(), ec);
        if (ec) return std::unexpected(std::format("{}: {}", file.parent_path().string(), ec.message()));
        // Unchanged files stay as they are (a running bot may be reading them).
        std::ifstream in(file, std::ios::binary);
        const std::string now((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (in.good() && now == f.text) continue;
        in.close();
        std::ofstream out(file, std::ios::binary | std::ios::trunc);
        out.write(f.text.data(), static_cast<std::streamsize>(f.text.size()));
        if (!out) return std::unexpected(std::format("{}: could not be written", file.string()));
    }
    return dir;
}

std::string pythonPathWith(const fs::path& dir) {
#ifdef _WIN32
    constexpr char kSep = ';';
#else
    constexpr char kSep = ':';
#endif
    std::string path = dir.string();
    if (const auto old = core::environment("PYTHONPATH"); old && !old->empty()) path += kSep + *old;
    return path;
}

} // namespace opense4::sdktool
