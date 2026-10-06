#include "sdk/players.hpp"

#include <algorithm>
#include <format>
#include <fstream>
#include <regex>
#include <sstream>

namespace opense4::sdk {

namespace {

std::string fileText(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

bool importable(std::string_view path) {
    for (size_t from = 0; from < path.size();) {
        size_t slash = path.find('/', from);
        if (slash == std::string_view::npos) slash = path.size() - 3;   // the file's name, without ".py"
        if (!mods::validPythonName(path.substr(from, slash - from))) return false;
        from = slash + 1;
        if (slash == path.size() - 3) break;
    }
    return true;
}

} // namespace

PlayerCheck checkModPlayers(const mods::Package& p) {
    PlayerCheck out;
    std::vector<std::pair<std::string, std::string>> files;   // under ai/, as the interpreter gets them
    for (const mods::PackageFile& f : p.files) {
        if (!f.path.starts_with("ai/")) continue;
        const std::string rel = f.path.substr(3);
        if (!rel.ends_with(".py")) continue;
        if (!importable(rel)) {
            out.warnings.push_back(std::format("{}: not a Python module name, so no player can import it", f.path));
            continue;
        }
        files.emplace_back(rel, fileText(f.real));
    }
    if (!files.empty() && p.manifest.aiPlayers.empty())
        out.warnings.push_back("the mod has Python files in ai/ but declares no computer player ([[ai.players]] in mod.toml)");

    // Each player: its module and its class.
    for (const mods::AiPlayer& a : p.manifest.aiPlayers) {
        std::string path = a.module;
        std::replace(path.begin(), path.end(), '.', '/');
        auto file = std::find_if(files.begin(), files.end(), [&](const auto& f) { return f.first == path + ".py"; });
        if (file == files.end()) file = std::find_if(files.begin(), files.end(), [&](const auto& f) { return f.first == path + "/__init__.py"; });
        if (file == files.end()) {
            out.errors.push_back(std::format("mod.toml:{}: the computer player '{}' is in module {}, but ai/ has no {}.py nor {}/__init__.py", a.line,
                                             a.name, a.module, path, path));
            continue;
        }
        const std::regex klass(std::format(R"((^|\n)class[ \t]+{}[ \t]*[(:])", a.className));
        if (!std::regex_search(file->second, klass))
            out.errors.push_back(std::format("mod.toml:{}: the computer player '{}' is class {}, but ai/{} defines no class {}", a.line, a.name,
                                             a.className, file->first, a.className));
    }

    // The files compile, and none takes a module name the runtime has.
    if (files.empty()) return out;
    script::Limits limits;
    limits.cStackBytes = size_t{1} << 20;
    auto interp = script::Interpreter::create(limits);
    if (!interp) {
        out.warnings.push_back("the Python files were not checked: " + interp.error().describe());
        return out;
    }
    for (const auto& [path, text] : files)
        if (auto r = (*interp)->addFile(path, text); !r) out.errors.push_back(std::format("ai/{}: {}", path, r.error().message));
    (void)(*interp)->addNativeFunction("_sdkcheck", "source", [&](std::span<const script::Value> args) -> script::Value {
        if (args.empty() || !args[0].isInt() || args[0].asInt() < 0 || static_cast<size_t>(args[0].asInt()) >= files.size()) return script::Value("");
        return script::Value(files[static_cast<size_t>(args[0].asInt())].second);
    });
    for (size_t i = 0; i < files.size(); ++i) {
        const std::string& path = files[i].first;
        auto r = (*interp)->exec(std::format("import _sdkcheck\ncompile(_sdkcheck.source({}), 'ai/{}', 'exec')\n", i, path));
        if (r) continue;
        // The traceback's place in the file, when it gives one.
        std::string where;
        const std::string& tb = r.error().traceback;
        if (const size_t at = tb.rfind(std::format("\"ai/{}\", line ", path)); at != std::string::npos) {
            const size_t from = at + path.size() + 11;
            where = std::format(" (line {})", tb.substr(from, tb.find_first_not_of("0123456789", from) - from));
        }
        out.errors.push_back(std::format("ai/{}{}: {}", path, where, r.error().describe()));
    }
    return out;
}

} // namespace opense4::sdk
