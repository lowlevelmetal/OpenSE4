#include "mods/package.hpp"

#include "mods/zip.hpp"
#include "ruleset/files.hpp"
#include "ruleset/ruleset.hpp"

#include <monocypher.h>

#include <algorithm>
#include <array>
#include <format>
#include <fstream>
#include <iterator>
#include <system_error>

namespace opense4::mods {

namespace fs = std::filesystem;

namespace {

std::string lower(std::string_view s) {
    std::string out(s);
    for (char& c : out)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return out;
}

// The first part of a package path, lowercase ("data" for "Data/x.txt"); empty for a top-level file.
std::string topFolder(std::string_view path) {
    const size_t slash = path.find('/');
    return slash == std::string_view::npos ? std::string{} : lower(path.substr(0, slash));
}

std::string_view afterTop(std::string_view path) {
    const size_t slash = path.find('/');
    return slash == std::string_view::npos ? path : path.substr(slash + 1);
}

std::string hex(const uint8_t* bytes, size_t n) {
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string out;
    for (size_t i = 0; i < n; ++i) {
        out += kDigits[bytes[i] >> 4];
        out += kDigits[bytes[i] & 15];
    }
    return out;
}

std::string readAll(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// A data folder file a classic mod may hold at its top (without a Data folder).
bool isDataFileName(std::string_view name) {
    for (const ruleset::DataFileName& d : ruleset::dataFileNames())
        if (lower(d.name) == lower(name)) return true;
    return false;
}

// Every file of a package but hidden ones (".git", ".DS_Store") and Python's
// caches: the files its identity covers and pack writes.
std::vector<PackageFile> listFiles(const fs::path& root) {
    std::vector<PackageFile> out;
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied, ec);
         it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) break;
        const std::string name = it->path().filename().string();
        if (name.starts_with(".") || name == "__pycache__") {
            if (it->is_directory(ec)) it.disable_recursion_pending();
            continue;
        }
        if (!it->is_regular_file(ec)) continue;
        PackageFile f;
        f.path = it->path().lexically_relative(root).generic_string();
        f.real = it->path();
        f.size = it->file_size(ec);
        out.push_back(std::move(f));
    }
    std::sort(out.begin(), out.end(), [](const PackageFile& a, const PackageFile& b) { return a.path < b.path; });
    return out;
}

const PackageFile* findFile(const std::vector<PackageFile>& files, std::string_view path) {
    const std::string want = lower(path);
    for (const PackageFile& f : files)
        if (lower(f.path) == want) return &f;
    return nullptr;
}

bool hasDataFolder(const std::vector<PackageFile>& files) {
    return std::any_of(files.begin(), files.end(), [](const PackageFile& f) { return topFolder(f.path) == "data"; });
}

// Where a classic mod's file goes in the game folder, if it is a game file.
std::string classicGamePath(const PackageFile& f, bool dataFolder) {
    if (!dataFolder && f.path.find('/') == std::string::npos && isDataFileName(f.path)) return "Data/" + f.path;
    return ruleset::isGameFile(f.path) ? f.path : std::string{};
}

// A classic mod's id from its folder or archive name.
std::string classicId(const fs::path& source) {
    std::string name = lower(source.stem().string());
    std::string id = "classic.";
    for (char c : name) id += (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' ? c : '-';
    if (id.size() > 64) id.resize(64);
    return id;
}

// A .zip's unpacked copy: one folder per archive content, made once.
std::expected<fs::path, std::string> unpack(const fs::path& zip, const OpenOptions& options) {
    std::error_code ec;
    fs::path cache = options.cacheDir;
    if (cache.empty()) cache = fs::temp_directory_path(ec) / "opense4-mod-cache";
    // The archive's own hash names its folder: a changed archive unpacks anew.
    crypto_blake2b_ctx ctx;
    crypto_blake2b_init(&ctx, 16);
    const std::string bytes = readAll(zip);
    if (bytes.size() > kMaxZipBytes) return std::unexpected(std::format("{}: the archive is too large", zip.string()));
    crypto_blake2b_update(&ctx, reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size());
    std::array<uint8_t, 16> digest{};
    crypto_blake2b_final(&ctx, digest.data());
    const fs::path dir = cache / std::format("{}-{}", lower(zip.stem().string()).substr(0, 40), hex(digest.data(), 8));
    if (fs::exists(dir / ".unpacked", ec)) return dir;
    fs::remove_all(dir, ec);
    if (auto r = extractZip(zip, dir); !r) {
        fs::remove_all(dir, ec);
        return std::unexpected(r.error());
    }
    std::ofstream(dir / ".unpacked") << zip.filename().string() << "\n";
    return dir;
}

} // namespace

