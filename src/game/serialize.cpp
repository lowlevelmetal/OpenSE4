#include "game/serialize.hpp"

#include "game/rules.hpp"
#include "game/serialize_io.hpp"

#include <algorithm>
#include <cstring>
#include <format>
#include <fstream>
#include <iterator>
#include <system_error>

namespace opense4::game {

namespace {

constexpr std::string_view kStateMagic = "OSE4STAT";
constexpr std::string_view kOrdersMagic = "OSE4ORDR";
constexpr std::string_view kSaveMagic = "OSE4SAVE";

struct KnownMagic {
    std::string_view magic;
    std::string_view what;
};
constexpr KnownMagic kKnownMagics[] = {
    {kStateMagic, "game state"},
    {kOrdersMagic, "order list"},
    {kSaveMagic, "saved game"},
    {"OSE4PLRF", "orders file (.plr)"},
};

void putU32(uint8_t* p, uint32_t v) {
    for (int i = 0; i < 4; ++i) p[i] = static_cast<uint8_t>(v >> (8 * i));
}
void putU64(uint8_t* p, uint64_t v) {
    for (int i = 0; i < 8; ++i) p[i] = static_cast<uint8_t>(v >> (8 * i));
}
uint32_t getU32(const uint8_t* p) {
    uint32_t v = 0;
    for (int i = 0; i < 4; ++i) v |= static_cast<uint32_t>(p[i]) << (8 * i);
    return v;
}
uint64_t getU64(const uint8_t* p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i) v |= static_cast<uint64_t>(p[i]) << (8 * i);
    return v;
}

uint64_t fnv(std::span<const uint8_t> bytes) {
    Hasher h;
    h.bytes(bytes.data(), bytes.size());
    return h.value();
}

// Fills the envelope in front of a payload that was appended after
// kEnvelopeSize placeholder bytes.
void seal(std::vector<uint8_t>& buf, std::string_view magic) {
    const std::span<const uint8_t> payload(buf.data() + kEnvelopeSize, buf.size() - kEnvelopeSize);
    std::memcpy(buf.data(), magic.data(), 8);
    putU32(buf.data() + 8, kSaveVersion);
    putU32(buf.data() + 12, 0);
    putU64(buf.data() + 16, payload.size());
    putU64(buf.data() + 24, fnv(payload));
}

template <class T>
std::vector<uint8_t> encodeSealed(std::string_view magic, const T& value) {
    std::vector<uint8_t> buf(kEnvelopeSize);
    serial::write(buf, value);
    seal(buf, magic);
    return buf;
}

template <class T>
std::expected<T, std::string> decodeSealed(std::span<const uint8_t> bytes, std::string_view magic, std::string_view what) {
    auto env = unwrapEnvelope(bytes, magic, what);
    if (!env) return std::unexpected(env.error());
    T value{};
    std::string error;
    if (!serial::decode(env->payload, value, error, env->version)) return std::unexpected(std::format("the {} is corrupt: {}", what, error));
    return value;
}

// ASCII only, whatever the C locale (the same bytes on every platform).
std::string lower(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

std::string_view fingerprintOf(std::string_view id) {
    const size_t hash = id.rfind('#');
    return hash == std::string_view::npos ? id : id.substr(hash + 1);
}

} // namespace

// ---- Envelopes ---------------------------------------------------------------------------------

std::vector<uint8_t> wrapEnvelope(std::string_view magic, std::span<const uint8_t> payload) {
    std::vector<uint8_t> buf(kEnvelopeSize + payload.size());
    std::copy(payload.begin(), payload.end(), buf.begin() + kEnvelopeSize);
    seal(buf, magic);
    return buf;
}

std::expected<Envelope, std::string> unwrapEnvelope(std::span<const uint8_t> bytes, std::string_view magic, std::string_view what) {
    const std::string_view head(reinterpret_cast<const char*>(bytes.data()), std::min<size_t>(bytes.size(), 8));
    if (head != magic) {
        for (const KnownMagic& k : kKnownMagics)
            if (head == k.magic) return std::unexpected(std::format("this is an OpenSE4 {}, not a {}", k.what, what));
        if (bytes.size() < 8) return std::unexpected(std::format("the {} is truncated ({} bytes)", what, bytes.size()));
        return std::unexpected(std::format("not an OpenSE4 {}", what));
    }
    if (bytes.size() < kEnvelopeSize) return std::unexpected(std::format("the {} is truncated ({} bytes)", what, bytes.size()));
    const uint32_t version = getU32(bytes.data() + 8);
    const uint32_t flags = getU32(bytes.data() + 12);
    const uint64_t size = getU64(bytes.data() + 16);
    const uint64_t checksum = getU64(bytes.data() + 24);
    if (version > kSaveVersion)
        return std::unexpected(std::format("the {} was written by a newer version of OpenSE4 (format {}; this build reads formats {} to {})",
                                           what, version, kMinSaveVersion, kSaveVersion));
    if (version < kMinSaveVersion)
        return std::unexpected(std::format("the {} uses an old format ({}) that this build no longer reads (formats {} to {})", what,
                                           version, kMinSaveVersion, kSaveVersion));
    if (flags != 0) return std::unexpected(std::format("the {} uses unsupported features (flags {:#x})", what, flags));
    const uint64_t present = bytes.size() - kEnvelopeSize;
    if (size > present) return std::unexpected(std::format("the {} is truncated ({} of {} bytes)", what, present, size));
    if (size < present) return std::unexpected(std::format("the {} has {} unexpected bytes at the end", what, present - size));
    const std::span<const uint8_t> payload = bytes.subspan(kEnvelopeSize);
    if (fnv(payload) != checksum) return std::unexpected(std::format("the {} is corrupt (checksum mismatch)", what));
    return Envelope{payload, version};
}

// ---- State and orders --------------------------------------------------------------------------------

std::vector<uint8_t> serializeState(const GameState& s) { return encodeSealed(kStateMagic, s); }

std::expected<GameState, std::string> deserializeState(std::span<const uint8_t> bytes) {
    auto s = decodeSealed<GameState>(bytes, kStateMagic, "game state");
    if (s)
        if (std::string problem = validateState(*s); !problem.empty())
            return std::unexpected(std::format("the game state is inconsistent: {}", problem));
    return s;
}

std::string validateState(const GameState& s, const Rules* rules) {
    const size_t nSys = s.galaxy.systems.size();
    const size_t nObj = s.galaxy.objects.size();
    const size_t nEmp = s.empires.size();
    const size_t nDes = s.designs.size();
    // "none" (invalid) or an existing entry.
    auto empireOk = [&](EmpireId e) { return !e.valid() || e.index() < nEmp; };
    auto systemOk = [&](SystemId x) { return !x.valid() || x.index() < nSys; };
    auto objectOk = [&](ObjectId x) { return !x.valid() || x.index() < nObj; };
    auto designOk = [&](DesignId x) { return !x.valid() || x.index() < nDes; };
    auto placeOk = [&](const Location& l) { return l.system.valid() && l.system.index() < nSys && l.sector.valid(); };
    auto ordersOk = [&](const std::vector<Order>& orders) {
        return std::all_of(orders.begin(), orders.end(), [&](const Order& o) {
            return o.kind < OrderKind::Count && systemOk(o.location.system) && objectOk(o.object) && designOk(o.design);
        });
    };
    auto cargoOk = [&](const Cargo& c) {
        return std::all_of(c.population.begin(), c.population.end(), [&](const PopulationGroup& p) { return p.race.valid() && empireOk(p.race); }) &&
               std::all_of(c.units.begin(), c.units.end(), [&](const UnitStack& u) { return u.design.valid() && designOk(u.design); });
    };
    auto queueOk = [&](const ConstructionQueue& q) {
        for (const QueueItem& item : q.items) {
            if (item.kind == QueueItem::Kind::Vehicle && !(item.design.valid() && designOk(item.design))) return false;
            if (rules && item.kind != QueueItem::Kind::Vehicle && item.facility >= rules->data().facilities.size()) return false;
        }
        return true;
    };

    for (size_t i = 0; i < nSys; ++i) {
        const StarSystem& sys = s.galaxy.systems[i];
        if (sys.id.index() != i) return std::format("system {} has id {}", i, sys.id.value);
        for (ObjectId o : sys.objects)
            if (!o.valid() || o.index() >= nObj) return std::format("system {} lists a missing object", i);
        if (rules && sys.type.valid() && sys.type.index() >= rules->data().systemTypes.size())
            return std::format("system {} has a system type the data set lacks", i);
    }
    for (size_t i = 0; i < nObj; ++i) {
        const SpaceObject& o = s.galaxy.objects[i];
        if (o.id.index() != i) return std::format("object {} has id {}", i, o.id.value);
        if (o.kind >= ObjectKind::Count || !o.system.valid() || o.system.index() >= nSys || !o.sector.valid() || !objectOk(o.destination))
            return std::format("object {} has an invalid place or link", i);
    }
    if (s.colonies.size() > nObj) return "there are more colony slots than objects";
    for (size_t i = 0; i < s.colonies.size(); ++i) {
        const auto& c = s.colonies[i];
        if (!c) continue;
        if (c->planet.index() != i || !c->owner.valid() || !empireOk(c->owner)) return std::format("colony {} has a wrong planet or owner", i);
        if (!cargoOk(c->cargo) || !queueOk(c->queue)) return std::format("colony {} refers to missing designs or empires", i);
        for (const PopulationGroup& p : c->population)
            if (!p.race.valid() || !empireOk(p.race)) return std::format("colony {} has population of a missing empire", i);
        if (rules)
            for (uint32_t f : c->facilities)
                if (f >= rules->data().facilities.size()) return std::format("colony {} has a facility the data set lacks", i);
    }
    for (const LeftFacilities& l : s.leftFacilities) {
        if (!l.planet.valid() || l.planet.index() >= nObj) return "facilities are left on a missing planet";
        if (rules)
            for (uint32_t f : l.facilities)
                if (f >= rules->data().facilities.size()) return "a planet holds a left facility the data set lacks";
    }
    for (size_t i = 0; i < nEmp; ++i) {
        const Empire& e = s.empires[i];
        if (e.id.index() != i) return std::format("empire {} has id {}", i, e.id.value);
        if (e.relations.size() != nEmp) return std::format("empire {} has {} relations for {} empires", i, e.relations.size(), nEmp);
        for (DesignId d : e.designs)
            if (!d.valid() || d.index() >= nDes) return std::format("empire {} lists a missing design", i);
        for (const SeenDesign& d : e.knowledge.seenDesigns)
            if (!d.design.valid() || d.design.index() >= nDes) return std::format("empire {} has seen a missing design", i);
        for (const HistoryEntry& h : e.historyEvents)
            if (!empireOk(h.empire) || (h.location && !placeOk(*h.location)))
                return std::format("empire {} has a history entry about a missing empire or place", i);
        for (const Waypoint& w : e.waypoints)
            if (w.set && !placeOk(w.location)) return std::format("empire {} has a waypoint outside the galaxy", i);
        if (rules) {
            const size_t nTech = rules->data().techAreas.size();
            if (e.techLevels.size() > nTech) return std::format("empire {} knows more tech areas than the data set has", i);
            for (const ResearchProject& p : e.research)
                if (!p.area.valid() || p.area.index() >= nTech) return std::format("empire {} researches a missing tech area", i);
        }
    }
    for (size_t i = 0; i < nDes; ++i) {
        const Design& d = s.designs[i];
        if (d.id.index() != i || !d.owner.valid() || !empireOk(d.owner)) return std::format("design {} has a wrong id or owner", i);
        if (rules) {
            const auto& data = rules->data();
            if (d.hull >= data.vehicleSizes.size()) return std::format("design {} uses a hull the data set lacks", i);
            for (const DesignEntry& entry : d.entries)
                if (entry.component >= data.components.size() || entry.mount < -1 || (entry.mount >= 0 && static_cast<size_t>(entry.mount) >= data.weaponMounts.size()))
                    return std::format("design {} uses a component the data set lacks", i);
        }
    }
    for (size_t i = 0; i < s.vehicles.size(); ++i) {
        const Vehicle& v = s.vehicles[i];
        if (i > 0 && !(s.vehicles[i - 1].id < v.id)) return "vehicles are not sorted by id";
        if (!v.id.valid() || v.id.value >= s.nextVehicleId) return std::format("vehicle {} has an id beyond the next free one", v.id.value);
        if (!v.owner.valid() || !empireOk(v.owner) || !v.design.valid() || !designOk(v.design) || !placeOk(v.location))
            return std::format("vehicle {} has a wrong owner, design or place", v.id.value);
        if (v.damage.size() != s.designs[v.design.index()].entries.size())
            return std::format("vehicle {} has damage for {} parts; its design has {}", v.id.value, v.damage.size(),
                               s.designs[v.design.index()].entries.size());
        if (!ordersOk(v.orders) || !cargoOk(v.cargo) || !queueOk(v.queue)) return std::format("vehicle {} refers to missing things", v.id.value);
        if (!v.mixed.empty() && v.count > 0) {
            // A group that mixes designs: two stacks or more, the first is `design`, the counts add up to `count`.
            int64_t total = 0;
            for (const UnitStack& st : v.mixed) {
                if (!st.design.valid() || !designOk(st.design) || st.count <= 0) return std::format("unit group {} holds a wrong stack", v.id.value);
                total += st.count;
            }
            if (v.mixed.size() < 2 || v.mixed.front().design != v.design || total != v.count)
                return std::format("unit group {} does not match its stacks", v.id.value);
        }
    }
    {
        // One object list: no two objects hold the same slot (spec 03 §19 Q62).
        const std::vector<ObjectRef> order = objectOrder(s);
        for (size_t i = 1; i < order.size(); ++i)
            if (order[i - 1].slot == order[i].slot) return std::format("two objects hold slot {}", order[i].slot);
    }
    for (size_t i = 0; i < s.fleets.size(); ++i) {
        const Fleet& f = s.fleets[i];
        if (i > 0 && !(s.fleets[i - 1].id < f.id)) return "fleets are not sorted by id";
        if (!f.id.valid() || f.id.value >= s.nextFleetId) return std::format("fleet {} has an id beyond the next free one", f.id.value);
        if (!f.owner.valid() || !empireOk(f.owner) || !placeOk(f.location)) return std::format("fleet {} has a wrong owner or place", f.id.value);
    }
    for (const DiplomaticMessage& m : s.messages)
        if (!empireOk(m.from) || !empireOk(m.to) || !empireOk(m.thirdEmpire)) return "a diplomatic message names a missing empire";
    if (!empireOk(s.winner)) return "the winner does not exist";
    return {};
}

std::vector<uint8_t> serializeOrders(const EmpireOrders& o) { return encodeSealed(kOrdersMagic, o); }

std::expected<EmpireOrders, std::string> deserializeOrders(std::span<const uint8_t> bytes) {
    return decodeSealed<EmpireOrders>(bytes, kOrdersMagic, "order list");
}

uint64_t stateChecksum(const GameState& s) { return serial::hash(s); }

namespace {

constexpr std::string_view kStatePartNames[] = {
    "date and options", "galaxy", "colonies", "empires", "designs", "vehicles", "fleets", "messages", "events",
    "battles", "counters", "random numbers", "player turn", "map",
};

template <class... T>
uint64_t hashParts(const T&... parts) {
    serial::HashWriter w;
    (serial::io(w, const_cast<T&>(parts)), ...);  // writers never modify
    return w.value();
}

} // namespace

std::vector<uint64_t> statePartHashes(const GameState& s) {
    return {
        hashParts(s.turn, s.seed, s.options),
        hashParts(s.galaxy),
        hashParts(s.colonies),
        hashParts(s.empires),
        hashParts(s.designs),
        hashParts(s.vehicles),
        hashParts(s.fleets),
        hashParts(s.messages),
        hashParts(s.pendingEvents),
        hashParts(s.combats),
        hashParts(s.nextVehicleId, s.nextFleetId, s.nextMessageId, s.peacefulTurns, s.gameOver, s.winner, s.arrivals),
        hashParts(s.rng),
        hashParts(s.playerTurn),
        hashParts(s.startingPoints, s.leftFacilities),
    };
}

std::span<const std::string_view> statePartNames() { return kStatePartNames; }

std::vector<std::string> differingStateParts(std::span<const uint64_t> a, std::span<const uint64_t> b) {
    if (a.size() != b.size() || a.size() != std::size(kStatePartNames)) return {"everything"};
    std::vector<std::string> out;
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i] != b[i]) out.emplace_back(kStatePartNames[i]);
    return out;
}

