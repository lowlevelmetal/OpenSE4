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

using Files = std::vector<std::pair<std::string, std::string>>;   // under ai/: path, text

const std::pair<std::string, std::string>* moduleFile(const Files& files, const std::string& path) {
    auto file = std::find_if(files.begin(), files.end(), [&](const auto& f) { return f.first == path + ".py"; });
    if (file == files.end()) file = std::find_if(files.begin(), files.end(), [&](const auto& f) { return f.first == path + "/__init__.py"; });
    return file == files.end() ? nullptr : &*file;
}

std::string trimmedName(std::string_view s) {
    const size_t a = s.find_first_not_of(" \t");
    const size_t b = s.find_last_not_of(" \t");
    return a == std::string_view::npos ? std::string() : std::string(s.substr(a, b - a + 1));
}

// Whether the module file `file` defines class `name`, or takes it from another
// module with `from M import ...` (`from .player import Hegemon`, `import X as
// name` forms included): one of the mod's modules that provides it, or a module
// outside ai/, which cannot be checked here.
bool providesClass(const Files& files, const std::pair<std::string, std::string>& file, const std::string& name, int depth = 0) {
    const std::regex klass(std::format(R"((^|\n)class[ \t]+{}[ \t]*[(:])", name));
    if (std::regex_search(file.second, klass)) return true;
    if (depth > 4) return false;
    static const std::regex from(R"((^|\n)from[ \t]+([.\w]+)[ \t]+import[ \t]+\(?([^\n)]*))");
    for (auto it = std::sregex_iterator(file.second.begin(), file.second.end(), from); it != std::sregex_iterator(); ++it) {
        const std::string module = (*it)[2].str();
        std::string original;
        std::stringstream names((*it)[3].str());
        for (std::string item; std::getline(names, item, ',');) {
            const std::string entry = trimmedName(item);
            const size_t as = entry.find(" as ");
            const std::string bound = as == std::string::npos ? entry : trimmedName(std::string_view(entry).substr(as + 4));
            if (bound == name) original = as == std::string::npos ? entry : trimmedName(std::string_view(entry).substr(0, as));
        }
        if (original.empty()) continue;
        // The module it names, as a path under ai/.
        std::string path;
        size_t dots = 0;
        while (dots < module.size() && module[dots] == '.') ++dots;
        std::string rest = module.substr(dots);
        std::replace(rest.begin(), rest.end(), '.', '/');
        if (dots > 0) {
            // Relative: from the package the file is in, one level up for each dot after the first.
            std::string base = file.first.substr(0, file.first.rfind('/') == std::string::npos ? 0 : file.first.rfind('/'));
            for (size_t up = 1; up < dots; ++up) base = base.substr(0, base.rfind('/') == std::string::npos ? 0 : base.rfind('/'));
            path = base.empty() ? rest : (rest.empty() ? base : base + "/" + rest);
        } else {
            path = rest;
        }
        const auto* source = moduleFile(files, path);
        if (!source) return dots == 0;   // a module outside the mod: taken on trust
        if (providesClass(files, *source, original, depth + 1)) return true;
    }
    return false;
}

} // namespace

PlayerCheck checkModPlayers(const mods::Package& p) {
    PlayerCheck out;
    Files files;   // under ai/, as the interpreter gets them
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
        const auto* file = moduleFile(files, path);
        if (!file) {
            out.errors.push_back(std::format("mod.toml:{}: the computer player '{}' is in module {}, but ai/ has no {}.py nor {}/__init__.py", a.line,
                                             a.name, a.module, path, path));
            continue;
        }
        if (!providesClass(files, *file, a.className))
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
        const std::string mark = std::format("\"ai/{}\", line ", path);
        if (const size_t at = tb.rfind(mark); at != std::string::npos) {
            const size_t from = at + mark.size();
            const std::string line = tb.substr(from, tb.find_first_not_of("0123456789", from) - from);
            if (!line.empty()) where = std::format(" (line {})", line);
        }
        out.errors.push_back(std::format("ai/{}{}: {}", path, where, r.error().describe()));
    }
    return out;
}

} // namespace opense4::sdk
