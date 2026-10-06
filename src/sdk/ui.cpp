// The interface tier's declarations (sdk/ui.hpp, docs/sdk/interface.md): the
// mods' ui/*.toml files, read and checked; how a mod order's arguments are
// asked for; the strings of text/<lang>.toml; values as text.

#include "sdk/ui.hpp"

#include "script/json.hpp"

#include <toml++/toml.hpp>

#include <algorithm>
#include <array>
#include <format>
#include <fstream>
#include <sstream>

namespace opense4::sdk {

using script::Value;
using script::ValueList;
using script::ValueMap;

namespace {

std::string fileText(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::string lower(std::string_view s) {
    std::string out(s);
    for (char& c : out)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return out;
}

int lineOf(const toml::node& n) { return static_cast<int>(n.source().begin.line); }

constexpr auto kReports = std::to_array<std::string_view>({"ship", "fleet", "planet", "colony", "system"});
constexpr auto kLists = std::to_array<std::string_view>({"ships", "planets", "colonies", "designs"});
constexpr auto kWhose = std::to_array<std::string_view>({"any", "mine", "others"});

template <size_t N>
std::string joined(const std::array<std::string_view, N>& names) {
    std::string out;
    for (std::string_view n : names) out += std::format("{}\"{}\"", out.empty() ? "" : ", ", n);
    return out;
}

template <size_t N>
std::optional<size_t> indexOf(const std::array<std::string_view, N>& names, std::string_view x) {
    for (size_t i = 0; i < N; ++i)
        if (names[i] == x) return i;
    return std::nullopt;
}

// "Ctrl+Shift+O", "F5", "Alt+Delete": modifiers, then one key name.
bool plausibleChord(std::string_view text) {
    while (true) {
        bool ate = false;
        for (std::string_view m : {"Ctrl+", "Shift+", "Alt+"})
            if (text.starts_with(m) && text.size() > m.size()) {
                text.remove_prefix(m.size());
                ate = true;
            }
        if (!ate) break;
    }
    if (text.empty() || text.size() > 24) return false;
    return std::all_of(text.begin(), text.end(), [](char c) { return c > ' ' && c < 127 && c != '+'; }) || text == "+";
}

class UiParser {
public:
    UiParser(std::string_view file, std::string_view mod, const mods::RulesDecl* decl, std::vector<std::string>& errors)
        : file_(file), mod_(mod), decl_(decl), errors_(errors) {}

    void parse(const toml::table& root, UiFile& out) {
        for (const auto& [k, v] : root) {
            if (k == "panel") each(v, "[[panel]]", [&](const toml::table& t, const toml::node& n) { readPanel(t, n, out); });
            else if (k == "column") each(v, "[[column]]", [&](const toml::table& t, const toml::node& n) { readColumn(t, n, out); });
            else if (k == "empire_page") each(v, "[[empire_page]]", [&](const toml::table& t, const toml::node& n) { readPage(t, n, out); });
            else if (k == "order") each(v, "[[order]]", [&](const toml::table& t, const toml::node& n) { readOrder(t, n, out); });
            else if (k == "button") each(v, "[[button]]", [&](const toml::table& t, const toml::node& n) { readButton(t, n, out); });
            else error(v, std::format("unknown table '{}' (panel, button, column, empire_page, order)", k.str()));
        }
    }

private:
    void error(const toml::node& n, std::string_view what) {
        const int line = lineOf(n);
        errors_.push_back(line > 0 ? std::format("{}:{}: {}", file_, line, what) : std::format("{}: {}", file_, what));
    }

    template <class F>
    void each(const toml::node& v, std::string_view what, F&& f) {
        const toml::array* a = v.as_array();
        if (!a) return error(v, std::format("write them as {} tables", what));
        for (const toml::node& e : *a) {
            if (const toml::table* t = e.as_table()) f(*t, e);
            else error(e, std::format("write them as {} tables", what));
        }
    }

    // Unknown keys are errors.
    void allow(const toml::table& t, std::string_view where, std::initializer_list<std::string_view> keys) {
        for (const auto& [k, v] : t)
            if (std::find(keys.begin(), keys.end(), k.str()) == keys.end()) {
                std::string list;
                for (std::string_view x : keys) list += (list.empty() ? "" : ", ") + std::string(x);
                error(v, std::format("unknown key '{}' in {} ({})", k.str(), where, list));
            }
    }

    std::string textOf(const toml::table& t, std::string_view key, bool required = false, const toml::node* at = nullptr) {
        const toml::node* n = t.get(key);
        if (!n) {
            if (required) error(at ? *at : static_cast<const toml::node&>(t), std::format("'{}' is missing", key));
            return {};
        }
        if (const auto* s = n->as_string()) return std::string(s->get());
        error(*n, std::format("'{}' should be text in quotes", key));
        return {};
    }

    std::string declName(const toml::table& t, const toml::node& at, std::string_view what, std::vector<std::string>& taken) {
        std::string n = textOf(t, "name", true, &at);
        if (n.empty()) return n;
        if (!mods::validRulesName(n)) {
            error(*t.get("name"), std::format("the name '{}' may hold only lowercase letters, digits and '_', starting with a letter (at most 64)", n));
            return {};
        }
        if (std::find(taken.begin(), taken.end(), n) != taken.end()) {
            error(*t.get("name"), std::format("the mod has another {} named '{}'", what, n));
            return {};
        }
        taken.push_back(n);
        return n;
    }

    std::string chordOf(const toml::table& t) {
        std::string k = textOf(t, "key");
        if (!k.empty() && !plausibleChord(k)) {
            error(*t.get("key"), std::format("'{}' is not a key: write it as the Settings' Controls page does (\"Ctrl+Shift+O\", \"F5\")", k));
            return {};
        }
        return k;
    }

    // The value source of a row or column: exactly one of field, mod_data, ability, value.
    bool sourceOf(const toml::table& t, const toml::node& at, UiValueSource& out) {
        int found = 0;
        for (const auto& [key, kind] : {std::pair{"field", UiValueSource::Kind::Field}, std::pair{"mod_data", UiValueSource::Kind::ModData},
                                        std::pair{"ability", UiValueSource::Kind::Ability}, std::pair{"value", UiValueSource::Kind::Computed}}) {
            if (!t.get(key)) continue;
            ++found;
            out.kind = kind;
            out.path = textOf(t, key);
            if (out.path.empty()) error(*t.get(key), std::format("'{}' should name something", key));
        }
        if (found != 1) {
            error(at, found == 0 ? "a value needs one of field, mod_data, ability or value" : "a value takes only one of field, mod_data, ability or value");
            return false;
        }
        out.mod = textOf(t, "mod");
        if (!out.mod.empty() && out.kind != UiValueSource::Kind::ModData) error(*t.get("mod"), "'mod' goes with mod_data only (whose data it is)");
        if (!out.mod.empty() && !mods::validModId(out.mod)) error(*t.get("mod"), std::format("'{}' is not a mod id", out.mod));
        if (out.mod.empty()) out.mod = mod_;
        if (out.kind == UiValueSource::Kind::Computed && !mods::validRulesName(out.path))
            error(*t.get("value"), std::format("the value '{}' should be named with lowercase letters, digits and '_'", out.path));
        out.format = textOf(t, "format");
        if (!out.format.empty() && out.format.find("{}") == std::string::npos)
            error(*t.get("format"), "'format' should hold {} where the value goes (\"{} kT\")");
        return true;
    }

    int widthOf(const toml::table& t, int fallback) {
        const toml::node* n = t.get("width");
        if (!n) return fallback;
        if (const auto* i = n->as_integer(); i && i->get() >= 20 && i->get() <= 400) return static_cast<int>(i->get());
        error(*n, "'width' should be a whole number of pixels from 20 to 400");
        return fallback;
    }

    // An order a button or [[order]] names: "name" (this mod's) or "<mod id>:name".
    bool orderRef(const toml::table& t, const toml::node& at, std::string& mod, std::string& name, std::string_view key) {
        std::string ref = textOf(t, key, true, &at);
        if (ref.empty()) return false;
        if (const size_t colon = ref.find(':'); colon != std::string::npos) {
            mod = ref.substr(0, colon);
            name = ref.substr(colon + 1);
            if (!mods::validModId(mod) || !mods::validRulesName(name)) {
                error(*t.get(key), std::format("'{}' should be an order of the mod (\"overcharge\") or of another (\"example.shields:overcharge\")", ref));
                return false;
            }
        } else {
            mod = mod_;
            name = ref;
        }
        if (mod == mod_ && decl_ && !decl_->order(name)) {
            error(*t.get(key), std::format("the mod declares no order '{}' ([[rules.orders]] in mod.toml)", name));
            return false;
        }
        return true;
    }

    void readPanel(const toml::table& t, const toml::node& at, UiFile& out) {
        allow(t, "[[panel]]", {"name", "title", "report", "whose", "key", "row", "button"});
        UiPanel p;
        p.mod = mod_;
        p.file = std::string(file_);
        p.line = lineOf(at);
        p.name = declName(t, at, "panel", panels_);
        p.title = textOf(t, "title");
        const std::string report = textOf(t, "report", true, &at);
        if (auto i = indexOf(kReports, report)) p.report = static_cast<UiReport>(*i);
        else if (!report.empty()) error(*t.get("report"), std::format("'report' is one of {}", joined(kReports)));
        if (const std::string whose = textOf(t, "whose"); !whose.empty()) {
            if (auto i = indexOf(kWhose, whose)) p.whose = static_cast<UiWhose>(*i);
            else error(*t.get("whose"), std::format("'whose' is one of {}", joined(kWhose)));
        }
        p.key = chordOf(t);
        if (const toml::node* rows = t.get("row"))
            each(*rows, "[[panel.row]]", [&](const toml::table& r, const toml::node& n) {
                allow(r, "[[panel.row]]", {"name", "label", "field", "mod_data", "ability", "value", "mod", "format"});
                UiRow row;
                row.line = lineOf(n);
                row.name = textOf(r, "name");
                if (!row.name.empty() && !mods::validRulesName(row.name)) error(*r.get("name"), "a row's name is lowercase letters, digits and '_'");
                row.label = textOf(r, "label");
                if (sourceOf(r, n, row.source)) p.rows.push_back(std::move(row));
            });
        if (const toml::node* buttons = t.get("button"))
            each(*buttons, "[[panel.button]]", [&](const toml::table& b, const toml::node& n) {
                allow(b, "[[panel.button]]", {"order", "label"});
                UiButton button;
                button.line = lineOf(n);
                button.label = textOf(b, "label");
                if (orderRef(b, n, button.mod, button.order, "order")) p.buttons.push_back(std::move(button));
            });
        if (p.rows.empty() && p.buttons.empty()) error(at, "a panel needs rows ([[panel.row]]) or buttons ([[panel.button]])");
        if (!p.name.empty()) out.panels.push_back(std::move(p));
    }

    // [[button]]: a panel with one button and nothing else.
    void readButton(const toml::table& t, const toml::node& at, UiFile& out) {
        allow(t, "[[button]]", {"name", "report", "whose", "order", "label", "key"});
        UiPanel p;
        p.mod = mod_;
        p.file = std::string(file_);
        p.line = lineOf(at);
        p.name = declName(t, at, "panel or button", panels_);
        const std::string report = textOf(t, "report", true, &at);
        if (auto i = indexOf(kReports, report)) p.report = static_cast<UiReport>(*i);
        else if (!report.empty()) error(*t.get("report"), std::format("'report' is one of {}", joined(kReports)));
        if (const std::string whose = textOf(t, "whose"); !whose.empty()) {
            if (auto i = indexOf(kWhose, whose)) p.whose = static_cast<UiWhose>(*i);
            else error(*t.get("whose"), std::format("'whose' is one of {}", joined(kWhose)));
        }
        p.key = chordOf(t);
        UiButton b;
        b.line = p.line;
        b.label = textOf(t, "label");
        if (!orderRef(t, at, b.mod, b.order, "order")) return;
        p.buttons.push_back(std::move(b));
        if (!p.name.empty()) out.panels.push_back(std::move(p));
    }

    void readColumn(const toml::table& t, const toml::node& at, UiFile& out) {
        allow(t, "[[column]]", {"name", "list", "label", "width", "field", "mod_data", "ability", "value", "mod", "format"});
        UiColumn c;
        c.mod = mod_;
        c.file = std::string(file_);
        c.line = lineOf(at);
        c.name = declName(t, at, "column", columns_);
        const std::string list = textOf(t, "list", true, &at);
        if (auto i = indexOf(kLists, list)) c.list = static_cast<UiList>(*i);
        else if (!list.empty()) error(*t.get("list"), std::format("'list' is one of {}", joined(kLists)));
        c.label = textOf(t, "label");
        c.width = widthOf(t, 80);
        if (sourceOf(t, at, c.source) && !c.name.empty()) out.columns.push_back(std::move(c));
    }

    void readPage(const toml::table& t, const toml::node& at, UiFile& out) {
        allow(t, "[[empire_page]]", {"name", "title", "key", "column"});
        UiEmpirePage p;
        p.mod = mod_;
        p.file = std::string(file_);
        p.line = lineOf(at);
        p.name = declName(t, at, "empire page", pages_);
        p.title = textOf(t, "title");
        p.key = chordOf(t);
        std::vector<std::string> names;
        if (const toml::node* cols = t.get("column"))
            each(*cols, "[[empire_page.column]]", [&](const toml::table& c, const toml::node& n) {
                allow(c, "[[empire_page.column]]", {"name", "label", "width", "field", "mod_data", "ability", "value", "mod", "format"});
                UiColumn col;
                col.mod = mod_;
                col.file = std::string(file_);
                col.line = lineOf(n);
                col.name = textOf(c, "name");
                if (!col.name.empty() && !mods::validRulesName(col.name)) error(*c.get("name"), "a column's name is lowercase letters, digits and '_'");
                col.label = textOf(c, "label");
                col.width = widthOf(c, 90);
                if (sourceOf(c, n, col.source)) p.columns.push_back(std::move(col));
            });
        if (p.columns.empty()) error(at, "an empire page needs columns ([[empire_page.column]])");
        if (!p.name.empty()) out.pages.push_back(std::move(p));
    }

    void readOrder(const toml::table& t, const toml::node& at, UiFile& out) {
        allow(t, "[[order]]", {"name", "icon", "key", "args"});
        UiOrderStyle o;
        o.file = std::string(file_);
        o.line = lineOf(at);
        if (!orderRef(t, at, o.mod, o.order, "name")) return;
        if (std::any_of(out.orders.begin(), out.orders.end(), [&](const UiOrderStyle& x) { return x.mod == o.mod && x.order == o.order; }))
            error(at, std::format("the order '{}' is styled twice", o.order));
        o.icon = textOf(t, "icon");
        if (!o.icon.empty()) {
            const std::string l = lower(o.icon);
            if (o.icon.find("..") != std::string::npos || o.icon.starts_with('/') || o.icon.find('\\') != std::string::npos)
                error(*t.get("icon"), "'icon' is a path under the mod's assets/ folder, written with '/'");
            else if (!l.ends_with(".png") && !l.ends_with(".bmp"))
                error(*t.get("icon"), "'icon' should be a .png or .bmp picture");
        }
        o.key = chordOf(t);
        const mods::ModOrderDecl* declared = o.mod == mod_ && decl_ ? decl_->order(o.order) : nullptr;
        if (const toml::node* args = t.get("args")) {
            const toml::table* a = args->as_table();
            if (!a) error(*args, "write the arguments as [order.args.<name>] tables");
            else
                for (const auto& [k, v] : *a) {
                    const toml::table* at2 = v.as_table();
                    if (!at2) {
                        error(v, "write each argument as a table: [order.args.<name>]");
                        continue;
                    }
                    allow(*at2, "[order.args]", {"label", "choices"});
                    UiArgStyle s;
                    s.name = std::string(k.str());
                    const mods::ModArgDecl* arg = nullptr;
                    if (declared)
                        for (const mods::ModArgDecl& x : declared->args)
                            if (x.name == s.name) arg = &x;
                    if (declared && !arg) error(v, std::format("the order '{}' declares no argument '{}'", o.order, s.name));
                    s.label = textOf(*at2, "label");
                    if (const toml::node* ch = at2->get("choices")) {
                        const toml::array* list = ch->as_array();
                        if (!list || list->empty()) error(*ch, "'choices' should be a list of values");
                        else
                            for (const toml::node& c : *list) {
                                if (const auto* i = c.as_integer()) s.choices.emplace_back(i->get());
                                else if (const auto* str = c.as_string()) s.choices.emplace_back(std::string(str->get()));
                                else error(c, "a choice is a whole number or text");
                            }
                        if (arg && arg->type != "int" && arg->type != "text") error(*ch, "only int and text arguments take choices");
                        if (arg)
                            for (const Value& c : s.choices) {
                                if ((arg->type == "int" && !c.isInt()) || (arg->type == "text" && !c.isString()))
                                    error(*ch, std::format("the choices of '{}' should be {}", s.name, arg->type == "int" ? "whole numbers" : "text"));
                                else if (c.isInt() && ((arg->min && c.asInt() < *arg->min) || (arg->max && c.asInt() > *arg->max)))
                                    error(*ch, std::format("the choice {} is outside the argument's range", c.asInt()));
                            }
                    }
                    o.args.push_back(std::move(s));
                }
        }
        out.orders.push_back(std::move(o));
    }

    std::string_view file_;
    std::string mod_;
    const mods::RulesDecl* decl_;
    std::vector<std::string>& errors_;
    std::vector<std::string> panels_, columns_, pages_;
};

void flatten(const toml::table& t, const std::string& prefix, std::map<std::string, std::string>& out, std::vector<std::string>& errors,
             std::string_view file) {
    for (const auto& [k, v] : t) {
        const std::string key = prefix.empty() ? std::string(k.str()) : prefix + "." + std::string(k.str());
        if (const toml::table* sub = v.as_table()) flatten(*sub, key, out, errors, file);
        else if (const auto* s = v.as_string()) out[key] = std::string(s->get());
        else errors.push_back(std::format("{}:{}: '{}' should be text in quotes", file, lineOf(v), key));
    }
}

} // namespace

std::string_view uiReportName(UiReport r) { return kReports[static_cast<size_t>(r)]; }
std::string_view uiListName(UiList l) { return kLists[static_cast<size_t>(l)]; }

const UiArgStyle* UiOrderStyle::arg(std::string_view name) const {
    for (const UiArgStyle& a : args)
        if (a.name == name) return &a;
    return nullptr;
}

std::expected<UiFile, std::vector<std::string>> parseUiFile(std::string_view text, std::string_view file, std::string_view mod,
                                                            const mods::RulesDecl* decl) {
    std::vector<std::string> errors;
    toml::table root;
    try {
        root = toml::parse(text, file);
    } catch (const toml::parse_error& e) {
        errors.push_back(std::format("{}:{}: {}", file, e.source().begin.line, e.description()));
        return std::unexpected(std::move(errors));
    }
    UiFile out;
    UiParser(file, mod, decl, errors).parse(root, out);
    if (!errors.empty()) return std::unexpected(std::move(errors));
    return out;
}

// ---- The set of mods --------------------------------------------------------------------------------

const UiOrderStyle* UiExtensions::order(std::string_view mod, std::string_view name) const {
    for (const UiOrderStyle& o : orders)
        if (o.mod == mod && o.order == name) return &o;
    return nullptr;
}

std::vector<const UiPanel*> UiExtensions::panelsFor(UiReport r) const {
    std::vector<const UiPanel*> out;
    for (const UiPanel& p : panels)
        if (p.report == r) out.push_back(&p);
    return out;
}

std::vector<const UiColumn*> UiExtensions::columnsFor(UiList l) const {
    std::vector<const UiColumn*> out;
    for (const UiColumn& c : columns)
        if (c.list == l) out.push_back(&c);
    return out;
}

std::vector<std::string> UiExtensions::problemsOf(std::string_view mod) const {
    std::vector<std::string> out;
    for (const auto& [m, p] : problems)
        if (m == mod) out.push_back(p);
    return out;
}

UiExtensions loadUiExtensions(std::span<const mods::Package> packages) {
    UiExtensions out;
    auto findPackage = [&](std::string_view id) -> const mods::Package* {
        for (const mods::Package& p : packages)
            if (p.id() == id) return &p;
        return nullptr;
    };
    for (const mods::Package& p : packages) {
        if (p.classic || !(p.tiers & mods::kTierInterface)) continue;
        std::vector<const mods::PackageFile*> files;
        bool scripts = false;
        for (const mods::PackageFile& f : p.files) {
            if (!f.path.starts_with("ui/")) continue;
            const std::string rest = f.path.substr(3);
            if (rest.ends_with(".py")) scripts = true;
            else if (rest.find('/') == std::string::npos && lower(rest).ends_with(".toml")) files.push_back(&f);
        }
        std::sort(files.begin(), files.end(), [](const mods::PackageFile* a, const mods::PackageFile* b) { return lower(a->path) < lower(b->path); });
        UiFile all;
        for (const mods::PackageFile* f : files) {
            auto parsed = parseUiFile(fileText(f->real), f->path, p.id(), &p.manifest.rules);
            if (!parsed) {
                for (std::string& e : parsed.error()) out.problems.emplace_back(p.id(), std::format("mod {}: {}", p.id(), e));
                continue;
            }
            // Names are unique across the mod's files too.
            auto clash = [&](auto& list, auto& added, std::string_view what) {
                for (auto& x : added) {
                    if (std::any_of(list.begin(), list.end(), [&](const auto& y) { return y.name == x.name; })) {
                        out.problems.emplace_back(p.id(), std::format("mod {}: {}:{}: another {} is named '{}'", p.id(), x.file, x.line, what, x.name));
                        continue;
                    }
                    list.push_back(std::move(x));
                }
            };
            clash(all.panels, parsed->panels, "panel");
            clash(all.columns, parsed->columns, "column");
            clash(all.pages, parsed->pages, "empire page");
            for (UiOrderStyle& o : parsed->orders) all.orders.push_back(std::move(o));
        }
        // Pictures are in the mod's own assets/.
        for (UiOrderStyle& o : all.orders)
            if (!o.icon.empty() && !p.file("assets/" + o.icon)) {
                out.problems.emplace_back(p.id(), std::format("mod {}: {}:{}: the icon assets/{} is not in the mod", p.id(), o.file, o.line, o.icon));
                o.icon.clear();
            }
        // Orders of other mods must be theirs.
        auto otherOrder = [&](const std::string& mod, const std::string& name, const std::string& file, int line) {
            if (mod == p.id()) return true;
            const mods::Package* q = findPackage(mod);
            if (q && q->manifest.rules.order(name)) return true;
            out.problems.emplace_back(p.id(), std::format("mod {}: {}:{}: {} {}", p.id(), file, line,
                                                          q ? std::format("the mod {} declares no order '{}'", mod, name)
                                                            : std::format("the order {}:{} is of a mod that is not loaded", mod, name),
                                                          "(left out)"));
            return false;
        };
        for (UiPanel& panel : all.panels)
            std::erase_if(panel.buttons, [&](const UiButton& b) { return !otherOrder(b.mod, b.order, panel.file, b.line); });
        std::erase_if(all.orders, [&](const UiOrderStyle& o) { return !otherOrder(o.mod, o.order, o.file, o.line); });
        std::erase_if(all.panels, [](const UiPanel& panel) { return panel.rows.empty() && panel.buttons.empty(); });
        for (UiOrderStyle& o : all.orders)
            if (!out.order(o.mod, o.order)) out.orders.push_back(std::move(o));
        out.panels.insert(out.panels.end(), std::make_move_iterator(all.panels.begin()), std::make_move_iterator(all.panels.end()));
        out.columns.insert(out.columns.end(), std::make_move_iterator(all.columns.begin()), std::make_move_iterator(all.columns.end()));
        out.pages.insert(out.pages.end(), std::make_move_iterator(all.pages.begin()), std::make_move_iterator(all.pages.end()));
        if (scripts) out.scriptMods.push_back(p.id());
    }
    return out;
}

// ---- Asking for a mod order's arguments ----------------------------------------------------------------

std::vector<UiArgStep> uiArgumentSteps(const mods::ModOrderDecl& order, const UiOrderStyle* style) {
    std::vector<UiArgStep> out;
    for (const mods::ModArgDecl& a : order.args) {
        UiArgStep s;
        s.name = a.name;
        s.type = a.type;
        s.min = a.min;
        s.max = a.max;
        if (a.hasDefault) s.defaultValue = a.defaultValue;
        const UiArgStyle* st = style ? style->arg(a.name) : nullptr;
        s.question = st && !st->label.empty() ? st->label : a.name;
        if (st) s.choices = st->choices;
        if (a.type == "int") s.ask = s.choices.empty() ? UiArgStep::Ask::Number : UiArgStep::Ask::Choice;
        else if (a.type == "bool") s.ask = UiArgStep::Ask::YesNo;
        else if (a.type == "text") s.ask = s.choices.empty() ? UiArgStep::Ask::Text : UiArgStep::Ask::Choice;
        else if (a.type == "empire") s.ask = UiArgStep::Ask::Empire;
        else if (a.type == "design") s.ask = UiArgStep::Ask::Design;
        else s.ask = UiArgStep::Ask::Pick;
        s.optional = a.type != "int" && a.type != "bool" && a.type != "text";
        out.push_back(std::move(s));
    }
    return out;
}

std::expected<Value, std::string> uiOrderArguments(const mods::ModOrderDecl& order, const Value& answers) {
    ValueMap out;
    for (const mods::ModArgDecl& a : order.args) {
        const Value* v = answers.find(a.name);
        if (!v || v->isNull()) {
            if (a.hasDefault) out.emplace_back(a.name, a.defaultValue);
            else if (a.type == "int" || a.type == "bool" || a.type == "text") return std::unexpected(std::format("'{}' needs an answer", a.name));
            else out.emplace_back(a.name, Value());
            continue;
        }
        if (a.type == "int") {
            if (!v->isInt()) return std::unexpected(std::format("'{}' should be a whole number", a.name));
            if ((a.min && v->asInt() < *a.min) || (a.max && v->asInt() > *a.max))
                return std::unexpected(std::format("'{}' should be {} to {}", a.name, a.min ? std::to_string(*a.min) : "any", a.max ? std::to_string(*a.max) : "any"));
        } else if (a.type == "bool") {
            if (!v->isBool()) return std::unexpected(std::format("'{}' should be yes or no", a.name));
        } else if (a.type == "text") {
            if (!v->isString()) return std::unexpected(std::format("'{}' should be text", a.name));
        } else if (!v->isInt()) {
            return std::unexpected(std::format("'{}' should name a {}", a.name, a.type));
        }
        out.emplace_back(a.name, *v);
    }
    for (const auto& [k, v] : answers.isMap() ? answers.asMap() : ValueMap{})
        if (std::none_of(order.args.begin(), order.args.end(), [&](const mods::ModArgDecl& a) { return a.name == k; }))
            return std::unexpected(std::format("the order has no argument '{}'", k));
    return Value(std::move(out));
}

// ---- Text ------------------------------------------------------------------------------------------

bool validLanguageTag(std::string_view tag) {
    // "en", "fr", "pt-br", "zh-hant": two or three lowercase letters, then
    // optionally '-' (or '_') and two to eight lowercase letters or digits.
    const size_t dash = tag.find_first_of("-_");
    const std::string_view primary = tag.substr(0, dash);
    if (primary.size() < 2 || primary.size() > 3) return false;
    if (!std::all_of(primary.begin(), primary.end(), [](char c) { return c >= 'a' && c <= 'z'; })) return false;
    if (dash == std::string_view::npos) return true;
    const std::string_view rest = tag.substr(dash + 1);
    return rest.size() >= 2 && rest.size() <= 8 && std::all_of(rest.begin(), rest.end(), [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'); });
}

std::expected<std::map<std::string, std::string>, std::vector<std::string>> parseUiTexts(std::string_view text, std::string_view file) {
    std::vector<std::string> errors;
    toml::table root;
    try {
        root = toml::parse(text, file);
    } catch (const toml::parse_error& e) {
        errors.push_back(std::format("{}:{}: {}", file, e.source().begin.line, e.description()));
        return std::unexpected(std::move(errors));
    }
    std::map<std::string, std::string> out;
    flatten(root, "", out, errors, file);
    if (!errors.empty()) return std::unexpected(std::move(errors));
    return out;
}

void UiTexts::add(const mods::Package& p) {
    if (p.classic) return;
    for (const mods::PackageFile& f : p.files) {
        if (!f.path.starts_with("text/") || f.path.find('/', 5) != std::string::npos) continue;
        const std::string name = lower(f.path.substr(5));
        if (!name.ends_with(".toml")) continue;
        const std::string lang = name.substr(0, name.size() - 5);
        if (!validLanguageTag(lang)) {
            problems_.push_back(std::format("mod {}: {}: the file's name should be a language: \"en.toml\", \"fr.toml\", \"pt-br.toml\"", p.id(), f.path));
            continue;
        }
        auto parsed = parseUiTexts(fileText(f.real), f.path);
        if (!parsed) {
            for (const std::string& e : parsed.error()) problems_.push_back(std::format("mod {}: {}", p.id(), e));
            continue;
        }
        auto& into = strings_[p.id()][lang];
        for (auto& [k, v] : *parsed) into[k] = std::move(v);
    }
}

void UiTexts::setLanguage(std::string language) { language_ = language.empty() ? std::string("en") : lower(language); }

std::vector<std::string> UiTexts::languages() const {
    std::vector<std::string> out;
    for (const auto& [mod, langs] : strings_)
        for (const auto& [lang, keys] : langs)
            if (std::find(out.begin(), out.end(), lang) == out.end()) out.push_back(lang);
    std::sort(out.begin(), out.end());
    return out;
}

std::string UiTexts::get(std::string_view mod, std::string_view key, std::string_view fallback) const {
    const auto m = strings_.find(std::string(mod));
    if (m == strings_.end()) return std::string(fallback);
    std::vector<std::string> chain{language_};
    if (const size_t dash = language_.find_first_of("-_"); dash != std::string::npos) chain.push_back(language_.substr(0, dash));
    chain.emplace_back("en");
    for (const std::string& lang : chain) {
        const auto l = m->second.find(lang);
        if (l == m->second.end()) continue;
        if (const auto k = l->second.find(std::string(key)); k != l->second.end()) return k->second;
    }
    return std::string(fallback);
}

// ---- Values as text ------------------------------------------------------------------------------------

std::string formatUiValue(const Value& v, std::string_view format) {
    std::string text;
    switch (v.kind()) {
        case script::Kind::Null: text = "-"; break;
        case script::Kind::Bool: text = v.asBool() ? "Yes" : "No"; break;
        case script::Kind::Int: text = std::to_string(v.asInt()); break;
        case script::Kind::String: text = v.asString(); break;
        case script::Kind::List:
            for (const Value& x : v.asList()) text += (text.empty() ? "" : ", ") + formatUiValue(x);
            break;
        case script::Kind::Map: text = script::toJson(v).value_or(std::string("{...}")); break;
    }
    if (format.empty() || v.isNull()) return text;
    std::string out(format);
    if (const size_t at = out.find("{}"); at != std::string::npos) out.replace(at, 2, text);
    return out;
}

Value uiFieldValue(const Value& record, std::string_view path) {
    const Value* at = &record;
    while (!path.empty()) {
        const size_t dot = path.find('.');
        const std::string_view key = path.substr(0, dot);
        if (at->isList()) {
            // A position in a list: "items.0".
            size_t i = 0;
            for (char c : key) {
                if (c < '0' || c > '9') return Value();
                i = i * 10 + static_cast<size_t>(c - '0');
            }
            if (key.empty() || i >= at->asList().size()) return Value();
            at = &at->asList()[i];
        } else {
            at = at->find(key);
            if (!at) return Value();
        }
        path = dot == std::string_view::npos ? std::string_view{} : path.substr(dot + 1);
    }
    return *at;
}

} // namespace opense4::sdk
