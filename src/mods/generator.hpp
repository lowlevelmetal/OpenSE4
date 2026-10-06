#pragma once

// Python data generators (data/*.py, docs/MODDING_SDK.md §5): a script that
// builds records programmatically, such as twelve levels of a weapon line. It
// runs once when the data loads, in the sandbox, and returns an ordinary
// patch: a script::Value shaped like a patch file ({"components": {"add":
// [{"name": ..., "set": {...}}]}}), which is hashed into the mod's identity
// with its source and applied like a .toml patch.
//
// The script runtime (src/script/) plugs in as a GeneratorRunner. Until it
// does, NoScriptRuntime reports every generator as an error.

#include "script/value.hpp"

#include <expected>
#include <memory>
#include <string>

namespace opense4::mods {

struct GeneratorRequest {
    std::string mod;     // the mod's id
    std::string file;    // "data/weapons.py"
    std::string source;  // the script's text
};

class GeneratorRunner {
public:
    virtual ~GeneratorRunner() = default;
    // The patch the script returns, or why it could not run.
    virtual std::expected<script::Value, std::string> run(const GeneratorRequest& request) = 0;
};

// The runner without a script runtime: every generator is an error.
class NoScriptRuntime final : public GeneratorRunner {
public:
    std::expected<script::Value, std::string> run(const GeneratorRequest& request) override;
};

// The runner data sets load with when LoadOptions names none: the one
// setDefaultGeneratorRunner installed (the SDK's, sdk::installPlayers), else
// NoScriptRuntime.
std::shared_ptr<GeneratorRunner> defaultGeneratorRunner();
void setDefaultGeneratorRunner(std::shared_ptr<GeneratorRunner> runner);

} // namespace opense4::mods
