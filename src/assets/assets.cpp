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
#define STBI_ONLY_BMP
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#include <stb_image.h>
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include <algorithm>
#include <cmath>
#include <cstring>

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

std::optional<Image> loadImage(const std::filesystem::path& path, bool blackIsTransparent) {
    int w = 0, h = 0, channels = 0;
    stbi_uc* data = stbi_load(path.string().c_str(), &w, &h, &channels, 4);
    if (!data) {
        log::warn("Cannot load image {}: {}", path.string(), stbi_failure_reason());
        return std::nullopt;
    }
    Image img;
    img.width = w;
    img.height = h;
    img.rgba.assign(data, data + static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
    stbi_image_free(data);
    if (blackIsTransparent)
        for (size_t i = 0; i < img.rgba.size(); i += 4)
            if (img.rgba[i] == 0 && img.rgba[i + 1] == 0 && img.rgba[i + 2] == 0) img.rgba[i + 3] = 0;
    return img;
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

InstallFiles::InstallFiles(std::filesystem::path root) : root_(std::move(root)) {
    std::error_code ec;
    for (auto it = std::filesystem::recursive_directory_iterator(root_, std::filesystem::directory_options::skip_permission_denied, ec);
         it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) break;
        if (!it->is_regular_file(ec)) continue;
        const auto rel = std::filesystem::relative(it->path(), root_, ec);
        index_.emplace(lowerSlashed(rel.generic_string()), it->path());
    }
}

std::optional<std::filesystem::path> InstallFiles::find(std::string_view relative) const {
    const auto it = index_.find(lowerSlashed(relative));
    if (it == index_.end()) return std::nullopt;
    return it->second;
}

std::optional<std::filesystem::path> InstallFiles::findAny(std::initializer_list<std::string_view> candidates) const {
    for (std::string_view c : candidates)
        if (auto p = find(c)) return p;
    return std::nullopt;
}

} // namespace opense4::assets
