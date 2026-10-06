// Data generators (docs/sdk/rules.md "Data generators"): a mod's data/*.py
// runs once when the data set loads, in the sandbox, and its generate()
// returns a patch, as a .toml patch file holds it. The same code gives the
// same patch on every computer (the runtime is deterministic), so the patch
// can be part of the game's identity.

#include "mods/manifest.hpp"
#include "script/runtime.hpp"
#include "sdk/rules.hpp"
#include "sdk/worker.hpp"

#include <chrono>
#include <format>
#include <thread>

namespace opense4::sdk {

namespace {

// A generator's limits: generous, as it runs once per load.
constexpr int64_t kGeneratorBudget = 500'000'000;
constexpr size_t kGeneratorHeap = size_t{64} << 20;
#if defined(__SANITIZE_ADDRESS__)
constexpr size_t kGeneratorCStack = size_t{1} << 20;
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
constexpr size_t kGeneratorCStack = size_t{1} << 20;
#else
constexpr size_t kGeneratorCStack = size_t{256} << 10;
#endif
#else
constexpr size_t kGeneratorCStack = size_t{256} << 10;
#endif

class ScriptGenerators final : public mods::GeneratorRunner {
public:
    std::expected<script::Value, std::string> run(const mods::GeneratorRequest& request) override {
        // data/weapons.py is the module data.weapons: its file and lines are
        // the traceback's.
        std::string_view file = request.file;
        const size_t slash = file.rfind('/');
        const std::string_view base = slash == std::string_view::npos ? file : file.substr(slash + 1);
        const std::string stem(base.substr(0, base.size() >= 3 && base.ends_with(".py") ? base.size() - 3 : base.size()));
        if (!mods::validPythonName(stem))
            return std::unexpected(std::format("{}: a generator's file name must be a Python name (letters, digits and '_', not starting with a digit)",
                                               request.file));
        const std::string path = "data/" + stem + ".py";
        const std::string module = "data." + stem;

        std::unique_lock slot(interpreterSlot(), std::defer_lock);
        if (!slot.try_lock_for(std::chrono::seconds(60)))
            return std::unexpected(std::format("{}: the script runtime is busy with a game; load the data again", request.file));
        std::expected<script::Value, std::string> out = std::unexpected(std::string());
        try {
            Worker worker;
            worker.run([&] { out = runOnWorker(request, path, module); });
        } catch (const std::exception& e) {
            return std::unexpected(std::format("{}: {}", request.file, e.what()));
        }
        return out;
    }

private:
    static std::expected<script::Value, std::string> runOnWorker(const mods::GeneratorRequest& request, const std::string& path,
                                                                 const std::string& module) {
        script::Limits limits;
        limits.heapBytes = kGeneratorHeap;
        limits.budget = kGeneratorBudget;
        limits.cStackBytes = kGeneratorCStack;
        std::unique_ptr<script::Interpreter> interp;
        const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(60);
        for (;;) {
            auto made = script::Interpreter::create(limits);
            if (made) {
                interp = std::move(*made);
                break;
            }
            if (made.error().kind != script::ErrorKind::Usage || !script::Interpreter::active() || std::chrono::steady_clock::now() > until)
                return std::unexpected(std::format("{}: the script runtime could not start: {}", request.file, made.error().describe()));
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if (auto r = interp->addFile(path, request.source); !r) return std::unexpected(std::format("{}: {}", request.file, r.error().describe()));
        auto r = interp->call(module, "generate");
        if (!r) {
            if (r.error().kind == script::ErrorKind::NotFound)
                return std::unexpected(std::format("{}: a generator defines generate(), which returns the patch ({})", request.file,
                                                   r.error().describe()));
            std::string text = std::format("{}: generate() failed: {}", request.file, r.error().describe());
            if (!r.error().traceback.empty()) text += "\n" + r.error().traceback;
            return std::unexpected(std::move(text));
        }
        if (!r->isMap()) return std::unexpected(std::format("{}: generate() should return a patch: a dict of tables, as a .toml patch holds them", request.file));
        return std::move(*r);
    }
};

} // namespace

std::shared_ptr<mods::GeneratorRunner> scriptGenerators() { return std::make_shared<ScriptGenerators>(); }

} // namespace opense4::sdk
