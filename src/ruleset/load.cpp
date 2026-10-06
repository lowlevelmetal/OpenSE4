#include "ruleset/ruleset.hpp"

#include "ruleset/ability_names.hpp"
#include "ruleset/effect_names.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <expected>
#include <functional>
#include <initializer_list>
#include <span>
#include <sstream>

namespace opense4::ruleset {

using datafile::DataFile;
using datafile::Diagnostics;
using datafile::Need;
using datafile::RecordReader;

namespace {

constexpr std::array<std::string_view, static_cast<size_t>(VehicleType::Count)> kVehicleNames{
    "Ship", "Base", "Fighter", "Satellite", "Mine", "Troop", "Drone", "Weapon Platform"};

// Vehicle type tokens as they appear in lists, including abbreviations.
std::optional<VehicleTypeMask> parseVehicleToken(std::string_view token) {
    static constexpr std::array<std::pair<std::string_view, VehicleType>, 14> kTokens{{
        {"Ship", VehicleType::Ship},
        {"Base", VehicleType::Base},
        {"Fighter", VehicleType::Fighter},
        {"Ftr", VehicleType::Fighter},
        {"Satellite", VehicleType::Satellite},
        {"Sat", VehicleType::Satellite},
        {"Mine", VehicleType::Mine},
        {"Troop", VehicleType::Troop},
        {"Trp", VehicleType::Troop},
        {"Drone", VehicleType::Drone},
        {"WeapPlatform", VehicleType::WeaponPlatform},
        {"WeapPlat", VehicleType::WeaponPlatform},
        {"Weapon Platform", VehicleType::WeaponPlatform},
        {"WeaponPlatform", VehicleType::WeaponPlatform},
    }};
    if (datafile::keysEqual(token, "All")) return static_cast<VehicleTypeMask>((1u << static_cast<unsigned>(VehicleType::Count)) - 1);
    for (const auto& [name, type] : kTokens)
        if (datafile::keysEqual(token, name)) return maskOf(type);
    return std::nullopt;
}

std::optional<int> parsePerVehicleRestriction(std::string_view text) {
    static constexpr std::array<std::string_view, 11> kCounts{"None", "One", "Two", "Three", "Four", "Five",
                                                              "Six", "Seven", "Eight", "Nine", "Ten"};
    if (datafile::keysEqual(text, "None") || text.empty()) return 0;
    for (size_t i = 1; i < kCounts.size(); ++i)
        if (datafile::keysEqual(text, std::string(kCounts[i]) + " Per Vehicle")) return static_cast<int>(i);
    return std::nullopt;
}

bool isNone(std::string_view s) { return s.empty() || datafile::keysEqual(s, "None"); }

using Source = std::function<std::expected<DataFile, std::string>(std::string_view name)>;

class Loader {
public:
    Loader(std::filesystem::path dir, Source source, const LoadOptions& options)
        : dir_(std::move(dir)), source_(std::move(source)), declared_(options.declaredAbilities) {
        rs_.dataDir = dir_;
        rs_.declaredAbilities = declared_;
    }

    LoadResult run() {
        // Tech areas first: nearly everything else refers to them by name.
        if (!loadRequired("TechArea.txt", [&](const DataFile& f) { loadTechAreas(f); })) return fail();
        loadRequired("VehicleSize.txt", [&](const DataFile& f) { eachRecord(f, [&](RecordReader& r) { loadVehicleSize(r); }); });
        loadRequired("Components.txt", [&](const DataFile& f) { eachRecord(f, [&](RecordReader& r) { loadComponent(r); }); });
        loadRequired("Facility.txt", [&](const DataFile& f) { eachRecord(f, [&](RecordReader& r) { loadFacility(r); }); });
        loadRequired("PlanetSize.txt", [&](const DataFile& f) { eachRecord(f, [&](RecordReader& r) { loadPlanetSize(r); }); });
        loadRequired("RacialTraits.txt", [&](const DataFile& f) { eachRecord(f, [&](RecordReader& r) { loadRacialTrait(r); }); });
        loadRequired("Cultures.txt", [&](const DataFile& f) { eachRecord(f, [&](RecordReader& r) { loadCulture(r); }); });
        loadRequired("SectType.txt", [&](const DataFile& f) { eachRecord(f, [&](RecordReader& r) { loadSectorType(r); }); });
        loadRequired("StellarAbilityTypes.txt",
                     [&](const DataFile& f) { eachRecord(f, [&](RecordReader& r) { loadStellarAbilityType(r); }); });
        loadRequired("SystemTypes.txt", [&](const DataFile& f) { eachRecord(f, [&](RecordReader& r) { loadSystemType(r); }); });
        loadRequired("QuadrantTypes.txt", [&](const DataFile& f) { eachRecord(f, [&](RecordReader& r) { loadQuadrantType(r); }); });
        loadRequired("CompEnhancement.txt", [&](const DataFile& f) { eachRecord(f, [&](RecordReader& r) { loadWeaponMount(r); }); });
        loadRequired("Formations.txt", [&](const DataFile& f) { eachRecord(f, [&](RecordReader& r) { loadFormation(r); }); });
        loadRequired("Happiness.txt", [&](const DataFile& f) { eachRecord(f, [&](RecordReader& r) { loadHappiness(r); }); });
        loadRequired("IntelProjects.txt", [&](const DataFile& f) { eachRecord(f, [&](RecordReader& r) { loadIntelProject(r); }); });
        loadRequired("Events.txt", [&](const DataFile& f) { eachRecord(f, [&](RecordReader& r) { loadEvent(r); }); });
        loadRequired("DefaultStrategies.txt", [&](const DataFile& f) { eachRecord(f, [&](RecordReader& r) { loadStrategy(r); }); });
        loadRequired("Settings.txt", [&](const DataFile& f) { loadSettings(f); });

        loadNames("DefaultDesignTypes.txt", rs_.names.designTypes);
        loadNames("DefaultColonyTypes.txt", rs_.names.colonyTypes);
        loadList("EmpireNames.txt", rs_.names.empireNames);
        loadList("EmpireTypes.txt", rs_.names.empireTypes);
        loadList("EmperorNames.txt", rs_.names.emperorNames);
        loadList("EmperorTitles.txt", rs_.names.emperorTitles);
        loadList("Demeanors.txt", rs_.names.demeanors);
        loadList("SystemNames.txt", rs_.names.systemNames);
        loadList("RepairPriorities.txt", rs_.names.repairPriorities);

        crossCheck();
        return LoadResult{std::move(rs_), std::move(diag_)};
    }

private:
    LoadResult fail() { return LoadResult{std::nullopt, std::move(diag_)}; }

