#pragma once

#include "gfx/device.hpp"

#include <vector>

struct ImDrawData;
struct ImTextureData;

namespace opense4::gfx {

// Renders Dear ImGui draw data through gfx::Device, so the UI works unchanged
// on every backend. Implements ImGui 1.92's dynamic texture protocol
// (ImGuiBackendFlags_RendererHasTextures) for on-demand font atlas updates.
class ImGuiRenderer {
public:
    explicit ImGuiRenderer(Device& device);
    ~ImGuiRenderer();
    ImGuiRenderer(const ImGuiRenderer&) = delete;
    ImGuiRenderer& operator=(const ImGuiRenderer&) = delete;

    void render(ImDrawData* data);

private:
    void updateTexture(ImTextureData* tex);

    Device& device_;
    std::vector<Vertex> vertices_;
    std::vector<uint32_t> indices_;
    std::vector<DrawCommand> commands_;
};

} // namespace opense4::gfx
