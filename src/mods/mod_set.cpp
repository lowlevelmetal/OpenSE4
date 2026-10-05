#include "mods/mod_set.hpp"

#include <algorithm>
#include <format>
#include <optional>
#include <system_error>

namespace opense4::mods {

namespace fs = std::filesystem;

std::vector<ruleset::ModRecord> ModSet::records() const {
    std::vector<ruleset::ModRecord> out;
    for (const Package& p : packages) out.push_back(p.record());
    return out;
}

std::string ModSet::identity() const {
    const auto r = records();
    return ruleset::modSetIdentity(r);
}

std::expected<ModSet, std::vector<std::string>> resolveModSet(std::vector<Package> enabled) {
    std::vector<std::string> errors;
    const size_t n = enabled.size();
    auto indexOf = [&](std::string_view id) -> std::optional<size_t> {
        for (size_t i = 0; i < n; ++i)
            if (enabled[i].id() == id) return i;
        return std::nullopt;
    };
    for (size_t i = 0; i < n; ++i)
        for (size_t j = i + 1; j < n; ++j)
            if (enabled[i].id() == enabled[j].id())
                errors.push_back(std::format("mod {} is enabled twice: {} and {}", enabled[i].id(), enabled[i].source.string(), enabled[j].source.string()));
    if (!errors.empty()) return std::unexpected(errors);

    // before[j] lists the mods that must load before mod j.
    std::vector<std::vector<size_t>> before(n);
    for (size_t i = 0; i < n; ++i) {
        const Package& p = enabled[i];
        for (const Requirement& r : p.manifest.requirements) {
            const auto dep = indexOf(r.id);
            if (!dep) {
                errors.push_back(std::format("mod {} needs mod {} ({}), which is not enabled", p.label(), r.id, r.range.text));
                continue;
            }
            if (!r.range.contains(enabled[*dep].manifest.version))
                errors.push_back(std::format("mod {} needs mod {} {}, but the one enabled is version {}", p.label(), r.id, r.range.text,
                                             enabled[*dep].manifest.version.text));
            if (*dep == i) errors.push_back(std::format("mod {} requires itself", p.label()));
            else before[i].push_back(*dep);
        }
        for (const std::string& id : p.manifest.loadAfter)
            if (const auto dep = indexOf(id); dep && *dep != i) before[i].push_back(*dep);
    }
    if (!errors.empty()) return std::unexpected(errors);

    // Each step takes the first mod in the player's order whose predecessors are all placed.
    std::vector<bool> placed(n, false);
    std::vector<size_t> order;
    while (order.size() < n) {
        bool progress = false;
        for (size_t i = 0; i < n; ++i) {
            if (placed[i]) continue;
            if (std::all_of(before[i].begin(), before[i].end(), [&](size_t d) { return placed[d]; })) {
                placed[i] = true;
                order.push_back(i);
                progress = true;
                break;
            }
        }
        if (!progress) {
            std::string cycle;
            for (size_t i = 0; i < n; ++i)
                if (!placed[i]) cycle += std::format("{}{}", cycle.empty() ? "" : ", ", enabled[i].id());
            errors.push_back(std::format("these mods each require, or load after, another of them, so none can load first: {}", cycle));
            return std::unexpected(errors);
        }
    }
    ModSet set;
    for (size_t i : order) set.packages.push_back(std::move(enabled[i]));
    return set;
}

const Package* ModLibrary::find(std::string_view id) const {
    for (const Package& p : packages)
        if (p.id() == id) return &p;
    return nullptr;
}

ModLibrary scanModsFolder(const fs::path& dir, const OpenOptions& options) {
    ModLibrary lib;
    std::error_code ec;
    if (dir.empty() || !fs::is_directory(dir, ec)) return lib;
    std::vector<fs::path> entries;
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        const std::string name = e.path().filename().string();
        if (name.starts_with(".")) continue;
        std::string ext = e.path().extension().string();
        for (char& c : ext)
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        if (e.is_directory(ec) || (e.is_regular_file(ec) && ext == ".zip")) entries.push_back(e.path());
    }
    std::sort(entries.begin(), entries.end());
    for (const fs::path& p : entries) {
        auto package = openPackage(p, options);
        if (!package) {
            lib.problems.push_back(package.error());
            continue;
        }
        if (const Package* other = lib.find(package->id())) {
            lib.problems.push_back(std::format("{} and {} are both mod {}: the first is used", other->source.string(), p.string(), package->id()));
            continue;
        }
        lib.packages.push_back(std::move(*package));
    }
    return lib;
}

