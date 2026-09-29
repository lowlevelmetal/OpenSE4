// OpenGL 3.3 core fallback backend. Mirrors the Vulkan backend's behavior
// exactly (same shader source, same premultiplied blending, UNORM framebuffer)
// so that the game looks identical on both.

#include "gfx/device.hpp"

#include "core/log.hpp"
#include "shaders/basic2d_frag.h"
#include "shaders/basic2d_vert.h"

#include <GL/glcorearb.h>
#include <SDL3/SDL.h>

#include <algorithm>
#include <cstring>
#include <format>
#include <utility>

namespace opense4::gfx {

namespace {

// --- Minimal function loader ------------------------------------------------------
#define OPENSE4_GL_FUNCTIONS(X)                                        \
    X(PFNGLGETSTRINGPROC, glGetString)                             \
    X(PFNGLGETINTEGERVPROC, glGetIntegerv)                         \
    X(PFNGLGETERRORPROC, glGetError)                               \
    X(PFNGLENABLEPROC, glEnable)                                   \
    X(PFNGLDISABLEPROC, glDisable)                                 \
    X(PFNGLBLENDFUNCSEPARATEPROC, glBlendFuncSeparate)             \
    X(PFNGLBLENDEQUATIONPROC, glBlendEquation)                     \
    X(PFNGLVIEWPORTPROC, glViewport)                               \
    X(PFNGLSCISSORPROC, glScissor)                                 \
    X(PFNGLCLEARCOLORPROC, glClearColor)                           \
    X(PFNGLCLEARPROC, glClear)                                     \
    X(PFNGLGENTEXTURESPROC, glGenTextures)                         \
    X(PFNGLDELETETEXTURESPROC, glDeleteTextures)                   \
    X(PFNGLBINDTEXTUREPROC, glBindTexture)                         \
    X(PFNGLTEXPARAMETERIPROC, glTexParameteri)                     \
    X(PFNGLTEXIMAGE2DPROC, glTexImage2D)                           \
    X(PFNGLTEXSUBIMAGE2DPROC, glTexSubImage2D)                     \
    X(PFNGLPIXELSTOREIPROC, glPixelStorei)                         \
    X(PFNGLACTIVETEXTUREPROC, glActiveTexture)                     \
    X(PFNGLCREATESHADERPROC, glCreateShader)                       \
    X(PFNGLSHADERSOURCEPROC, glShaderSource)                       \
    X(PFNGLCOMPILESHADERPROC, glCompileShader)                     \
    X(PFNGLGETSHADERIVPROC, glGetShaderiv)                         \
    X(PFNGLGETSHADERINFOLOGPROC, glGetShaderInfoLog)               \
    X(PFNGLDELETESHADERPROC, glDeleteShader)                       \
    X(PFNGLCREATEPROGRAMPROC, glCreateProgram)                     \
    X(PFNGLATTACHSHADERPROC, glAttachShader)                       \
    X(PFNGLLINKPROGRAMPROC, glLinkProgram)                         \
    X(PFNGLGETPROGRAMIVPROC, glGetProgramiv)                       \
    X(PFNGLGETPROGRAMINFOLOGPROC, glGetProgramInfoLog)             \
    X(PFNGLDELETEPROGRAMPROC, glDeleteProgram)                     \
    X(PFNGLUSEPROGRAMPROC, glUseProgram)                           \
    X(PFNGLGETUNIFORMLOCATIONPROC, glGetUniformLocation)           \
    X(PFNGLUNIFORMMATRIX4FVPROC, glUniformMatrix4fv)               \
    X(PFNGLUNIFORM1IPROC, glUniform1i)                             \
    X(PFNGLGENVERTEXARRAYSPROC, glGenVertexArrays)                 \
    X(PFNGLDELETEVERTEXARRAYSPROC, glDeleteVertexArrays)           \
    X(PFNGLBINDVERTEXARRAYPROC, glBindVertexArray)                 \
    X(PFNGLGENBUFFERSPROC, glGenBuffers)                           \
    X(PFNGLDELETEBUFFERSPROC, glDeleteBuffers)                     \
    X(PFNGLBINDBUFFERPROC, glBindBuffer)                           \
    X(PFNGLBUFFERDATAPROC, glBufferData)                           \
    X(PFNGLENABLEVERTEXATTRIBARRAYPROC, glEnableVertexAttribArray) \
    X(PFNGLVERTEXATTRIBPOINTERPROC, glVertexAttribPointer)         \
    X(PFNGLDRAWELEMENTSBASEVERTEXPROC, glDrawElementsBaseVertex)   \
    X(PFNGLREADPIXELSPROC, glReadPixels)                           \
    X(PFNGLFINISHPROC, glFinish)

struct GlFunctions {
#define OPENSE4_DECLARE(type, name) type name = nullptr;
    OPENSE4_GL_FUNCTIONS(OPENSE4_DECLARE)
#undef OPENSE4_DECLARE
    PFNGLDEBUGMESSAGECALLBACKPROC glDebugMessageCallback = nullptr;  // optional (GL 4.3 / KHR_debug)