std::string identityFileText(const Package& p) {
    return std::format("# Written by opense4-sdk pack: the mod's identity when it was packed.\nid = \"{}\"\nversion = \"{}\"\nidentity = \"{}\"\n", p.id(),
                       p.manifest.version.text, p.hash);
}

std::string tierNames(unsigned tiers) {
    static constexpr std::array<std::pair<unsigned, std::string_view>, 6> kNames{{
        {kTierAssets, "assets"}, {kTierData, "data"}, {kTierAi, "AI"}, {kTierScripts, "rules"}, {kTierInterface, "interface"}, {kTierText, "text"}}};
    std::string out;
    for (const auto& [bit, name] : kNames)
        if (tiers & bit) out += std::format("{}{}", out.empty() ? "" : ", ", name);
    return out.empty() ? std::string("nothing") : out;
}

bool isTextPath(std::string_view path) {
    const std::string p = lower(path);
    for (std::string_view ext : {".txt", ".toml", ".py", ".pyi", ".md", ".json", ".csv", ".ini", ".cfg", ".yaml", ".yml"})
        if (p.ends_with(ext)) return true;
    return false;
}

std::string packageHash(const std::vector<PackageFile>& files, bool classic) {
    crypto_blake2b_ctx ctx;
    crypto_blake2b_init(&ctx, 16);
    auto feed = [&](std::string_view s) { crypto_blake2b_update(&ctx, reinterpret_cast<const uint8_t*>(s.data()), s.size()); };
    feed(classic ? "opense4 mod identity 1 classic\n" : "opense4 mod identity 1\n");
    const bool dataFolder = hasDataFolder(files);
    std::vector<const PackageFile*> hashed;
    for (const PackageFile& f : files) {
        const std::string top = topFolder(f.path);
        // Pictures and sounds, interface and text change nothing in a game.
        if (classic ? classicGamePath(f, dataFolder).empty() : (top == "assets" || top == "ui" || top == "text")) continue;
        if (lower(f.path) == kIdentityFile) continue;  // what pack wrote about the rest
        hashed.push_back(&f);
    }
    // By path, byte by byte: the same order on every computer.
    std::sort(hashed.begin(), hashed.end(), [](const PackageFile* a, const PackageFile* b) { return a->path < b->path; });
    for (const PackageFile* f : hashed) {
        const std::string content = isTextPath(f->path) ? ruleset::readNormalized(f->real) : readAll(f->real);
        feed(std::format("{}\n{}\n", f->path, content.size()));
        feed(content);
    }
    std::array<uint8_t, 16> digest{};
    crypto_blake2b_final(&ctx, digest.data());
    return hex(digest.data(), digest.size());
}

ruleset::ModRecord Package::record() const { return ruleset::ModRecord{manifest.id, manifest.version.text, hash, affectsGame()}; }

std::string Package::label() const { return std::format("{} {}", manifest.id, manifest.version.text); }

const PackageFile* Package::file(std::string_view path) const { return findFile(files, path); }

std::vector<GameMount> Package::gameFiles() const {
    std::vector<GameMount> out;
    if (classic) {
        const bool dataFolder = hasDataFolder(files);
        for (const PackageFile& f : files)
            if (std::string target = classicGamePath(f, dataFolder); !target.empty()) out.push_back({target, f.path, f.real});
        return out;
    }
    for (const PackageFile& f : files) {
        if (topFolder(f.path) != "data") continue;
        const std::string_view rest = afterTop(f.path);
        if (rest.find('/') == std::string_view::npos) {
            if (lower(rest).ends_with(".txt")) out.push_back({"Data/" + std::string(rest), f.path, f.real});
            continue;
        }
        if (ruleset::isGameFile(rest)) out.push_back({std::string(rest), f.path, f.real});
    }
    return out;
}

fs::path Package::assetRoot() const {
    if (classic) return root;
    for (const PackageFile& f : files)
        if (topFolder(f.path) == "assets") return root / f.path.substr(0, f.path.find('/'));
    return {};
}

