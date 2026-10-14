#include "client/classic/command_line.hpp"

#include "game/classic_save.hpp"
#include "game/serialize.hpp"
#include "game/turn.hpp"
#include "mods/data_set.hpp"
#include "net/auth.hpp"

#include <algorithm>
#include <charconv>
#include <format>
#include <system_error>
#include <utility>

namespace opense4::client::classic {

namespace fs = std::filesystem;

namespace {

char lower(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }

std::string lowered(std::string s) {
    for (char& c : s) c = lower(c);
    return s;
}

SignInCheck refused(SignInVerdict verdict, std::string why) { return SignInCheck{verdict, std::move(why)}; }

// The form of `typed` that opens `verifier` (Login To Game's forms, the
// trimmed one in small letters first); none when no form does. An empty
// verifier (no password) is opened by an empty password only. Fails when the
// computer lacks the memory Argon2id needs.
std::expected<std::optional<std::string>, std::string> openingForm(const std::string& verifier, std::string_view typed, uint64_t gameId) {
    if (verifier.empty()) return trimPassword(typed).empty() ? std::optional<std::string>(std::string()) : std::nullopt;
    for (const std::string& form : passwordForms(typed, true)) {
        if (form.empty()) continue;
        try {
            if (net::checkPassword(verifier, form, gameId)) return std::optional<std::string>(form);
        } catch (const net::PasswordWorkError& e) {
            return std::unexpected(std::string(e.what()));
        }
    }
    return std::nullopt;
}

std::string otherDataSet(const game::SaveInfo& info, const game::Rules& rules) {
    return std::format("The game was made with the data set {}, but this data set is {}.", info.dataSet,
                       info.formatVersion <= 8 ? game::legacyDataSetIdentity(rules) : game::dataSetIdentity(rules));
}

std::string empireLabel(const game::GameState& s, game::EmpireId e) {
    return e.valid() && e.index() < s.empires.size() ? s.empire(e).name : std::format("empire {}", e.value + 1);
}

} // namespace

bool isNoDialogsSwitch(std::string_view a) { return a.size() == 3 && (a[0] == '-' || a[0] == '/') && lower(a[1]) == 'n' && lower(a[2]) == 'd'; }

int playerNumberArgument(std::string_view text) {
    int n = 0;
    const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), n);
    if (text.empty() || ec != std::errc{} || end != text.data() + text.size() || n < 1) return 0;
    return n;
}

std::optional<CommandLineGame> commandLineGame(std::span<const std::string> positional) {
    if (positional.empty()) return std::nullopt;
    CommandLineGame g;
    g.gameFile = positional[0];
    if (positional.size() >= 2) g.password = positional[1];
    if (positional.size() >= 3) g.player = playerNumberArgument(positional[2]);
    if (positional.size() >= 4) g.modFolder = positional[3];
    return g;
}

bool samePassword(std::string_view a, std::string_view b) { return lowered(trimPassword(a)) == lowered(trimPassword(b)); }

fs::path modFolderPath(const std::string& folder, const fs::path& gameRoot) {
    fs::path path(folder);
    std::error_code ec;
    if (path.is_relative() && fs::is_directory(gameRoot / path, ec)) path = gameRoot / path;
    return path;
}

GameFileKind gameFileKind(const fs::path& file) {
    if (game::readSaveInfo(file)) return GameFileKind::OpenSE4Game;
    const auto bytes = game::readFileBytes(file);
    if (!bytes) return GameFileKind::Unknown;
    if (net::pbem::decodeTurnFile(*bytes)) return GameFileKind::TurnFile;
    if (game::classic::looksLikeClassicSave(*bytes)) return GameFileKind::ClassicGame;
    return GameFileKind::Unknown;
}

std::optional<fs::path> findGameFile(std::string_view name, std::span<const fs::path> folders) {
    if (name.empty()) return std::nullopt;
    const fs::path given{std::string(name)};
    std::error_code ec;
    if (fs::is_regular_file(given, ec)) return given;
    if (!given.is_relative()) return std::nullopt;
    for (const fs::path& folder : folders)
        if (!folder.empty() && fs::is_regular_file(folder / given, ec)) return folder / given;
    return std::nullopt;
}