// ---- Save files ------------------------------------------------------------------------------------

std::vector<uint8_t> serializeSave(const GameState& s, const SaveInfo& info) {
    SaveInfo header = info;
    header.turn = s.turn;
    header.empires.clear();
    for (const Empire& e : s.empires) header.empires.push_back(e.name);
    std::vector<uint8_t> state = serializeState(s);
    std::vector<uint8_t> buf(kEnvelopeSize);
    buf.reserve(kEnvelopeSize + state.size() + 1024);
    serial::write(buf, header);
    serial::write(buf, state);
    seal(buf, kSaveMagic);
    return buf;
}

std::expected<std::pair<GameState, SaveInfo>, std::string> deserializeSave(std::span<const uint8_t> bytes) {
    auto env = unwrapEnvelope(bytes, kSaveMagic, "saved game");
    if (!env) return std::unexpected(env.error());
    serial::Reader r(env->payload, env->version);
    SaveInfo info;
    std::vector<uint8_t> blob;
    serial::io(r, info);
    serial::io(r, blob);
    if (r.ok() && r.remaining() != 0) r.fail("unexpected data after the end");
    if (!r.ok()) return std::unexpected(std::format("the saved game is corrupt: {} (at byte {})", r.error(), r.position()));
    auto state = deserializeState(blob);
    if (!state) return std::unexpected(std::format("the saved game is corrupt: {}", state.error()));
    if (state->turn != info.turn || state->empires.size() != info.empires.size())
        return std::unexpected(std::string("the saved game is corrupt: its header does not match the game"));
    return std::pair<GameState, SaveInfo>{std::move(*state), std::move(info)};
}

