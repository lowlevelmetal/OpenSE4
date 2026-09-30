#include "sim/content.hpp"

#include "core/embedded.hpp"

#include <toml++/toml.hpp>

#include <algorithm>
#include <format>
#include <functional>
#include <initializer_list>

namespace opense4::sim {

namespace {

constexpr std::array<std::string_view, enumIndex(AbilityType::Count)> kAbilityKeys{
    "movement",         "command",          "life_support",       "crew_quarters",    "supply_storage",
    "cargo_storage",    "colonize",         "sensor",             "shield_generation", "produce_minerals",
    "produce_organics", "produce_radioactives", "produce_research", "space_yard"};

constexpr std::array<std::string_view, enumIndex(AbilityType::Count)> kAbilityNames{
    "Movement",         "Command",          "Life Support",       "Crew Quarters",    "Supply Storage",
    "Cargo Storage",    "Colonize",         "Sensor",             "Shield Generation", "Mineral Production",
    "Organics Production", "Radioactives Production", "Research", "Space Yard"};

std::string lineOf(const toml::node& n) {
    const auto& src = n.source();
    return src.begin.line ? std::format(":{}", src.begin.line) : std::string{};
}

// Reads fields of one TOML table, recording problems with full context.
class Fields {
public:
    Fields(const toml::table& table, std::string context, std::vector<std::string>& errors)
        : table_(table), context_(std::move(context)), errors_(errors) {}

    void error(std::string_view message) { errors_.push_back(std::format("{}: {}", context_, message)); }

    // Rejects keys not in `allowed` - catches typos in mod files.
    void allowOnly(std::initializer_list<std::string_view> allowed) {
        for (auto&& [key, node] : table_) {
            if (std::find(allowed.begin(), allowed.end(), key.str()) == allowed.end())
                error(std::format("unknown field '{}'{}", key.str(), lineOf(node)));
        }
    }

    bool has(std::string_view key) const { return table_.contains(key); }

    std::string string(std::string_view key, bool required = true, std::string fallback = {}) {
        const toml::node* n = table_.get(key);
        if (!n) {
            if (required) error(std::format("missing required field '{}'", key));
            return fallback;
        }
        if (const auto* s = n->as_string()) return s->get();
        error(std::format("field '{}' must be a string{}", key, lineOf(*n)));
        return fallback;
    }

    int64_t integer(std::string_view key, bool required, int64_t fallback, int64_t min = INT64_MIN) {
        const toml::node* n = table_.get(key);
        if (!n) {
            if (required) error(std::format("missing required field '{}'", key));
            return fallback;
        }
        const auto* i = n->as_integer();
        if (!i) {
            error(std::format("field '{}' must be an integer{}", key, lineOf(*n)));
            return fallback;
        }
        if (i->get() < min) {
            error(std::format("field '{}' must be at least {}{}", key, min, lineOf(*n)));
            return fallback;
        }
        return i->get();
    }

    int integer32(std::string_view key, bool required, int fallback, int min = INT32_MIN, int max = INT32_MAX) {
        const int64_t v = integer(key, required, fallback, min);
        if (v > max) {
            error(std::format("field '{}' must be at most {}", key, max));
            return fallback;
        }
        return static_cast<int>(v);
    }

    Resources resources(std::string_view key, bool required = false) {
        Resources r;
        const toml::node* n = table_.get(key);
        if (!n) {
            if (required) error(std::format("missing required field '{}'", key));
            return r;
        }
        const auto* t = n->as_table();
        if (!t) {
            error(std::format("field '{}' must be a table like {{ minerals = 10 }}{}", key, lineOf(*n)));
            return r;
        }
        for (auto&& [name, value] : *t) {
            const auto type = parseResourceType(name.str());
            if (!type) {
                error(std::format("'{}': unknown resource '{}'{}", key, name.str(), lineOf(value)));
                continue;
            }
            const auto* i = value.as_integer();
            if (!i || i->get() < 0) {
                error(std::format("'{}.{}' must be a non-negative integer{}", key, name.str(), lineOf(value)));
                continue;
            }
            r[*type] = i->get();
        }
        return r;
    }

