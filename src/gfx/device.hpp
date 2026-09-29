#pragma once

// Rendering hardware interface (RHI). A deliberately small abstraction sized
// for a 2D strategy game: textures plus batches of transient 2D geometry drawn
// with one premultiplied-alpha "uber" shader. Implemented by a Vulkan 1.3
// backend and an OpenGL 3.3 fallback that must look identical.

#include "core/math.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

struct SDL_Window;

namespace opense4::gfx {

enum class Backend { Vulkan, OpenGL };
const char* backendName(Backend b);

struct TextureId {
    uint32_t value = 0;  // 0 = invalid
    constexpr explicit operator bool() const { return value != 0; }
    constexpr bool operator==(const TextureId&) const = default;
};

enum class Filter { Linear, Nearest };

struct TextureDesc {
    int width = 0;
    int height = 0;
    Filter filter = Filter::Linear;
    const char* debugName = nullptr;
};

// How the fragment shader interprets a vertex (see shaders/basic2d.frag).
enum class ShapeMode : uint8_t {
    Textured = 0,  // texture * color
    Disc = 1,      // SDF disc/ring in uv [-1,1]; param = inner radius (0 = filled)
    Glow = 2,      // additive radial falloff; param = falloff exponent
    Line = 3,      // antialiased line; uv.y in [-1,1] across the line
};

// 28 bytes. Positions are transformed by DrawBatch::transform.
struct Vertex {
    float x, y;
    float u, v;
    uint32_t color;  // RGBA8, straight (non-premultiplied) alpha
    float mode;      // ShapeMode as float
    float param;
};
static_assert(sizeof(Vertex) == 28);

struct Scissor {
    int32_t x = 0, y = 0;
    uint32_t width = 0, height = 0;  // framebuffer pixels, top-left origin
};

struct DrawCommand {
    TextureId texture;  // invalid = built-in white texture
    uint32_t firstIndex = 0;
    uint32_t indexCount = 0;
    int32_t vertexOffset = 0;
    Scissor scissor;
};

struct DrawBatch {
    Mat4 transform;  // batch coordinates -> clip space (Vulkan convention, y down)
    std::span<const Vertex> vertices;
    std::span<const uint32_t> indices;
    std::span<const DrawCommand> commands;
};

struct Image {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgba;  // tightly packed, top row first
};

struct DeviceOptions {
    bool vsync = true;
    bool validation = false;  // Vulkan validation layers, GL debug output
};

struct FrameInfo {
    uint32_t width = 0;   // framebuffer size in pixels
    uint32_t height = 0;
};

class Device {
public:
    virtual ~Device() = default;

    virtual Backend backend() const = 0;
    virtual const std::string& deviceName() const = 0;

    // `rgba` may be null: the texture starts out transparent black.
    virtual TextureId createTexture(const TextureDesc& desc, const void* rgba) = 0;
    // `rgba` points at the first pixel of the region; rows are `pitchBytes` apart.
    // Update a texture before drawing with it in a frame, never after: Vulkan
    // uploads ahead of the frame's commands, OpenGL in call order.
    virtual void updateTexture(TextureId tex, int x, int y, int width, int height, const void* rgba, int pitchBytes) = 0;
    // Safe at any time; the GPU copy is released once no frame in flight uses it.
    virtual void destroyTexture(TextureId tex) = 0;

    // Returns nullopt when no frame can be rendered (e.g. minimized window).
    virtual std::optional<FrameInfo> beginFrame(Color clear) = 0;
    virtual void draw(const DrawBatch& batch) = 0;
    virtual void endFrame() = 0;

    virtual void setVSync(bool enabled) = 0;
    // Copies the next presented frame to CPU memory; fetch it with takeCapture().
    virtual void requestCapture() = 0;
    virtual std::optional<Image> takeCapture() = 0;

    virtual void waitIdle() = 0;
};

// Each factory returns nullptr and fills `error` if the backend cannot run.
// The window must have been created with the matching SDL window flag.
std::unique_ptr<Device> createVulkanDevice(SDL_Window* window, const DeviceOptions& options, std::string& error);
std::unique_ptr<Device> createOpenGLDevice(SDL_Window* window, const DeviceOptions& options, std::string& error);

bool writePng(const std::string& path, const Image& image);

} // namespace opense4::gfx
