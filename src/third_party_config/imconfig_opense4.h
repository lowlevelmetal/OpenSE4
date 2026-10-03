// Dear ImGui compile-time configuration for opense4 (see imconfig.h).
#pragma once

// 32-bit indices: our RHI only speaks uint32 index buffers.
#define ImDrawIdx unsigned int

#define IMGUI_DISABLE_OBSOLETE_FUNCTIONS

// Dear ImGui's item hooks: input scripts (docs/BUILDING.md "Input scripts")
// find widgets by their labels. The hooks are ours (imgui_item_hook.cpp) and
// do nothing until a script turns them on (ImGuiContext::TestEngineHookItems).
#define IMGUI_ENABLE_TEST_ENGINE
