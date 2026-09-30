#include "game/map_file.hpp"

#include "datafile/datafile.hpp"
#include "game/generate.hpp"
#include "game/query.hpp"

#include <toml++/toml.hpp>

#include <algorithm>
#include <format>
#include <fstream>
#include <map>
#include <sstream>

namespace opense4::game {

namespace {

using datafile::keysEqual;

constexpr std::string_view kFormatName = "opense4-map";

// A TOML basic string.
std::string tomlString(std::string_view s) {
    std::string out = "\"";
    for (const char ch : s) {
        const auto c = static_cast<unsigned char>(ch);
        switch (ch) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\t': out += "\\t"; break;
            case '\r': out += "\\r"; break;
            default:
                if (c < 0x20 || c == 0x7f) out += std::format("\\u{:04X}", static_cast<unsigned>(c));
                else out += ch;
        }
    }
    return out + "\"";
}

std::string abilityList(const std::vector<ruleset::Ability>& list) {
    std::string out = "[";
    for (size_t i = 0; i < list.size(); ++i) {
        const ruleset::Ability& a = list[i];
        out += i ? ", " : "";
        out += std::format("{{ type = {}, value1 = {}, value2 = {}", tomlString(a.type), tomlString(a.value1), tomlString(a.value2));
        if (!a.description.empty()) out += std::format(", description = {}", tomlString(a.description));
        out += " }";
    }
    return out + "]";
}

bool hasPlanetData(ObjectKind k) { return k == ObjectKind::Planet || k == ObjectKind::Asteroids; }
bool hasStarData(ObjectKind k) { return k == ObjectKind::Star || k == ObjectKind::DestroyedStar; }

// ---- Reading ----------------------------------------------------------------------------------------

struct Reader {
    const ruleset::Ruleset& rs;
    std::vector<std::string> warnings;

    std::expected<std::vector<ruleset::Ability>, std::string> abilities(const toml::table& t, std::string_view where) const {
        std::vector<ruleset::Ability> out;
        const toml::node* node = t.get("abilities");
        if (!node) return out;
        const toml::array* list = node->as_array();
        if (!list) return std::unexpected(std::format("{}: 'abilities' must be a list", where));
        for (const toml::node& n : *list) {
            const toml::table* a = n.as_table();
            if (!a) return std::unexpected(std::format("{}: every ability is a table with type, value1 and value2", where));
            ruleset::Ability ab;
            ab.type = (*a)["type"].value_or(std::string{});
            if (ab.type.empty()) return std::unexpected(std::format("{}: an ability has no type", where));
            ab.value1 = (*a)["value1"].value_or(std::string{});
            ab.value2 = (*a)["value2"].value_or(std::string{});
            ab.description = (*a)["description"].value_or(std::string{});
            out.push_back(std::move(ab));
        }
        return out;
    }

    // The SectType record for an object of this kind: the index written, when
    // this data set has it with the same physical type, else the first record
    // of the kind with the same attributes, else the first of the kind.
    std::optional<uint32_t> sectorType(int64_t index, ObjectKind kind, const SpaceObject& wanted, std::string_view where) {
        const auto& types = rs.sectorObjectTypes;
        auto kindOf = [&](uint32_t i) { return parseObjectKind(types[i].physicalType); };
        if (index >= 0 && static_cast<uint64_t>(index) < types.size() && kindOf(static_cast<uint32_t>(index)) == kind)
            return static_cast<uint32_t>(index);
        std::optional<uint32_t> first;
        for (uint32_t i = 0; i < types.size(); ++i) {
            if (kindOf(i) != kind) continue;
            if (!first) first = i;
            const ruleset::SectorObjectType& t = types[i];
            const bool same = hasPlanetData(kind) ? keysEqual(t.planetSize, wanted.size) && keysEqual(t.planetPhysicalType, wanted.surface) &&
                                                        keysEqual(t.planetAtmosphere, wanted.atmosphere)
                              : hasStarData(kind) ? keysEqual(t.starSize, wanted.size) && keysEqual(t.starColor, wanted.starColor)
                                                  : true;
            if (same) {
                warnings.push_back(std::format("{}: SectType record {} is not a {} here; record {} is used.", where, index, displayName(kind), i));
                return i;
            }
        }
        if (first) warnings.push_back(std::format("{}: no SectType record matches; record {} is used.", where, *first));
        return first;
    }