    template <class Fn>
    bool loadRequired(const char* name, Fn&& fn) {
        auto file = source_(name);
        if (!file) {
            diag_.errors.push_back(std::format("{}: {}", name, file.error()));
            return false;
        }
        if (!file->hasDataSection) diag_.errors.push_back(std::format("{}: no *BEGIN* data section", name));
        fn(*file);
        return true;
    }

    template <class Fn>
    void eachRecord(const DataFile& f, Fn&& fn) {
        for (const auto& record : f.records) {
            RecordReader r(f, record, diag_);
            fn(r);
        }
    }

    void loadList(const char* name, std::vector<std::string>& out) {
        auto file = source_(name);
        if (!file) {
            diag_.warnings.push_back(std::format("{}: {}", name, file.error()));
            return;
        }
        if (file->hasDataSection) {
            for (const auto& rec : file->records)
                for (const auto& field : rec.fields) out.push_back(field.value);
        } else {
            out = file->entries;
        }
    }

    void loadNames(const char* name, std::vector<std::string>& out) {
        loadRequired(name, [&](const DataFile& f) { eachRecord(f, [&](RecordReader& r) { out.push_back(r.str("Name")); }); });
    }

    // ---- shared field groups -------------------------------------------------------
    Cost cost(RecordReader& r) {
        return Cost{r.integer("Cost Minerals"), r.integer("Cost Organics"), r.integer("Cost Radioactives")};
    }

    std::vector<TechRequirement> techRequirements(RecordReader& r) {
        std::vector<TechRequirement> out;
        const int count = r.int32("Number of Tech Req", Need::Optional);
        for (int n = 1; n <= count; ++n) {
            const std::string area = r.str(RecordReader::key("Tech Area Req {}", n));
            const int level = r.int32(RecordReader::key("Tech Level Req {}", n));
            if (auto id = rs_.findTechArea(area)) out.push_back({*id, level});
            else if (!area.empty()) r.error(std::format("unknown tech area '{}'", area));
        }
        return out;
    }

    // A type name the original does not know is a data error; a few names that
    // file headers list but the original never accepted load with a warning
    // and do nothing (spec 03 §2.1, spec 01 §4.4). Names a mod declared load
    // too (docs/sdk/packages-and-data.md).
    void checkAbilityType(const RecordReader& r, std::string_view type) const {
        switch (abilityNameStatus(type)) {
            case AbilityNameStatus::Known: break;
            case AbilityNameStatus::Ignored: r.warn(std::format("ability type '{}' has no effect", type)); break;
            case AbilityNameStatus::Unknown:
                if (std::none_of(declared_.begin(), declared_.end(), [&](const DeclaredAbility& d) { return datafile::keysEqual(d.name, type); }))
                    r.error(std::format("unknown ability type '{}'", type));
                break;
        }
    }

