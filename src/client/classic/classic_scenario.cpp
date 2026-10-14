#include "client/classic/classic_scenario.hpp"

#include "datafile/datafile.hpp"
#include "datafile/reader.hpp"
#include "game/rules.hpp"
#include "mods/package.hpp"
#include "ruleset/ruleset.hpp"
#include "sdk/players.hpp"

#include <algorithm>
#include <format>
#include <fstream>
#include <sstream>
#include <system_error>

namespace opense4::client::classic {

namespace fs = std::filesystem;

namespace {

constexpr std::string_view kSettingsSuffix = "_Settings.txt";
constexpr std::string_view kTextSuffix = "_Text.txt";

std::string lower(std::string_view s) {
    std::string out(s);
    for (char& c : out)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return out;
}

// The file `name` (any case) in `dir`, when it is there.
std::optional<fs::path> fileIn(const fs::path& dir, std::string_view name) {
    std::error_code ec;
    if (dir.empty() || !fs::is_directory(dir, ec)) return std::nullopt;
    const fs::path p = ruleset::childIgnoringCase(dir, name);
    if (fs::is_regular_file(p, ec)) return p;
    return std::nullopt;
}

// <stem><suffix> at the stem's own path, else by its name in the scenario
// folder; nullopt when neither has it (`tried` is then the path at the stem,
// or in the folder for a stem without one).
std::optional<fs::path> locate(std::string_view stem, std::string_view suffix, const fs::path& folder, fs::path& tried) {
    const std::string name = stemName(stem) + std::string(suffix);
    // A stem written on another computer (backslashes on Linux, another
    // drive) has no folder here, and is found by its name instead.
    const fs::path at = fs::path(std::string(stem) + std::string(suffix));
    tried = at;
    if (auto p = fileIn(at.parent_path(), name)) return p;
    if (auto p = fileIn(folder, name)) return p;
    if (!folder.empty() && at.parent_path().empty()) tried = folder / name;
    return std::nullopt;
}

std::optional<std::string> readText(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return std::nullopt;
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// For Players: player numbers separated by commas (or spaces).
std::vector<int> players(datafile::RecordReader& r) {
    std::vector<int> out;
    const std::string text = r.str("For Players");
    std::string_view rest = text;
    while (!rest.empty()) {
        while (!rest.empty() && (rest.front() == ' ' || rest.front() == '\t' || rest.front() == ',')) rest.remove_prefix(1);
        size_t n = 0;
        while (n < rest.size() && rest[n] != ' ' && rest[n] != '\t' && rest[n] != ',') ++n;
        if (n == 0) break;
        const std::string_view token = rest.substr(0, n);
        if (auto v = datafile::parseInteger(token); v && *v >= 0 && *v <= 255) out.push_back(static_cast<int>(*v));
        else r.error(std::format("'For Players' should list player numbers, not '{}'", token));
        rest.remove_prefix(n);
    }
    return out;
}

} // namespace

std::string ScenarioFiles::problem() const {
    std::string out;
    auto list = [&](std::string_view title, const fs::path& file, const std::vector<std::string>& errors) {
        if (errors.empty()) return;
        if (!out.empty()) out += "\n";
        out += std::format("{}: {}", title, file.filename().string());
        for (const std::string& e : errors) out += "\n  " + e;
    };
    list("Scenario Settings Errors", settingsFile, settingsErrors);
    list("Scenario Text Errors", textFile, textErrors);
    return out;
}

fs::path ScenarioFiles::startingGameFile() const {
    // Always from the scenario folder, whatever folder the stem names (confirmed: binary).
    const std::string file = stemName(startingGame);
    if (auto p = fileIn(folder, file)) return *p;
    return folder / file;
}

fs::path scenarioFolder(const game::Rules& r) {
    fs::path folder;
    std::error_code ec;
    for (const mods::Package& p : sdk::gamePackages(r)) {
        if (!p.classic) continue;
        const fs::path dir = ruleset::childIgnoringCase(p.root, "Scenarios");
        if (fs::is_directory(dir, ec)) folder = dir;
    }
    if (!folder.empty()) return folder;
    if (r.gameRoot().empty()) return {};
    return ruleset::childIgnoringCase(r.gameRoot(), "Scenarios");
}

std::vector<std::string> scenarioNames(const fs::path& folder) {
    std::vector<std::string> out;
    std::error_code ec;
    if (folder.empty() || !fs::is_directory(folder, ec)) return out;
    const std::string suffix = lower(kSettingsSuffix);
    for (const auto& e : fs::directory_iterator(folder, ec)) {
        if (!e.is_regular_file(ec)) continue;
        const std::string name = e.path().filename().string();
        if (name.size() <= suffix.size() || !lower(name).ends_with(suffix)) continue;
        out.push_back(name.substr(0, name.size() - suffix.size()));
    }
    std::sort(out.begin(), out.end(), [](const std::string& a, const std::string& b) {
        const std::string la = lower(a), lb = lower(b);
        return la != lb ? la < lb : a < b;
    });
    return out;
}

std::string stemName(std::string_view stem) {
    const size_t cut = stem.find_last_of("/\\");
    return std::string(cut == std::string_view::npos ? stem : stem.substr(cut + 1));
}

bool isTutorial(std::string_view stem) { return lower(stemName(stem)) == "tutorial"; }

void parseScenarioSettings(ScenarioFiles& f, std::string_view text, const std::string& fileName) {
    const datafile::DataFile file = datafile::parse(text, fileName);
    datafile::Diagnostics diag;
    if (!file.hasDataSection || file.records.empty()) {
        // (inferred: the original's loader reports a file without its record)
        f.settingsErrors.push_back(std::format("{}: no record between *BEGIN* and *END*", fileName));
        return;
    }
    // One record; Name and Description are read and never shown (spec 01 §12).
    // Which fields the original insists on is inferred: Starting Game, which it uses.
    datafile::RecordReader r(file, file.records.front(), diag);
    f.name = r.str("Name", datafile::Need::Optional);
    f.description = r.str("Description", datafile::Need::Optional);
    f.startingGame = r.str("Starting Game");
    if (f.startingGame.empty() && r.has("Starting Game")) r.error("'Starting Game' names no file");
    f.settingsErrors.insert(f.settingsErrors.end(), diag.errors.begin(), diag.errors.end());
}

void parseScenarioText(ScenarioFiles& f, std::string_view text, const std::string& fileName) {
    const datafile::DataFile file = datafile::parse(text, fileName);
    datafile::Diagnostics diag;
    if (!file.hasDataSection) {
        f.textErrors.push_back(std::format("{}: no records between *BEGIN* and *END*", fileName));
        return;
    }
    for (const datafile::Record& rec : file.records) {
        // Which fields are required is inferred: every one the file
        // describes, but ID (not used) and Image (none is allowed).
        datafile::RecordReader r(file, rec, diag);
        ScenarioPage page;
        (void)r.int32("ID", datafile::Need::Optional);
        page.series = r.int32("Series");
        page.segment = r.int32("Segment");
        const int turn = r.int32("Turn");
        if (turn < 0) r.error("'Turn' cannot be negative");
        page.turn = static_cast<uint32_t>(std::max(turn, 0));
        page.players = players(r);
        page.title = r.str("Text Title");
        const int count = r.int32("Text Number of Paragraphs");
        if (count < 0) r.error("'Text Number of Paragraphs' cannot be negative");
        for (int k = 1; k <= count; ++k) page.paragraphs.push_back(r.str(datafile::RecordReader::key("Text Paragraph {}", k)));
        page.image = r.str("Image", datafile::Need::Optional);
        if (datafile::keysEqual(page.image, "None")) page.image.clear();
        f.pages.push_back(std::move(page));
    }
    f.textErrors.insert(f.textErrors.end(), diag.errors.begin(), diag.errors.end());
}

ScenarioFiles readScenarioFiles(std::string_view stem, const fs::path& folder) {
    ScenarioFiles f;
    f.stem = std::string(stem);
    f.folder = folder;
    // Both files are always read, so that the errors of both are listed.
    auto read = [&](std::string_view suffix, fs::path& file, std::vector<std::string>& errors, auto parse) {
        fs::path tried;
        const std::optional<fs::path> found = locate(stem, suffix, folder, tried);
        file = found.value_or(tried);
        if (!found) {
            errors.push_back(std::format("{} was found neither there nor in the scenario folder {}", file.string(), folder.string()));
            return;
        }
        const std::optional<std::string> text = readText(*found);
        if (!text) {
            errors.push_back(std::format("{} cannot be read", found->string()));
            return;
        }
        parse(f, *text, found->filename().string());
    };
    read(kSettingsSuffix, f.settingsFile, f.settingsErrors, parseScenarioSettings);
    read(kTextSuffix, f.textFile, f.textErrors, parseScenarioText);
    return f;
}

game::ClassicScenarioState scenarioState(std::string_view stem) {
    game::ClassicScenarioState s;
    s.active = true;
    s.tutorial = isTutorial(stem);
    s.stem = std::string(stem);
    return s;
}

std::optional<size_t> pageForTurn(std::span<const ScenarioPage> pages, uint32_t turn, int player) {
    for (size_t i = 0; i < pages.size(); ++i)
        if (pages[i].turn == turn && std::find(pages[i].players.begin(), pages[i].players.end(), player) != pages[i].players.end()) return i;
    return std::nullopt;
}

std::optional<size_t> previousPage(std::span<const ScenarioPage> pages, size_t at) {
    if (at == 0 || at >= pages.size() || pages[at].segment <= 1) return std::nullopt;
    return at - 1;
}

std::optional<size_t> nextPage(std::span<const ScenarioPage> pages, size_t at) {
    if (at + 1 >= pages.size() || pages[at + 1].series != pages[at].series) return std::nullopt;
    return at + 1;
}

SeriesPlace placeInSeries(std::span<const ScenarioPage> pages, size_t at) {
    SeriesPlace p;
    if (at >= pages.size()) return p;
    for (size_t i = 0; i < pages.size(); ++i) {
        if (pages[i].series != pages[at].series) continue;
        ++p.of;
        if (i <= at) ++p.at;
    }
    return p;
}

std::optional<size_t> lastShownPage(std::span<const ScenarioPage> pages, uint16_t scenarioPage) {
    const size_t at = std::max<size_t>(scenarioPage, 1) - 1;
    if (at >= pages.size()) return std::nullopt;
    return at;
}

} // namespace opense4::client::classic