std::vector<fs::path> gameFileFolders(const CommandLineGame& line, const fs::path& gameRoot) {
    std::vector<fs::path> out;
    if (!line.modFolder.empty()) out.push_back(modFolderPath(line.modFolder, gameRoot));
    out.push_back(savesDir());
    out.push_back(pbemDir());
    return out;
}

// ---- Signing in ------------------------------------------------------------------------------------

SavedGameSignIn signInToSavedGame(std::shared_ptr<const game::Rules> rules, const fs::path& file, int player, std::string_view password) {
    SavedGameSignIn out;
    const auto bytes = game::readFileBytes(file);
    if (!bytes) {
        out.check = refused(SignInVerdict::Unreadable, bytes.error());
        return out;
    }
    if (player < 1) {
        out.check = refused(SignInVerdict::NotPlayable, "No player number was given.");
        return out;
    }
    const game::EmpireId empire{static_cast<uint32_t>(player - 1)};
    const bool classicGame = game::classic::looksLikeClassicSave(*bytes) && !game::readSaveInfo(file);
    if (classicGame) {
        // The original's file keeps its passwords in the clear, and the seven
        // checksums of the data files it was made with (spec 08 §3.2).
        auto save = game::classic::decodeClassicSave(*bytes, rules->data().racialTraits.size());
        if (!save) {
            // It reads with another data set's number of racial traits: other data files.
            out.check = game::classic::traitCountsThatDecode(*bytes).empty()
                            ? refused(SignInVerdict::Unreadable, save.error())
                            : refused(SignInVerdict::InvalidDataFiles, "The game was saved with other data files (another number of racial traits).");
            return out;
        }
        if (empire.index() >= save->empires.size()) {
            out.check = refused(SignInVerdict::WrongPassword, std::format("The game has no empire {}.", player));
            return out;
        }
        if (!samePassword(save->empires[empire.index()].password, password)) {
            out.check = refused(SignInVerdict::WrongPassword, std::format("Wrong password for empire {}.", player));
            return out;
        }
        const game::classic::DataSetChecksums& stored = save->options.checksums;
        const bool held = std::any_of(stored.begin(), stored.end(), [](int32_t v) { return v != 0; });
        if (held) {
            const auto computed = game::classic::dataSetChecksums(rules->data().dataDir);
            if (!computed || *computed != stored) {
                out.check = refused(SignInVerdict::InvalidDataFiles,
                                    computed ? std::string("The game was saved with other data files than the ones loaded.")
                                             : std::format("The data files could not be checked: {}.", computed.error()));
                return out;
            }
        }
    } else {
        auto loaded = game::loadGame(file);
        if (!loaded) {
            out.check = refused(SignInVerdict::Unreadable, loaded.error());
            return out;
        }
        const game::GameState& s = loaded->first;
        const game::SaveInfo& info = loaded->second;
        if (empire.index() >= s.empires.size()) {
            out.check = refused(SignInVerdict::WrongPassword, std::format("The game has no empire {}.", player));
            return out;
        }
        // As Login To Game compares them (ClassicSession::loginPasswordMatches),
        // but an empire without a password takes only an empty one.
        const std::string& stored = s.empire(empire).passwordHash;
        if (stored.empty()) {
            if (!trimPassword(password).empty()) {
                out.check = refused(SignInVerdict::WrongPassword, std::format("Wrong password for {}.", s.empire(empire).name));
                return out;
            }
        } else {
            const auto opens = storedPasswordOpens(stored, password, true, info.gameId != 0, info.gameId);
            if (!opens) {
                out.check = refused(SignInVerdict::NotPlayable, opens.error());
                return out;
            }
            if (!*opens) {
                out.check = refused(SignInVerdict::WrongPassword, std::format("Wrong password for {}.", s.empire(empire).name));
                return out;
            }
        }
        // The data files, as Login To Game checks them, and the game's mods.
        if (const auto mods = game::modDifferences(s.mods, *rules, "the game"); !mods.empty()) {
            std::string why = "The game was played with other mods than the ones in use:";
            for (const std::string& m : mods) why += "\n  " + m;
            out.check = refused(SignInVerdict::InvalidDataFiles, why);
            return out;
        }
        if (!savedDataSetMatches(info, *rules)) {
            out.check = refused(SignInVerdict::InvalidDataFiles, otherDataSet(info, *rules));
            return out;
        }
    }
    auto session = ClassicSession::load(rules, file);
    if (!session) {
        // The import refuses a data set the original's game does not fit.
        out.check = refused(classicGame ? SignInVerdict::InvalidDataFiles : SignInVerdict::Unreadable, session.error());
        return out;
    }
    (void)(*session)->takeSignIn();   // signed in here: no Login To Game, no Next Player window
    const game::GameState& s = (*session)->state();
    const game::Empire& e = s.empire(empire);
    if (!e.alive) {
        out.check = refused(SignInVerdict::NotPlayable, std::format("{} has been destroyed.", e.name));
        return out;
    }
    if (e.kind != game::PlayerKind::Human) {
        out.check = refused(SignInVerdict::NotPlayable, std::format("The computer plays {}.", e.name));
        return out;
    }
    if ((*session)->turnBased() && game::activePlayer(s) != empire) {
        out.check = refused(SignInVerdict::NotPlayable, std::format("It is {}'s turn, not {}'s.", empireLabel(s, game::activePlayer(s)), e.name));
        return out;
    }
    (*session)->setPlayer(empire);
    out.session = std::move(*session);
    return out;
}

