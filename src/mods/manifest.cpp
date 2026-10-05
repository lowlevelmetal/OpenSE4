#include "mods/manifest.hpp"

#include <toml++/toml.hpp>

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

namespace {

std::string at(std::string_view source, const toml::node& n) {
    const auto line = n.source().begin.line;
    return line > 0 ? std::format("{}:{}", source, line) : std::string(source);
}

} // namespace

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
    auto error = [&](const toml::node& n, std::string_view what) { errors.push_back(std::format("{}: {}", at(source, n), what)); };
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
        } else {
            error(node, std::format("unknown table '{}' (mod.toml has [mod], [requires] and [load])", key.str()));
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
    return out;
}

} // namespace opense4::mods