    // Returns the name of the first missing function, or nullptr on success.
    const char* load() {
#define OPENSE4_LOAD(type, name)                                                   \
    name = reinterpret_cast<type>(SDL_GL_GetProcAddress(#name));               \
    if (!name) return #name;
        OPENSE4_GL_FUNCTIONS(OPENSE4_LOAD)
#undef OPENSE4_LOAD
        glDebugMessageCallback = reinterpret_cast<PFNGLDEBUGMESSAGECALLBACKPROC>(SDL_GL_GetProcAddress("glDebugMessageCallback"));
        return nullptr;
    }
};

void APIENTRY debugCallback(GLenum, GLenum type, GLuint, GLenum severity, GLsizei, const GLchar* message, const void*) {
    if (severity == GL_DEBUG_SEVERITY_NOTIFICATION) return;
    if (type == GL_DEBUG_TYPE_ERROR) log::error("[opengl] {}", message);
    else log::warn("[opengl] {}", message);
}

class OpenGLDevice final : public Device {
public:
    OpenGLDevice(SDL_Window* window, const DeviceOptions& options) : window_(window), vsync_(options.vsync) {}

    ~OpenGLDevice() override {
        if (!context_) return;
        SDL_GL_MakeCurrent(window_, context_);
        if (gl_.glDeleteTextures) {
            for (GLuint t : textures_)
                if (t) gl_.glDeleteTextures(1, &t);
            if (program_) gl_.glDeleteProgram(program_);
            if (vbo_) gl_.glDeleteBuffers(1, &vbo_);
            if (ibo_) gl_.glDeleteBuffers(1, &ibo_);
            if (vao_) gl_.glDeleteVertexArrays(1, &vao_);
        }
        SDL_GL_DestroyContext(context_);
    }

    bool init(bool validation, std::string& error) {
        context_ = SDL_GL_CreateContext(window_);
        if (!context_) {
            error = std::format("SDL_GL_CreateContext failed: {}", SDL_GetError());
            return false;
        }
        SDL_GL_MakeCurrent(window_, context_);
        if (const char* missing = gl_.load()) {
            error = std::format("OpenGL function {} is unavailable", missing);
            return false;
        }
        GLint major = 0, minor = 0;
        gl_.glGetIntegerv(GL_MAJOR_VERSION, &major);
        gl_.glGetIntegerv(GL_MINOR_VERSION, &minor);
        if (major * 10 + minor < 33) {
            error = std::format("OpenGL 3.3 required, driver provides {}.{}", major, minor);
            return false;
        }
        deviceName_ = std::format("{} (OpenGL {})", reinterpret_cast<const char*>(gl_.glGetString(GL_RENDERER)),
                                  reinterpret_cast<const char*>(gl_.glGetString(GL_VERSION)));

        if (validation && gl_.glDebugMessageCallback) {
            gl_.glEnable(GL_DEBUG_OUTPUT);
            gl_.glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
            gl_.glDebugMessageCallback(debugCallback, nullptr);
        }
        applyVSync();

        if (!createProgram(error)) return false;

        gl_.glGenVertexArrays(1, &vao_);
        gl_.glGenBuffers(1, &vbo_);
        gl_.glGenBuffers(1, &ibo_);
        gl_.glBindVertexArray(vao_);
        gl_.glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        gl_.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo_);
        auto attrib = [&](GLuint loc, GLint count, GLenum type, GLboolean normalized, size_t offset) {
            gl_.glEnableVertexAttribArray(loc);
            gl_.glVertexAttribPointer(loc, count, type, normalized, sizeof(Vertex), reinterpret_cast<const void*>(offset));
        };
        attrib(0, 2, GL_FLOAT, GL_FALSE, offsetof(Vertex, x));
        attrib(1, 2, GL_FLOAT, GL_FALSE, offsetof(Vertex, u));
        attrib(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, offsetof(Vertex, color));
        attrib(3, 2, GL_FLOAT, GL_FALSE, offsetof(Vertex, mode));
        gl_.glBindVertexArray(0);

        const uint32_t white = 0xffffffffu;
        whiteTexture_ = createTexture(TextureDesc{1, 1, Filter::Nearest, "white"}, &white);
        return true;
    }

    Backend backend() const override { return Backend::OpenGL; }
    const std::string& deviceName() const override { return deviceName_; }

    TextureId createTexture(const TextureDesc& desc, const void* rgba) override {
        GLuint tex = 0;
        gl_.glGenTextures(1, &tex);
        gl_.glBindTexture(GL_TEXTURE_2D, tex);
        const GLint filter = desc.filter == Filter::Linear ? GL_LINEAR : GL_NEAREST;
        gl_.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
        gl_.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
        gl_.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        gl_.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        gl_.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
        gl_.glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        gl_.glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        std::vector<uint8_t> zeros;  // same zeroed start as the Vulkan backend
        if (!rgba) {
            zeros.assign(static_cast<size_t>(desc.width) * static_cast<size_t>(desc.height) * 4, 0);
            rgba = zeros.data();
        }
        gl_.glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, desc.width, desc.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);

        uint32_t slot;
        if (!freeSlots_.empty()) {
            slot = freeSlots_.back();
            freeSlots_.pop_back();
            textures_[slot] = tex;
        } else {
            slot = static_cast<uint32_t>(textures_.size());
            textures_.push_back(tex);
        }
        return TextureId{slot + 1};
    }