    std::vector<Ability> abilities(RecordReader& r, const char* countKey = "Number of Abilities") {
        std::vector<Ability> out;
        // At most 20 are read; a larger count is treated as 20 (spec 03 §2.1, confirmed: binary).
        const int count = std::min(r.int32(countKey, Need::Optional), kMaxAbilitiesPerRecord);
        for (int n = 1; n <= count; ++n) {
            Ability a;
            a.type = r.str(RecordReader::key("Ability {} Type", n));
            a.description = r.str(RecordReader::key("Ability {} Descr", n), Need::Optional);
            a.value1 = r.str(RecordReader::key("Ability {} Val 1", n), Need::Optional);
            a.value2 = r.str(RecordReader::key("Ability {} Val 2", n), Need::Optional);
            if (isNone(a.type)) continue;
            checkAbilityType(r, a.type);
            out.push_back(std::move(a));
        }
        return out;
    }

    // `Vechicle List Type Override`: lower-cased, and every class whose keyword
    // occurs anywhere in the text is enabled (spec 03 §2.3, confirmed: binary).
    static VehicleTypeMask overrideMask(std::string_view text) {
        std::string lower(text);
        for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        static constexpr std::array<std::pair<std::string_view, VehicleType>, 8> kKeywords{{
            {"ship", VehicleType::Ship},
            {"base", VehicleType::Base},
            {"fighter", VehicleType::Fighter},
            {"satellite", VehicleType::Satellite},
            {"mine", VehicleType::Mine},
            {"troop", VehicleType::Troop},
            {"drone", VehicleType::Drone},
            {"weapplatform", VehicleType::WeaponPlatform},
        }};
        VehicleTypeMask mask = 0;
        for (const auto& [word, type] : kKeywords)
            if (lower.find(word) != std::string::npos) mask |= maskOf(type);
        return mask;
    }

    VehicleTypeMask vehicleMask(RecordReader& r, std::string_view key, Need need = Need::Required) {
        VehicleTypeMask mask = 0;
        for (const std::string& token : r.list(key, '\\', need)) {
            // Override lists are comma separated; plain fields use backslashes.
            std::string_view rest = token;
            while (!rest.empty()) {
                const size_t comma = rest.find(',');
                std::string_view t = rest.substr(0, comma);
                while (!t.empty() && t.front() == ' ') t.remove_prefix(1);
                while (!t.empty() && t.back() == ' ') t.remove_suffix(1);
                if (!t.empty()) {
                    if (auto m = parseVehicleToken(t)) mask |= *m;
                    else r.error(std::format("unknown vehicle type '{}' in '{}'", t, key));
                }
                if (comma == std::string_view::npos) break;
                rest.remove_prefix(comma + 1);
            }
        }
        return mask;
    }

    std::vector<Message> messages(RecordReader& r, const char* countKey, const char* titleFmt, const char* textFmt) {
        std::vector<Message> out;
        const int count = r.int32(countKey, Need::Optional);
        if (count < 0) r.error(std::format("'{}' must not be negative, not {}", countKey, count));
        for (int n = 1; n <= count; ++n) {
            Message m;
            if (titleFmt) m.title = r.str(std::vformat(titleFmt, std::make_format_args(n)), Need::Optional);
            m.text = r.str(std::vformat(textFmt, std::make_format_args(n)), Need::Optional);
            out.push_back(std::move(m));
        }
        return out;
    }

    // ---- individual files -----------------------------------------------------------------
    void loadTechAreas(const DataFile& f) {
        // Pass 1: names, so requirements may refer to areas defined later in the file.
        for (const auto& record : f.records)
            if (const auto* name = record.find("Name")) {
                const bool duplicate = std::any_of(rs_.techAreas.begin(), rs_.techAreas.end(),
                                                   [&](const TechArea& t) { return datafile::keysEqual(t.name, name->value); });
                if (duplicate) diag_.errors.push_back(std::format("{}:{}: duplicate tech area '{}'", f.name, record.line, name->value));
                TechArea t;
                t.name = name->value;
                rs_.techAreas.push_back(std::move(t));
            }
        rs_.reindex();
        size_t i = 0;
        eachRecord(f, [&](RecordReader& r) {
            if (!r.has("Name")) {
                r.error("record has no Name");
                return;
            }
            TechArea& t = rs_.techAreas[i++];
            r.str("Name");
            t.group = r.str("Group");
            t.description = r.str("Description", Need::Optional);
            t.maxLevel = r.int32("Maximum Level");
            t.levelCost = r.integer("Level Cost");
            t.startLevel = r.int32("Start Level", Need::Optional);
            t.raiseLevel = r.int32("Raise Level", Need::Optional);
            t.racialArea = r.int32("Racial Area", Need::Optional);
            t.uniqueArea = r.int32("Unique Area", Need::Optional);
            t.canBeRemoved = r.boolean("Can Be Removed", Need::Optional, true);
            t.requirements = techRequirements(r);
        });
    }

