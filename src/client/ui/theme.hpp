#pragma once

#include "client/fonts.hpp"

#include <filesystem>

namespace opense4::client {

// Loads the UI fonts from assets/fonts (falls back to ImGui's built-in font).
Fonts loadFonts(const std::filesystem::path& assetsDir);

// Dark sci-fi ImGui style, scaled for the display.
void applyTheme(float uiScale);

} // namespace opense4::client
