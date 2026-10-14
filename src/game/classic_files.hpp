#pragma once

// The original's empire files (`Empires\*.emp`) and map files (`Maps\*.map`)
// as OpenSE4's empires of Game Setup and maps (docs/spec/08 §13, §14). The
// files themselves are decoded and encoded by classic_save.hpp
// (decodeClassicEmpire, decodeClassicMap); this converts them:
//
//   importClassicEmpire / exportClassicEmpire   ClassicEmpireFile <-> EmpireSetup
//   importClassicMap / exportClassicMap         ClassicMapFile <-> QuadrantMap
//   read... / write...                          the same, on files
//
// OpenSE4's own TOML empire files and maps stay the formats it saves in;
// these sit beside them (Game Setup's Add Existing and Load Map list both;
// the Save Empire and Save Map windows offer "Save for SE IV").

#include "game/classic_save.hpp"
#include "game/map_file.hpp"
#include "game/setup.hpp"

#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace opense4::game {
class Rules;
}

namespace opense4::game::classic {

inline constexpr std::string_view kEmpireFileExtension = ".emp";
inline constexpr std::string_view kMapFileExtension = ".map";

// ---- Empire files (§13) ------------------------------------------------------------------------------

// An empire file as an empire of Game Setup (§13.6): identity, race, password
// (hashed), e-mail, minister style, experience, strategies, designs, settings
// and name lists. The data set must have the file's number of racial traits
// (the decoder already needed it); a design whose hull or parts lie past the
// data set's records is left out with a note.
std::expected<EmpireSetup, std::string> importClassicEmpire(const Rules& r, const ClassicEmpireFile& file, ConversionReport& report);

struct EmpireExportOptions {
    uint64_t keySeed = 0;    // draws the keys (drawKeys)
    uint8_t player = 1;      // the player number the record names: its place in the game or the list
    bool designs = true;     // Save Designs: with the designs the empire holds
};
// An empire of Game Setup as an empire file (§13.2, §13.6), with computer
// control off. Fails on what the format cannot hold.
std::expected<ClassicEmpireFile, std::string> exportClassicEmpire(const Rules& r, const EmpireSetup& e, ConversionReport& report,
                                                                  const EmpireExportOptions& options = {});

// Add Existing's checks against the setup on the screen (§13.4, confirmed:
// binary): the empire's tech levels (`techLevels`: an empire file's; empty
// for OpenSE4's own files, which hold none) and its racial points. The
// message for the "Invalid Empire" box, or nullopt when the empire may join.
std::optional<std::string> addExistingProblem(const Rules& r, const GameOptions& o, const EmpireSetup& e, std::span<const uint16_t> techLevels);
// The two sums of the technology check: the empire's levels, each capped at
// its area's maximum, and what a new player gets in the allowed areas.
int64_t empireTechSum(const Rules& r, std::span<const uint16_t> techLevels);
int64_t allowedTechSum(const Rules& r, const GameOptions& o);

struct LoadedClassicEmpire {
    ClassicEmpireFile file;
    EmpireSetup empire;
};
// Reads and imports an empire file; a file that does not decode with the
// data set's trait count says with which counts it does.
std::expected<LoadedClassicEmpire, std::string> readClassicEmpire(const Rules& r, const std::filesystem::path& file, ConversionReport& report);
std::expected<void, std::string> writeClassicEmpire(const Rules& r, const EmpireSetup& e, const std::filesystem::path& file,
                                                    ConversionReport& report, const EmpireExportOptions& options = {});

// ---- Map files (§14) ---------------------------------------------------------------------------------

// A map file as OpenSE4's map (§14.5). The map is named `name` (the format
// holds no name; callers give the file's).
std::expected<QuadrantMap, std::string> importClassicMap(const ruleset::Ruleset& rs, const ClassicMapFile& file, ConversionReport& report,
                                                         std::string name = {});
// OpenSE4's map as a map file (§14.5): the systems, the starting points and
// the stellar objects each system lists.
std::expected<ClassicMapFile, std::string> exportClassicMap(const ruleset::Ruleset& rs, const QuadrantMap& map, ConversionReport& report,
                                                            uint64_t keySeed = 0);

std::expected<QuadrantMap, std::string> readClassicMap(const ruleset::Ruleset& rs, const std::filesystem::path& file, ConversionReport& report);
std::expected<void, std::string> writeClassicMap(const ruleset::Ruleset& rs, const QuadrantMap& map, const std::filesystem::path& file,
                                                 ConversionReport& report, uint64_t keySeed = 0);

// The file name Save for SE IV gives a name: its characters a file name may
// hold, in lower case as the original writes its paths (§13.5, §14.3), with
// the extension.
std::string classicFileName(std::string_view name, std::string_view extension);

} // namespace opense4::game::classic