TurnFileSignIn signInToTurnFile(const game::Rules& rules, const fs::path& file, int player, std::string_view password, const fs::path& ordersDir,
                                const PbemTrust& trust) {
    TurnFileSignIn out;
    auto turnFile = net::pbem::readTurnFile(file);
    if (!turnFile) {
        out.check = refused(SignInVerdict::Unreadable, turnFile.error());
        return out;
    }
    const game::SaveInfo& info = turnFile->info;
    const game::EmpireId empire{static_cast<uint32_t>(std::max(player, 1) - 1)};
    auto name = [&](game::EmpireId e) { return e.valid() && e.index() < info.empires.size() ? info.empires[e.index()] : std::format("empire {}", e.value + 1); };
    if (player < 1 || empire != turnFile->empire) {
        out.check = refused(SignInVerdict::NotPlayable,
                            std::format("This turn file is {}'s (empire {}), not empire {}'s.", name(turnFile->empire), turnFile->empire.value + 1, player));
        return out;
    }
    if (net::isLegacyVerifier(turnFile->verifier)) {
        out.check = refused(SignInVerdict::NotPlayable, std::format("This game was made by OpenSE4 0.6: {} chooses a new password this turn. Open the turn "
                                                                    "file in the Play by E-mail window.",
                                                                    name(empire)));
        return out;
    }
    // The password first, then the data files (spec 05 §9.2).
    const auto form = openingForm(turnFile->verifier, password, info.gameId);
    if (!form) {
        out.check = refused(SignInVerdict::NotPlayable, form.error());
        return out;
    }
    if (!*form) {
        out.check = refused(SignInVerdict::WrongPassword, std::format("Wrong password for {}.", name(empire)));
        return out;
    }
    if (const auto mods = game::modDifferences(info.mods, rules, "the game"); !mods.empty()) {
        std::string why = "The game was played with other mods than the ones in use:";
        for (const std::string& m : mods) why += "\n  " + m;
        out.check = refused(SignInVerdict::InvalidDataFiles, why);
        return out;
    }
    if (!game::sameDataSet(info, rules)) {
        out.check = refused(SignInVerdict::InvalidDataFiles, otherDataSet(info, rules));
        return out;
    }
    auto game = loadPbemGame(rules, file);
    if (!game) {
        out.check = refused(SignInVerdict::Unreadable, game.error());
        return out;
    }
    auto turn = beginPbemTurn(*game, empire, **form, ordersDir, {}, trust);
    if (!turn) {
        out.check = refused(SignInVerdict::NotPlayable, turn.error());
        return out;
    }
    out.game = std::move(*game);
    out.turn = std::move(*turn);
    return out;
}

