#include "assets/assets.hpp"

#include "core/log.hpp"

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#define STB_IMAGE_IMPLEMENTATION
#define STBI_WINDOWS_UTF8  // file names are UTF-8 (path::string() with MinGW, or the UTF-8 code page), as on Linux
#define STBI_ONLY_BMP
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#include <stb_image.h>
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <format>
#include <fstream>
#include <iterator>
#include <map>

namespace opense4::assets {

namespace {

std::string lowerSlashed(std::string_view s) {
    std::string out(s);
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        if (c == '\\') c = '/';
    }
    return out;
}

} // namespace

ImageFormat imageFormat(std::span<const uint8_t> b) {
    static constexpr uint8_t kPng[] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    if (b.size() >= 8 && std::equal(std::begin(kPng), std::end(kPng), b.begin())) return ImageFormat::Png;
    if (b.size() >= 2 && b[0] == 'B' && b[1] == 'M') return ImageFormat::Bmp;
    if (b.size() >= 3 && b[0] == 0xff && b[1] == 0xd8 && b[2] == 0xff) return ImageFormat::Jpeg;
    return ImageFormat::Unknown;
}

std::string_view formatName(ImageFormat f) {
    switch (f) {
        case ImageFormat::Bmp: return "BMP";
        case ImageFormat::Png: return "PNG";
        case ImageFormat::Jpeg: return "JPEG";
        case ImageFormat::Unknown: break;
    }
    return "unknown";
}

namespace {

bool readBytes(const std::filesystem::path& path, std::vector<uint8_t>& out, std::string& error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "it cannot be opened";
        return false;
    }
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

} // namespace

std::optional<Image> loadImageMemory(std::span<const uint8_t> bytes, bool blackIsTransparent, std::string* error) {
    int w = 0, h = 0, channels = 0;
    if (bytes.size() > static_cast<size_t>(INT32_MAX)) {
        if (error) *error = "the file is too large";
        return std::nullopt;
    }
    stbi_uc* data = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &w, &h, &channels, 4);
    if (!data) {
        if (error) *error = stbi_failure_reason();
        return std::nullopt;
    }
    Image img;
    img.width = w;
    img.height = h;
    img.rgba.assign(data, data + static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
    stbi_image_free(data);
    // A PNG has transparency of its own; the classic formats have none, and black stands for it.
    if (blackIsTransparent && imageFormat(bytes) != ImageFormat::Png)
        for (size_t i = 0; i < img.rgba.size(); i += 4)
            if (img.rgba[i] == 0 && img.rgba[i + 1] == 0 && img.rgba[i + 2] == 0) img.rgba[i + 3] = 0;
    return img;
}

std::optional<Image> loadImage(const std::filesystem::path& path, bool blackIsTransparent) {
    std::vector<uint8_t> bytes;
    std::string error;
    if (!readBytes(path, bytes, error)) {
        log::warn("Cannot load image {}: {}", path.string(), error);
        return std::nullopt;
    }
    auto img = loadImageMemory(bytes, blackIsTransparent, &error);
    if (!img) log::warn("Cannot load image {}: {}", path.string(), error);
    return img;
}

std::expected<ImageInfo, std::string> probeImage(const std::filesystem::path& path) {
    std::vector<uint8_t> bytes;
    std::string error;
    if (!readBytes(path, bytes, error)) return std::unexpected(error);
    ImageInfo info;
    info.format = imageFormat(bytes);
    if (info.format == ImageFormat::Unknown) return std::unexpected(std::string("it is not a BMP, PNG or JPEG picture"));
    if (bytes.size() > static_cast<size_t>(INT32_MAX)) return std::unexpected(std::string("the file is too large"));
    int channels = 0;
    // Decoded in full: a damaged file must not pass for a good one.
    auto img = loadImageMemory(bytes, false, &error);
    if (!img) return std::unexpected(std::format("it cannot be read: {}", error));
    if (!stbi_info_from_memory(bytes.data(), static_cast<int>(bytes.size()), &info.width, &info.height, &channels))
        return std::unexpected(std::format("it cannot be read: {}", stbi_failure_reason()));
    info.alpha = info.format == ImageFormat::Png && (channels == 2 || channels == 4);
    return info;
}

