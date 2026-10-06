#include "mods/generator.hpp"

#include <format>
#include <mutex>

namespace opense4::mods {

std::expected<script::Value, std::string> NoScriptRuntime::run(const GeneratorRequest& request) {
    return std::unexpected(std::format("{}: data generators need the script runtime, which this OpenSE4 does not have yet: "
                                       "write the records as a .toml patch for now",
                                       request.file));
}

namespace {

std::mutex& runnerMutex() {
    static std::mutex m;
    return m;
}
std::shared_ptr<GeneratorRunner>& installedRunner() {
    static std::shared_ptr<GeneratorRunner> r;
    return r;
}

} // namespace

std::shared_ptr<GeneratorRunner> defaultGeneratorRunner() {
    std::lock_guard lock(runnerMutex());
    if (installedRunner()) return installedRunner();
    return std::make_shared<NoScriptRuntime>();
}

void setDefaultGeneratorRunner(std::shared_ptr<GeneratorRunner> runner) {
    std::lock_guard lock(runnerMutex());
    installedRunner() = std::move(runner);
}

} // namespace opense4::mods