    std::vector<TechRequirement> requirements(std::string_view key, const Content& content) {
        std::vector<TechRequirement> out;
        const toml::node* n = table_.get(key);
        if (!n) return out;
        const auto* t = n->as_table();
        if (!t) {
            error(std::format("field '{}' must be a table like {{ physics = 2 }}{}", key, lineOf(*n)));
            return out;
        }
        for (auto&& [techKey, value] : *t) {
            const auto tech = content.findTech(techKey.str());
            if (!tech) {
                error(std::format("'{}': unknown tech '{}'{}", key, techKey.str(), lineOf(value)));
                continue;
            }
            const auto* level = value.as_integer();
            if (!level || level->get() < 1) {
                error(std::format("'{}.{}' must be a level >= 1{}", key, techKey.str(), lineOf(value)));
                continue;
            }
            out.push_back({*tech, static_cast<int>(level->get())});
        }
        // Table iteration order is by key; keep output deterministic regardless.
        std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.tech < b.tech; });
        return out;
    }

    std::vector<Ability> abilities(std::string_view key) {
        std::vector<Ability> out;
        const toml::node* n = table_.get(key);
        if (!n) return out;
        const auto* arr = n->as_array();
        if (!arr) {
            error(std::format("field '{}' must be an array of tables{}", key, lineOf(*n)));
            return out;
        }
        for (const toml::node& item : *arr) {
            const auto* t = item.as_table();
            if (!t) {
                error(std::format("'{}' entries must be tables like {{ type = \"movement\", amount = 1 }}{}", key, lineOf(item)));
                continue;
            }
            Fields f(*t, std::format("{} {}{}", context_, key, lineOf(item)), errors_);
            f.allowOnly({"type", "amount", "param"});
            Ability a;
            const std::string typeKey = f.string("type");
            const auto type = parseAbilityType(typeKey);
            if (!type) {
                if (!typeKey.empty()) f.error(std::format("unknown ability type '{}'", typeKey));
                continue;
            }
            a.type = *type;
            a.amount = f.integer32("amount", false, 1, 0);
            a.param = f.string("param", false);
            if (a.type == AbilityType::Colonize && !parsePlanetSurface(a.param))
                f.error("colonize ability needs param = \"rock\", \"ice\" or \"gas\"");
            out.push_back(std::move(a));
        }
        return out;
    }

    std::vector<std::string> stringList(std::string_view key, bool required = false) {
        std::vector<std::string> out;
        const toml::node* n = table_.get(key);
        if (!n) {
            if (required) error(std::format("missing required field '{}'", key));
            return out;
        }
        const auto* arr = n->as_array();
        if (!arr) {
            error(std::format("field '{}' must be an array of strings{}", key, lineOf(*n)));
            return out;
        }
        for (const toml::node& item : *arr) {
            if (const auto* s = item.as_string())
                out.push_back(s->get());
            else
                error(std::format("'{}' entries must be strings{}", key, lineOf(item)));
        }
        return out;
    }

    const toml::table* subtable(std::string_view key, bool required = false) {
        const toml::node* n = table_.get(key);
        if (!n) {
            if (required) error(std::format("missing required section '{}'", key));
            return nullptr;
        }
        const auto* t = n->as_table();
        if (!t) error(std::format("'{}' must be a table{}", key, lineOf(*n)));
        return t;
    }

    const std::string& context() const { return context_; }

private:
    const toml::table& table_;
    std::string context_;
    std::vector<std::string>& errors_;
};

class Loader {
public:
    explicit Loader(std::filesystem::path dir) : dir_(std::move(dir)) {}