    void loadVehicleSize(RecordReader& r) {
        VehicleSize v;
        v.name = r.str("Name");
        v.shortName = r.str("Short Name", Need::Optional);
        v.description = r.str("Description", Need::Optional);
        v.code = r.str("Code", Need::Optional);
        v.primaryBitmap = r.str("Primary Bitmap Name", Need::Optional);
        v.alternateBitmap = r.str("Alternate Bitmap Name", Need::Optional);
        const VehicleTypeMask mask = vehicleMask(r, "Vehicle Type");
        for (size_t t = 0; t < static_cast<size_t>(VehicleType::Count); ++t)
            if (mask == maskOf(static_cast<VehicleType>(t))) v.type = static_cast<VehicleType>(t);
        if (std::popcount(static_cast<unsigned>(mask)) != 1) r.error("'Vehicle Type' must name exactly one vehicle type");
        v.tonnage = r.int32("Tonnage");
        v.cost = cost(r);
        v.enginesPerMove = r.int32("Engines Per Move", Need::Optional);
        v.requirements = techRequirements(r);
        v.abilities = abilities(r);
        v.mustHaveBridge = r.boolean("Requirement Must Have Bridge", Need::Optional);
        v.canHaveAuxControl = r.boolean("Requirement Can Have Aux Con", Need::Optional);
        v.minLifeSupport = r.int32("Requirement Min Life Support", Need::Optional);
        v.minCrewQuarters = r.int32("Requirement Min Crew Quarters", Need::Optional);
        v.usesEngines = r.boolean("Requirement Uses Engines", Need::Optional);
        v.maxEngines = r.int32("Requirement Max Engines", Need::Optional);
        v.minPercentFighterBays = r.int32("Requirement Pct Fighter Bays", Need::Optional);
        v.minPercentColonyModules = r.int32("Requirement Pct Colony Mods", Need::Optional);
        v.minPercentCargo = r.int32("Requirement Pct Cargo", Need::Optional);
        // Documented unit-only flags that the original never reads (spec 03 §2.2).
        r.str("Launched from Ship", Need::Optional);
        r.str("Launched from Planet", Need::Optional);
        rs_.vehicleSizes.push_back(std::move(v));
    }

    void loadComponent(RecordReader& r) {
        Component c;
        c.name = r.str("Name");
        c.description = r.str("Description", Need::Optional);
        c.picture = r.int32("Pic Num", Need::Optional);
        c.tonnage = r.int32("Tonnage Space Taken");
        c.structure = r.int32("Tonnage Structure");
        c.cost = cost(r);
        c.vehicles = vehicleMask(r, "Vehicle Type");
        c.vehicleText = r.str("Vehicle Type", Need::Optional);
        // Optional override (the classic files spell the key "Vechicle").
        for (const char* key : {"Vechicle List Type Override", "Vehicle List Type Override"})
            if (r.has(key)) c.vehicles = overrideMask(r.str(key));
        c.vehicleDescription = r.str("Vehicle List Type Description", Need::Optional);
        c.supplyUsed = r.int32("Supply Amount Used", Need::Optional);
        const std::string restriction = r.str("Restrictions", Need::Optional);
        if (auto n = parsePerVehicleRestriction(restriction)) c.maxPerVehicle = *n;
        else r.error(std::format("unknown restriction '{}'", restriction));
        c.generalGroup = r.str("General Group", Need::Optional);
        c.family = r.int32("Family", Need::Optional);
        c.romanNumeral = r.int32("Roman Numeral", Need::Optional);
        c.customGroup = r.int32("Custom Group", Need::Optional);
        c.requirements = techRequirements(r);
        c.abilities = abilities(r);

        const std::string kind = r.str("Weapon Type", Need::Optional);
        Weapon& w = c.weapon;
        if (isNone(kind)) w.kind = WeaponKind::None;
        else if (datafile::keysEqual(kind, "Direct Fire")) w.kind = WeaponKind::DirectFire;
        else if (datafile::keysEqual(kind, "Seeking")) w.kind = WeaponKind::Seeking;
        else if (datafile::keysEqual(kind, "Warhead")) w.kind = WeaponKind::Warhead;
        else if (datafile::keysEqual(kind, "Point-Defense")) w.kind = WeaponKind::PointDefense;
        else r.error(std::format("unknown weapon type '{}'", kind));
        const Need weaponNeed = w.kind == WeaponKind::None ? Need::Optional : Need::Required;
        w.targets = r.list("Weapon Target", '\\', weaponNeed);
        w.damageAtRange = r.intList("Weapon Damage At Rng", weaponNeed);
        w.damageType = r.str("Weapon Damage Type", weaponNeed);
        w.reloadRate = r.int32("Weapon Reload Rate", weaponNeed);
        w.displayType = r.str("Weapon Display Type", Need::Optional);
        w.display = r.str("Weapon Display", Need::Optional);
        w.modifier = r.int32("Weapon Modifier", Need::Optional);
        w.sound = r.str("Weapon Sound", Need::Optional);
        w.family = r.int32("Weapon Family", Need::Optional);
        w.seekerSpeed = r.int32("Weapon Seeker Speed", Need::Optional);
        w.seekerDamageResistance = r.int32("Weapon Seeker Dmg Res", Need::Optional);
        rs_.components.push_back(std::move(c));
    }

