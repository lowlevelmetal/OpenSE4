#include "client/script/items.hpp"

#include "imgui_item_hook.h"

#include <imgui_internal.h>

#include <algorithm>
#include <utility>

namespace opense4::client::script {

namespace {

struct Registry {
    bool on = false;
    std::string scope;
    std::vector<Item> current;
    std::vector<Item> last;
};

Registry& registry() {
    static Registry r;
    return r;
}

const char* rootName() {
    const ImGuiContext* g = ImGui::GetCurrentContext();
    if (!g || !g->CurrentWindow) return "";
    const ImGuiWindow* root = g->CurrentWindow->RootWindow ? g->CurrentWindow->RootWindow : g->CurrentWindow;
    return root->Name ? root->Name : "";
}

// The part of [min, max] the current window shows.
bool visiblePart(ImVec2& min, ImVec2& max) {
    const ImGuiContext* g = ImGui::GetCurrentContext();
    if (g && g->CurrentWindow) {
        const ImRect clip = g->CurrentWindow->ClipRect;
        min.x = std::max(min.x, clip.Min.x);
        min.y = std::max(min.y, clip.Min.y);
        max.x = std::min(max.x, clip.Max.x);
        max.y = std::min(max.y, clip.Max.y);
    }
    return max.x > min.x && max.y > min.y;
}

void add(std::string_view label, ImVec2 min, ImVec2 max, bool disabled) {
    Registry& r = registry();
    if (!visiblePart(min, max)) return;
    r.current.push_back(Item{std::string(label), r.scope, rootName(), min, max, disabled});
}

// Dear ImGui's hook: a labelled item was just added; LastItemData holds it.
void onItem(ImGuiContext* ctx, ImGuiID id, const char* label, int /*flags*/) {
    if (!registry().on || !label || !ctx) return;
    const ImGuiLastItemData& last = ctx->LastItemData;
    if (last.ID != id) return;   // a window's own entry, or an item registered elsewhere
    if (std::string_view(label) == "##classic") return;   // a classic button reports its own label (reportItem)
    add(label, last.Rect.Min, last.Rect.Max, (last.ItemFlags & ImGuiItemFlags_Disabled) != 0);
}

} // namespace

std::string_view visibleLabel(std::string_view label) {
    const size_t hash = label.find("##");
    return hash == std::string_view::npos ? label : label.substr(0, hash);
}

namespace {

// A pattern with * (any run of characters) and ? (one character).
bool glob(std::string_view text, std::string_view pattern) {
    size_t t = 0, p = 0, star = std::string_view::npos, mark = 0;
    while (t < text.size()) {
        if (p < pattern.size() && (pattern[p] == '?' || pattern[p] == text[t])) {
            ++t;
            ++p;
        } else if (p < pattern.size() && pattern[p] == '*') {
            star = p++;
            mark = t;
        } else if (star != std::string_view::npos) {
            p = star + 1;
            t = ++mark;
        } else {
            return false;
        }
    }
    while (p < pattern.size() && pattern[p] == '*') ++p;
    return p == pattern.size();
}

} // namespace

bool labelMatches(std::string_view label, std::string_view wanted) {
    if (wanted.empty()) return false;
    // A pattern matches the label as shown (never a widget without one).
    if (wanted.find('*') != std::string_view::npos) {
        const std::string_view shown = visibleLabel(label);
        return !shown.empty() && glob(shown, wanted);
    }
    if (label == wanted) return true;
    const std::string_view shown = visibleLabel(label);
    if (!shown.empty() && shown == wanted) return true;
    // "##id" names a label "Text##id" or "Text###id" by its hidden part.
    if (wanted.starts_with("##")) {
        const size_t hash = label.find("##");
        if (hash != std::string_view::npos) {
            std::string_view hidden = label.substr(hash);
            if (hidden.starts_with("###")) hidden.remove_prefix(1);
            return hidden == wanted;
        }
    }
    return false;
}

void collectItems(bool on) {
    Registry& r = registry();
    r.on = on;
    opense4SetItemHook(on ? &onItem : nullptr);
    if (ImGuiContext* g = ImGui::GetCurrentContext()) g->TestEngineHookItems = on;
    if (!on) {
        r.current.clear();
        r.last.clear();
    }
}

bool collectingItems() { return registry().on; }

void reportItem(std::string_view label) {
    if (!registry().on) return;
    const ImGuiContext* g = ImGui::GetCurrentContext();
    if (!g) return;
    add(label, g->LastItemData.Rect.Min, g->LastItemData.Rect.Max, (g->LastItemData.ItemFlags & ImGuiItemFlags_Disabled) != 0);
}

void reportItem(std::string_view label, ImVec2 min, ImVec2 max, bool disabled) {
    if (!registry().on) return;
    add(label, min, max, disabled);
}

ItemScope::ItemScope(std::string_view scope) {
    Registry& r = registry();
    if (!r.on) return;
    active_ = true;
    previous_ = std::exchange(r.scope, std::string(scope));
}

ItemScope::~ItemScope() {
    if (active_) registry().scope = std::move(previous_);
}

void endItemFrame() {
    Registry& r = registry();
    if (!r.on) return;
    // Dear ImGui turns the hooks off with a new context; keep them on.
    if (ImGuiContext* g = ImGui::GetCurrentContext()) g->TestEngineHookItems = true;
    r.last = std::move(r.current);
    r.current.clear();
    r.scope.clear();
}

const std::vector<Item>& lastItems() { return registry().last; }

} // namespace opense4::client::script