std::optional<std::pair<int, int>> classicPictureSize(std::string_view relative) {
    // The sizes the classic art has, kind by kind (the layout draws them there).
    using Size = std::optional<std::pair<int, int>>;
    const std::string p = lowerSlashed(relative);
    const size_t slash = p.rfind('/');
    const std::string_view name = slash == std::string::npos ? std::string_view(p) : std::string_view(p).substr(slash + 1);
    const std::string_view stem = name.substr(0, name.rfind('.'));
    auto under = [&](std::string_view folder) { return p.starts_with(folder); };
    if (under("pictures/races/") || under("pictures/raceneutral/") || under("pictures/racegeneric/")) {
        // "<Style>_<suffix>": the suffix names the kind.
        const size_t sep = stem.find('_');
        const std::string_view suffix = sep == std::string_view::npos ? stem : stem.substr(sep + 1);
        if (suffix.starts_with("mini_")) return Size{{36, 36}};
        if (suffix.starts_with("portrait_") || suffix == "race_portrait") return Size{{128, 128}};
        if (suffix == "pop_mini") return Size{{20, 20}};
        if (suffix == "pop_portrait") return Size{{36, 36}};
        if (suffix == "main") return Size{{100, 20}};
        if (suffix == "shields") return Size{{288, 36}};
        if (suffix == "bigexplosion") return Size{{576, 72}};
        return std::nullopt;
    }
    const bool picture = name.ends_with(".bmp") || name.ends_with(".png");
    if (under("pictures/components/comp_") || under("pictures/facilities/facil_") || under("pictures/planets/p") || under("pictures/events/"))
        return picture ? Size{{128, 128}} : std::nullopt;
    if (under("pictures/systems/1024x768/")) return Size{{660, 660}};
    if (under("pictures/systems/800x600/")) return Size{{490, 490}};
    if (under("pictures/systems/")) {
        if (p.find('/', std::string_view("pictures/systems/").size()) != std::string::npos) return std::nullopt;
        return stem.find("tile") != std::string_view::npos ? Size{{72, 72}} : Size{{128, 128}};
    }
    if (p == "pictures/game/screens/1024x768/intro.bmp" || p == "pictures/game/screens/1024x768/intro.png") return Size{{1024, 768}};
    if (p == "pictures/game/screens/800x600/intro.bmp" || p == "pictures/game/screens/800x600/intro.png") return Size{{800, 600}};
    return std::nullopt;
}

std::optional<std::pair<int, int>> probeImageSize(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;
    // The headers of BMP, PNG and JPEG files: the size is near the start (a
    // JPEG's may come after its other segments, so read a fair amount).
    std::vector<char> head(64 * 1024);
    in.read(head.data(), static_cast<std::streamsize>(head.size()));
    const auto got = static_cast<int>(in.gcount());
    int w = 0, h = 0, channels = 0;
    if (got <= 0 || !stbi_info_from_memory(reinterpret_cast<const stbi_uc*>(head.data()), got, &w, &h, &channels)) return std::nullopt;
    return std::pair{w, h};
}

Image downscale(const Image& src, int w, int h) {
    Image out;
    if (src.empty() || w <= 0 || h <= 0) return out;
    out.width = w;
    out.height = h;
    out.rgba.assign(static_cast<size_t>(w) * static_cast<size_t>(h) * 4, 0);
    // Per target column (row): the source columns it covers and how much of each.
    struct Span {
        int first = 0;
        std::vector<double> weights;
    };
    auto spans = [](int from, int to) {
        std::vector<Span> list(static_cast<size_t>(to));
        const double step = static_cast<double>(from) / to;
        for (int i = 0; i < to; ++i) {
            const double a = i * step, b = (i + 1) * step;
            Span& s = list[static_cast<size_t>(i)];
            s.first = static_cast<int>(std::floor(a));
            const int last = std::min(from - 1, static_cast<int>(std::ceil(b)) - 1);
            for (int k = s.first; k <= std::max(s.first, last); ++k)
                s.weights.push_back(std::max(0.0, std::min(b, k + 1.0) - std::max(a, static_cast<double>(k))));
        }
        return list;
    };
    const std::vector<Span> xs = spans(src.width, w), ys = spans(src.height, h);
    for (int y = 0; y < h; ++y) {
        const Span& sy = ys[static_cast<size_t>(y)];
        for (int x = 0; x < w; ++x) {
            const Span& sx = xs[static_cast<size_t>(x)];
            double r = 0, g = 0, b = 0, a = 0, total = 0;
            for (size_t j = 0; j < sy.weights.size(); ++j) {
                const int py = std::min(src.height - 1, sy.first + static_cast<int>(j));
                for (size_t i = 0; i < sx.weights.size(); ++i) {
                    const int px = std::min(src.width - 1, sx.first + static_cast<int>(i));
                    const double wgt = sx.weights[i] * sy.weights[j];
                    const uint8_t* p = &src.rgba[(static_cast<size_t>(py) * static_cast<size_t>(src.width) + static_cast<size_t>(px)) * 4];
                    const double pa = wgt * p[3];
                    r += pa * p[0];
                    g += pa * p[1];
                    b += pa * p[2];
                    a += pa;
                    total += wgt;
                }
            }
            uint8_t* d = &out.rgba[(static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)) * 4];
            auto byte = [](double v) { return static_cast<uint8_t>(std::clamp(std::lround(v), 0L, 255L)); };
            if (a > 0) {
                d[0] = byte(r / a);
                d[1] = byte(g / a);
                d[2] = byte(b / a);
            }
            d[3] = total > 0 ? byte(a / total) : 0;
        }
    }
    return out;
}

