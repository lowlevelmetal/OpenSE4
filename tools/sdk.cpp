// opense4-sdk: the modding SDK's tool (docs/MODDING_SDK.md §11,
// docs/sdk/packages-and-data.md).
//
//   opense4-sdk new <assets|data|ai|rules> <dir> [--id=ID] [--name=NAME]
//   opense4-sdk check <mod> [--data=DIR] [--mods-dir=DIR] [--mod=OTHER...]
//   opense4-sdk dump <mod...> [--out=DIR] [--data=DIR] [--mods-dir=DIR]
//   opense4-sdk pack <mod> [--out=FILE.zip]
//   opense4-sdk info <mod>
//
// It reads the player's installed game (or --data=DIR) and never writes into it.

#include "assets/assets.hpp"
#include "core/environment.hpp"
#include "datafile/datafile.hpp"
#include "mods/data_set.hpp"
#include "mods/zip.hpp"
#include "net/secure.hpp"
#include "ruleset/ruleset.hpp"
#include "sdk/players.hpp"

#include <algorithm>
#include <cstdio>
#include <deque>
#include <format>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace opense4;
namespace fs = std::filesystem;

namespace {

constexpr std::string_view kUsage = R"(opense4-sdk: make, check and pack OpenSE4 mods (docs/sdk/packages-and-data.md).

Usage:
  opense4-sdk new <kind> <dir> [--id=ID] [--name=NAME]
        A new mod from a template: kind is assets, data, ai or rules.
  opense4-sdk check <mod> [--data=DIR] [--mods-dir=DIR] [--mod=OTHER...]
        Checks the mod's manifest, its dependencies (found by id in the mods
        folder, or given with --mod), its patches applied to the installed data
        set, the references they leave, and its pictures and files.
  opense4-sdk dump <mod...> [--out=DIR] [--data=DIR] [--mods-dir=DIR]
        Writes the data set with these mods applied (in load order) as data
        files into DIR (default ./dump): Data/*.txt and the AI tables.
  opense4-sdk pack <mod> [--out=FILE.zip]
        Packs a mod folder into a .zip (default <id>-<version>.zip) with its
        identity recorded in it.
  opense4-sdk info <mod>
        What a mod is and holds, and its identity.
  opense4-sdk run | test | arena | publish
        Not yet: they come with later steps of the SDK.

<mod> is a mod folder or .zip, or the id of a mod in the mods folder (default:
Mods in OpenSE4's user folder; --mods-dir=DIR). --data=DIR is the game folder
or its Data folder (default: the installed game, found as the game finds it).
Exit status: 0 when all is well, 1 when problems were found, 2 for usage errors.
)";

int fail(std::string_view message, int code = 2) {
    std::fprintf(stderr, "opense4-sdk: %.*s\n", static_cast<int>(message.size()), message.data());
    return code;
}

struct Args {
    std::vector<std::string> positional;
    std::map<std::string, std::vector<std::string>> options;
    bool has(const std::string& k) const { return options.contains(k); }
    std::string get(const std::string& k, std::string fallback = {}) const {
        auto it = options.find(k);
        return it == options.end() || it->second.empty() ? fallback : it->second.back();
    }
    std::vector<std::string> all(const std::string& k) const {
        auto it = options.find(k);
        return it == options.end() ? std::vector<std::string>{} : it->second;
    }
};

std::expected<Args, std::string> parse(const std::vector<std::string>& argv, size_t from, std::initializer_list<std::string_view> known) {
    Args a;
    for (size_t i = from; i < argv.size(); ++i) {
        const std::string& arg = argv[i];
        if (!arg.starts_with("--")) {
            a.positional.push_back(arg);
            continue;
        }
        const size_t eq = arg.find('=');
        const std::string key = arg.substr(2, eq == std::string::npos ? std::string::npos : eq - 2);
        if (std::find(known.begin(), known.end(), key) == known.end()) return std::unexpected(std::format("unknown option --{} (see --help)", key));
        if (eq == std::string::npos) {
            if (i + 1 >= argv.size()) return std::unexpected(std::format("--{} needs a value", key));
            a.options[key].push_back(argv[++i]);
        } else {
            a.options[key].push_back(arg.substr(eq + 1));
        }
    }
    return a;
}

fs::path userDir() { return net::secure::userDataDir(); }

mods::OpenOptions openOptions() {
    mods::OpenOptions o;
    o.cacheDir = mods::modCacheIn(userDir());
    return o;
}

fs::path modsDir(const Args& a) { return a.has("mods-dir") ? fs::path(a.get("mods-dir")) : mods::modsFolderIn(userDir()); }

std::string lower(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

// A mod named on the command line: a path, or an id in the mods folder.
std::expected<mods::Package, std::string> findMod(const std::string& what, const Args& a) {
    std::error_code ec;
    if (fs::exists(what, ec)) return mods::openPackage(what, openOptions());
    if (mods::validModId(what)) {
        const mods::ModLibrary lib = mods::scanModsFolder(modsDir(a), openOptions());
        if (const mods::Package* p = lib.find(what)) return *p;
        return std::unexpected(std::format("no mod '{}' in {}", what, modsDir(a).string()));
    }
    return std::unexpected(std::format("{}: no such mod folder or .zip", what));
}

struct DataPaths {
    fs::path root, data;
};
std::expected<DataPaths, std::string> installed(const Args& a) {
    const auto dir = ruleset::findInstalledDataDir(a.get("data"));
    if (!dir) return std::unexpected(a.has("data") ? std::format("no data set at {}", a.get("data"))
                                                   : std::string("no installed game found: give its folder with --data=DIR"));
    return DataPaths{dir->parent_path(), *dir};
}

// The mods to load for `targets`: each with the mods it requires (found in the
// mods folder unless --mod gives them), in load order.
std::expected<mods::ModSet, std::vector<std::string>> withDependencies(std::vector<mods::Package> targets, const Args& a) {
    std::vector<mods::Package> enabled;
    std::vector<std::string> errors;
    for (const std::string& other : a.all("mod")) {
        auto p = findMod(other, a);
        if (p) enabled.push_back(std::move(*p));
        else errors.push_back(p.error());
    }
    std::optional<mods::ModLibrary> lib;
    auto have = [&](std::string_view id) {
        return std::any_of(enabled.begin(), enabled.end(), [&](const mods::Package& p) { return p.id() == id; }) ||
               std::any_of(targets.begin(), targets.end(), [&](const mods::Package& p) { return p.id() == id; });
    };
    // Requirements, and theirs, from the mods folder.
    std::vector<const mods::Package*> queue;
    for (const mods::Package& t : targets) queue.push_back(&t);
    std::deque<mods::Package> found;  // stays in place as it grows
    while (!queue.empty()) {
        const mods::Package* p = queue.back();
        queue.pop_back();
        for (const mods::Requirement& r : p->manifest.requirements) {
            if (have(r.id) || std::any_of(found.begin(), found.end(), [&](const mods::Package& f) { return f.id() == r.id; })) continue;
            if (!lib) lib = mods::scanModsFolder(modsDir(a), openOptions());
            if (const mods::Package* dep = lib->find(r.id)) {
                found.push_back(*dep);
                queue.push_back(&found.back());
            }
        }
    }
    if (!errors.empty()) return std::unexpected(errors);
    for (mods::Package& f : found) enabled.push_back(std::move(f));
    for (mods::Package& t : targets) enabled.push_back(std::move(t));
    return mods::resolveModSet(std::move(enabled));
}

void printList(std::string_view label, const std::vector<std::string>& items) {
    for (const std::string& i : items) std::printf("  %.*s: %s\n", static_cast<int>(label.size()), label.data(), i.c_str());
}

// ---- new ---------------------------------------------------------------------------------------

void writeFile(const fs::path& file, std::string_view text) {
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    std::ofstream out(file, std::ios::binary);
    out << text;
}

int cmdNew(const std::vector<std::string>& argv) {
    auto a = parse(argv, 2, {"id", "name"});
    if (!a) return fail(a.error());
    if (a->positional.size() != 2) return fail("new needs a kind (assets, data, ai or rules) and a folder");
    const std::string kind = a->positional[0];
    const fs::path dir = a->positional[1];
    if (kind != "assets" && kind != "data" && kind != "ai" && kind != "rules") return fail(std::format("unknown kind '{}': assets, data, ai or rules", kind));
    std::error_code ec;
    if (fs::exists(dir, ec) && !fs::is_empty(dir, ec)) return fail(std::format("{} exists and is not empty", dir.string()));
    std::string id = a->get("id");
    if (id.empty()) {
        id = "my.";
        for (char c : lower(dir.filename().string())) id += (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' ? c : '-';
    }
    if (!mods::validModId(id)) return fail(std::format("'{}' is not a mod id: lowercase letters, digits, '.', '-' and '_'", id));
    mods::Manifest m;
    m.id = id;
    m.name = a->get("name", dir.filename().string());
    m.version = *mods::parseVersion("0.1.0");
    m.api = mods::kApiVersion;
    m.authors = {"You"};
    m.description = std::format("A new {} mod.", kind);
    if (kind == "ai") m.aiPlayers.push_back({"Player", "player", "Player", "A computer player made from the template.", 0});
    writeFile(dir / "mod.toml", mods::writeManifest(m));
    writeFile(dir / "README.md", std::format("# {}\n\nAn OpenSE4 mod made from the `{}` template of `opense4-sdk new`.\n\n"
                                             "See docs/sdk/packages-and-data.md in OpenSE4 for the layout, data patches and the checks.\n"
                                             "Run `opense4-sdk check {}` after each change.\n",
                                             m.name, kind, dir.string()));
    if (kind == "assets") {
        writeFile(dir / "assets" / "README.md",
                  "Pictures, sounds, music, fonts and pointers go here in the game folder's own layout,\n"
                  "for example Pictures/RaceGeneric/Generic_Mini_MyHull.bmp or Sounds/MyWeapon.wav.\n"
                  "A file here takes the place of the installed game's file of the same path (any letter case).\n");
    } else if (kind == "data") {
        writeFile(dir / "data" / "changes.toml",
                  "# Data patches: each operation names a table, and fields by the data files' own names.\n"
                  "# opense4-sdk check applies them to your installed game and reports any problem.\n"
                  "#\n"
                  "# [[components.change]]\n"
                  "# name = \"<a component of your game>\"\n"
                  "# set = { \"Supply Amount Used\" = 3 }\n"
                  "#\n"
                  "# [[vehicle_sizes.add]]\n"
                  "# name = \"My Hull\"\n"
                  "# copy_from = \"<a hull of your game>\"\n"
                  "# set = { \"Primary Bitmap Name\" = \"MyHull\", \"Tonnage\" = 350 }\n"
                  "#\n"
                  "# [[abilities.declare]]\n"
                  "# name = \"My Ability\"\n"
                  "# combine = \"sum\"\n");
    } else {
        const std::string file = kind == "ai" ? "ai/player.py" : "scripts/rules.py";
        writeFile(dir / file,
                  kind == "ai" ? "# A computer player, in Python (docs/sdk/ai-protocol.md; mod.toml's [[ai.players]] names it).\n"
                                 "# Choose it for a computer empire with --ai=<mod id>:Player. Every decision it leaves\n"
                                 "# out is the built-in AI's.\n"
                                 "\n"
                                 "from opense4 import ai\n"
                                 "\n"
                                 "\n"
                                 "class Player(ai.Player):\n"
                                 "    pass\n"
                               : "# Rules hooks, in Python.\n"
                                 "#\n"
                                 "# Scripting arrives in a later step of the OpenSE4 SDK: OpenSE4 does not run this file yet.\n"
                                 "# docs/MODDING_SDK.md section 7 outlines the hooks it will have.\n"
                                 "\n"
                                 "def colony_end_of_turn(game, colony, fx):\n"
                                 "    pass\n");
    }
    std::printf("Made the %s mod %s in %s. Next: opense4-sdk check %s\n", kind.c_str(), id.c_str(), dir.string().c_str(), dir.string().c_str());
    return 0;
}

// ---- info --------------------------------------------------------------------------------------

int cmdInfo(const std::vector<std::string>& argv) {
    auto a = parse(argv, 2, {"mods-dir"});
    if (!a) return fail(a.error());
    if (a->positional.size() != 1) return fail("info needs one mod");
    auto p = findMod(a->positional[0], *a);
    if (!p) return fail(p.error(), 1);
    const mods::Manifest& m = p->manifest;
    std::printf("%s %s%s\n", m.id.c_str(), m.version.text.c_str(), p->classic ? " (a classic mod: no mod.toml)" : "");
    std::printf("  name:        %s\n", m.name.c_str());
    if (!m.description.empty()) std::printf("  description: %s\n", m.description.c_str());
    if (!m.authors.empty()) {
        std::string authors;
        for (const std::string& s : m.authors) authors += (authors.empty() ? "" : ", ") + s;
        std::printf("  authors:     %s\n", authors.c_str());
    }
    std::printf("  SDK api:     %d\n", m.api);
    for (const mods::Requirement& r : m.requirements) std::printf("  requires:    %s %s\n", r.id.c_str(), r.range.text.c_str());
    for (const std::string& id : m.loadAfter) std::printf("  loads after: %s\n", id.c_str());
    for (const mods::AiPlayer& player : m.aiPlayers)
        std::printf("  player:      %s:%s (%s.%s)%s%s\n", m.id.c_str(), player.name.c_str(), player.module.c_str(), player.className.c_str(),
                    player.description.empty() ? "" : ": ", player.description.c_str());
    std::printf("  holds:       %s%s\n", mods::tierNames(p->tiers).c_str(), p->affectsGame() ? "; it changes the game, so every player needs it" : "");
    std::printf("  identity:    %s\n", p->hash.c_str());
    std::printf("  source:      %s (%zu files)\n", p->source.string().c_str(), p->files.size());
    for (const mods::GameMount& g : p->gameFiles()) std::printf("  game file:   %s (from %s)\n", g.installPath.c_str(), g.packagePath.c_str());
    for (const mods::PackageFile* f : p->dataScripts()) std::printf("  patch:       %s\n", f->path.c_str());
    if (!p->assetRoot().empty()) std::printf("  assets:      %s\n", p->assetRoot().string().c_str());
    printList("warning", p->warnings);
    return 0;
}

// ---- check -------------------------------------------------------------------------------------

// The asset checks: pictures records name, formats, files nothing reads.
void checkAssets(const mods::Package& p, const mods::GameData& data, const DataPaths& paths, const mods::ModSet& set, std::vector<std::string>& warnings,
                 std::vector<std::string>& errors) {
    assets::InstallFiles files(paths.root);
    for (const mods::Package& q : set.packages)
        if (!q.assetRoot().empty()) files.addLayer(q.assetRoot(), q.label());
    const std::string mine = std::format("mod {}", p.id());
    auto fromThisMod = [&](const datafile::Record& r, std::string_view key) {
        if (r.origin.starts_with(mine)) return true;
        const datafile::Field* f = r.find(key);
        return f && f->origin.starts_with(mine);
    };
    // Hull pictures: Mini_ and Portrait_ of the primary or alternate bitmap, in
    // the shared folder or some race's folder.
    std::set<std::string> raceFolders;
    for (const ruleset::FileEntry& e : data.list("Pictures/Races")) raceFolders.insert(e.name);
    auto hullPicture = [&](std::string_view kind, std::string_view bitmap) {
        if (files.find(std::format("Pictures/RaceGeneric/Generic_{}_{}.bmp", kind, bitmap))) return true;
        for (const std::string& race : raceFolders)
            if (files.find(std::format("Pictures/Races/{}/{}_{}_{}.bmp", race, race, kind, bitmap))) return true;
        return false;
    };
    std::set<std::string> bitmaps;
    for (const datafile::DataFile* f : data.dataFiles()) {
        if (!datafile::keysEqual(f->name, "VehicleSize.txt")) continue;
        for (const datafile::Record& r : f->records) {
            for (const char* key : {"Primary Bitmap Name", "Alternate Bitmap Name"})
                if (const datafile::Field* b = r.find(key)) bitmaps.insert(lower(b->value));
            if (!fromThisMod(r, "Primary Bitmap Name") && !fromThisMod(r, "Alternate Bitmap Name")) continue;
            const datafile::Field* primary = r.find("Primary Bitmap Name");
            const datafile::Field* alternate = r.find("Alternate Bitmap Name");
            for (std::string_view kind : {"Mini", "Portrait"}) {
                const bool hasPrimary = primary && hullPicture(kind, primary->value);
                const bool hasAlternate = alternate && hullPicture(kind, alternate->value);
                if (hasPrimary) continue;
                warnings.push_back(std::format("{}: the hull [{}] has no {} picture '{}' (looked for Pictures/RaceGeneric/Generic_{}_{}.bmp and in "
                                               "the race folders){}",
                                               r.origin.empty() ? std::string("VehicleSize.txt") : r.origin, mods::recordLabel(r), kind,
                                               primary ? primary->value : std::string{}, kind, primary ? primary->value : std::string{},
                                               hasAlternate ? std::format(": the game shows its Alternate Bitmap Name's, '{}'", alternate->value) : ""));
            }
        }
    }
    // Component and facility pictures: a cell of their sheet.
    auto sheetCells = [&](std::string_view sheet, int cell) -> int {
        const auto path = files.find(sheet);
        if (!path) return -1;
        const auto img = assets::loadImage(*path, false);
        return img ? (img->width / cell) * (img->height / cell) : -1;
    };
    for (const auto& [file, sheet] : {std::pair<std::string_view, std::string_view>{"Components.txt", "Pictures/Components/Components.bmp"},
                                      {"Facility.txt", "Pictures/Facilities/Facility.bmp"}}) {
        int cells = -2;
        for (const datafile::DataFile* f : data.dataFiles()) {
            if (!datafile::keysEqual(f->name, file)) continue;
            for (const datafile::Record& r : f->records) {
                if (!fromThisMod(r, "Pic Num")) continue;
                const datafile::Field* pic = r.find("Pic Num");
                const auto n = pic ? datafile::parseInteger(pic->value) : std::nullopt;
                if (!n) continue;
                if (cells == -2) cells = sheetCells(sheet, 36);
                if (cells >= 0 && (*n < 1 || *n > cells))
                    warnings.push_back(std::format("{} [{}]: Pic Num {} is not a cell of {} ({} cells)", file, mods::recordLabel(r), *n, sheet, cells));
            }
        }
    }
    // The mod's own files: formats the game reads, and folders it looks in.
    for (const mods::PackageFile& f : p.files) {
        std::string rel = f.path;
        if (!p.classic) {
            if (lower(rel).rfind("assets/", 0) != 0) continue;
            rel = rel.substr(7);
        }
        const std::string l = lower(rel);
        const std::string ext = fs::path(l).extension().string();
        const std::string top = l.substr(0, l.find('/'));
        if (ruleset::isGameFile(rel)) continue;
        if (l.find('/') == std::string::npos && (ext == ".md" || ext == ".txt")) continue;  // notes beside the files
        if (top != "pictures" && top != "sounds" && top != "music" && top != "fonts") {
            if (!p.classic || l.find('/') != std::string::npos)
                warnings.push_back(std::format("{}: the game reads pictures, sounds, music and fonts only (Pictures/, Sounds/, Music/, Fonts/)", f.path));
            continue;
        }
        static const std::set<std::string> kPictures{".bmp", ".cur", ".ani"};
        static const std::set<std::string> kSounds{".wav", ".mp3"};
        static const std::set<std::string> kFonts{".fon", ".fnt", ".ttf", ".otf"};
        const bool ok = (top == "pictures" && kPictures.contains(ext)) || ((top == "sounds" || top == "music") && kSounds.contains(ext)) ||
                        (top == "fonts" && kFonts.contains(ext)) || ext == ".txt" || ext == ".md";
        if (top == "pictures" && (ext == ".png" || ext == ".jpg" || ext == ".jpeg")) {
            warnings.push_back(std::format("{}: the game asks for pictures by names ending in .bmp, so it never finds this one: rename it to .bmp "
                                           "(PNG or JPG inside is fine)",
                                           f.path));
            continue;
        }
        if (!ok) {
            errors.push_back(std::format("{}: a {} file is not a format the game reads here{}", f.path, ext.empty() ? "nameless" : ext,
                                         ext == ".ogg" ? " (OGG is not supported yet: use WAV or MP3)" : ""));
            continue;
        }
        if (top == "pictures" && kPictures.contains(ext) && ext != ".cur" && ext != ".ani" && !assets::loadImage(f.real, false))
            errors.push_back(std::format("{}: the picture cannot be read", f.path));
        // A hull picture no hull names.
        const std::string name = fs::path(l).stem().string();
        for (std::string_view kind : {"mini_", "portrait_"}) {
            const size_t at = name.find(kind);
            if (at == std::string::npos || (l.find("pictures/races/") != 0 && l.find("pictures/raceneutral/") != 0 && l.find("pictures/racegeneric/") != 0))
                continue;
            const std::string bitmap = name.substr(at + kind.size());
            static const std::set<std::string> kGroups{"fleet", "fightergroup", "minegroup", "satellitegroup", "troopgroup", "dronegroup",
                                                       "weaponplatformgroup"};
            if (!bitmaps.contains(bitmap) && !kGroups.contains(bitmap))
                warnings.push_back(std::format("{}: no hull's Primary or Alternate Bitmap Name is '{}', so nothing shows this picture", f.path, bitmap));
        }
    }
}

int cmdCheck(const std::vector<std::string>& argv) {
    auto a = parse(argv, 2, {"data", "mods-dir", "mod"});
    if (!a) return fail(a.error());
    if (a->positional.size() != 1) return fail("check needs one mod");
    std::vector<std::string> errors, warnings;
    auto p = findMod(a->positional[0], *a);
    if (!p) {
        std::printf("%s\n", p.error().c_str());
        std::printf("\nThe mod has problems: 1 error.\n");
        return 1;
    }
    std::printf("Mod %s (%s): %s\n", p->label().c_str(), p->manifest.name.c_str(), mods::ModManager::summary(*p).c_str());
    std::printf("Identity %s\n", p->hash.c_str());
    warnings = p->warnings;
    // Its computer players (docs/sdk/ai-protocol.md §1).
    for (const mods::AiPlayer& player : p->manifest.aiPlayers)
        std::printf("Computer player %s:%s (%s.%s)\n", p->id().c_str(), player.name.c_str(), player.module.c_str(), player.className.c_str());
    const sdk::PlayerCheck players = sdk::checkModPlayers(*p);
    errors.insert(errors.end(), players.errors.begin(), players.errors.end());
    warnings.insert(warnings.end(), players.warnings.begin(), players.warnings.end());
    const std::string id = p->id();
    auto set = withDependencies({*p}, *a);
    if (!set) {
        errors = set.error();
    } else {
        std::printf("Load order: %s\n", ruleset::describeMods(set->records()).c_str());
        auto paths = installed(*a);
        if (!paths) return fail(paths.error());
        std::printf("Data set: %s\n", paths->data.string().c_str());
        const mods::LoadedDataSet loaded = mods::loadDataSet(paths->root, paths->data, *set);
        errors.insert(errors.end(), loaded.diagnostics.errors.begin(), loaded.diagnostics.errors.end());
        for (const std::string& w : loaded.diagnostics.warnings)
            if (w.find("mod ") != std::string::npos) warnings.push_back(w);
        if (loaded.ruleset) {
            const ruleset::Ruleset& rs = *loaded.ruleset;
            std::printf("Loaded: %zu tech areas, %zu hulls, %zu components, %zu facilities", rs.techAreas.size(), rs.vehicleSizes.size(), rs.components.size(),
                        rs.facilities.size());
            if (!rs.declaredAbilities.empty()) std::printf(", %zu declared abilities", rs.declaredAbilities.size());
            std::printf("\n");
        }
        if (loaded.data)
            for (const mods::Package& q : set->packages)
                if (q.id() == id) checkAssets(q, *loaded.data, *paths, *set, warnings, errors);
    }
    std::printf("\n");
    printList("error", errors);
    printList("warning", warnings);
    if (errors.empty()) std::printf("%s\n", warnings.empty() ? "No problems found." : std::format("No errors; {} warnings.", warnings.size()).c_str());
    else std::printf("The mod has problems: %zu errors, %zu warnings.\n", errors.size(), warnings.size());
    return errors.empty() ? 0 : 1;
}

// ---- dump --------------------------------------------------------------------------------------

int cmdDump(const std::vector<std::string>& argv) {
    auto a = parse(argv, 2, {"data", "mods-dir", "mod", "out"});
    if (!a) return fail(a.error());
    if (a->positional.empty()) return fail("dump needs one or more mods");
    std::vector<mods::Package> targets;
    for (const std::string& m : a->positional) {
        auto p = findMod(m, *a);
        if (!p) return fail(p.error(), 1);
        targets.push_back(std::move(*p));
    }
    auto set = withDependencies(std::move(targets), *a);
    if (!set) {
        for (const std::string& e : set.error()) std::fprintf(stderr, "error: %s\n", e.c_str());
        return 1;
    }
    auto paths = installed(*a);
    if (!paths) return fail(paths.error());
    const fs::path out = a->get("out", "dump");
    std::error_code ec;
    const fs::path absOut = fs::weakly_canonical(fs::absolute(out, ec), ec), absRoot = fs::weakly_canonical(paths->root, ec);
    if (!absRoot.empty() && absOut.string().rfind(absRoot.string(), 0) == 0)
        return fail(std::format("{} is inside the installed game: dump writes elsewhere", out.string()));
    const mods::LoadedDataSet loaded = mods::loadDataSet(paths->root, paths->data, *set);
    for (const std::string& e : loaded.diagnostics.errors) std::fprintf(stderr, "error: %s\n", e.c_str());
    if (!loaded.data) return 1;
    const std::string header = std::format("Written by opense4-sdk dump: the data set with the mods {}.", ruleset::describeMods(set->records()));
    size_t written = 0;
    for (const datafile::DataFile* f : loaded.data->dataFiles()) {
        writeFile(out / "Data" / f->name, datafile::write(*f, header));
        ++written;
    }
    for (const auto& [path, f] : loaded.data->aiFiles()) {
        writeFile(out / fs::path(path), datafile::write(*f, header));
        ++written;
    }
    std::printf("Wrote %zu files of the data set with %s to %s.\n", written, ruleset::describeMods(set->records()).c_str(), out.string().c_str());
    return loaded.diagnostics.errors.empty() ? 0 : 1;
}

// ---- pack --------------------------------------------------------------------------------------

int cmdPack(const std::vector<std::string>& argv) {
    auto a = parse(argv, 2, {"out", "mods-dir"});
    if (!a) return fail(a.error());
    if (a->positional.size() != 1) return fail("pack needs one mod folder");
    std::error_code ec;
    if (!fs::is_directory(a->positional[0], ec)) return fail(std::format("{}: pack takes a mod folder", a->positional[0]));
    auto p = mods::openPackage(a->positional[0], openOptions());
    if (!p) return fail(p.error(), 1);
    const fs::path out = a->get("out", std::format("{}-{}.zip", p->id(), p->manifest.version.text));
    std::vector<mods::ZipInput> entries;
    for (const mods::PackageFile& f : p->files) {  // hidden files and caches are not part of a package
        if (lower(f.path) == mods::kIdentityFile) continue;
        if (fs::equivalent(f.real, out, ec)) continue;
        std::ifstream in(f.real, std::ios::binary);
        entries.push_back({f.path, std::vector<uint8_t>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>())});
    }
    const std::string identity = mods::identityFileText(*p);
    entries.push_back({std::string(mods::kIdentityFile), std::vector<uint8_t>(identity.begin(), identity.end())});
    if (auto r = mods::writeZip(out, entries); !r) return fail(r.error(), 1);
    std::printf("Packed %s (%zu files, identity %s) into %s.\n", p->label().c_str(), entries.size(), p->hash.c_str(), out.string().c_str());
    for (const std::string& w : p->warnings) std::printf("  warning: %s\n", w.c_str());
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    const std::vector<std::string> args = core::utf8Arguments(argc, argv);  // UTF-8 on Windows too
    if (args.size() < 2 || args[1] == "--help" || args[1] == "-h" || args[1] == "help") {
        std::printf("%.*s", static_cast<int>(kUsage.size()), kUsage.data());
        return args.size() < 2 ? 2 : 0;
    }
    const std::string& command = args[1];
    if (command == "new") return cmdNew(args);
    if (command == "check") return cmdCheck(args);
    if (command == "dump") return cmdDump(args);
    if (command == "pack") return cmdPack(args);
    if (command == "info") return cmdInfo(args);
    if (command == "run" || command == "test" || command == "arena" || command == "publish")
        return fail(std::format("'{}' is not there yet: it comes with a later step of the SDK (docs/MODDING_SDK.md §12)", command), 2);
    return fail(std::format("unknown command '{}' (see --help)", command));
}