std::expected<void, std::string> saveGame(const std::filesystem::path& file, const GameState& s, const SaveInfo& info) {
    const std::vector<uint8_t> bytes = serializeSave(s, info);
    return writeFileAtomic(file, bytes);
}

std::expected<std::pair<GameState, SaveInfo>, std::string> loadGame(const std::filesystem::path& file) {
    auto bytes = readFileBytes(file);
    if (!bytes) return std::unexpected(bytes.error());
    auto game = deserializeSave(*bytes);
    if (!game) return std::unexpected(std::format("{}: {}", file.string(), game.error()));
    return game;
}

std::expected<SaveInfo, std::string> readSaveInfo(const std::filesystem::path& file) {
    auto bytes = readFileBytes(file);
    if (!bytes) return std::unexpected(bytes.error());
    auto env = unwrapEnvelope(*bytes, kSaveMagic, "saved game");
    if (!env) return std::unexpected(std::format("{}: {}", file.string(), env.error()));
    serial::Reader r(env->payload, env->version);
    SaveInfo info;
    serial::io(r, info);
    if (!r.ok()) return std::unexpected(std::format("{}: the saved game is corrupt: {}", file.string(), r.error()));
    return info;
}

// ---- Data set identity -----------------------------------------------------------------------------

std::string dataSetIdentity(const Rules& r) {
    namespace fs = std::filesystem;
    const ruleset::Ruleset& d = r.data();
    Hasher h;
    // The loaded tables: catches data sets built in memory and mods that
    // differ outside the data directory (race presets).
    auto names = [&](const auto& list) {
        h.add(list.size());
        for (const auto& item : list) h.add(std::string_view(item.name));
    };
    names(d.techAreas);
    names(d.vehicleSizes);
    names(d.components);
    names(d.facilities);
    names(d.planetSizes);
    names(d.racialTraits);
    names(d.systemTypes);
    names(d.quadrantTypes);
    names(d.intelProjects);
    h.add(d.eventTypes.size());
    for (const auto& e : d.eventTypes) h.add(std::string_view(e.type));
    names(d.combatStrategies);
    names(r.racePresets());
    h.add(d.settings.size());

    // Every data file, in a platform-independent order and line-ending form.
    std::error_code ec;
    if (!d.dataDir.empty() && fs::is_directory(d.dataDir, ec)) {
        std::vector<std::pair<std::string, fs::path>> files;
        for (const fs::directory_entry& entry : fs::directory_iterator(d.dataDir, ec)) {
            if (!entry.is_regular_file(ec)) continue;
            const std::string name = lower(entry.path().filename().string());
            if (name.size() > 4 && name.ends_with(".txt")) files.emplace_back(name, entry.path());
        }
        std::sort(files.begin(), files.end());
        for (const auto& [name, path] : files) {
            h.add(std::string_view(name));
            std::ifstream in(path, std::ios::binary);
            std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            std::erase(content, '\r');
            h.add(std::string_view(content));
        }
    }

    std::string label = "data";
    if (!d.dataDir.empty()) {
        const fs::path dir = d.dataDir.filename().empty() ? d.dataDir.parent_path() : d.dataDir;
        label = dir.parent_path().filename().empty() ? dir.filename().string()
                                                     : dir.parent_path().filename().string() + "/" + dir.filename().string();
    }
    return std::format("{}#{:016x}", label, h.value());
}

