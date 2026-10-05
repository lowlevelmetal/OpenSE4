// opense4-convert: the original's saved games (.gam) and OpenSE4's, both ways
// (docs/spec/08-saved-games.md, docs/SETUP.md "Games of the original").
//
//   opense4-convert --info GAME.gam                  what an original save holds
//   opense4-convert IN.gam OUT.gam --to=opense4      an original save as an OpenSE4 save
//   opense4-convert IN.gam OUT.gam --to=original     an OpenSE4 save as an original save
//   opense4-convert --compare A.gam B.gam            field differences of two original saves
//
// The data set is the installed game's (or --classic-dir=DIR): a save holds
// bare positions in the data files, so it converts only with the data set it
// was played with.

#include "core/environment.hpp"
#include "game/classic_save.hpp"
#include "game/rules.hpp"
#include "game/serialize.hpp"
#include "ruleset/ruleset.hpp"

#include <cstdio>
#include <filesystem>
#include <format>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <vector>

using namespace opense4;

namespace {

void usage() {
    std::fputs("Usage:\n"
               "  opense4-convert --info GAME.gam [--classic-dir=DIR]\n"
               "  opense4-convert IN.gam OUT.gam --to=opense4|original [--classic-dir=DIR] [--seed=N]\n"
               "  opense4-convert --compare A.gam B.gam [--classic-dir=DIR]\n"
               "\n"
               "  --info        describe a saved game of the original (sections, counts)\n"
               "  --to=opense4  read an original save and write it as an OpenSE4 save\n"
               "  --to=original read an OpenSE4 save and write it as an original save\n"
               "  --compare     list the fields in which two original saves differ\n"
               "  --classic-dir the installed game (or its Data folder); default: found automatically\n"
               "  --seed=N      the keys of the written file (default: random)\n"
               "  -v            every detail of what was approximated\n",
               stderr);
}

struct Options {
    std::vector<std::string> files;
    std::string to;
    std::string classicDir;
    bool info = false;
    bool compare = false;
    bool verbose = false;
    std::optional<uint64_t> seed;
};

std::unique_ptr<game::Rules> loadRules(const std::string& classicDir, std::string& error) {
    const auto dir = ruleset::findInstalledDataDir(classicDir);
    if (!dir) {
        error = classicDir.empty() ? "No installed Space Empires IV found; name it with --classic-dir=DIR."
                                   : std::format("No Space Empires IV data set at {}.", classicDir);
        return nullptr;
    }
    auto loaded = ruleset::loadRuleset(*dir);
    if (!loaded.ruleset) {
        error = std::format("The data set at {} could not be read.", dir->string());
        return nullptr;
    }
    return std::make_unique<game::Rules>(std::move(*loaded.ruleset), dir->parent_path());
}

std::expected<game::classic::ClassicSave, std::string> decodeFile(const game::Rules& rules, const std::string& file) {
    auto bytes = game::readFileBytes(file);
    if (!bytes) return std::unexpected(bytes.error());
    auto save = game::classic::decodeClassicSave(*bytes, rules.data().racialTraits.size());
    if (!save) return std::unexpected(std::format("{}: {}", file, save.error()));
    return save;
}

void printReport(const game::classic::ConversionReport& report, bool verbose) {
    for (const std::string& n : report.notes) std::printf("  note: %s\n", n.c_str());
    if (verbose)
        for (const std::string& d : report.details) std::printf("  detail: %s\n", d.c_str());
    else if (!report.details.empty())
        std::printf("  (%zu details; -v lists them)\n", report.details.size());
}

int run(const Options& o) {
    std::string error;
    const std::unique_ptr<game::Rules> rules = loadRules(o.classicDir, error);
    if (!rules) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 2;
    }
    if (o.info) {
        auto save = decodeFile(*rules, o.files[0]);
        if (!save) {
            std::fprintf(stderr, "%s\n", save.error().c_str());
            return 1;
        }
        std::printf("%s", game::classic::describe(*save).c_str());
        return 0;
    }
    if (o.compare) {
        auto a = decodeFile(*rules, o.files[0]);
        auto b = decodeFile(*rules, o.files[1]);
        if (!a || !b) {
            std::fprintf(stderr, "%s\n", (!a ? a.error() : b.error()).c_str());
            return 1;
        }
        const std::vector<std::string> diff = game::classic::compareSaves(*a, *b, 1000000);
        for (const std::string& d : diff) std::printf("%s\n", d.c_str());
        std::printf("%zu differences\n", diff.size());
        return diff.empty() ? 0 : 3;
    }
    const std::string& in = o.files[0];
    const std::string& out = o.files[1];
    auto bytes = game::readFileBytes(in);
    if (!bytes) {
        std::fprintf(stderr, "%s\n", bytes.error().c_str());
        return 1;
    }
    const bool original = game::classic::looksLikeClassicSave(*bytes);
    game::classic::ConversionReport report;
    game::GameState state;
    std::string gameName = std::filesystem::path(in).stem().string();
    if (original) {
        auto imported = game::classic::readClassicGame(*rules, in, report);
        if (!imported) {
            std::fprintf(stderr, "%s\n", imported.error().c_str());
            return 1;
        }
        state = std::move(*imported);
    } else {
        auto loaded = game::loadGame(in);
        if (!loaded) {
            std::fprintf(stderr, "%s\n", loaded.error().c_str());
            return 1;
        }
        if (!loaded->second.gameName.empty()) gameName = loaded->second.gameName;
        if (o.to == "opense4") {
            std::fprintf(stderr, "%s is already an OpenSE4 saved game.\n", in.c_str());
            return 1;
        }
        state = std::move(loaded->first);
    }
    if (std::string problem = game::validateState(state, rules.get()); !problem.empty()) {
        std::fprintf(stderr, "%s: the game does not fit the data set: %s\n", in.c_str(), problem.c_str());
        return 1;
    }
    if (o.to == "opense4") {
        game::SaveInfo info;
        info.gameName = gameName;
        info.dataSet = rules->data().dataDir.parent_path().filename().string();
        if (auto saved = game::saveGame(out, state, info); !saved) {
            std::fprintf(stderr, "%s\n", saved.error().c_str());
            return 1;
        }
        std::printf("Imported %s (%s, %zu empires) into %s.\n", in.c_str(), game::classic::describeDate(state.turn).c_str(), state.empires.size(),
                    out.c_str());
    } else {
        game::classic::ExportOptions options;
        options.keySeed = o.seed ? *o.seed : std::random_device{}();
        options.gameName = std::filesystem::path(out).stem().string();
        if (auto written = game::classic::writeClassicGame(*rules, state, out, report, options); !written) {
            std::fprintf(stderr, "%s\n", written.error().c_str());
            return 1;
        }
        std::printf("Exported %s (%s, %zu empires) to %s.\n", in.c_str(), game::classic::describeDate(state.turn).c_str(), state.empires.size(),
                    out.c_str());
    }
    printReport(report, o.verbose);
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    Options o;
    const std::vector<std::string> args = core::utf8Arguments(argc, argv);
    for (size_t i = 1; i < args.size(); ++i) {
        const std::string& a = args[i];
        if (a == "--info") o.info = true;
        else if (a == "--compare") o.compare = true;
        else if (a == "-v" || a == "--verbose") o.verbose = true;
        else if (a.starts_with("--to=")) o.to = a.substr(5);
        else if (a.starts_with("--classic-dir=")) o.classicDir = a.substr(14);
        else if (a.starts_with("--seed=")) o.seed = std::strtoull(a.c_str() + 7, nullptr, 10);
        else if (a == "-h" || a == "--help") {
            usage();
            return 0;
        } else if (a.starts_with("-")) {
            std::fprintf(stderr, "Unknown option %s\n", a.c_str());
            usage();
            return 2;
        } else {
            o.files.push_back(a);
        }
    }
    const size_t want = o.info ? 1 : 2;
    if (o.files.size() != want || (!o.info && !o.compare && o.to != "opense4" && o.to != "original")) {
        usage();
        return 2;
    }
    return run(o);
}