    void updateTexture(TextureId id, int x, int y, int width, int height, const void* rgba, int pitchBytes) override {
        const GLuint tex = lookup(id);
        if (!tex || width <= 0 || height <= 0) return;
        gl_.glBindTexture(GL_TEXTURE_2D, tex);
        gl_.glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        gl_.glPixelStorei(GL_UNPACK_ROW_LENGTH, pitchBytes / 4);
        gl_.glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, width, height, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
        gl_.glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    }

    void destroyTexture(TextureId id) override {
        const GLuint tex = lookup(id);
        if (!tex || id == whiteTexture_) return;
        gl_.glDeleteTextures(1, &tex);  // GL defers the actual free until the GPU is done
        textures_[id.value - 1] = 0;
        freeSlots_.push_back(id.value - 1);
    }

    std::optional<FrameInfo> beginFrame(Color clear) override {
        int w = 0, h = 0;
        SDL_GetWindowSizeInPixels(window_, &w, &h);
        if (w <= 0 || h <= 0 || (SDL_GetWindowFlags(window_) & SDL_WINDOW_MINIMIZED)) return std::nullopt;
        width_ = static_cast<uint32_t>(w);
        height_ = static_cast<uint32_t>(h);

        gl_.glViewport(0, 0, w, h);
        gl_.glDisable(GL_SCISSOR_TEST);
        gl_.glClearColor(clear.r, clear.g, clear.b, clear.a);
        gl_.glClear(GL_COLOR_BUFFER_BIT);

        gl_.glEnable(GL_BLEND);
        gl_.glBlendEquation(GL_FUNC_ADD);
        gl_.glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        gl_.glDisable(GL_CULL_FACE);
        gl_.glDisable(GL_DEPTH_TEST);
        gl_.glDisable(GL_STENCIL_TEST);
        gl_.glEnable(GL_SCISSOR_TEST);
        gl_.glUseProgram(program_);
        gl_.glActiveTexture(GL_TEXTURE0);
        gl_.glBindVertexArray(vao_);
        inFrame_ = true;
        return FrameInfo{width_, height_};
    }