    void loadFacility(RecordReader& r) {
        Facility f;
        f.name = r.str("Name");
        f.description = r.str("Description", Need::Optional);
        f.group = r.str("Facility Group", Need::Optional);
        f.family = r.int32("Facility Family", Need::Optional);
        f.romanNumeral = r.int32("Roman Numeral", Need::Optional);
        f.restriction = r.str("Restrictions", Need::Optional);
        f.picture = r.int32("Pic Num", Need::Optional);
        f.cost = cost(r);
        f.requirements = techRequirements(r);
        f.abilities = abilities(r);
        rs_.facilities.push_back(std::move(f));
    }

    void loadPlanetSize(RecordReader& r) {
        PlanetSize p;
        p.name = r.str("Name");
        p.physicalType = r.str("Physical Type");
        p.stellarSize = r.str("Stellar Size");
        p.maxFacilities = r.int32("Max Facilities");
        p.maxPopulation = r.int32("Max Population");
        p.maxCargo = r.int32("Max Cargo Spaces");
        p.maxFacilitiesDomed = r.int32("Max Facilities Domed", Need::Optional);
        p.maxPopulationDomed = r.int32("Max Population Domed", Need::Optional);
        p.maxCargoDomed = r.int32("Max Cargo Spaces Domed", Need::Optional);
        p.constructed = r.boolean("Constructed", Need::Optional);
        p.specialAbilityId = r.int32("Special Ability ID", Need::Optional);
        rs_.planetSizes.push_back(std::move(p));
    }

    void loadRacialTrait(RecordReader& r) {
        RacialTrait t;
        t.name = r.str("Name");
        t.description = r.str("Description", Need::Optional);
        t.picture = r.int32("Pic Num", Need::Optional);
        t.generalType = r.str("General Type", Need::Optional);
        // Only these three are valid; the game uses the value for nothing else (spec 02 §1.6).
        if (r.has("General Type") && !datafile::keysEqual(t.generalType, "Advantage") && !datafile::keysEqual(t.generalType, "Disadvantage") &&
            !datafile::keysEqual(t.generalType, "Neither"))
            r.error(std::format("'General Type' must be Advantage, Disadvantage or Neither, not '{}'", t.generalType));
        t.cost = r.int32("Cost");
        t.traitType = r.str("Trait Type");
        for (int n = 1; r.has(RecordReader::key("Value {}", n)); ++n) t.values.push_back(r.str(RecordReader::key("Value {}", n)));
        for (int n = 1; r.has(RecordReader::key("Required Trait {}", n)); ++n)
            if (auto v = r.str(RecordReader::key("Required Trait {}", n)); !isNone(v)) t.requiredTraits.push_back(v);
        for (int n = 1; r.has(RecordReader::key("Restricted Trait {}", n)); ++n)
            if (auto v = r.str(RecordReader::key("Restricted Trait {}", n)); !isNone(v)) t.restrictedTraits.push_back(v);
        rs_.racialTraits.push_back(std::move(t));
    }

    void loadCulture(RecordReader& r) {
        Culture c;
        c.name = r.str("Name");
        c.description = r.str("Description", Need::Optional);
        c.production = r.int32("Production");
        c.research = r.int32("Research");
        c.intelligence = r.int32("Intelligence");
        c.trade = r.int32("Trade");
        c.spaceCombat = r.int32("Space Combat");
        c.groundCombat = r.int32("Ground Combat");
        c.happiness = r.int32("Happiness");
        c.maintenance = r.int32("Maintenance");
        c.shipyardRate = r.int32("SY Rate");
        c.repair = r.int32("Repair");
        rs_.cultures.push_back(std::move(c));
    }

    void loadSectorType(RecordReader& r) {
        SectorObjectType s;
        s.physicalType = r.str("Physical Type");
        s.picture = r.int32("Picture Num");
        s.description = r.str("Description", Need::Optional);
        s.planetSize = r.str("Planet Size", Need::Optional);
        s.planetPhysicalType = r.str("Planet Physical Type", Need::Optional);
        s.planetAtmosphere = r.str("Planet Atmosphere", Need::Optional);
        s.starSize = r.str("Star Size", Need::Optional);
        s.starAge = r.str("Star Age", Need::Optional);
        s.starColor = r.str("Star Color", Need::Optional);
        s.starLuminosity = r.str("Star Luminosity", Need::Optional);
        s.stormSize = r.str("Storm Size", Need::Optional);
        s.combatTile = r.str("Combat Tile", Need::Optional);
        s.warpPointSize = r.str("Warp Point Size", Need::Optional);
        s.warpPointOneWay = r.boolean("Warp Point One-Way", Need::Optional);
        s.unusual = r.boolean("Unusual", Need::Optional);
        rs_.sectorObjectTypes.push_back(std::move(s));
    }