Image crop(const Image& src, int x, int y, int w, int h) {
    Image out;
    out.width = w;
    out.height = h;
    out.rgba.assign(static_cast<size_t>(w) * static_cast<size_t>(h) * 4, 0);
    for (int row = 0; row < h; ++row) {
        const int sy = y + row;
        if (sy < 0 || sy >= src.height) continue;
        for (int col = 0; col < w; ++col) {
            const int sx = x + col;
            if (sx < 0 || sx >= src.width) continue;
            std::memcpy(&out.rgba[(static_cast<size_t>(row) * static_cast<size_t>(w) + static_cast<size_t>(col)) * 4],
                        &src.rgba[(static_cast<size_t>(sy) * static_cast<size_t>(src.width) + static_cast<size_t>(sx)) * 4], 4);
        }
    }
    return out;
}

Image rotateNearest(const Image& src, double degrees, bool transparentOutside) {
    Image out;
    out.width = src.width;
    out.height = src.height;
    out.rgba.assign(src.rgba.size(), 0);
    const double a = degrees * 3.14159265358979323846 / 180.0;
    const double c = std::cos(a), s = std::sin(a);
    const double cx = src.width * 0.5, cy = src.height * 0.5;
    for (int y = 0; y < src.height; ++y)
        for (int x = 0; x < src.width; ++x) {
            // The source of a destination pixel: the pixel centre turned back (anticlockwise on screen).
            const double dx = x + 0.5 - cx, dy = y + 0.5 - cy;
            const int sx = static_cast<int>(std::floor(cx + dx * c + dy * s));
            const int sy = static_cast<int>(std::floor(cy - dx * s + dy * c));
            uint8_t* d = &out.rgba[(static_cast<size_t>(y) * static_cast<size_t>(src.width) + static_cast<size_t>(x)) * 4];
            if (sx < 0 || sy < 0 || sx >= src.width || sy >= src.height) {
                d[3] = transparentOutside ? 0 : 255;
                continue;
            }
            std::memcpy(d, &src.rgba[(static_cast<size_t>(sy) * static_cast<size_t>(src.width) + static_cast<size_t>(sx)) * 4], 4);
        }
    return out;
}

bool isBlank(const Image& img) {
    for (size_t i = 3; i < img.rgba.size(); i += 4)
        if (img.rgba[i] != 0) return false;
    return true;
}

namespace {

std::unordered_map<std::string, std::filesystem::path> indexFolder(const std::filesystem::path& root) {
    std::unordered_map<std::string, std::filesystem::path> index;
    std::error_code ec;
    for (auto it = std::filesystem::recursive_directory_iterator(root, std::filesystem::directory_options::skip_permission_denied, ec);
         it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) break;
        if (!it->is_regular_file(ec)) continue;
        const auto rel = std::filesystem::relative(it->path(), root, ec);
        index.emplace(lowerSlashed(rel.generic_string()), it->path());
    }
    return index;
}

} // namespace

InstallFiles::InstallFiles(std::filesystem::path root) : root_(std::move(root)) {
    index_ = indexFolder(root_);
    if (const auto path = find("Path.txt")) {
        std::ifstream in(*path, std::ios::binary);
        const std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
        mod_ = lowerSlashed(modDirectoryFromPathTxt(text));
        if (!mod_.empty()) log::info("Mod folder from Path.txt: {}", mod_);
    }
}

std::string modDirectoryFromPathTxt(std::string_view text) {
    // One record, "Using Mod Directory := <name>"; the record markers and other lines are ignored.
    constexpr std::string_view kKey = "using mod directory";
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string_view::npos) end = text.size();
        const std::string_view line = text.substr(start, end - start);
        start = end + 1;
        const size_t key = lowerSlashed(line).find(kKey), sep = line.find(":=");
        if (key == std::string::npos || sep == std::string_view::npos || sep < key) continue;
        std::string_view value = line.substr(sep + 2);
        auto blank = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '/' || c == '\\'; };
        while (!value.empty() && blank(value.front())) value.remove_prefix(1);
        while (!value.empty() && blank(value.back())) value.remove_suffix(1);
        if (value.empty() || lowerSlashed(value) == "none") return {};
        return std::string(value);
    }
    return {};
}

