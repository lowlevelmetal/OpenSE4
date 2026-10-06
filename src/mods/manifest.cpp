#include "mods/manifest.hpp"
#include "mods/manifest_internal.hpp"

#include <toml++/toml.hpp>

#include <algorithm>
#include <format>
#include <limits>

namespace opense4::mods {

bool validModId(std::string_view id) {
    if (id.empty() || id.size() > 64) return false;
    auto alnum = [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'); };
    if (!alnum(id.front())) return false;
    for (char c : id)
        if (!alnum(c) && c != '.' && c != '-' && c != '_') return false;
    return true;
}

bool validPythonName(std::string_view name) {
    if (name.empty() || (name.front() >= '0' && name.front() <= '9')) return false;
    for (char c : name)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')) return false;
    return true;
}

const AiPlayer* Manifest::aiPlayer(std::string_view player) const {
    for (const AiPlayer& p : aiPlayers)
        if (p.name == player) return &p;
    return nullptr;
}

std::string manifestAt(std::string_view source, const toml::node& n) {
    const auto line = n.source().begin.line;
    return line > 0 ? std::format("{}:{}", source, line) : std::string(source);
}

std::expected<Manifest, std::vector<std::string>> parseManifest(std::string_view text, std::string_view source) {
    std::vector<std::string> errors;
    toml::table root;
    try {
        root = toml::parse(text, std::string_view(source));
    } catch (const toml::parse_error& e) {
        errors.push_back(std::format("{}:{}: {}", source, e.source().begin.line, e.description()));
        return std::unexpected(errors);
    }
    Manifest m;
    auto error = [&](const toml::node& n, std::string_view what) { errors.push_back(std::format("{}: {}", manifestAt(source, n), what)); };
    auto text_of = [&](const toml::node& n, std::string_view key) -> std::string {
        if (const auto* s = n.as_string()) return std::string(s->get());
        error(n, std::format("'{}' should be text in quotes", key));
        return {};
    };

    for (const auto& [key, node] : root) {
        if (key == "mod") {
            const toml::table* t = node.as_table();
            if (!t) {
                error(node, "[mod] should be a table");
                continue;
            }
            for (const auto& [k, v] : *t) {
                if (k == "id") m.id = text_of(v, "id");
                else if (k == "name") m.name = text_of(v, "name");
                else if (k == "description") m.description = text_of(v, "description");
                else if (k == "version") {
                    const std::string written = text_of(v, "version");
                    if (auto parsed = parseVersion(written)) m.version = *parsed;
                    else if (!written.empty()) error(v, std::format("version '{}' should be numbers separated by dots, such as \"1.2.0\"", written));
                } else if (k == "api") {
                    if (const auto* i = v.as_integer(); i && i->get() > 0 && i->get() < 1000) m.api = static_cast<int>(i->get());
                    else error(v, "'api' should be the SDK interface version, a whole number such as 1");
                } else if (k == "authors") {
                    if (const toml::array* a = v.as_array()) {
                        for (const toml::node& e : *a) m.authors.push_back(text_of(e, "authors"));
                    } else {
                        error(v, "'authors' should be a list of names, such as [\"Ada\"]");
                    }
                } else {
                    error(v, std::format("unknown key '{}' in [mod] (id, name, version, api, authors, description)", k.str()));
                }
            }
            if (m.id.empty()) error(node, "[mod] needs an id, such as id = \"example.my-mod\"");
            else if (!validModId(m.id)) error(node, std::format("id '{}' may hold only lowercase letters, digits, '.', '-' and '_' (at most 64)", m.id));
            if (m.name.empty()) error(node, "[mod] needs a name");
            if (m.version.parts.empty()) error(node, "[mod] needs a version, such as version = \"1.0.0\"");
            if (m.api == 0) error(node, "[mod] needs the SDK interface version it was written for, such as api = 1");
            else if (m.api > kApiVersion)
                error(node, std::format("the mod needs SDK interface {}; this OpenSE4 offers {}: it needs a newer OpenSE4", m.api, kApiVersion));
        } else if (key == "requires") {
            const toml::table* t = node.as_table();
            if (!t) {
                error(node, "[requires] should be a table of mod ids and version ranges");
                continue;
            }
            for (const auto& [k, v] : *t) {
                Requirement r;
                r.id = std::string(k.str());
                r.line = static_cast<int>(v.source().begin.line);
                if (!validModId(r.id)) error(v, std::format("'{}' is not a mod id", r.id));
                const std::string written = text_of(v, r.id);
                if (written.empty()) continue;
                auto range = parseVersionRange(written);
                if (!range) {
                    error(v, range.error());
                    continue;
                }
                r.range = std::move(*range);
                m.requirements.push_back(std::move(r));
            }
        } else if (key == "load") {
            const toml::table* t = node.as_table();
            if (!t) {
                error(node, "[load] should be a table");
                continue;
            }
            for (const auto& [k, v] : *t) {
                if (k == "after") {
                    const toml::array* a = v.as_array();
                    if (!a) {
                        error(v, "'after' should be a list of mod ids");
                        continue;
                    }
                    for (const toml::node& e : *a) {
                        std::string id = text_of(e, "after");
                        if (!id.empty() && !validModId(id)) error(e, std::format("'{}' is not a mod id", id));
                        else if (!id.empty()) m.loadAfter.push_back(std::move(id));
                    }
                } else {
                    error(v, std::format("unknown key '{}' in [load] (after)", k.str()));
                }
            }
        } else if (key == "ai") {
            const toml::table* t = node.as_table();
            if (!t) {
                error(node, "[ai] should be a table, such as [[ai.players]]");
                continue;
            }
            for (const auto& [k, v] : *t) {
                if (k != "players") {
                    error(v, std::format("unknown key '{}' in [ai] (players)", k.str()));
                    continue;
                }
                const toml::array* a = v.as_array();
                if (!a) {
                    error(v, "computer players are written as [[ai.players]] tables");
                    continue;
                }
                for (const toml::node& e : *a) {
                    const toml::table* pt = e.as_table();
                    if (!pt) {
                        error(e, "computer players are written as [[ai.players]] tables");
                        continue;
                    }
                    AiPlayer p;
                    p.line = static_cast<int>(e.source().begin.line);
                    for (const auto& [pk, pv] : *pt) {
                        if (pk == "name") p.name = text_of(pv, "name");
                        else if (pk == "module") p.module = text_of(pv, "module");
                        else if (pk == "class") p.className = text_of(pv, "class");
                        else if (pk == "description") p.description = text_of(pv, "description");
                        else if (pk == "classic_state") {
                            if (const auto* b = pv.as_boolean()) p.classicState = b->get();
                            else error(pv, "'classic_state' should be true or false");
                        } else error(pv, std::format("unknown key '{}' in [[ai.players]] (name, module, class, description, classic_state)", pk.str()));
                    }
                    if (p.name.empty()) error(e, "[[ai.players]] needs a name, such as name = \"Admiral\"");
                    else if (std::any_of(m.aiPlayers.begin(), m.aiPlayers.end(), [&](const AiPlayer& o) { return o.name == p.name; }))
                        error(e, std::format("two computer players are named '{}'", p.name));
                    else if (p.name.find(':') != std::string::npos) error(e, std::format("the player name '{}' may not hold ':'", p.name));
                    const bool moduleOk = dottedPythonName(p.module);
                    if (p.module.empty()) error(e, "[[ai.players]] needs the module under ai/ that holds the player, such as module = \"admiral\"");
                    else if (!moduleOk) error(e, std::format("module '{}' should be Python names separated by dots, such as \"admiral\"", p.module));
                    if (p.className.empty()) error(e, "[[ai.players]] needs the player's class, such as class = \"Admiral\"");
                    else if (!validPythonName(p.className)) error(e, std::format("class '{}' is not a Python name", p.className));
                    m.aiPlayers.push_back(std::move(p));
                }
            }
        } else if (key == "rules") {
            parseRulesTable(node, source, m.rules, errors);
        } else {
            error(node, std::format("unknown table '{}' (mod.toml has [mod], [requires], [load], [[ai.players]] and [rules])", key.str()));
        }
    }
    if (!root.contains("mod")) errors.push_back(std::format("{}: no [mod] table", source));
    if (!errors.empty()) return std::unexpected(errors);
    return m;
}

namespace {

// A TOML basic string.
std::string tomlString(std::string_view text) {
    std::string out = "\"";
    for (char c : text) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) out += std::format("\\u{:04x}", static_cast<unsigned>(c));
                else out += c;
        }
    }
    return out + "\"";
}

} // namespace