bool sameDataSet(std::string_view a, std::string_view b) { return fingerprintOf(a) == fingerprintOf(b); }

// ---- Files ---------------------------------------------------------------------------------------------

std::expected<std::vector<uint8_t>, std::string> readFileBytes(const std::filesystem::path& file, size_t maxBytes) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(file, ec);
    if (ec) return std::unexpected(std::format("{}: {}", file.string(), ec.message()));
    if (size > maxBytes) return std::unexpected(std::format("{}: file is too large ({} bytes)", file.string(), size));
    std::ifstream in(file, std::ios::binary);
    if (!in) return std::unexpected(std::format("{}: cannot open the file", file.string()));
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (in.gcount() != static_cast<std::streamsize>(bytes.size())) return std::unexpected(std::format("{}: read error", file.string()));
    return bytes;
}

std::expected<void, std::string> writeFileAtomic(const std::filesystem::path& file, std::span<const uint8_t> bytes) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (file.has_parent_path()) fs::create_directories(file.parent_path(), ec);
    fs::path tmp = file;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return std::unexpected(std::format("{}: cannot write the file", tmp.string()));
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        out.flush();
        if (!out) {
            out.close();
            fs::remove(tmp, ec);
            return std::unexpected(std::format("{}: write error (disk full?)", tmp.string()));
        }
    }
    fs::rename(tmp, file, ec);
    if (ec) {
        const std::string why = ec.message();
        fs::remove(tmp, ec);
        return std::unexpected(std::format("{}: cannot replace the file: {}", file.string(), why));
    }
    return {};
}

} // namespace opense4::game