std::expected<ModSet, std::vector<std::string>> selectMods(const ModChoice& choice) {
    std::vector<std::string> errors;
    std::vector<Package> enabled;
    std::optional<ModLibrary> library;
    std::error_code ec;
    for (const std::string& entry : choice.mods) {
        fs::path path(entry);
        if (path.is_relative() && !choice.baseDir.empty()) path = choice.baseDir / path;
        if (fs::exists(path, ec)) {
            auto p = openPackage(path, choice.open);
            if (p) enabled.push_back(std::move(*p));
            else errors.push_back(p.error());
            continue;
        }
        if (!validModId(entry)) {
            errors.push_back(std::format("{}: no such mod (a folder or .zip, or the id of a mod in the mods folder)", entry));
            continue;
        }
        if (!library) library = scanModsFolder(choice.modsDir, choice.open);
        if (const Package* p = library->find(entry)) {
            enabled.push_back(*p);
        } else {
            errors.push_back(std::format("no mod '{}' in the mods folder {}", entry, choice.modsDir.string()));
            for (const std::string& problem : library->problems) errors.push_back("  " + problem);
        }
    }
    if (!errors.empty()) return std::unexpected(errors);
    return resolveModSet(std::move(enabled));
}

std::expected<ModSet, std::vector<std::string>> modsForGame(std::span<const ruleset::ModRecord> recorded, const fs::path& modsDir,
                                                            const OpenOptions& options) {
    ModSet set;
    if (recorded.empty()) return set;
    const ModLibrary library = scanModsFolder(modsDir, options);
    std::vector<std::string> errors;
    for (const ruleset::ModRecord& r : recorded) {
        const Package* p = library.find(r.id);
        if (!r.affectsGame) {
            if (p) set.packages.push_back(*p);  // pictures and sounds: welcome, not needed
            continue;
        }
        if (!p) errors.push_back(std::format("the game uses mod {} {}, which is not in {}", r.id, r.version, modsDir.string()));
        else if (p->manifest.version.text != r.version)
            errors.push_back(std::format("the game uses mod {} {}; {} has version {}", r.id, r.version, modsDir.string(), p->manifest.version.text));
        else if (p->hash != r.hash)
            errors.push_back(std::format("the game uses mod {} {}, but the copy in {} has other files", r.id, r.version, modsDir.string()));
        else set.packages.push_back(*p);
    }
    if (!errors.empty()) return std::unexpected(errors);
    return set;
}

fs::path modsFolderIn(const fs::path& userDataDir) { return userDataDir / "Mods"; }
fs::path modCacheIn(const fs::path& userDataDir) { return userDataDir / "ModCache"; }

ModManager::ModManager(ModLibrary library, std::vector<std::string> enabled) : library_(std::move(library)) {
    for (std::string& id : enabled)
        if (library_.find(id) && !isEnabled(id)) enabled_.push_back(std::move(id));
}

bool ModManager::isEnabled(std::string_view id) const { return std::find(enabled_.begin(), enabled_.end(), id) != enabled_.end(); }

void ModManager::enable(std::string_view id) {
    if (library_.find(id) && !isEnabled(id)) enabled_.emplace_back(id);
}

void ModManager::disable(std::string_view id) { std::erase(enabled_, std::string(id)); }

void ModManager::move(std::string_view id, int by) {
    const auto it = std::find(enabled_.begin(), enabled_.end(), id);
    if (it == enabled_.end() || by == 0) return;
    const auto from = static_cast<std::ptrdiff_t>(it - enabled_.begin());
    const auto to = std::clamp<std::ptrdiff_t>(from + by, 0, static_cast<std::ptrdiff_t>(enabled_.size()) - 1);
    std::string moved = std::move(*it);
    enabled_.erase(it);
    enabled_.insert(enabled_.begin() + to, std::move(moved));
}

std::expected<ModSet, std::vector<std::string>> ModManager::resolve() const {
    std::vector<Package> chosen;
    for (const std::string& id : enabled_)
        if (const Package* p = library_.find(id)) chosen.push_back(*p);
    return resolveModSet(std::move(chosen));
}

std::string ModManager::summary(const Package& p) {
    return std::format("{}{}{}", tierNames(p.tiers), p.affectsGame() ? "; changes the game" : "", p.classic ? "; a classic mod" : "");
}

} // namespace opense4::mods
