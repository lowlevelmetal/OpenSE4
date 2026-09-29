#pragma once

// Runtime access to the player's own installed copy of the classic game's
// pictures (see docs/CLEANROOM.md: nothing is copied into this project).
// File names in the original data disagree in case with the files on disk,
// so all lookups are case-insensitive.

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace opense4::assets {

struct Image {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgba;  // top row first, straight alpha

    bool empty() const { return width <= 0 || height <= 0; }
};

// Loads BMP/JPG/PNG. With `blackIsTransparent`, pure black pixels get alpha 0
// (the classic art has no alpha channel and uses black backgrounds).
std::optional<Image> loadImage(const std::filesystem::path& path, bool blackIsTransparent);

// Copies a w×h rectangle; out-of-range parts are transparent.
Image crop(const Image& src, int x, int y, int w, int h);

// True if the image has no visible pixel (e.g. an unused sprite-sheet cell).
bool isBlank(const Image& img);

class InstallFiles {
public:
    InstallFiles() = default;
    explicit InstallFiles(std::filesystem::path root);  // the game directory (holds Data/, Pictures/)

    bool valid() const { return !index_.empty(); }
    const std::filesystem::path& root() const { return root_; }

    // `relative` uses '/' separators, any case: "Pictures/Planets/Planets.bmp".
    std::optional<std::filesystem::path> find(std::string_view relative) const;
    // First existing file among several candidates.
    std::optional<std::filesystem::path> findAny(std::initializer_list<std::string_view> candidates) const;

private:
    std::filesystem::path root_;
    std::unordered_map<std::string, std::filesystem::path> index_;  // lowercase relative path -> real path
};

} // namespace opense4::assets