    std::expected<Content, std::vector<std::string>> run() {
        auto techs = parse("techs.toml");
        auto hulls = parse("hulls.toml");
        auto components = parse("components.toml");
        auto facilities = parse("facilities.toml");
        auto races = parse("races.toml");
        auto designs = parse("designs.toml");
        auto rules = parse("rules.toml");
        if (!errors_.empty()) return std::unexpected(std::move(errors_));

        loadTechs(*techs);
        loadHulls(*hulls);
        loadComponents(*components);
        loadFacilities(*facilities);
        loadRaces(*races);
        loadDesigns(*designs);
        loadRules(*rules);
        validate();

        if (!errors_.empty()) return std::unexpected(std::move(errors_));
        return std::move(content_);
    }

private:
    std::optional<toml::table> parse(const char* fileName) {
        const auto path = dir_ / fileName;
        try {
            // A file on disk wins; otherwise the copy built into the executable.
            std::error_code ec;
            if (!std::filesystem::exists(path, ec)) {
                const std::string builtIn = std::string("data/") + fileName;
                if (const auto bytes = embeddedResource(builtIn); !bytes.empty())
                    return toml::parse(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()),
                                       "(built-in) " + builtIn);
            }
            return toml::parse_file(path.string());
        } catch (const toml::parse_error& e) {
            const auto& begin = e.source().begin;
            errors_.push_back(std::format("{}:{}:{}: {}", path.string(), begin.line, begin.column, e.description()));
            return std::nullopt;
        }
    }

    // Calls fn(Fields&, index) for each [[entry]] in the file's array of tables.
    template <class Fn>
    void forEachEntry(const toml::table& root, const char* file, const char* entry, Fn&& fn) {
        const toml::node* n = root.get(entry);
        if (!n) {
            errors_.push_back(std::format("{}: expected at least one [[{}]] entry", file, entry));
            return;
        }
        const auto* arr = n->as_array();
        if (!arr) {
            errors_.push_back(std::format("{}: '{}' must be an array of tables ([[{}]])", file, entry, entry));
            return;
        }
        size_t index = 0;
        for (const toml::node& item : *arr) {
            const auto* t = item.as_table();
            if (!t) {
                errors_.push_back(std::format("{}{}: '{}' entries must be tables ([[{}]])", file, lineOf(item), entry, entry));
                continue;
            }
            std::string label;
            if (const auto* k = t->get_as<std::string>("key")) label = k->get();
            else if (const auto* nm = t->get_as<std::string>("name")) label = nm->get();
            else label = std::format("#{}", index + 1);
            Fields f(*t, std::format("{}{} [{} '{}']", file, lineOf(item), entry, label), errors_);
            fn(f, *t);
            ++index;
        }
    }

    template <class Vec>
    bool checkUniqueKey(Fields& f, const Vec& existing, const std::string& key) {
        if (key.empty()) return false;
        const bool dup = std::any_of(existing.begin(), existing.end(), [&](const auto& e) { return e.key == key; });
        if (dup) f.error(std::format("duplicate key '{}'", key));
        return !dup;
    }

    void loadTechs(const toml::table& root) {
        // Pass 1: keys, so requirements can reference techs defined later.
        forEachEntry(root, "techs.toml", "tech", [&](Fields& f, const toml::table&) {
            TechDef t;
            t.key = f.string("key");
            if (checkUniqueKey(f, content_.techs, t.key)) content_.techs.push_back(std::move(t));
        });
        content_.buildIndices();
        std::vector<bool> filled(content_.techs.size(), false);
        forEachEntry(root, "techs.toml", "tech", [&](Fields& f, const toml::table&) {
            f.allowOnly({"key", "name", "category", "description", "max_level", "base_cost", "requires"});
            const auto idx = content_.findTech(f.string("key", false));
            if (!idx || filled[idx->index()]) return;  // missing or duplicate key: reported in pass 1
            filled[idx->index()] = true;
            TechDef& t = content_.techs[idx->index()];
            t.name = f.string("name");
            t.category = f.string("category", false, "General");
            t.description = f.string("description", false);
            t.maxLevel = f.integer32("max_level", true, 1, 1);
            t.baseCost = f.integer("base_cost", true, 100, 1);
            t.prerequisites = f.requirements("requires", content_);
            for (const auto& req : t.prerequisites)
                if (req.tech == *idx) f.error("a tech cannot require itself");
        });
    }

    void loadHulls(const toml::table& root) {
        forEachEntry(root, "hulls.toml", "hull", [&](Fields& f, const toml::table&) {
            f.allowOnly({"key", "name", "description", "size", "structure", "cost", "engines_per_move", "max_engines", "requires"});
            HullDef h;
            h.key = f.string("key");
            h.name = f.string("name");
            h.description = f.string("description", false);
            h.size = f.integer32("size", true, 0, 1);
            h.structure = f.integer32("structure", true, 0, 1);
            h.cost = f.resources("cost", true);
            h.enginesPerMove = f.integer32("engines_per_move", true, 1, 1);
            h.maxEngines = f.integer32("max_engines", true, 0, 0);
            h.prerequisites = f.requirements("requires", content_);
            if (checkUniqueKey(f, content_.hulls, h.key)) content_.hulls.push_back(std::move(h));
        });
    }

    void loadComponents(const toml::table& root) {
        forEachEntry(root, "components.toml", "component", [&](Fields& f, const toml::table&) {
            f.allowOnly({"key", "name", "description", "tonnage", "structure", "cost", "abilities", "weapon", "requires"});
            ComponentDef c;
            c.key = f.string("key");
            c.name = f.string("name");
            c.description = f.string("description", false);
            c.tonnage = f.integer32("tonnage", true, 0, 0);
            c.structure = f.integer32("structure", false, 0, 0);
            c.cost = f.resources("cost", true);
            c.abilities = f.abilities("abilities");
            if (const toml::table* w = f.subtable("weapon")) {
                Fields wf(*w, f.context() + " weapon", errors_);
                wf.allowOnly({"damage", "range"});
                c.weapon = WeaponStats{wf.integer32("damage", true, 0, 1), wf.integer32("range", true, 1, 0)};
            }
            c.prerequisites = f.requirements("requires", content_);
            if (checkUniqueKey(f, content_.components, c.key)) content_.components.push_back(std::move(c));
        });
    }

    void loadFacilities(const toml::table& root) {
        forEachEntry(root, "facilities.toml", "facility", [&](Fields& f, const toml::table&) {
            f.allowOnly({"key", "name", "description", "cost", "abilities", "requires"});
            FacilityDef d;
            d.key = f.string("key");
            d.name = f.string("name");
            d.description = f.string("description", false);
            d.cost = f.resources("cost", true);
            d.abilities = f.abilities("abilities");
            d.prerequisites = f.requirements("requires", content_);
            if (checkUniqueKey(f, content_.facilities, d.key)) content_.facilities.push_back(std::move(d));
        });
        content_.buildIndices();
    }

    static std::optional<uint32_t> parseColor(std::string_view s) {
        if (s.size() != 7 || s[0] != '#') return std::nullopt;
        uint32_t v = 0;
        for (char ch : s.substr(1)) {
            v <<= 4;
            if (ch >= '0' && ch <= '9') v |= static_cast<uint32_t>(ch - '0');
            else if (ch >= 'a' && ch <= 'f') v |= static_cast<uint32_t>(ch - 'a' + 10);
            else if (ch >= 'A' && ch <= 'F') v |= static_cast<uint32_t>(ch - 'A' + 10);
            else return std::nullopt;
        }
        return v;
    }

    void loadRaces(const toml::table& root) {
        forEachEntry(root, "races.toml", "race", [&](Fields& f, const toml::table&) {
            f.allowOnly({"key", "name", "empire_name", "description", "color", "surface", "breathes", "colony_component", "bonus_techs"});
            RaceDef r;
            r.key = f.string("key");
            r.name = f.string("name");
            r.empireName = f.string("empire_name");
            r.description = f.string("description", false);
            const std::string color = f.string("color");
            if (auto c = parseColor(color)) r.color = *c;
            else if (!color.empty()) f.error(std::format("color '{}' must look like \"#4a90e2\"", color));
            const std::string surface = f.string("surface");
            if (auto s = parsePlanetSurface(surface)) r.nativeSurface = *s;
            else if (!surface.empty()) f.error(std::format("unknown surface '{}' (rock, ice, gas)", surface));
            const std::string atmo = f.string("breathes");
            if (auto a = parseAtmosphere(atmo)) r.breathes = *a;
            else if (!atmo.empty()) f.error(std::format("unknown atmosphere '{}'", atmo));
            const std::string colony = f.string("colony_component");
            if (auto c = content_.findComponent(colony)) r.colonyComponent = *c;
            else if (!colony.empty()) f.error(std::format("unknown component '{}'", colony));
            r.bonusTechs = f.requirements("bonus_techs", content_);
            if (checkUniqueKey(f, content_.races, r.key)) content_.races.push_back(std::move(r));
        });
        content_.buildIndices();
    }

    void loadDesigns(const toml::table& root) {
        forEachEntry(root, "designs.toml", "design", [&](Fields& f, const toml::table&) {
            f.allowOnly({"name", "role", "hull", "components"});
            DesignTemplate d;
            d.name = f.string("name");
            d.role = f.string("role");
            if (d.role != "scout" && d.role != "colony" && d.role != "warship" && !d.role.empty())
                f.error(std::format("role '{}' must be scout, colony or warship", d.role));
            const std::string hull = f.string("hull");
            if (auto h = content_.findHull(hull)) d.hull = *h;
            else if (!hull.empty()) f.error(std::format("unknown hull '{}'", hull));
            for (const std::string& key : f.stringList("components", true)) {
                if (key == "{colony_module}") {
                    d.components.push_back(ComponentIndex{});
                } else if (auto c = content_.findComponent(key)) {
                    d.components.push_back(*c);
                } else {
                    f.error(std::format("unknown component '{}'", key));
                }
            }
            if (content_.findDesignTemplate(d.name)) f.error(std::format("duplicate design name '{}'", d.name));
            content_.designTemplates.push_back(std::move(d));
        });
    }

    void loadRules(const toml::table& root) {
        Rules& r = content_.rules;
        Fields top(root, "rules.toml", errors_);
        top.allowOnly({"galaxy", "start", "population", "construction", "ships", "combat"});

        if (const toml::table* t = top.subtable("galaxy", true)) {
            Fields f(*t, "rules.toml [galaxy]", errors_);
            f.allowOnly({"default_systems", "sector_radius"});
            r.defaultSystemCount = f.integer32("default_systems", false, r.defaultSystemCount, 2);
            r.sectorRadius = f.integer32("sector_radius", false, r.sectorRadius, 4);
            if (r.sectorRadius > 20) f.error("sector_radius must be at most 20");
        }

        if (const toml::table* t = top.subtable("start", true)) {
            Fields f(*t, "rules.toml [start]", errors_);
            f.allowOnly({"resources", "homeworld_population", "homeworld_facilities", "techs", "ships"});
            r.startingResources = f.resources("resources");
            r.homeworldPopulation = f.integer("homeworld_population", false, r.homeworldPopulation, 1);
            for (const std::string& key : f.stringList("homeworld_facilities")) {
                if (auto fac = content_.findFacility(key)) r.homeworldFacilities.push_back(*fac);
                else f.error(std::format("homeworld_facilities: unknown facility '{}'", key));
            }
            r.startingTechs = f.requirements("techs", content_);
            if (const toml::node* ships = t->get("ships")) {
                if (const auto* arr = ships->as_array()) {
                    for (const toml::node& item : *arr) {
                        const auto* st = item.as_table();
                        if (!st) {
                            f.error("ships entries must be tables like { design = \"Scout\", count = 2 }");
                            continue;
                        }
                        Fields sf(*st, std::format("rules.toml [start] ships{}", lineOf(item)), errors_);
                        sf.allowOnly({"design", "count"});
                        StartingShip s{sf.string("design"), sf.integer32("count", false, 1, 1, 100)};
                        if (!s.design.empty() && !content_.findDesignTemplate(s.design))
                            sf.error(std::format("unknown design '{}' (see designs.toml)", s.design));
                        r.startingShips.push_back(std::move(s));
                    }
                } else {
                    f.error("'ships' must be an array of tables");
                }
            }
        }

        auto bySize = [&](Fields& f, std::string_view key, auto& out) {
            const toml::table* t = f.subtable(key);
            if (!t) return;
            for (auto&& [name, value] : *t) {
                const auto size = parsePlanetSize(name.str());
                const auto* i = value.as_integer();
                if (!size) f.error(std::format("{}: unknown planet size '{}'", key, name.str()));
                else if (!i || i->get() < 0) f.error(std::format("{}.{} must be a non-negative integer", key, name.str()));
                else out[enumIndex(*size)] = static_cast<std::remove_reference_t<decltype(out[0])>>(i->get());
            }
        };

        if (const toml::table* t = top.subtable("population", true)) {
            Fields f(*t, "rules.toml [population]", errors_);
            f.allowOnly({"colony_start", "growth_percent", "hostile_atmosphere_percent", "max_by_size", "facility_slots"});
            r.colonyStartPopulation = f.integer("colony_start", false, r.colonyStartPopulation, 1);
            r.popGrowthPercent = f.integer32("growth_percent", false, r.popGrowthPercent, 0);
            r.hostileAtmospherePopPercent = f.integer32("hostile_atmosphere_percent", false, r.hostileAtmospherePopPercent, 0);
            bySize(f, "max_by_size", r.maxPopulation);
            bySize(f, "facility_slots", r.facilitySlots);
        }

        if (const toml::table* t = top.subtable("construction")) {
            Fields f(*t, "rules.toml [construction]", errors_);
            f.allowOnly({"base_colony_rate"});
            r.baseColonyConstructionRate = f.integer("base_colony_rate", false, r.baseColonyConstructionRate, 0);
        }

        if (const toml::table* t = top.subtable("ships")) {
            Fields f(*t, "rules.toml [ships]", errors_);
            f.allowOnly({"required_abilities"});
            for (const std::string& key : f.stringList("required_abilities")) {
                if (auto a = parseAbilityType(key)) r.requiredShipAbilities.push_back(*a);
                else f.error(std::format("required_abilities: unknown ability '{}'", key));
            }
        }

        if (const toml::table* t = top.subtable("combat")) {
            Fields f(*t, "rules.toml [combat]", errors_);
            f.allowOnly({"rounds"});
            r.combatRounds = f.integer32("rounds", false, r.combatRounds, 1);
        }
    }

    // Every requirement must name a level the prerequisite can actually reach.
    void checkLevels(const std::vector<TechRequirement>& reqs, std::string_view where) {
        for (const auto& req : reqs) {
            const TechDef& t = content_.tech(req.tech);
            if (req.level > t.maxLevel)
                errors_.push_back(std::format("{}: requires {} level {}, but its max_level is {}", where, t.key, req.level, t.maxLevel));
        }
    }

    // Prerequisite cycles make every tech on the cycle unresearchable.
    void checkTechCycles() {
        enum class Mark : uint8_t { None, Visiting, Done };
        std::vector<Mark> marks(content_.techs.size(), Mark::None);
        std::vector<size_t> stack;
        std::function<bool(size_t)> visit = [&](size_t i) {
            if (marks[i] == Mark::Done) return false;
            if (marks[i] == Mark::Visiting) {
                std::string cycle;
                const auto start = std::find(stack.begin(), stack.end(), i);
                for (auto it = start; it != stack.end(); ++it) cycle += content_.techs[*it].key + " -> ";
                errors_.push_back(std::format("techs.toml: prerequisite cycle: {}{}", cycle, content_.techs[i].key));
                return true;
            }
            marks[i] = Mark::Visiting;
            stack.push_back(i);
            for (const auto& req : content_.techs[i].prerequisites)
                if (visit(req.tech.index())) return true;
            stack.pop_back();
            marks[i] = Mark::Done;
            return false;
        };
        for (size_t i = 0; i < content_.techs.size(); ++i)
            if (visit(i)) return;  // one cycle report is enough
    }

    // Cross-file consistency checks.
    void validate() {
        if (content_.races.empty()) errors_.push_back("races.toml: at least one race is required");
        checkTechCycles();
        for (const auto& t : content_.techs) checkLevels(t.prerequisites, std::format("techs.toml [tech '{}']", t.key));
        for (const auto& h : content_.hulls) checkLevels(h.prerequisites, std::format("hulls.toml [hull '{}']", h.key));
        for (const auto& comp : content_.components)
            checkLevels(comp.prerequisites, std::format("components.toml [component '{}']", comp.key));
        for (const auto& f : content_.facilities) checkLevels(f.prerequisites, std::format("facilities.toml [facility '{}']", f.key));
        for (const auto& r : content_.races) {
            checkLevels(r.bonusTechs, std::format("races.toml [race '{}'] bonus_techs", r.key));
            if (!r.colonyComponent.valid()) continue;
            const ComponentDef& module = content_.component(r.colonyComponent);
            const bool fits = std::any_of(module.abilities.begin(), module.abilities.end(), [&](const Ability& a) {
                return a.type == AbilityType::Colonize && parsePlanetSurface(a.param) == r.nativeSurface;
            });
            if (!fits)
                errors_.push_back(std::format("races.toml [race '{}']: colony_component '{}' cannot colonize the race's native {} worlds",
                                              r.key, module.key, displayName(r.nativeSurface)));
        }
        checkLevels(content_.rules.startingTechs, "rules.toml [start] techs");
        for (const auto& d : content_.designTemplates) {
            if (!d.hull.valid()) continue;
            const HullDef& hull = content_.hull(d.hull);
            // Check tonnage using the largest colony component any race might substitute.
            int tonnage = 0;
            int engines = 0;
            for (ComponentIndex c : d.components) {
                if (!c.valid()) {
                    int maxColony = 0;
                    for (const auto& race : content_.races)
                        if (race.colonyComponent.valid())
                            maxColony = std::max(maxColony, content_.component(race.colonyComponent).tonnage);
                    tonnage += maxColony;
                    continue;
                }
                const ComponentDef& comp = content_.component(c);
                tonnage += comp.tonnage;
                if (comp.hasAbility(AbilityType::Movement)) ++engines;
            }
            if (tonnage > hull.size)
                errors_.push_back(std::format("designs.toml [design '{}']: components need {} kT but hull '{}' holds {} kT",
                                              d.name, tonnage, hull.key, hull.size));
            if (engines > hull.maxEngines)
                errors_.push_back(std::format("designs.toml [design '{}']: {} engines exceed hull '{}' maximum of {}",
                                              d.name, engines, hull.key, hull.maxEngines));
        }
    }

    std::filesystem::path dir_;
    Content content_;
    std::vector<std::string> errors_;
};

