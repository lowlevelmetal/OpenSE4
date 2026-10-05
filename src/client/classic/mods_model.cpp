#include "client/classic/mods_model.hpp"

#include "game/rules.hpp"
#include "game/serialize.hpp"

#include <algorithm>
#include <format>

namespace opense4::client::classic {

LoadedMods& loadedMods() {
    static LoadedMods mods;
    return mods;
}

std::string modsSummary(const std::vector<mods::Package>& packages) {
    if (packages.empty()) return "none";
    std::string out;
    size_t game = 0;
    for (const mods::Package& p : packages) {
        out += std::format("{}{}", out.empty() ? "" : ", ", p.manifest.name.empty() ? p.id() : p.manifest.name);
        game += p.affectsGame() ? 1 : 0;
    }
    if (game == 0) return out + " (pictures and sounds)";
    if (packages.size() == 1) return out + " (changes the game)";
    if (game == packages.size()) return out + " (they change the game)";
    return out + std::format(" ({} of them change{} the game)", game, game == 1 ? "s" : "");
}

std::vector<std::string> scriptedMods(const std::vector<mods::Package>& packages) {
    std::vector<std::string> out;
    for (const mods::Package& p : packages)
        if (p.tiers & (mods::kTierAi | mods::kTierScripts)) out.push_back(p.id());
    return out;
}

bool mentionsMod(std::string_view text, std::string_view id) {
    auto idChar = [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_'; };
    for (size_t at = text.find(id); at != std::string_view::npos; at = text.find(id, at + 1)) {
        const bool before = at == 0 || !idChar(text[at - 1]);
        const size_t end = at + id.size();
        // A sentence's full stop after the id is not part of it.
        const bool after = end == text.size() || !idChar(text[end]) || (text[end] == '.' && (end + 1 == text.size() || text[end + 1] == ' '));
        if (before && after) return true;
    }
    return false;
}

ModsChoice::ModsChoice(mods::ModLibrary library, std::vector<std::string> enabled) : manager_(std::move(library)), start_(enabled) {
    for (const std::string& id : enabled) {
        if (manager_.library().find(id)) {
            manager_.enable(id);
        } else if (std::find(missing_.begin(), missing_.end(), id) == missing_.end()) {
            missing_.push_back(id);
        }
    }
}

bool ModsChoice::isEnabled(std::string_view id) const { return manager_.isEnabled(id); }

std::vector<ModsChoice::Row> ModsChoice::rows() const {
    std::vector<Row> out;
    int order = 0;
    for (const std::string& id : manager_.enabled()) out.push_back({id, manager_.library().find(id), true, ++order});
    for (const std::string& id : missing_) out.push_back({id, nullptr, true, 0});
    std::vector<const mods::Package*> off;
    for (const mods::Package& p : manager_.library().packages)
        if (!manager_.isEnabled(p.id())) off.push_back(&p);
    auto key = [](const mods::Package* p) {
        std::string k = p->manifest.name.empty() ? p->id() : p->manifest.name;
        for (char& c : k)
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        return k + "\n" + p->id();
    };
    std::sort(off.begin(), off.end(), [&](const mods::Package* a, const mods::Package* b) { return key(a) < key(b); });
    for (const mods::Package* p : off) out.push_back({p->id(), p, false, 0});
    return out;
}

void ModsChoice::toggle(std::string_view id) {
    if (manager_.isEnabled(id)) manager_.disable(id);
    else manager_.enable(id);
}

void ModsChoice::move(std::string_view id, int by) { manager_.move(id, by); }

void ModsChoice::dropMissing(std::string_view id) { std::erase(missing_, std::string(id)); }

std::expected<mods::ModSet, std::vector<std::string>> ModsChoice::resolve() const { return manager_.resolve(); }

std::vector<std::string> ModsChoice::problemsOf(std::string_view id) const {
    std::vector<std::string> out;
    if (std::find(missing_.begin(), missing_.end(), id) != missing_.end())
        out.push_back("It is not in the mods folder any more: Done leaves it out.");
    if (auto set = resolve(); !set)
        for (const std::string& e : set.error())
            if (mentionsMod(e, id)) out.push_back(e);
    return out;
}

bool ModsChoice::changed() const { return !missing_.empty() || manager_.enabled() != start_; }

std::optional<SavedGameMods> savedGameMods(const std::filesystem::path& file, const game::Rules& rules, const LoadedMods& loaded) {
    auto info = game::readSaveInfo(file);
    if (!info) return std::nullopt;
    SavedGameMods out;
    out.differences = game::modDifferences(info->mods, rules, "the game");
    if (out.differences.empty()) return std::nullopt;
    out.recorded = info->mods;
    if (auto set = mods::modsForGame(info->mods, loaded.modsDir, loaded.open)) {
        for (const mods::Package& p : set->packages) out.ids.push_back(p.id());
    } else {
        out.unavailable = set.error();
    }
    return out;
}

} // namespace opense4::client::classic
