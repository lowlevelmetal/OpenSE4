#pragma once

// Runtime access to the player's own installed copy of the classic game's
// pictures (see docs/CLEANROOM.md: nothing is copied into this project).
// File names in the original data disagree in case with the files on disk,
// so all lookups are case-insensitive.

#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace opense4::assets {

struct Image {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgba;  // top row first, straight alpha

    bool empty() const { return width <= 0 || height <= 0; }
};

// What a picture file holds, by its first bytes (not its name: a ".bmp" may
// hold PNG data).
enum class ImageFormat { Unknown, Bmp, Png, Jpeg };
ImageFormat imageFormat(std::span<const uint8_t> head);
std::string_view formatName(ImageFormat f);  // "BMP", "PNG", "JPEG", "unknown"

// Loads BMP/JPG/PNG. With `blackIsTransparent`, pure black pixels get alpha 0
// (the classic art has no alpha channel and uses black backgrounds). A PNG
// keeps its own transparency: its alpha channel, and black is opaque in it
// (docs/sdk/packages-and-data.md "Pictures").
std::optional<Image> loadImage(const std::filesystem::path& path, bool blackIsTransparent);
std::optional<Image> loadImageMemory(std::span<const uint8_t> bytes, bool blackIsTransparent, std::string* error = nullptr);

// A picture file's size and format without decoding it all; the reason when
// it cannot be read.
struct ImageInfo {
    int width = 0;
    int height = 0;
    ImageFormat format = ImageFormat::Unknown;
    bool alpha = false;  // PNG with an alpha channel (or a transparent colour)
};
std::expected<ImageInfo, std::string> probeImage(const std::filesystem::path& path);
// Only the width and height, from the file's header.
std::optional<std::pair<int, int>> probeImageSize(const std::filesystem::path& path);

// The size a picture of the classic game has in its layout, by its kind
// (docs/sdk/packages-and-data.md "Larger pictures"): minis 36×36, portraits
// 128×128, population pictures 20×20 and 36×36, a race's Main.bmp 100×20,
// component, facility, planet, event and system pictures 128×128, combat
// tiles 72×72, the system panels' backgrounds and the intro pictures at their
// layout's size. Nullopt when the kind has no fixed size (sheets, frame
// pieces): the install's own copy of the picture gives its classic size.
std::optional<std::pair<int, int>> classicPictureSize(std::string_view relative);

// The picture resampled to w×h by averaging the source pixels each target
// pixel covers (an area filter: for making a larger picture smaller, with no
// ringing), weighted by alpha so that transparent pixels do not darken the
// edges. Deterministic, the same on every platform.
Image downscale(const Image& src, int w, int h);

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
    // A picture asked for by its classic name ("Pictures/Events/Plague.bmp"):
    // that file or a PNG with the same base name. In each layer (the mods'
    // from the last, then the install) the PNG comes first; a later layer wins
    // over an earlier one whatever their formats. `fromInstall`: set to whether
    // the file found is the install's own.
    std::optional<std::filesystem::path> findPicture(std::string_view relative, bool* fromInstall = nullptr) const;
    // A sound or a music track by its classic name ("Sounds/button.wav",
    // "Music/Track 01.mp3"): that file or an OGG Vorbis file with the same base
    // name, the OGG first in each layer, as findPicture.
    std::optional<std::filesystem::path> findSound(std::string_view relative) const;
    // The first of several names of a sound ("Sounds/New/button.wav", then
    // "Sounds/button.wav"), each with its OGG first, in the mods' layers before
    // the install: a mod's sound under either name wins over the install's.
    std::optional<std::filesystem::path> findSoundAmong(std::span<const std::string> names) const;
    // findPicture in the install alone, without the mods' layers: the classic
    // picture a mod's replaces (its size is the classic size, docs/sdk/packages-and-data.md).
    std::optional<std::filesystem::path> findInstalledPicture(std::string_view relative) const;
    // The files of the mods' layers under a folder ("Pictures/RaceGeneric"),
    // as paths relative to the layer with '/' and their spelling on disk,
    // each once (the later layer's spelling), sorted by lowercase path.
    std::vector<std::string> layerFiles(std::string_view folder) const;
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
    // The first of `keys` in each layer, then in the install.
    std::optional<std::filesystem::path> findFirstOf(std::span<const std::string> keys, bool layers, bool* fromInstall) const;

    std::filesystem::path root_;
    std::unordered_map<std::string, std::filesystem::path> index_;  // lowercase relative path -> real path
    std::vector<Layer> layers_;                                     // in load order
    std::string mod_;
    mutable std::set<std::string> missing_;  // lowercase relative paths noted missing
};

// The mod folder named by the text of a Path.txt (empty for "None" or none).
std::string modDirectoryFromPathTxt(std::string_view text);

} // namespace opense4::assets