    void draw(const DrawBatch& batch) override {
        if (!inFrame_ || batch.indices.empty() || batch.vertices.empty()) return;

        // Vulkan-convention clip space has y pointing down; flip for OpenGL.
        Mat4 flip = Mat4::identity();
        flip.m[5] = -1.0f;
        const Mat4 transform = flip * batch.transform;
        gl_.glUniformMatrix4fv(transformLocation_, 1, GL_FALSE, transform.m.data());

        // Orphan-and-refill: the driver hands us fresh storage each time.
        gl_.glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        gl_.glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(batch.vertices.size_bytes()), batch.vertices.data(), GL_STREAM_DRAW);
        gl_.glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(batch.indices.size_bytes()), batch.indices.data(),
                         GL_STREAM_DRAW);

        GLuint bound = 0;
        for (const DrawCommand& dc : batch.commands) {
            if (dc.indexCount == 0) continue;
            const int32_t x0 = std::clamp(dc.scissor.x, 0, static_cast<int32_t>(width_));
            const int32_t y0 = std::clamp(dc.scissor.y, 0, static_cast<int32_t>(height_));
            const int32_t x1 = std::clamp(dc.scissor.x + static_cast<int32_t>(dc.scissor.width), 0, static_cast<int32_t>(width_));
            const int32_t y1 = std::clamp(dc.scissor.y + static_cast<int32_t>(dc.scissor.height), 0, static_cast<int32_t>(height_));
            if (x1 <= x0 || y1 <= y0) continue;
            gl_.glScissor(x0, static_cast<GLint>(height_) - y1, x1 - x0, y1 - y0);

            GLuint tex = lookup(dc.texture);
            if (!tex) tex = lookup(whiteTexture_);
            if (tex != bound) {
                gl_.glBindTexture(GL_TEXTURE_2D, tex);
                bound = tex;
            }
            gl_.glDrawElementsBaseVertex(GL_TRIANGLES, static_cast<GLsizei>(dc.indexCount), GL_UNSIGNED_INT,
                                         reinterpret_cast<const void*>(size_t{dc.firstIndex} * sizeof(uint32_t)), dc.vertexOffset);
        }
    }

    void endFrame() override {
        if (!inFrame_) return;
        inFrame_ = false;
        gl_.glBindVertexArray(0);
        if (captureRequested_) {
            captureRequested_ = false;
            Image img;
            img.width = static_cast<int>(width_);
            img.height = static_cast<int>(height_);
            img.rgba.resize(size_t{width_} * height_ * 4);
            gl_.glPixelStorei(GL_PACK_ALIGNMENT, 1);
            gl_.glReadPixels(0, 0, img.width, img.height, GL_RGBA, GL_UNSIGNED_BYTE, img.rgba.data());
            // OpenGL rows are bottom-up.
            const size_t row = size_t{width_} * 4;
            std::vector<uint8_t> tmp(row);
            for (size_t y = 0; y < height_ / 2; ++y) {
                uint8_t* a = img.rgba.data() + y * row;
                uint8_t* b = img.rgba.data() + (height_ - 1 - y) * row;
                std::memcpy(tmp.data(), a, row);
                std::memcpy(a, b, row);
                std::memcpy(b, tmp.data(), row);
            }
            for (size_t i = 3; i < img.rgba.size(); i += 4) img.rgba[i] = 255;
            captured_ = std::move(img);
        }
        SDL_GL_SwapWindow(window_);
    }