std::string writeManifest(const Manifest& m) {
    std::string out = "[mod]\n";
    out += std::format("id = {}\n", tomlString(m.id));
    out += std::format("name = {}\n", tomlString(m.name));
    out += std::format("version = {}\n", tomlString(m.version.text));
    out += std::format("api = {}\n", m.api);
    std::string authors;
    for (const std::string& a : m.authors) authors += std::format("{}{}", authors.empty() ? "" : ", ", tomlString(a));
    out += std::format("authors = [{}]\n", authors);
    out += std::format("description = {}\n", tomlString(m.description));
    if (!m.requirements.empty()) {
        out += "\n[requires]\n";
        for (const Requirement& r : m.requirements) out += std::format("{} = {}\n", tomlString(r.id), tomlString(r.range.text));
    }
    if (!m.loadAfter.empty()) {
        std::string after;
        for (const std::string& id : m.loadAfter) after += std::format("{}{}", after.empty() ? "" : ", ", tomlString(id));
        out += std::format("\n[load]\nafter = [{}]\n", after);
    }
    for (const AiPlayer& p : m.aiPlayers) {
        out += std::format("\n[[ai.players]]\nname = {}\nmodule = {}\nclass = {}\n", tomlString(p.name), tomlString(p.module), tomlString(p.className));
        if (!p.description.empty()) out += std::format("description = {}\n", tomlString(p.description));
        if (!p.classicState) out += "classic_state = false\n";
    }
    return out;
}

} // namespace opense4::mods
