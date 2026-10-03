// Dear ImGui's test-engine hooks (IMGUI_ENABLE_TEST_ENGINE): built into the
// imgui library, so every program that links it has them. Only the item
// labels are used (imgui_item_hook.h).

#include "imgui_item_hook.h"

#include "imgui_internal.h"

namespace {
OpenSE4ItemHook g_itemHook = nullptr;
}

void opense4SetItemHook(OpenSE4ItemHook hook) { g_itemHook = hook; }

void ImGuiTestEngineHook_ItemAdd(ImGuiContext*, ImGuiID, const ImRect&, const ImGuiLastItemData*) {}

void ImGuiTestEngineHook_ItemInfo(ImGuiContext* ctx, ImGuiID id, const char* label, ImGuiItemStatusFlags flags) {
    if (g_itemHook) g_itemHook(ctx, id, label, static_cast<int>(flags));
}

void ImGuiTestEngineHook_Log(ImGuiContext*, const char*, ...) {}

const char* ImGuiTestEngine_FindItemDebugLabel(ImGuiContext*, ImGuiID) { return nullptr; }