void InstallFiles::addLayer(const std::filesystem::path& folder, std::string name) {
    Layer layer{std::move(name), indexFolder(folder)};
    log::info("Mod files: {} ({} files from {})", layer.name, layer.index.size(), folder.string());
    layers_.push_back(std::move(layer));
}

std::optional<std::filesystem::path> InstallFiles::findInLayers(const std::string& key) const {
    for (auto it = layers_.rbegin(); it != layers_.rend(); ++it)
        if (const auto hit = it->index.find(key); hit != it->index.end()) return hit->second;
    return std::nullopt;
}

std::optional<std::filesystem::path> InstallFiles::find(std::string_view relative) const {
    const std::string key = lowerSlashed(relative);
    if (auto p = findInLayers(key)) return p;
    const auto it = index_.find(key);
    if (it == index_.end()) return std::nullopt;
    return it->second;
}

namespace {

// The keys to try for a file asked for by name: `preferred` (".png") with the
// same base name first, then the name as given.
std::vector<std::string> variantKeys(std::string_view relative, std::string_view preferred) {
    const std::string key = lowerSlashed(relative);
    const size_t slash = key.rfind('/');
    const size_t dot = key.rfind('.');
    const bool hasExt = dot != std::string::npos && (slash == std::string::npos || dot > slash);
    const std::string stem = hasExt ? key.substr(0, dot) : key;
    if (hasExt && std::string_view(key).substr(dot) == preferred) return {key};
    return {stem + std::string(preferred), key};
}

} // namespace

std::optional<std::filesystem::path> InstallFiles::findFirstOf(std::span<const std::string> keys, bool layers, bool* fromInstall) const {
    if (fromInstall) *fromInstall = false;
    if (layers)
        for (auto it = layers_.rbegin(); it != layers_.rend(); ++it)
            for (const std::string& key : keys)
                if (const auto hit = it->index.find(key); hit != it->index.end()) return hit->second;
    for (const std::string& key : keys)
        if (const auto it = index_.find(key); it != index_.end()) {
            if (fromInstall) *fromInstall = true;
            return it->second;
        }
    return std::nullopt;
}

std::optional<std::filesystem::path> InstallFiles::findPicture(std::string_view relative, bool* fromInstall) const {
    return findFirstOf(variantKeys(relative, ".png"), true, fromInstall);
}

std::optional<std::filesystem::path> InstallFiles::findSound(std::string_view relative) const {
    return findFirstOf(variantKeys(relative, ".ogg"), true, nullptr);
}

std::optional<std::filesystem::path> InstallFiles::findSoundAmong(std::span<const std::string> names) const {
    std::vector<std::string> keys;
    for (const std::string& n : names)
        for (std::string& k : variantKeys(n, ".ogg")) keys.push_back(std::move(k));
    return findFirstOf(keys, true, nullptr);
}

std::optional<std::filesystem::path> InstallFiles::findInstalledPicture(std::string_view relative) const {
    return findFirstOf(variantKeys(relative, ".png"), false, nullptr);
}

std::vector<std::string> InstallFiles::layerFiles(std::string_view folder) const {
    std::string prefix = lowerSlashed(folder);
    if (!prefix.empty() && prefix.back() != '/') prefix += '/';
    std::map<std::string, std::string> found;  // lowercase -> as spelled (the later layer's)
    for (const Layer& layer : layers_)
        for (const auto& [key, real] : layer.index) {
            if (!key.starts_with(prefix)) continue;
            // The spelling on disk: the real path's last parts.
            std::string spelled = key;
            const std::string generic = real.generic_string();
            if (generic.size() >= key.size()) spelled = generic.substr(generic.size() - key.size());
            found[key] = spelled;
        }
    std::vector<std::string> out;
    out.reserve(found.size());
    for (auto& [key, spelled] : found) out.push_back(std::move(spelled));
    return out;
}

std::optional<std::filesystem::path> InstallFiles::findModFirst(std::string_view relative) const {
    const std::string key = lowerSlashed(relative);
    if (auto p = findInLayers(key)) return p;
    if (!mod_.empty())
        if (const auto it = index_.find(mod_ + "/" + key); it != index_.end()) return it->second;
    const auto it = index_.find(key);
    if (it == index_.end()) return std::nullopt;
    return it->second;
}

void InstallFiles::noteMissing(std::string_view relative) const {
    if (missing_.insert(lowerSlashed(relative)).second) log::info("Not in the installed game: {}", relative);
}

std::optional<std::filesystem::path> InstallFiles::findAny(std::initializer_list<std::string_view> candidates) const {
    for (std::string_view c : candidates)
        if (auto p = find(c)) return p;
    return std::nullopt;
}

} // namespace opense4::assets