std::vector<const PackageFile*> Package::dataScripts() const {
    std::vector<const PackageFile*> out;
    if (classic) return out;
    for (const PackageFile& f : files) {
        if (topFolder(f.path) != "data") continue;
        const std::string rest = lower(afterTop(f.path));
        if (rest.find('/') == std::string::npos && (rest.ends_with(".toml") || rest.ends_with(".py"))) out.push_back(&f);
    }
    std::sort(out.begin(), out.end(), [](const PackageFile* a, const PackageFile* b) { return lower(a->path) < lower(b->path); });
    return out;
}

std::expected<Package, std::string> openPackage(const fs::path& path, const OpenOptions& options) {
    std::error_code ec;
    Package p;
    p.source = path;
    if (fs::is_directory(path, ec)) {
        p.root = path;
    } else if (fs::is_regular_file(path, ec) && lower(path.extension().string()) == ".zip") {
        auto dir = unpack(path, options);
        if (!dir) return std::unexpected(dir.error());
        p.zipped = true;
        p.root = *dir;
        // An archive of a folder: the package is that folder.
        std::vector<fs::path> top;
        for (const auto& e : fs::directory_iterator(p.root, ec))
            if (e.path().filename() != ".unpacked") top.push_back(e.path());
        if (top.size() == 1 && fs::is_directory(top.front(), ec) && !fs::exists(p.root / "mod.toml", ec)) p.root = top.front();
    } else {
        return std::unexpected(std::format("{}: not a mod (a folder or a .zip)", path.string()));
    }
    p.files = listFiles(p.root);
    const PackageFile* manifest = findFile(p.files, "mod.toml");
    if (manifest) {
        auto parsed = parseManifest(readAll(manifest->real), (p.source / manifest->path).generic_string());
        if (!parsed) {
            std::string why;
            for (const std::string& e : parsed.error()) why += (why.empty() ? "" : "\n") + e;
            return std::unexpected(why);
        }
        p.manifest = std::move(*parsed);
    } else {
        p.classic = true;
        p.manifest.id = classicId(path);
        p.manifest.name = path.stem().string();
        p.manifest.version = *parseVersion("0");
        p.manifest.api = kApiVersion;
        p.manifest.description = "A classic mod (no mod.toml)";
    }

    // What it holds.
    const bool dataFolder = hasDataFolder(p.files);
    for (const PackageFile& f : p.files) {
        const std::string top = topFolder(f.path);
        if (p.classic) {
            p.tiers |= classicGamePath(f, dataFolder).empty() ? (f.path.find('/') == std::string::npos ? 0u : unsigned{kTierAssets}) : unsigned{kTierData};
            continue;
        }
        if (top == "assets") {
            p.tiers |= kTierAssets;
            if (ruleset::isGameFile(afterTop(f.path)))
                p.warnings.push_back(std::format("{}: game files are read from data/, not assets/ (they change the game): this one is ignored", f.path));
        } else if (top == "data") {
            const std::string rest = lower(afterTop(f.path));
            const bool topLevel = rest.find('/') == std::string::npos;
            if (topLevel && (rest.ends_with(".toml") || rest.ends_with(".py") || rest.ends_with(".txt"))) p.tiers |= kTierData;
            else if (!topLevel && ruleset::isGameFile(afterTop(f.path))) p.tiers |= kTierData;
            else p.warnings.push_back(std::format("{}: not something data/ holds (patches, data files, AI tables, race files, design names): ignored", f.path));
        } else if (top == "ai") {
            p.tiers |= kTierAi;
        } else if (top == "scripts") {
            p.tiers |= kTierScripts;
        } else if (top == "ui") {
            p.tiers |= kTierInterface;
        } else if (top == "text") {
            p.tiers |= kTierText;
        }
    }
    p.hash = packageHash(p.files, p.classic);
    // A packed mod says what its identity was: a difference means its files changed since.
    if (const PackageFile* id = findFile(p.files, kIdentityFile)) {
        const std::string text = readAll(id->real);
        const size_t at = text.find("identity = \"");
        const std::string recorded = at == std::string::npos ? std::string{} : text.substr(at + 12, 32);
        if (recorded != p.hash)
            p.warnings.push_back(std::format("{}: the files differ from those it was packed with (identity {} when packed, {} now)", kIdentityFile,
                                             recorded.empty() ? "unknown" : recorded, p.hash));
    }
    return p;
}

} // namespace opense4::mods