template <class Map>
std::optional<uint32_t> lookup(const Map& map, std::string_view key) {
    const auto it = map.find(std::string(key));
    if (it == map.end()) return std::nullopt;
    return it->second;
}

template <class Vec, class Map>
void index(const Vec& items, Map& map) {
    map.clear();
    for (size_t i = 0; i < items.size(); ++i) map.emplace(items[i].key, static_cast<uint32_t>(i));
}

} // namespace

std::string_view displayName(AbilityType t) { return kAbilityNames[enumIndex(t)]; }

std::optional<AbilityType> parseAbilityType(std::string_view key) {
    const auto it = std::find(kAbilityKeys.begin(), kAbilityKeys.end(), key);
    if (it == kAbilityKeys.end()) return std::nullopt;
    return static_cast<AbilityType>(it - kAbilityKeys.begin());
}

int ComponentDef::abilityTotal(AbilityType t) const {
    int total = 0;
    for (const auto& a : abilities)
        if (a.type == t) total += a.amount;
    return total;
}

int FacilityDef::abilityTotal(AbilityType t) const {
    int total = 0;
    for (const auto& a : abilities)
        if (a.type == t) total += a.amount;
    return total;
}

std::optional<TechIndex> Content::findTech(std::string_view key) const {
    if (auto i = lookup(techIndex_, key)) return TechIndex{*i};
    return std::nullopt;
}
std::optional<HullIndex> Content::findHull(std::string_view key) const {
    if (auto i = lookup(hullIndex_, key)) return HullIndex{*i};
    return std::nullopt;
}
std::optional<ComponentIndex> Content::findComponent(std::string_view key) const {
    if (auto i = lookup(componentIndex_, key)) return ComponentIndex{*i};
    return std::nullopt;
}
std::optional<FacilityIndex> Content::findFacility(std::string_view key) const {
    if (auto i = lookup(facilityIndex_, key)) return FacilityIndex{*i};
    return std::nullopt;
}
std::optional<RaceIndex> Content::findRace(std::string_view key) const {
    if (auto i = lookup(raceIndex_, key)) return RaceIndex{*i};
    return std::nullopt;
}
const DesignTemplate* Content::findDesignTemplate(std::string_view name) const {
    for (const auto& d : designTemplates)
        if (d.name == name) return &d;
    return nullptr;
}

void Content::buildIndices() {
    index(techs, techIndex_);
    index(hulls, hullIndex_);
    index(components, componentIndex_);
    index(facilities, facilityIndex_);
    index(races, raceIndex_);
}

std::expected<Content, std::vector<std::string>> loadContent(const std::filesystem::path& dataDir) {
    return Loader(dataDir).run();
}

} // namespace opense4::sim
