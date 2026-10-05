#include "ruleset/files.hpp"

#include "core/hash.hpp"
#include "ruleset/ruleset.hpp"

#include <algorithm>
#include <format>
#include <fstream>
#include <iterator>
#include <mutex>

namespace opense4::ruleset {

namespace fs = std::filesystem;

std::string indexKey(std::string_view relative) {
    std::string out;
    out.reserve(relative.size());
    for (char c : relative) {
        if (c == '\\') c = '/';
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        if (c == '/' && (out.empty() || out.back() == '/')) continue;  // no leading or doubled separators
        out += c;
    }
    while (!out.empty() && out.back() == '/') out.pop_back();
    return out;
}

bool isGameFile(std::string_view relative) {
    const std::string k = indexKey(relative);
    if (k.starts_with("data/")) return k.find('/', 5) == std::string::npos;
    if (k.starts_with("ai/") || k.starts_with("dsgnname/")) return true;
    if (k.starts_with("pictures/races/") || k.starts_with("pictures/raceneutral/")) return k.ends_with(".txt");
    return false;
}

bool mayHoldGameFiles(std::string_view relativeDir) {
    const std::string k = indexKey(relativeDir);
    if (k.empty() || k == "data" || k == "ai" || k == "dsgnname" || k == "pictures" || k == "pictures/races" || k == "pictures/raceneutral")
        return true;
    return k.starts_with("ai/") || k.starts_with("dsgnname/") || k.starts_with("pictures/races/") || k.starts_with("pictures/raceneutral/");
}

void FileIndex::add(std::string_view relative, fs::path real) {
    std::string written(relative);
    for (char& c : written)
        if (c == '\\') c = '/';
    const std::string key = indexKey(written);
    if (key.empty() || !files_.emplace(key, Entry{written, std::move(real)}).second) return;
    // Every folder on the way, so that listing finds subfolders.
    std::string folder;
    std::vector<std::string_view> parts;
    std::string_view rest = written;
    while (!rest.empty()) {
        const size_t slash = rest.find('/');
        const std::string_view part = rest.substr(0, slash);
        if (!part.empty()) parts.push_back(part);
        if (slash == std::string_view::npos) break;
        rest.remove_prefix(slash + 1);
    }
    for (size_t i = 0; i < parts.size(); ++i) {
        const bool last = i + 1 == parts.size();
        auto& entries = folders_[folder];
        const std::string lower = indexKey(parts[i]);
        if (!entries.contains(lower)) entries.emplace(lower, FileEntry{std::string(parts[i]), !last});
        folder = folder.empty() ? lower : folder + "/" + lower;
    }
}

const fs::path* FileIndex::find(std::string_view relative) const {
    const auto it = files_.find(indexKey(relative));
    return it == files_.end() ? nullptr : &it->second.real;
}

void FileIndex::listInto(std::string_view relativeDir, std::map<std::string, FileEntry>& out) const {
    const auto it = folders_.find(indexKey(relativeDir));
    if (it == folders_.end()) return;
    for (const auto& [lower, entry] : it->second) out[lower] = entry;
}

std::vector<std::pair<std::string, fs::path>> FileIndex::files() const {
    std::vector<std::pair<std::string, const Entry*>> sorted;
    sorted.reserve(files_.size());
    for (const auto& [key, entry] : files_) sorted.emplace_back(key, &entry);
    std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<std::pair<std::string, fs::path>> out;
    out.reserve(sorted.size());
    for (const auto& [key, entry] : sorted) out.emplace_back(entry->written, entry->real);
    return out;
}

FileIndex FileIndex::ofGameFiles(const fs::path& root) {
    FileIndex index;
    std::error_code ec;
    if (root.empty() || !fs::is_directory(root, ec)) return index;
    for (auto it = fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied, ec);
         it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) break;
        const std::string relative = it->path().lexically_relative(root).generic_string();
        if (it->is_directory(ec)) {
            if (!mayHoldGameFiles(relative)) it.disable_recursion_pending();
            continue;
        }
        if (it->is_regular_file(ec) && isGameFile(relative)) index.add(relative, it->path());
    }
    return index;
}

std::vector<FileEntry> sortedEntries(const std::map<std::string, FileEntry>& entries) {
    std::vector<FileEntry> out;
    out.reserve(entries.size());
    for (const auto& [lower, entry] : entries) out.push_back(entry);
    return out;
}

std::string readNormalized(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    const std::string raw{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    std::string out;
    out.reserve(raw.size());
    for (size_t i = 0; i < raw.size(); ++i) {
        if (raw[i] == '\r') {
            out += '\n';
            if (i + 1 < raw.size() && raw[i + 1] == '\n') ++i;
            continue;
        }
        out += raw[i];
    }
    return out;
}

uint64_t hashGameFiles(const FileIndex& index) {
    Hasher h;
    for (const auto& [written, real] : index.files()) {
        const std::string key = indexKey(written);
        if (key.starts_with("data/")) continue;  // the data folder is hashed on its own
        h.add(std::string_view(key));
        h.add(std::string_view(readNormalized(real)));
    }
    return h.value();
}

namespace {

class InstallGameFiles final : public GameFiles {
public:
    InstallGameFiles(fs::path root, fs::path dataDir)
        : root_(std::move(root)), dataDir_(dataDir.empty() && !root_.empty() ? childIgnoringCase(root_, "Data") : std::move(dataDir)),
          index_(FileIndex::ofGameFiles(root_)), key_(std::format("install|{}|{}", root_.generic_string(), dataDir_.generic_string())) {}

    const fs::path& root() const override { return root_; }
    const fs::path& dataDir() const override { return dataDir_; }

    std::expected<datafile::DataFile, std::string> dataFile(std::string_view name) const override {
        return datafile::load(childIgnoringCase(dataDir_, name));
    }

    std::expected<datafile::DataFile, std::string> file(std::string_view relative) const override {
        const fs::path* path = index_.find(relative);
        if (!path) return std::unexpected(std::format("{}: no such file in the game folder", relative));
        return datafile::load(*path);
    }

    std::optional<fs::path> path(std::string_view relative) const override {
        if (const fs::path* p = index_.find(relative)) return *p;
        return std::nullopt;
    }

    std::vector<FileEntry> list(std::string_view relativeDir) const override {
        std::map<std::string, FileEntry> entries;
        index_.listInto(relativeDir, entries);
        return sortedEntries(entries);
    }

    const std::string& cacheKey() const override { return key_; }

    uint64_t fingerprint() const override {
        std::call_once(fingerprintOnce_, [&] { fingerprint_ = hashGameFiles(index_); });
        return fingerprint_;
    }

private:
    fs::path root_;
    fs::path dataDir_;
    FileIndex index_;
    std::string key_;
    mutable std::once_flag fingerprintOnce_;
    mutable uint64_t fingerprint_ = 0;
};

} // namespace

std::shared_ptr<const GameFiles> openInstallFiles(fs::path root, fs::path dataDir) {
    return std::make_shared<const InstallGameFiles>(std::move(root), std::move(dataDir));
}

} // namespace opense4::ruleset
