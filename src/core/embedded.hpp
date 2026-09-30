#pragma once

// Our own resources built into the executable: the Noto Sans fonts and the
// prototype's data files. A release therefore runs without any file next to it;
// files on disk still win when they exist, so edits to data/ show up without a
// rebuild. Paths are relative to the source tree ("assets/fonts/NotoSans-Bold.ttf",
// "data/rules.toml"). Builds configured with OPENSE4_EMBED_RESOURCES=OFF have none.
// Nothing from the original game is ever embedded (docs/CLEANROOM.md).

#include <span>
#include <string_view>

namespace opense4 {

// The bytes of a built-in resource, or an empty span.
std::span<const unsigned char> embeddedResource(std::string_view path);

} // namespace opense4
