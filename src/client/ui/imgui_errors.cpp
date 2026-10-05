#include "client/ui/imgui_errors.hpp"

#include "core/log.hpp"

#include <imgui.h>
#include <imgui_internal.h>

#include <format>
#include <mutex>
#include <set>
#include <utility>

namespace opense4::client {

namespace {

struct Seen {
    std::mutex lock;
    std::set<std::pair<std::string, std::string>> errors;
    std::string last;
};

Seen& seen() {
    static Seen s;
    return s;
}

// Dear ImGui's error callback: the window the error came from is the one
// being drawn. An error repeats every frame while its cause lasts, so each
// is logged once.
void onError(ImGuiContext* ctx, void*, const char* message) {
    const ImGuiWindow* window = ctx ? ctx->CurrentWindow : nullptr;
    const std::string line = noteImGuiError(window ? window->Name : "NULL", message ? message : "");
    if (!line.empty()) log::error("Dear ImGui: {}", line);
}

} // namespace

std::string noteImGuiError(std::string_view window, std::string_view message) {
    // Dear ImGui's messages run over two lines.
    std::string text(message);
    for (char& c : text)
        if (c == '\n') c = ' ';
    Seen& s = seen();
    const std::scoped_lock guard(s.lock);
    if (!s.errors.emplace(std::string(window), text).second) return {};
    s.last = std::format("In window '{}': {}", window, text);
    return s.last;
}

size_t imguiErrorCount() {
    Seen& s = seen();
    const std::scoped_lock guard(s.lock);
    return s.errors.size();
}

std::string lastImGuiError() {
    Seen& s = seen();
    const std::scoped_lock guard(s.lock);
    return s.last;
}

void setupImGuiErrors(ImGuiIO& io, bool forPlayers, bool scripted) {
    io.ConfigErrorRecovery = true;   // go on after the error, with the state Dear ImGui expects
    if (ImGuiContext* ctx = ImGui::GetCurrentContext()) {
        ctx->ErrorCallback = onError;
        ctx->ErrorCallbackUserData = nullptr;
    }
    if (forPlayers) {
        io.ConfigErrorRecoveryEnableAssert = false;
        io.ConfigErrorRecoveryEnableTooltip = false;
        io.ConfigErrorRecoveryEnableDebugLog = false;   // its own debug log window: ours is opense4.log
        io.ConfigDebugHighlightIdConflicts = false;
    } else if (scripted) {
        io.ConfigErrorRecoveryEnableAssert = false;
    }
}

} // namespace opense4::client
