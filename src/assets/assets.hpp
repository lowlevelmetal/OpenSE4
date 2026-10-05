#pragma once

// Runtime access to the player's own installed copy of the classic game's
// pictures (see docs/CLEANROOM.md: nothing is copied into this project).
// File names in the original data disagree in case with the files on disk,
// so all lookups are case-insensitive.

#include <cstdint>
#include <filesystem>
#include <optional>
#include <set>
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

// The image turned clockwise about its centre by `degrees`, the same size,
// sampling the nearest source pixel with no smoothing (docs/spec/06 §2.4).
// Pixels whose source falls outside the picture come out black: transparent
// when `transparentOutside`, else opaque.
Image rotateNearest(const Image& src, double degrees, bool transparentOutside);

// True if the image has no visible pixel (e.g. an unused sprite-sheet cell).
bool isBlank(const Image& img);

// The install's files, with the pictures, sounds, music, fonts and pointers of
// mods layered over them (docs/sdk/packages-and-data.md): every lookup tries
// the mods, the last in load order first, then the install. Nothing is ever
// written into the install.
class InstallFiles {
public:
    InstallFiles() = default;
    explicit InstallFiles(std::filesystem::path root);  // the game directory (holds Data/, Pictures/)

    bool valid() const { return !index_.empty(); }
    const std::filesystem::path& root() const { return root_; }

    // A mod's folder in the game folder's layout ("Pictures/Races/...",
    // "Sounds/...", "Music/..."), over the install and the layers added
    // before it. `name` is for the log.
    void addLayer(const std::filesystem::path& folder, std::string name);
    size_t layerCount() const { return layers_.size(); }

    // `relative` uses '/' separators, any case: "Pictures/Planets/Planets.bmp".
    // The mods' layers first, then the install.
    std::optional<std::filesystem::path> find(std::string_view relative) const;
    // First existing file among several candidates.
    std::optional<std::filesystem::path> findAny(std::initializer_list<std::string_view> candidates) const;

    // The mod folder Path.txt names (`Using Mod Directory`; "None" or no
    // file: none), as a lowercase path relative to the root, and a lookup
    // that tries the mod's copy first and then the base tree, the way the
    // classic game finds its fonts and pointers (docs/spec/06 §5.1, §5.4,
    // §5.8). The mods' layers come before both.
    const std::string& modDirectory() const { return mod_; }
    std::optional<std::filesystem::path> findModFirst(std::string_view relative) const;

    // A file the game wanted is not in the install (the caller gives up on
    // it, or falls back to a stand-in): logged once per name and session.
    // Lookups ignore case, so a name logged here is really absent, on every
    // platform alike; probes that try several names report only their final
    // miss. Main thread only.
    void noteMissing(std::string_view relative) const;
    const std::set<std::string>& missing() const { return missing_; }

private:
    struct Layer {
        std::string name;
        std::unordered_map<std::string, std::filesystem::path> index;
    };
    std::optional<std::filesystem::path> findInLayers(const std::string& key) const;

    std::filesystem::path root_;
    std::unordered_map<std::string, std::filesystem::path> index_;  // lowercase relative path -> real path
    std::vector<Layer> layers_;                                     // in load order
    std::string mod_;
    mutable std::set<std::string> missing_;  // lowercase relative paths noted missing
};

// The mod folder named by the text of a Path.txt (empty for "None" or none).
std::string modDirectoryFromPathTxt(std::string_view text);

} // namespace opense4::assets
