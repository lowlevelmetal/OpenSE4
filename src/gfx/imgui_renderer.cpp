#include "gfx/imgui_renderer.hpp"

#include <imgui.h>

#include <cassert>
#include <cmath>

namespace opense4::gfx {

ImGuiRenderer::ImGuiRenderer(Device& device) : device_(device) {
    ImGuiIO& io = ImGui::GetIO();
    io.BackendRendererName = "opense4-rhi";
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures;
}

ImGuiRenderer::~ImGuiRenderer() {
    for (ImTextureData* tex : ImGui::GetPlatformIO().Textures) {
        if (tex->RefCount == 1 && tex->TexID != ImTextureID_Invalid) {
            device_.destroyTexture(TextureId{static_cast<uint32_t>(tex->TexID)});
            tex->SetTexID(ImTextureID_Invalid);
            tex->SetStatus(ImTextureStatus_Destroyed);
        }
    }
    ImGuiIO& io = ImGui::GetIO();
    io.BackendRendererName = nullptr;
    io.BackendFlags &= ~(ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures);
}

void ImGuiRenderer::updateTexture(ImTextureData* tex) {
    if (tex->Status == ImTextureStatus_WantCreate) {
        assert(tex->Format == ImTextureFormat_RGBA32);
        const TextureId id = device_.createTexture(TextureDesc{tex->Width, tex->Height, Filter::Linear, "imgui"}, tex->GetPixels());
        tex->SetTexID(static_cast<ImTextureID>(id.value));
        tex->SetStatus(ImTextureStatus_OK);
    } else if (tex->Status == ImTextureStatus_WantUpdates) {
        // One upload of the bounding box instead of one per glyph rect: ImGui keeps the
        // whole atlas in CPU memory, so the untouched pixels inside it are current too.
        const TextureId id{static_cast<uint32_t>(tex->TexID)};
        const ImTextureRect& r = tex->UpdateRect;
        if (r.w > 0 && r.h > 0) device_.updateTexture(id, r.x, r.y, r.w, r.h, tex->GetPixelsAt(r.x, r.y), tex->GetPitch());
        tex->SetStatus(ImTextureStatus_OK);
    } else if (tex->Status == ImTextureStatus_WantDestroy && tex->UnusedFrames > 0) {
        device_.destroyTexture(TextureId{static_cast<uint32_t>(tex->TexID)});
        tex->SetTexID(ImTextureID_Invalid);
        tex->SetStatus(ImTextureStatus_Destroyed);
    }
}

void ImGuiRenderer::render(ImDrawData* data) {
    if (!data || data->DisplaySize.x <= 0.0f || data->DisplaySize.y <= 0.0f) return;
    if (data->Textures)
        for (ImTextureData* tex : *data->Textures)
            if (tex->Status != ImTextureStatus_OK) updateTexture(tex);

    const ImVec2 pos = data->DisplayPos;
    const ImVec2 size = data->DisplaySize;
    const ImVec2 scale = data->FramebufferScale;
    const Mat4 transform = Mat4::ortho2D(pos.x, pos.x + size.x, pos.y, pos.y + size.y);

    vertices_.clear();
    indices_.clear();
    commands_.clear();
    for (const ImDrawList* list : data->CmdLists) {
        const auto vertexBase = static_cast<uint32_t>(vertices_.size());
        const auto indexBase = static_cast<uint32_t>(indices_.size());
        for (const ImDrawVert& v : list->VtxBuffer) vertices_.push_back(Vertex{v.pos.x, v.pos.y, v.uv.x, v.uv.y, v.col, 0.0f, 0.0f});
        indices_.insert(indices_.end(), list->IdxBuffer.begin(), list->IdxBuffer.end());

        for (const ImDrawCmd& cmd : list->CmdBuffer) {
            if (cmd.UserCallback) {
                if (cmd.UserCallback != ImGui::GetPlatformIO().DrawCallback_ResetRenderState) cmd.UserCallback(list, &cmd);
                continue;
            }
            const float x0 = (cmd.ClipRect.x - pos.x) * scale.x;
            const float y0 = (cmd.ClipRect.y - pos.y) * scale.y;
            const float x1 = (cmd.ClipRect.z - pos.x) * scale.x;
            const float y1 = (cmd.ClipRect.w - pos.y) * scale.y;
            if (x1 <= x0 || y1 <= y0) continue;
            DrawCommand dc;
            dc.texture = TextureId{static_cast<uint32_t>(cmd.GetTexID())};
            dc.firstIndex = indexBase + cmd.IdxOffset;
            dc.indexCount = cmd.ElemCount;
            dc.vertexOffset = static_cast<int32_t>(vertexBase + cmd.VtxOffset);
            dc.scissor = Scissor{static_cast<int32_t>(std::floor(x0)), static_cast<int32_t>(std::floor(y0)),
                                 static_cast<uint32_t>(std::ceil(x1) - std::floor(x0)), static_cast<uint32_t>(std::ceil(y1) - std::floor(y0))};
            commands_.push_back(dc);
        }
    }
    device_.draw(DrawBatch{transform, vertices_, indices_, commands_});
}

} // namespace opense4::gfx
