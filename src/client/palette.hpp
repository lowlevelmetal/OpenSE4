#pragma once

// Presentation colors and sizes for simulation entities.

#include "core/math.hpp"
#include "sim/types.hpp"

namespace opense4::client::palette {

inline constexpr Color kBackground = Color::hex(0x05070d);
inline constexpr Color kLane = Color::hex(0x5d7fa3, 0.55f);
inline constexpr Color kLaneUnknown = Color::hex(0x3a4a5e, 0.45f);
inline constexpr Color kGrid = Color::hex(0x2b3b52, 0.55f);
inline constexpr Color kGridBorder = Color::hex(0x4a6d91, 0.8f);
inline constexpr Color kOrbit = Color::hex(0x6d87a6, 0.16f);
inline constexpr Color kWarp = Color::hex(0xb07cff);
inline constexpr Color kSelection = Color::hex(0x7fe3ff);
inline constexpr Color kHover = Color::hex(0xffffff, 0.35f);
inline constexpr Color kPath = Color::hex(0x7fe3ff, 0.85f);
inline constexpr Color kUnexplored = Color::hex(0x55606e);

inline constexpr Color empireColor(uint32_t rgb, float alpha = 1.0f) { return Color::hex(rgb, alpha); }

inline constexpr Color starColor(sim::StarClass s) {
    switch (s) {
        case sim::StarClass::Blue: return Color::hex(0x9bb8ff);
        case sim::StarClass::White: return Color::hex(0xf2f5ff);
        case sim::StarClass::Yellow: return Color::hex(0xffe7a0);
        case sim::StarClass::Orange: return Color::hex(0xffbe73);
        case sim::StarClass::Red: return Color::hex(0xff8163);
        case sim::StarClass::WhiteDwarf: return Color::hex(0xdce7ff);
        case sim::StarClass::Neutron: return Color::hex(0xa5f0ff);
        case sim::StarClass::Count: break;
    }
    return Color{};
}

// Relative star size (1.0 = sun-like).
inline constexpr float starScale(sim::StarClass s) {
    switch (s) {
        case sim::StarClass::Blue: return 1.45f;
        case sim::StarClass::White: return 1.15f;
        case sim::StarClass::Yellow: return 1.0f;
        case sim::StarClass::Orange: return 0.92f;
        case sim::StarClass::Red: return 0.78f;
        case sim::StarClass::WhiteDwarf: return 0.5f;
        case sim::StarClass::Neutron: return 0.42f;
        case sim::StarClass::Count: break;
    }
    return 1.0f;
}

inline constexpr Color planetColor(sim::PlanetSurface surface, sim::Atmosphere air) {
    using sim::Atmosphere;
    switch (surface) {
        case sim::PlanetSurface::Rock:
            switch (air) {
                case Atmosphere::Oxygen: return Color::hex(0x4f9a6a);
                case Atmosphere::CarbonDioxide: return Color::hex(0xb5763f);
                case Atmosphere::Methane: return Color::hex(0x5c8f8c);
                case Atmosphere::Hydrogen: return Color::hex(0xa39062);
                default: return Color::hex(0x8a7f72);
            }
        case sim::PlanetSurface::Ice:
            switch (air) {
                case Atmosphere::Methane: return Color::hex(0x8fd0d4);
                case Atmosphere::Hydrogen: return Color::hex(0xa8b8e8);
                case Atmosphere::CarbonDioxide: return Color::hex(0xdfe6ea);
                case Atmosphere::Oxygen: return Color::hex(0xc8e8f0);
                default: return Color::hex(0xbfd7e6);
            }
        case sim::PlanetSurface::Gas:
            switch (air) {
                case Atmosphere::Methane: return Color::hex(0x6a9fd8);
                case Atmosphere::None: return Color::hex(0xc98f8f);
                default: return Color::hex(0xd8a86b);
            }
        case sim::PlanetSurface::Count: break;
    }
    return Color{};
}

inline constexpr Color atmosphereColor(sim::Atmosphere air) {
    switch (air) {
        case sim::Atmosphere::Oxygen: return Color::hex(0x7cc8ff, 0.35f);
        case sim::Atmosphere::Hydrogen: return Color::hex(0xffd9a8, 0.25f);
        case sim::Atmosphere::CarbonDioxide: return Color::hex(0xffb080, 0.3f);
        case sim::Atmosphere::Methane: return Color::hex(0x9fffe0, 0.3f);
        default: return Color::hex(0x000000, 0.0f);
    }
}

// Planet radius as a fraction of a sector.
inline constexpr float planetScale(sim::PlanetSize s) {
    switch (s) {
        case sim::PlanetSize::Tiny: return 0.13f;
        case sim::PlanetSize::Small: return 0.17f;
        case sim::PlanetSize::Medium: return 0.21f;
        case sim::PlanetSize::Large: return 0.26f;
        case sim::PlanetSize::Huge: return 0.32f;
        case sim::PlanetSize::Count: break;
    }
    return 0.2f;
}

} // namespace opense4::client::palette
