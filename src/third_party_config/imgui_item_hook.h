// Dear ImGui's item hooks (IMGUI_ENABLE_TEST_ENGINE, imconfig_opense4.h),
// defined in imgui_item_hook.cpp as part of the imgui library. They forward
// each labelled item to one function that OpenSE4 sets (client/script/items.cpp);
// nothing is called while ImGuiContext::TestEngineHookItems is false.
#pragma once

#include "imgui.h"

struct ImGuiContext;

// The item just added (its label, Dear ImGui's status flags); the context's
// LastItemData describes it.
using OpenSE4ItemHook = void (*)(ImGuiContext* ctx, ImGuiID id, const char* label, int flags);
void opense4SetItemHook(OpenSE4ItemHook hook);