    void setVSync(bool enabled) override {
        vsync_ = enabled;
        applyVSync();
    }

    void requestCapture() override { captureRequested_ = true; }
    std::optional<Image> takeCapture() override { return std::exchange(captured_, std::nullopt); }
    void waitIdle() override { gl_.glFinish(); }

private:
    GLuint lookup(TextureId id) const {
        if (!id || id.value > textures_.size()) return 0;
        return textures_[id.value - 1];
    }

    void applyVSync() {
        // Prefer adaptive vsync (-1) when available.
        if (vsync_) {
            if (!SDL_GL_SetSwapInterval(-1)) SDL_GL_SetSwapInterval(1);
        } else {
            SDL_GL_SetSwapInterval(0);
        }
    }

    GLuint compile(GLenum stage, const char* body, std::string& error) {
        const char* sources[] = {"#version 330 core\n", body};
        const GLuint shader = gl_.glCreateShader(stage);
        gl_.glShaderSource(shader, 2, sources, nullptr);
        gl_.glCompileShader(shader);
        GLint ok = GL_FALSE;
        gl_.glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char buf[2048] = {};
            gl_.glGetShaderInfoLog(shader, sizeof buf, nullptr, buf);
            error = std::format("{} shader failed to compile: {}", stage == GL_VERTEX_SHADER ? "Vertex" : "Fragment", buf);
            gl_.glDeleteShader(shader);
            return 0;
        }
        return shader;
    }

    bool createProgram(std::string& error) {
        const GLuint vs = compile(GL_VERTEX_SHADER, shaders::basic2d_vert_glsl, error);
        if (!vs) return false;
        const GLuint fs = compile(GL_FRAGMENT_SHADER, shaders::basic2d_frag_glsl, error);
        if (!fs) {
            gl_.glDeleteShader(vs);
            return false;
        }
        program_ = gl_.glCreateProgram();
        gl_.glAttachShader(program_, vs);
        gl_.glAttachShader(program_, fs);
        gl_.glLinkProgram(program_);
        gl_.glDeleteShader(vs);
        gl_.glDeleteShader(fs);
        GLint ok = GL_FALSE;
        gl_.glGetProgramiv(program_, GL_LINK_STATUS, &ok);
        if (!ok) {
            char buf[2048] = {};
            gl_.glGetProgramInfoLog(program_, sizeof buf, nullptr, buf);
            error = std::format("Shader program failed to link: {}", buf);
            return false;
        }
        transformLocation_ = gl_.glGetUniformLocation(program_, "uTransform");
        gl_.glUseProgram(program_);
        gl_.glUniform1i(gl_.glGetUniformLocation(program_, "uTexture"), 0);
        return true;
    }

    SDL_Window* window_;
    SDL_GLContext context_ = nullptr;
    GlFunctions gl_;
    bool vsync_;
    std::string deviceName_;

    GLuint program_ = 0;
    GLint transformLocation_ = -1;
    GLuint vao_ = 0, vbo_ = 0, ibo_ = 0;
    std::vector<GLuint> textures_;
    std::vector<uint32_t> freeSlots_;
    TextureId whiteTexture_;

    uint32_t width_ = 0, height_ = 0;
    bool inFrame_ = false;
    bool captureRequested_ = false;
    std::optional<Image> captured_;
};

} // namespace

std::unique_ptr<Device> createOpenGLDevice(SDL_Window* window, const DeviceOptions& options, std::string& error) {
    auto device = std::make_unique<OpenGLDevice>(window, options);
    if (!device->init(options.validation, error)) return nullptr;
    return device;
}

} // namespace opense4::gfx
