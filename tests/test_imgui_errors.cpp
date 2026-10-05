// Dear ImGui's recoverable errors (client/ui/imgui_errors.hpp): logged once
// each, never a tooltip or an assert in a release build, and the error the
// Log window's details pane made (issue #9) is one.

#include "client/ui/imgui_errors.hpp"

#include <doctest/doctest.h>

#include <imgui.h>

using namespace opense4;

namespace {

// A Dear ImGui context of its own, with a font atlas and a display, for frames without a renderer.
struct Context {
    ImGuiContext* previous = ImGui::GetCurrentContext();
    ImGuiContext* ctx = ImGui::CreateContext();
    Context() {
        ImGui::SetCurrentContext(ctx);
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = ImVec2(800, 600);
        io.DeltaTime = 1.0f / 60.0f;
        // The font atlas is made as frames need it, as with a renderer that takes textures.
        io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
        io.Fonts->AddFontDefault();
    }
    ~Context() {
        ImGui::DestroyContext(ctx);
        ImGui::SetCurrentContext(previous);
    }
    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;
};

// One frame of a window whose child places the cursor past its content and
// submits nothing after it.
void frameWithCursorPastContent() {
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(10, 10));
    ImGui::SetNextWindowSize(ImVec2(300, 200));
    ImGui::Begin("Errors");
    ImGui::BeginChild("##details", ImVec2(200, 100));
    ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, ImGui::GetCursorScreenPos().y + 150));
    ImGui::EndChild();
    ImGui::End();
    ImGui::EndFrame();
}

} // namespace

TEST_CASE("Dear ImGui errors: a release build logs each one once, with no tooltip or assert") {
    Context c;
    ImGuiIO& io = ImGui::GetIO();
    client::setupImGuiErrors(io, true, false);
    CHECK_FALSE(io.ConfigErrorRecoveryEnableAssert);
    CHECK_FALSE(io.ConfigErrorRecoveryEnableTooltip);
    CHECK_FALSE(io.ConfigDebugHighlightIdConflicts);
    CHECK(io.ConfigErrorRecovery);
    const size_t before = client::imguiErrorCount();
    // The error repeats every frame while its cause lasts: logged once.
    for (int i = 0; i < 3; ++i) frameWithCursorPastContent();
    CHECK(client::imguiErrorCount() == before + 1);
    const std::string last = client::lastImGuiError();
    CHECK(last.find("##details") != std::string::npos);
    CHECK(last.find("SetCursorPos") != std::string::npos);
    CHECK(last.find('\n') == std::string::npos);
}

TEST_CASE("Dear ImGui errors: debug builds keep the tooltip and assert, input scripts fail instead of asserting") {
    Context c;
    ImGuiIO& io = ImGui::GetIO();
    client::setupImGuiErrors(io, false, false);
    CHECK(io.ConfigErrorRecoveryEnableAssert);
    CHECK(io.ConfigErrorRecoveryEnableTooltip);
    client::setupImGuiErrors(io, false, true);
    CHECK_FALSE(io.ConfigErrorRecoveryEnableAssert);
    CHECK(io.ConfigErrorRecoveryEnableTooltip);
}

TEST_CASE("Dear ImGui errors: the same window and message are noted once") {
    const std::string first = client::noteImGuiError("Test/##a", "Line one.\nLine two.");
    CHECK(first == "In window 'Test/##a': Line one. Line two.");
    CHECK(client::noteImGuiError("Test/##a", "Line one.\nLine two.").empty());
    CHECK_FALSE(client::noteImGuiError("Test/##b", "Line one.\nLine two.").empty());
}