// ---- Hosting ---------------------------------------------------------------------------------------

HostOutcome hostTurn(const game::Rules& rules, const fs::path& gameFile, std::string_view password, const fs::path& ordersDir,
                     const net::secure::PbemHostKeys& host) {
    HostOutcome out;
    out.exitCode = kHostRefused;
    const std::string shown = gameFile.filename().string();
    switch (gameFileKind(gameFile)) {
        case GameFileKind::OpenSE4Game: break;
        case GameFileKind::ClassicGame:
            out.error = std::format("{} is a saved game of Space Empires IV. OpenSE4 cannot read the orders files of the original's players: host its "
                                    "turns in the original, or open it with Load Game to play it on in OpenSE4.",
                                    shown);
            return out;
        case GameFileKind::TurnFile:
            out.error = std::format("{} is a player's turn file. To play it, give the empire's number after the password; the host processes "
                                    "the turn with its game file (.gam).",
                                    shown);
            return out;
        case GameFileKind::Unknown: out.error = std::format("{} is not a saved game.", shown); return out;
    }
    auto info = game::readSaveInfo(gameFile);
    if (!info) {
        out.error = info.error();
        return out;
    }
    if (info->gameId == 0) {
        out.error = std::format("{} is a game played on one computer: it has no host to process its turns. Open it with Load Game (or --load).", shown);
        return out;
    }
    // The host's password, as the original compares it (spec 05 §9.2).
    const auto form = openingForm(info->masterPasswordVerifier, password, info->gameId);
    if (!form) {
        out.error = form.error();
        return out;
    }
    if (!*form) {
        out.wrongPassword = true;
        out.error = std::format("The password is not the host password of {}.", shown);
        return out;
    }
    net::pbem::ProcessOptions options;
    options.host = host;
    options.masterPassword = **form;
    const fs::path orders = !ordersDir.empty() ? ordersDir : gameFile.has_parent_path() ? gameFile.parent_path() : fs::path(".");
    auto rep = net::pbem::processGameFile(rules, gameFile, orders, options);
    if (!rep) {
        out.error = rep.error();
        return out;
    }
    const game::SaveInfo after = game::readSaveInfo(gameFile).value_or(*info);
    out.report = net::pbem::reportLines(*rep, options.deleteProcessed, after);
    out.exitCode = 0;
    return out;
}

std::expected<std::shared_ptr<const game::Rules>, std::string> rulesForHosting(const fs::path& dataDir, const fs::path& gameFile,
                                                                                const std::vector<std::string>& modList, bool modsGiven,
                                                                                const mods::ModFolders& folders, const mods::OpenOptions& open) {
    std::vector<ruleset::ModRecord> recorded;
    if (!modsGiven)
        if (auto info = game::readSaveInfo(gameFile)) recorded = info->mods;
    mods::ModChoice choice;
    if (modsGiven) choice.mods = modList;
    choice.folders = folders;
    choice.open = open;
    auto modSet = !recorded.empty() ? mods::modsForGame(recorded, folders, open) : mods::selectMods(choice);
    if (!modSet) {
        std::string why = "The mods could not be loaded:";
        for (const std::string& e : modSet.error()) why += "\n  " + e;
        return std::unexpected(why);
    }
    const fs::path gameRoot = dataDir.parent_path();
    auto loaded = mods::loadDataSet(gameRoot, dataDir, *modSet);
    // As the host's processing elsewhere: only a data set that cannot be read
    // at all is refused without mods; a mod's problems are errors.
    if (!loaded.ruleset || (!modSet->empty() && !loaded.diagnostics.errors.empty())) {
        std::string why = std::format("Could not load the data set at {}", dataDir.string());
        for (size_t i = 0; i < std::min<size_t>(loaded.diagnostics.errors.size(), 15); ++i) why += "\n  " + loaded.diagnostics.errors[i];
        return std::unexpected(why);
    }
    return std::make_shared<const game::Rules>(std::move(*loaded.ruleset), gameRoot);
}

} // namespace opense4::client::classic