    std::expected<LoadedMap, std::string> read(const toml::table& root) {
        LoadedMap out;
        QuadrantMap& m = out.map;
        if (root["format"].value_or(std::string{}) != kFormatName) return std::unexpected("Not an OpenSE4 map file.");
        const int64_t version = root["version"].value_or(int64_t{0});
        if (version < 1 || version > kMapFormatVersion) return std::unexpected("A map file from a newer version of OpenSE4.");
        m.name = root["name"].value_or(std::string{});
        Galaxy& g = m.galaxy;
        g.quadrantType = root["quadrant_type"].value_or(std::string{});
        g.width = static_cast<int>(root["width"].value_or(int64_t{kQuadrantWidth}));
        g.height = static_cast<int>(root["height"].value_or(int64_t{kQuadrantHeight}));
        if (g.width < 1 || g.height < 1 || g.width > 1000 || g.height > 1000) return std::unexpected("The map's width or height is out of range.");

        const toml::array* systems = root["systems"].as_array();
        if (!systems || systems->empty()) return std::unexpected("The map has no systems.");
        // Warp links are resolved once every object exists: (system, object) in file order.
        std::vector<std::vector<ObjectId>> ids;
        std::vector<std::pair<ObjectId, std::pair<int64_t, int64_t>>> links;
        for (size_t si = 0; si < systems->size(); ++si) {
            const toml::table* st = (*systems)[si].as_table();
            if (!st) return std::unexpected(std::format("System {} is not a table.", si + 1));
            StarSystem sys;
            sys.id = SystemId{si};
            sys.name = (*st)["name"].value_or(std::string{});
            const std::string where = std::format("System {} ({})", si + 1, sys.name);
            const auto x = (*st)["x"].value<int64_t>();
            const auto y = (*st)["y"].value<int64_t>();
            if (!x || !y || *x < 0 || *y < 0 || *x >= g.width || *y >= g.height) return std::unexpected(std::format("{}: x and y must lie on the map.", where));
            sys.position = {static_cast<int>(*x), static_cast<int>(*y)};
            const std::string typeName = (*st)["type"].value_or(std::string{});
            if (auto t = rs.findSystemType(typeName)) {
                sys.type = *t;
            } else {
                if (rs.systemTypes.empty()) return std::unexpected("The data set has no system types.");
                sys.type = ruleset::SystemTypeId{0u};
                warnings.push_back(std::format("{}: unknown system type '{}'; {} is used.", where, typeName, rs.systemTypes.front().name));
            }
            sys.physicalType = (*st)["physical_type"].value_or(rs.systemTypes[sys.type.index()].physicalType);
            auto sysAbilities = abilities(*st, where);
            if (!sysAbilities) return std::unexpected(sysAbilities.error());
            sys.abilities = std::move(*sysAbilities);
            g.systems.push_back(std::move(sys));
            ids.emplace_back();

            const toml::node* objNode = st->get("objects");
            const toml::array* objects = objNode ? objNode->as_array() : nullptr;
            if (objNode && !objects) return std::unexpected(std::format("{}: 'objects' must be a list.", where));
            for (size_t oi = 0; objects && oi < objects->size(); ++oi) {
                const toml::table* ot = (*objects)[oi].as_table();
                const std::string owhere = std::format("{}, object {}", where, oi + 1);
                if (!ot) return std::unexpected(std::format("{} is not a table.", owhere));
                const auto kind = parseObjectKind((*ot)["kind"].value_or(std::string{}));
                if (!kind) return std::unexpected(std::format("{}: unknown kind.", owhere));
                SpaceObject obj;
                obj.id = ObjectId{g.objects.size()};
                obj.kind = *kind;
                obj.system = SystemId{si};
                obj.name = (*ot)["name"].value_or(std::string{});
                const auto ox = (*ot)["x"].value<int64_t>();
                const auto oy = (*ot)["y"].value<int64_t>();
                if (!ox || !oy || *ox < 0 || *oy < 0 || *ox >= kSystemSize || *oy >= kSystemSize)
                    return std::unexpected(std::format("{}: x and y must be sectors 0 to {}.", owhere, kSystemSize - 1));
                obj.sector = Sector{static_cast<int>(*ox), static_cast<int>(*oy)};
                // Attributes as written; the SectType record fills what is missing.
                SpaceObject wanted = obj;
                wanted.size = (*ot)["size"].value_or(std::string{});
                wanted.surface = (*ot)["surface"].value_or(std::string{});
                wanted.atmosphere = (*ot)["atmosphere"].value_or(std::string{});
                wanted.starColor = (*ot)["star_color"].value_or(std::string{});
                const auto type = sectorType((*ot)["sect_type"].value_or(int64_t{-1}), *kind, wanted, owhere);
                if (!type) return std::unexpected(std::format("{}: the data set has no SectType record for a {}.", owhere, displayName(*kind)));
                applySectorType(rs, obj, *type);
                auto text = [&](std::string_view key, std::string& field) {
                    if (auto v = (*ot)[key].value<std::string>()) field = *v;
                };
                text("size", obj.size);
                if (hasPlanetData(*kind)) {
                    text("surface", obj.surface);
                    text("atmosphere", obj.atmosphere);
                    // Hundredths in the file; conditions never exceed 1.5 (spec 02 §2).
                    obj.conditions = Conditions::hundredths(std::clamp<int64_t>((*ot)["conditions"].value_or(int64_t{100}), 0, 150));
                    if (const toml::array* values = (*ot)["values"].as_array()) {
                        if (values->size() != obj.value.size())
                            return std::unexpected(std::format("{}: 'values' lists minerals, organics and radioactives.", owhere));
                        for (size_t k = 0; k < obj.value.size(); ++k)
                            obj.value[k] = static_cast<int>(std::clamp<int64_t>((*values)[k].value_or(int64_t{0}), 0, 1'000'000'000));
                    } else {
                        obj.value = {100, 100, 100};
                    }
                }
                if (hasStarData(*kind)) {
                    text("star_age", obj.starAge);
                    text("star_color", obj.starColor);
                    text("star_luminosity", obj.starLuminosity);
                }
                auto objAbilities = abilities(*ot, owhere);
                if (!objAbilities) return std::unexpected(objAbilities.error());
                obj.abilities = std::move(*objAbilities);
                if (*kind == ObjectKind::WarpPoint)
                    if (const toml::array* to = (*ot)["warp_to"].as_array()) {
                        if (to->size() != 2) return std::unexpected(std::format("{}: warp_to is [system, object].", owhere));
                        links.push_back({obj.id, {(*to)[0].value_or(int64_t{-1}), (*to)[1].value_or(int64_t{-1})}});
                    }
                g.systems.back().objects.push_back(obj.id);
                ids.back().push_back(obj.id);
                g.objects.push_back(std::move(obj));
            }
        }

        // Warp links: both ends point at each other (every link is two-way, spec 01 §8).
        for (const auto& [from, target] : links) {
            const auto [ts, to] = target;
            const std::string where = std::format("The warp point {}", g.object(from).name);
            if (ts < 0 || static_cast<size_t>(ts) >= ids.size() || to < 0 || static_cast<size_t>(to) >= ids[static_cast<size_t>(ts)].size())
                return std::unexpected(std::format("{} links to an object that does not exist.", where));
            const ObjectId dest = ids[static_cast<size_t>(ts)][static_cast<size_t>(to)];
            if (dest == from || g.object(dest).kind != ObjectKind::WarpPoint)
                return std::unexpected(std::format("{} must link to another warp point.", where));
            SpaceObject& a = g.object(from);
            SpaceObject& b = g.object(dest);
            if ((a.destination.valid() && a.destination != dest) || (b.destination.valid() && b.destination != from))
                return std::unexpected(std::format("{} is part of two different links.", where));
            a.destination = dest;
            b.destination = from;
        }

        // Starting points.
        if (const toml::node* startsNode = root.get("starts")) {
            const toml::array* starts = startsNode->as_array();
            if (!starts) return std::unexpected("'starts' must be a list.");
            for (size_t i = 0; i < starts->size(); ++i) {
                const toml::table* t = (*starts)[i].as_table();
                const std::string where = std::format("Starting point {}", i + 1);
                if (!t) return std::unexpected(std::format("{} is not a table.", where));
                const int64_t sys = (*t)["system"].value_or(int64_t{-1});
                const int64_t x = (*t)["x"].value_or(int64_t{-1});
                const int64_t y = (*t)["y"].value_or(int64_t{-1});
                const int64_t player = (*t)["player"].value_or(int64_t{0});
                if (sys < 0 || static_cast<size_t>(sys) >= g.systems.size()) return std::unexpected(std::format("{}: no such system.", where));
                if (x < 0 || y < 0 || x >= kSystemSize || y >= kSystemSize) return std::unexpected(std::format("{}: no such sector.", where));
                if (player < 0) return std::unexpected(std::format("{}: players are numbered from 1.", where));
                StartingPoint p;
                p.system = SystemId{static_cast<size_t>(sys)};
                p.sector = Sector{static_cast<int>(x), static_cast<int>(y)};
                p.player = player == 0 ? kCommonStart : static_cast<int>(player - 1);
                m.startingPoints.push_back(p);
            }
        }
        return out;
    }
};

} // namespace

std::string mapToText(const ruleset::Ruleset& rs, const QuadrantMap& map) {
    const Galaxy& g = map.galaxy;
    std::string out;
    out += "# An OpenSE4 map (docs/MAPS.md).\n";
    out += std::format("format = {}\nversion = {}\n", tomlString(kFormatName), kMapFormatVersion);
    out += std::format("name = {}\n", tomlString(map.name));
    if (!g.quadrantType.empty()) out += std::format("quadrant_type = {}\n", tomlString(g.quadrantType));
    out += std::format("width = {}\nheight = {}\n", g.width > 0 ? g.width : kQuadrantWidth, g.height > 0 ? g.height : kQuadrantHeight);

    // Objects are written in each system's list order; removed objects (no
    // longer in a list) are left out, so indices are recomputed.
    std::map<uint32_t, std::pair<size_t, size_t>> index;  // object -> (system, position in its list)
    for (size_t si = 0; si < g.systems.size(); ++si)
        for (size_t oi = 0; oi < g.systems[si].objects.size(); ++oi) index[g.systems[si].objects[oi].value] = {si, oi};

    for (const StarSystem& sys : g.systems) {
        out += "\n[[systems]]\n";
        out += std::format("name = {}\n", tomlString(sys.name));
        out += std::format("x = {}\ny = {}\n", sys.position.x, sys.position.y);
        const std::string typeName = sys.type.index() < rs.systemTypes.size() ? rs.systemTypes[sys.type.index()].name : std::string{};
        out += std::format("type = {}\n", tomlString(typeName));
        out += std::format("physical_type = {}\n", tomlString(sys.physicalType));
        if (!sys.abilities.empty()) out += std::format("abilities = {}\n", abilityList(sys.abilities));
        for (ObjectId id : sys.objects) {
            const SpaceObject& o = g.object(id);
            out += "\n  [[systems.objects]]\n";
            out += std::format("  kind = {}\n", tomlString(displayName(o.kind)));
            out += std::format("  sect_type = {}\n", o.sectorType);
            out += std::format("  name = {}\n", tomlString(o.name));
            out += std::format("  x = {}\n  y = {}\n", o.sector.x, o.sector.y);
            if (!o.size.empty()) out += std::format("  size = {}\n", tomlString(o.size));
            if (hasPlanetData(o.kind)) {
                out += std::format("  surface = {}\n  atmosphere = {}\n", tomlString(o.surface), tomlString(o.atmosphere));
                out += std::format("  conditions = {}\n", o.conditions.inHundredths());  // to the nearest hundredth
                out += std::format("  values = [{}, {}, {}]\n", o.value[0], o.value[1], o.value[2]);
            }
            if (hasStarData(o.kind)) {
                out += std::format("  star_age = {}\n  star_color = {}\n  star_luminosity = {}\n", tomlString(o.starAge), tomlString(o.starColor),
                                   tomlString(o.starLuminosity));
            }
            if (!o.abilities.empty()) out += std::format("  abilities = {}\n", abilityList(o.abilities));
            if (o.kind == ObjectKind::WarpPoint && o.destination.valid())
                if (auto it = index.find(o.destination.value); it != index.end())
                    out += std::format("  warp_to = [{}, {}]\n", it->second.first, it->second.second);
        }
    }
    for (const StartingPoint& p : map.startingPoints) {
        out += "\n[[starts]]\n";
        out += std::format("system = {}\nx = {}\ny = {}\n", p.system.index(), p.sector.x, p.sector.y);
        if (p.player != kCommonStart) out += std::format("player = {}\n", p.player + 1);
    }
    return out;
}

std::expected<LoadedMap, std::string> mapFromText(const ruleset::Ruleset& rs, std::string_view text) {
    toml::table root;
    try {
        root = toml::parse(text);
    } catch (const toml::parse_error& err) {
        return std::unexpected(std::format("Not a valid map file (line {}): {}", err.source().begin.line, err.description()));
    }
    Reader reader{rs, {}};
    auto out = reader.read(root);
    if (out) out->warnings = std::move(reader.warnings);
    return out;
}

std::expected<void, std::string> saveMapFile(const std::filesystem::path& file, const ruleset::Ruleset& rs, const QuadrantMap& map) {
    std::error_code ec;
    if (file.has_parent_path()) std::filesystem::create_directories(file.parent_path(), ec);
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    if (!out) return std::unexpected(std::format("Cannot write {}", file.string()));
    out << mapToText(rs, map);
    if (!out) return std::unexpected(std::format("Cannot write {}", file.string()));
    return {};
}

std::expected<LoadedMap, std::string> loadMapFile(const std::filesystem::path& file, const ruleset::Ruleset& rs) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return std::unexpected(std::format("Cannot read {}", file.string()));
    std::stringstream ss;
    ss << in.rdbuf();
    auto loaded = mapFromText(rs, ss.str());
    if (loaded && loaded->map.name.empty()) loaded->map.name = file.stem().string();
    return loaded;
}

QuadrantMap mapOfGame(const GameState& s, std::string name) {
    // The systems and their stellar objects, and the starting points the game
    // still holds; no capitals (spec 01 §12, §14 Q37, confirmed: binary).
    QuadrantMap m;
    m.name = std::move(name);
    m.galaxy = s.galaxy;
    m.startingPoints = s.startingPoints;
    return m;
}

std::string mapFileStem(std::string_view name) {
    std::string out;
    for (const char c : name) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == ' ';
        if (ok) out += c;
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    while (!out.empty() && out.front() == ' ') out.erase(out.begin());
    return out.empty() ? std::string("Map") : out;
}

} // namespace opense4::game