    void loadStellarAbilityType(RecordReader& r) {
        StellarAbilityType s;
        s.name = r.str("Name");
        const int count = r.int32("Number of Poss Abilities");
        for (int n = 1; n <= count; ++n) {
            Ability a;
            const int chance = r.int32(RecordReader::key("Ability {} Chance", n));
            a.type = r.str(RecordReader::key("Ability {} Type", n));
            a.description = r.str(RecordReader::key("Ability {} Descr", n), Need::Optional);
            a.value1 = r.str(RecordReader::key("Ability {} Val 1", n), Need::Optional);
            a.value2 = r.str(RecordReader::key("Ability {} Val 2", n), Need::Optional);
            if (!isNone(a.type)) checkAbilityType(r, a.type);
            s.possibleAbilities.emplace_back(chance, std::move(a));
        }
        rs_.stellarAbilityTypes.push_back(std::move(s));
    }

    void loadSystemType(RecordReader& r) {
        SystemType s;
        s.name = r.str("Name");
        s.description = r.str("Description", Need::Optional);
        s.physicalType = r.str("System Physical Type", Need::Optional);
        s.backgroundBitmap = r.str("Background Bitmap", Need::Optional);
        s.empiresCanStartIn = r.boolean("Empires Can Start In", Need::Optional);
        s.maskBackgroundObjects = r.boolean("Mask Background Objs", Need::Optional);
        s.nonTiledCenterPicture = r.boolean("Non-Tiled Center Pic", Need::Optional);
        s.abilities = abilities(r);
        s.warpPointStellarAbilityType = r.str("WP Stellar Abil Type", Need::Optional);
        const int count = r.int32("Number of System Objs");
        for (int n = 1; n <= count; ++n) {
            SystemObjectTemplate o;
            o.physicalType = r.str(RecordReader::key("Obj {} Physical Type", n));
            o.position = r.str(RecordReader::key("Obj {} Position", n));
            o.stellarAbilityType = r.str(RecordReader::key("Obj {} Stellar Abil Type", n), Need::Optional);
            o.size = r.str(RecordReader::key("Obj {} Size", n), Need::Optional);
            o.age = r.str(RecordReader::key("Obj {} Age", n), Need::Optional);
            o.color = r.str(RecordReader::key("Obj {} Color", n), Need::Optional);
            o.luminosity = r.str(RecordReader::key("Obj {} Luminosity", n), Need::Optional);
            o.atmosphere = r.str(RecordReader::key("Obj {} Atmosphere", n), Need::Optional);
            o.composition = r.str(RecordReader::key("Obj {} Composition", n), Need::Optional);
            s.objects.push_back(std::move(o));
        }
        rs_.systemTypes.push_back(std::move(s));
    }

    void loadQuadrantType(RecordReader& r) {
        QuadrantType q;
        q.name = r.str("Name");
        q.description = r.str("Description", Need::Optional);
        q.minDistanceBetweenSystems = r.int32("Min Dist Between Systems", Need::Optional);
        q.systemPlacement = r.str("System Placement");
        q.maxWarpPointsPerSystem = r.int32("Max Warp Points per Sys", Need::Optional);
        q.minAngleBetweenWarpPoints = r.int32("Min Angle Between WP", Need::Optional);
        const int count = r.int32("Number of System Types");
        for (int n = 1; n <= count; ++n) {
            const std::string type = r.str(RecordReader::key("Type {} Name", n));
            const int chance = r.int32(RecordReader::key("Type {} Chance", n));
            pendingQuadrantTypes_.push_back({rs_.quadrantTypes.size(), type, r.context()});
            q.systemTypeChances.emplace_back(SystemTypeId{}, chance);
        }
        rs_.quadrantTypes.push_back(std::move(q));
    }

    void loadWeaponMount(RecordReader& r) {
        WeaponMount m;
        m.longName = r.str("Long Name");
        m.shortName = r.str("Short Name", Need::Optional);
        m.description = r.str("Description", Need::Optional);
        m.code = r.str("Code", Need::Optional);
        m.costPercent = r.int32("Cost Percent", Need::Optional, 100);
        m.tonnagePercent = r.int32("Tonnage Percent", Need::Optional, 100);
        m.structurePercent = r.int32("Tonnage Structure Percent", Need::Optional, 100);
        m.damagePercent = r.int32("Damage Percent", Need::Optional, 100);
        m.supplyPercent = r.int32("Supply Percent", Need::Optional, 100);
        m.shieldPercent = r.int32("Shield Percent", Need::Optional, 100);
        m.rangeModifier = r.int32("Range Modifier", Need::Optional);
        m.toHitModifier = r.int32("Weapon To Hit Modifier", Need::Optional);
        m.minimumVehicleSize = r.int32("Vehicle Size Minimum", Need::Optional);
        m.maximumVehicleSize = r.int32("Vehicle Size Maximum", Need::Optional);
        for (const std::string& family : r.list("Comp Family Requirement", ',', Need::Optional)) {
            if (family.empty()) continue;
            if (auto id = datafile::parseInteger(family)) m.familyRequirement.push_back(static_cast<int>(*id));
            else r.error(std::format("'Comp Family Requirement' should list whole numbers, not '{}'", family));
        }
        m.weaponTypeRequirement = r.str("Weapon Type Requirement", Need::Optional);
        m.vehicleType = r.str("Vehicle Type", Need::Optional);
        m.requirements = techRequirements(r);
        rs_.weaponMounts.push_back(std::move(m));
    }

