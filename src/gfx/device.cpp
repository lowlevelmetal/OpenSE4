#include "gfx/device.hpp"

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBIW_WINDOWS_UTF8  // file names are UTF-8, as on Linux
#define STBI_WRITE_NO_STDIO_FALLBACK
#include <stb_image_write.h>
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace opense4::gfx {

const char* backendName(Backend b) {
    switch (b) {
        case Backend::Vulkan: return "Vulkan";
        case Backend::OpenGL: return "OpenGL";
    }
    return "?";
}

bool writePng(const std::string& path, const Image& image) {
    if (image.width <= 0 || image.height <= 0) return false;
    return stbi_write_png(path.c_str(), image.width, image.height, 4, image.rgba.data(), image.width * 4) != 0;
}

} // namespace opense4::gfx
