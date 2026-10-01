#include "client/classic/pointers.hpp"

#include "core/log.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace opense4::client::classic {

namespace {

SDL_SystemCursor systemFallback(Pointer p) {
    switch (p) {
        case Pointer::Normal: return SDL_SYSTEM_CURSOR_DEFAULT;
        case Pointer::Hourglass: return SDL_SYSTEM_CURSOR_WAIT;
        case Pointer::Select: return SDL_SYSTEM_CURSOR_POINTER;
        case Pointer::Target: return SDL_SYSTEM_CURSOR_CROSSHAIR;
        default: return SDL_SYSTEM_CURSOR_MOVE;
    }
}

} // namespace

PointerSet& pointers() {
    static PointerSet set;
    return set;
}

PointerSet::~PointerSet() { release(); }

void PointerSet::release() {
    for (SDL_Cursor*& c : cursors_) {
        if (c) SDL_DestroyCursor(c);
        c = nullptr;
    }
    for (SDL_Cursor*& c : system_) {
        if (c) SDL_DestroyCursor(c);
        c = nullptr;
    }
    shown_.reset();
}

void PointerSet::load(const assets::InstallFiles& files) {
    release();
    int found = 0;
    for (size_t i = 0; i < kPointerCount; ++i) {
        const Pointer p = static_cast<Pointer>(i);
        images_[i].reset();
        const auto path = files.findModFirst("Pictures/Game/" + std::string(pointerFile(p)));
        if (!path) continue;
        std::string error;
        if (auto image = assets::loadCursor(*path, &error)) {
            images_[i] = std::move(*image);
            ++found;
        } else {
            log::warn("Pointer {}: {}", path->string(), error);
        }
    }
    if (found < int(kPointerCount)) log::info("Classic pointers: {} of {} found; the system's pointer stands in for the others", found, kPointerCount);
}

SDL_Cursor* PointerSet::cursorFor(Pointer p) {
    const size_t i = static_cast<size_t>(p);
    if (!images_[i]) {
        if (!system_[i]) system_[i] = SDL_CreateSystemCursor(systemFallback(p));
        return system_[i];
    }
    if (cursors_[i]) return cursors_[i];
    const assets::CursorImage& img = *images_[i];
    const int s = std::max(1, scale_);
    // Whole multiples of the art, each pixel repeated (no smoothing).
    std::vector<uint8_t> pixels(size_t(img.width * s) * size_t(img.height * s) * 4);
    for (int y = 0; y < img.height * s; ++y)
        for (int x = 0; x < img.width * s; ++x)
            std::memcpy(&pixels[(size_t(y) * size_t(img.width * s) + size_t(x)) * 4],
                        &img.rgba[(size_t(y / s) * size_t(img.width) + size_t(x / s)) * 4], 4);
    SDL_Surface* surface = SDL_CreateSurfaceFrom(img.width * s, img.height * s, SDL_PIXELFORMAT_RGBA32, pixels.data(), img.width * s * 4);
    if (!surface) return nullptr;
    cursors_[i] = SDL_CreateColorCursor(surface, img.hotX * s + s / 2, img.hotY * s + s / 2);
    SDL_DestroySurface(surface);
    if (!cursors_[i]) log::warn("Pointer {}: {}", pointerFile(p), SDL_GetError());
    return cursors_[i];
}

void PointerSet::show(Pointer p) {
    if (shown_ == p) return;
    if (SDL_Cursor* c = cursorFor(p)) {
        SDL_SetCursor(c);
        shown_ = p;
    }
}

void PointerSet::apply(int scale) {
    scale = std::clamp(scale, 1, 4);
    if (scale != scale_) {
        // A new size: the art pointers are made again.
        for (SDL_Cursor*& c : cursors_) {
            if (c) SDL_DestroyCursor(c);
            c = nullptr;
        }
        scale_ = scale;
        shown_.reset();
    }
    show(requested_);
    requested_ = Pointer::Normal;
}

void PointerSet::showBusy() {
    show(Pointer::Hourglass);
    SDL_PumpEvents();  // let the window system show it before the work starts
}

BusyPointer::BusyPointer() { pointers().showBusy(); }
BusyPointer::~BusyPointer() { pointers().request(Pointer::Normal); }

} // namespace opense4::client::classic