    void loadFormation(RecordReader& r) {
        Formation f;
        f.name = r.str("Name");
        f.description = r.str("Description", Need::Optional);
        f.leader.x = r.int32("Leader Position Xpos");
        f.leader.y = r.int32("Leader Position Ypos");
        f.leader.designType = r.str("Leader Design Type", Need::Optional);
        const int count = r.int32("Number of Positions");
        for (int n = 1; n <= count; ++n) {
            FormationSlot s;
            s.x = r.int32(RecordReader::key("Position {} Xpos", n));
            s.y = r.int32(RecordReader::key("Position {} Ypos", n));
            s.designType = r.str(RecordReader::key("Position {} Type", n), Need::Optional);
            f.positions.push_back(std::move(s));
        }
        rs_.formations.push_back(std::move(f));
    }

    void loadHappiness(RecordReader& r) {
        HappinessModel h;
        h.name = r.str("Name");
        h.description = r.str("Description", Need::Optional);
        h.maxPositiveChange = r.int32("Max Positive Anger Change");
        h.maxNegativeChange = r.int32("Max Negative Anger Change");
        for (auto& [key, value] : r.remaining()) {
            if (auto v = datafile::parseInteger(value)) h.triggers.emplace_back(key, static_cast<int>(*v));
            else r.error(std::format("'{}' should be a whole number, not '{}'", key, value));
        }
        rs_.happinessModels.push_back(std::move(h));
    }

    void loadIntelProject(RecordReader& r) {
        IntelProject p;
        p.name = r.str("Name");
        p.description = r.str("Description", Need::Optional);
        p.group = r.str("Group", Need::Optional);
        p.cost = r.integer("Cost");
        p.type = r.str("Type");
        p.effectAmount = r.int32("Effect Amount", Need::Optional);
        for (Message& m : messages(r, "Num Source Messages", nullptr, "Source Message {}")) p.sourceMessages.push_back(std::move(m.text));
        p.targetMessages = messages(r, "Num Target Messages", "Target Message Title {}", "Target Message {}");
        p.sourcePicture = r.str("Source Picture", Need::Optional);
        p.targetPicture = r.str("Target Picture", Need::Optional);
        p.requirements = techRequirements(r);
        rs_.intelProjects.push_back(std::move(p));
    }

    void loadEvent(RecordReader& r) {
        EventType e;
        e.type = r.str("Type");
        e.severity = r.str("Severity", Need::Optional);
        e.effectAmount = r.int32("Effect Amount", Need::Optional);
        e.messageTo = r.str("Message To", Need::Optional);
        e.messages = messages(r, "Num Messages", "Message Title {}", "Message {}");
        e.picture = r.str("Picture", Need::Optional);
        e.turnsToComplete = r.int32("Time Till Completion", Need::Optional);
        e.startMessages = messages(r, "Num Start Messages", "Start Message Title {}", "Start Message {}");
        checkEvent(r, e);
        rs_.eventTypes.push_back(std::move(e));
    }

    // What spec 01 §10 and spec 05 §4 allow an event record. A type the
    // engine does not know loads, as the original's does, but the event never
    // fires; an amount a magnitude effect cannot use does nothing.
    void checkEvent(RecordReader& r, const EventType& e) {
        const bool known = std::any_of(kEffectTypes.begin(), kEffectTypes.end(), [&](std::string_view t) { return datafile::keysEqual(t, e.type); });
        if (!known) r.warn(std::format("unknown event type '{}': the event never fires", e.type));
        auto oneOf = [](std::string_view value, std::initializer_list<std::string_view> allowed) {
            return std::any_of(allowed.begin(), allowed.end(), [&](std::string_view a) { return datafile::keysEqual(a, value); });
        };
        if (!e.severity.empty() && !oneOf(e.severity, {"Low", "Medium", "High", "Catastrophic"}))
            r.error(std::format("'Severity' must be Low, Medium, High or Catastrophic, not '{}'", e.severity));
        if (!e.messageTo.empty() && !oneOf(e.messageTo, {"None", "Owner", "Sector", "System", "All"}))
            r.error(std::format("'Message To' must be None, Owner, Sector, System or All, not '{}'", e.messageTo));
        if (e.turnsToComplete < 0)
            r.error(std::format("'Time Till Completion' must be 0 (at once) or a number of turns, not {}", e.turnsToComplete));
        if (e.effectAmount <= 0 &&
            oneOf(e.type, {"Ship - Damage", "Ship - Lose Movement", "Ship - Lose Supply", "Planet - Cargo Damage", "Planet - Facility Damage"}))
            r.warn(std::format("an event of type '{}' with 'Effect Amount' {} has no effect", e.type, e.effectAmount));
    }

