#include "mods/generator.hpp"

#include <format>

namespace opense4::mods {

std::expected<script::Value, std::string> NoScriptRuntime::run(const GeneratorRequest& request) {
    return std::unexpected(std::format("{}: data generators need the script runtime, which this OpenSE4 does not have yet: "
                                       "write the records as a .toml patch for now",
                                       request.file));
}

std::shared_ptr<GeneratorRunner> defaultGeneratorRunner() { return std::make_shared<NoScriptRuntime>(); }

} // namespace opense4::mods