    void loadStrategy(RecordReader& r) {
        CombatStrategy s;
        s.name = r.str("Name");
        s.settings = r.remaining();
        rs_.combatStrategies.push_back(std::move(s));
    }

    void loadSettings(const DataFile& f) {
        eachRecord(f, [&](RecordReader& r) {
            for (auto& [key, value] : r.remaining()) {
                if (rs_.settings.has(key)) r.warn(std::format("setting '{}' appears more than once", key));
                rs_.settings.set(key, value);
            }
        });
    }

    void crossCheck() {
        rs_.reindex();
        for (const auto& p : pendingQuadrantTypes_) {
            if (auto id = rs_.findSystemType(p.systemType)) {
                // Fill in the ids in order of appearance.
                for (auto& [sid, chance] : rs_.quadrantTypes[p.quadrant].systemTypeChances)
                    if (!sid.valid()) {
                        sid = *id;
                        break;
                    }
            } else {
                diag_.errors.push_back(std::format("{}: unknown system type '{}'", p.context, p.systemType));
            }
        }
        for (const SystemType& s : rs_.systemTypes) {
            auto check = [&](const std::string& name) {
                if (!isNone(name) && !rs_.findStellarAbilityType(name))
                    diag_.errors.push_back(std::format("SystemTypes.txt [{}]: unknown stellar ability type '{}'", s.name, name));
            };
            check(s.warpPointStellarAbilityType);
            for (const auto& o : s.objects) check(o.stellarAbilityType);
        }
        for (const RacialTrait& t : rs_.racialTraits) {
            auto exists = [&](const std::string& name) {
                return std::any_of(rs_.racialTraits.begin(), rs_.racialTraits.end(),
                                   [&](const RacialTrait& o) { return datafile::keysEqual(o.name, name); });
            };
            for (const auto& n : t.requiredTraits)
                if (!exists(n)) diag_.errors.push_back(std::format("RacialTraits.txt [{}]: unknown required trait '{}'", t.name, n));
            for (const auto& n : t.restrictedTraits)
                if (!exists(n)) diag_.errors.push_back(std::format("RacialTraits.txt [{}]: unknown restricted trait '{}'", t.name, n));
        }
        for (const TechArea& t : rs_.techAreas)
            for (const auto& req : t.requirements)
                if (req.level > rs_.techArea(req.area).maxLevel)
                    diag_.warnings.push_back(std::format("TechArea.txt [{}]: requires {} level {} but its maximum is {}", t.name,
                                                         rs_.techArea(req.area).name, req.level, rs_.techArea(req.area).maxLevel));
    }

    struct PendingSystemType {
        size_t quadrant;
        std::string systemType;
        std::string context;
    };

    std::filesystem::path dir_;
    Source source_;
    std::vector<DeclaredAbility> declared_;
    Ruleset rs_;
    Diagnostics diag_;
    std::vector<PendingSystemType> pendingQuadrantTypes_;
};

} // namespace

std::string_view displayName(VehicleType t) { return kVehicleNames[static_cast<size_t>(t)]; }

int64_t Ability::number1() const { return datafile::parseInteger(value1).value_or(0); }
int64_t Ability::number2() const { return datafile::parseInteger(value2).value_or(0); }

LoadResult loadRuleset(const std::filesystem::path& dataDir) {
    return Loader(dataDir, [&](std::string_view name) { return datafile::load(childIgnoringCase(dataDir, name)); }, LoadOptions{}).run();
}

LoadResult loadRuleset(std::shared_ptr<const GameFiles> files, const LoadOptions& options) {
    const GameFiles& source = *files;
    LoadResult result = Loader(source.dataDir(), [&](std::string_view name) { return source.dataFile(name); }, options).run();
    if (result.ruleset) result.ruleset->files = std::move(files);
    return result;
}

std::span<const DataFileName> dataFileNames() {
    static constexpr std::array<DataFileName, 27> kNames{{
        {"TechArea.txt"},          {"VehicleSize.txt"},      {"Components.txt"},       {"Facility.txt"},
        {"PlanetSize.txt"},        {"RacialTraits.txt"},     {"Cultures.txt"},         {"SectType.txt"},
        {"StellarAbilityTypes.txt"}, {"SystemTypes.txt"},    {"QuadrantTypes.txt"},    {"CompEnhancement.txt"},
        {"Formations.txt"},        {"Happiness.txt"},        {"IntelProjects.txt"},    {"Events.txt"},
        {"DefaultStrategies.txt"}, {"Settings.txt"},         {"DefaultDesignTypes.txt"}, {"DefaultColonyTypes.txt"},
        {"EmpireNames.txt", false}, {"EmpireTypes.txt", false}, {"EmperorNames.txt", false}, {"EmperorTitles.txt", false},
        {"Demeanors.txt", false},  {"SystemNames.txt", false}, {"RepairPriorities.txt", false},
    }};
    return kNames;
}

} // namespace opense4::ruleset
