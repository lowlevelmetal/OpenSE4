"""What Hegemon knows of the rules: the data set's facilities, components, hulls,
mounts and tech areas, read once per session from the rules view and classified by
their abilities (never by their names, so any data set or mod works), and what the
empire's technology makes available now."""

from .util import RES, ability_sum, ability_max, has_ability

GEN = ("Resource Generation - Minerals", "Resource Generation - Organics", "Resource Generation - Radioactives")
PLANET_MOD = ("Resource Gen Modifier Planet - Minerals", "Resource Gen Modifier Planet - Organics",
              "Resource Gen Modifier Planet - Radioactives")
SYSTEM_MOD = ("Resource Gen Modifier System - Minerals", "Resource Gen Modifier System - Organics",
              "Resource Gen Modifier System - Radioactives")
STORAGE = ("Resource Storage - Mineral", "Resource Storage - Organics", "Resource Storage - Radioactives")
SOLAR = ("Solar Resource Generation - Minerals", "Solar Resource Generation - Organics", "Solar Resource Generation - Radioactives")
SURFACES = ("Rock", "Ice", "Gas Giant")
COLONIZE = {"Colonize Planet - Rock": "Rock", "Colonize Planet - Ice": "Ice", "Colonize Planet - Gas": "Gas Giant"}


class Facility:
    __slots__ = ("id", "family", "numeral", "cost", "reqs", "gen", "research", "intel", "spaceport", "yard", "supply",
                 "storage", "pmod", "smod", "pres", "sres", "happy", "repro", "popgrowth", "shield", "repair", "solar",
                 "combat", "sensor", "maint", "training", "other", "useful", "atmos", "value_change", "conditions",
                 "plague", "cargo", "convert")

    def __init__(self, d):
        ab = d["abilities"]
        self.id = d["id"]
        self.family = d["family"]
        self.numeral = d["roman_numeral"]
        self.cost = d["cost"]
        self.reqs = d["requirements"]
        self.gen = [ability_sum(ab, n) for n in GEN]
        self.research = ability_sum(ab, "Point Generation - Research")
        self.intel = ability_sum(ab, "Point Generation - Intelligence")
        self.spaceport = has_ability(ab, "Spaceport")
        yard = [0, 0, 0]
        for a in ab:
            if a["name"] == "Space Yard" and isinstance(a["value1"], int) and isinstance(a["value2"], int) and 1 <= a["value1"] <= 3:
                yard[a["value1"] - 1] += a["value2"]
        self.yard = yard
        self.supply = has_ability(ab, "Supply Generation")
        self.storage = [ability_sum(ab, n) for n in STORAGE]
        self.pmod = [ability_sum(ab, n) for n in PLANET_MOD]
        self.smod = [ability_sum(ab, n) for n in SYSTEM_MOD]
        self.pres = ability_sum(ab, "Planet Point Generation Modifier - Research")
        self.sres = ability_sum(ab, "System Point Generation Modifier - Research")
        # Anger: the system ability calms; the planet ability's positive values anger.
        self.happy = ability_sum(ab, "Change Population Happiness - System") - ability_sum(ab, "Planet - Change Population Happiness")
        self.repro = ability_sum(ab, "Modify Reproduction - System")
        self.popgrowth = ability_sum(ab, "Change Population - System")
        self.shield = ability_sum(ab, "Planet - Shield Generation")
        self.repair = ability_sum(ab, "Component Repair")
        self.solar = [ability_sum(ab, n) for n in SOLAR]
        self.combat = ability_sum(ab, "Combat Modifier - System") + ability_sum(ab, "Damage Modifier - System") + \
            ability_sum(ab, "Shield Modifier - System")
        self.sensor = has_ability(ab, "Sensor Level") or has_ability(ab, "Long Range Scanner - System")
        self.maint = ability_sum(ab, "Reduced Maintenance Cost - System")
        self.training = ability_sum(ab, "Ship Training") + ability_sum(ab, "Fleet Training") + \
            ability_sum(ab, "Ship Training - System") + ability_sum(ab, "Fleet Training - System")
        self.atmos = ability_sum(ab, "Planet - Change Atmosphere")
        self.value_change = ability_sum(ab, "Planet - Change Minerals Value") + ability_sum(ab, "Planet - Change Organics Value") + \
            ability_sum(ab, "Planet - Change Radioactives Value") + ability_sum(ab, "Planet Value Change - System")
        self.conditions = ability_sum(ab, "Planet - Change Conditions") + ability_sum(ab, "Planet Conditions Change - System")
        self.plague = ability_sum(ab, "Plague Prevention - System")
        self.cargo = ability_sum(ab, "Cargo Storage")
        self.convert = has_ability(ab, "Resource Conversion")
        self.useful = True


class Weapon:
    __slots__ = ("kind", "targets", "damage", "dtype", "reload", "modifier", "max_range", "seeker_speed", "seeker_res", "family")

    def __init__(self, w, family):
        self.kind = w["kind"]
        self.targets = w["targets"]
        # By range in squares: the view's list starts at range 1, nothing hits at 0.
        self.damage = [0] + list(w["damage_at_range"])
        self.dtype = w["damage_type"]
        self.reload = w["reload_rate"] if w["reload_rate"] > 0 else 1
        self.modifier = w["modifier"]
        self.max_range = w["max_range"]
        self.seeker_speed = w["seeker_speed"]
        self.seeker_res = w["seeker_resistance"]
        self.family = family

    def hits(self, what):
        """Whether its targets name `what` ("ship", "planet", "ftr", "sat", "seeker", "drone"); "All" names everything."""
        w = what.lower()
        for t in self.targets:
            lt = t.lower()
            if lt == "all" or w in lt:
                return True
        return False


class Component:
    __slots__ = ("id", "tonnage", "structure", "cost", "types", "supply_used", "max_per", "family", "numeral", "reqs",
                 "engine", "move_bonus", "bridge", "aux", "life", "crew", "master", "armor", "shield", "phased", "regen",
                 "weapon", "colonize", "cargo", "supply", "reactor", "yard", "sensor", "cloak", "offense", "defense",
                 "multiplex", "fighter_bay", "sat_bay", "mine_layer", "drone_bay", "repair", "boarding", "board_def",
                 "emissive", "combat_move", "mining", "pd", "solar_supply", "extra_move", "group", "useless", "yard_rate")

    def __init__(self, d):
        ab = d["abilities"]
        self.id = d["id"]
        self.tonnage = d["tonnage"]
        self.structure = d["structure"]
        self.cost = d["cost"]
        self.types = d["vehicle_types"]
        self.supply_used = d["supply_used"]
        self.max_per = d["max_per_vehicle"]
        self.family = d["family"]
        self.numeral = d["roman_numeral"]
        self.reqs = d["requirements"]
        self.group = d["group"]
        self.engine = ability_sum(ab, "Standard Ship Movement")
        mb = ability_max(ab, "Movement Bonus")
        self.move_bonus = mb if mb is not None else 0
        self.extra_move = ability_sum(ab, "Extra Movement Generation")
        self.bridge = has_ability(ab, "Ship Bridge")
        self.aux = has_ability(ab, "Ship Auxiliary Control")
        self.life = has_ability(ab, "Ship Life Support")
        self.crew = has_ability(ab, "Ship Crew Quarters")
        self.master = has_ability(ab, "Master Computer")
        self.armor = has_ability(ab, "Armor")
        self.shield = ability_sum(ab, "Shield Generation")
        self.phased = ability_sum(ab, "Phased Shield Generation")
        self.regen = ability_sum(ab, "Shield Regeneration")
        w = d["weapon"]
        self.weapon = Weapon(w, d["family"]) if w is not None and w["kind"] != "none" else None
        self.pd = self.weapon is not None and self.weapon.kind == "point_defense"
        col = []
        for a in ab:
            s = COLONIZE.get(a["name"])
            if s is not None and s not in col:
                col.append(s)
        self.colonize = col
        self.cargo = ability_sum(ab, "Cargo Storage")
        self.supply = ability_sum(ab, "Supply Storage")
        self.reactor = has_ability(ab, "Quantum Reactor")
        self.solar_supply = ability_sum(ab, "Solar Supply Generation")
        self.yard = has_ability(ab, "Space Yard")
        self.yard_rate = ability_sum(ab, "Space Yard", "value2")
        self.sensor = has_ability(ab, "Sensor Level") or has_ability(ab, "Long Range Scanner")
        self.cloak = has_ability(ab, "Cloak Level")
        self.offense = ability_sum(ab, "Combat To Hit Offense Plus") - ability_sum(ab, "Combat To Hit Offense Minus")
        self.defense = ability_sum(ab, "Combat To Hit Defense Plus") - ability_sum(ab, "Combat To Hit Defense Minus")
        self.multiplex = ability_sum(ab, "Multiplex Tracking")
        self.fighter_bay = ability_sum(ab, "Launch/Recover Fighters")
        self.sat_bay = ability_sum(ab, "Launch/Recover Satellites")
        self.mine_layer = ability_sum(ab, "Lay Mines")
        self.drone_bay = ability_sum(ab, "Launch Drones")
        self.repair = ability_sum(ab, "Component Repair")
        self.boarding = ability_sum(ab, "Boarding Attack")
        self.board_def = ability_sum(ab, "Boarding Defense")
        self.emissive = ability_sum(ab, "Emissive Armor")
        self.combat_move = ability_sum(ab, "Combat Movement")
        self.mining = ability_sum(ab, "Remote Resource Generation - Minerals") + ability_sum(ab, "Remote Resource Generation - Organics") + \
            ability_sum(ab, "Remote Resource Generation - Radioactives")
        self.useless = False

    def fits(self, vehicle_type):
        return vehicle_type in self.types


class Hull:
    __slots__ = ("id", "type", "tonnage", "cost", "epm", "reqs", "bridge", "aux", "life", "crew", "uses_engines",
                 "max_engines", "pct_bays", "pct_colony", "pct_cargo", "defense", "offense", "maint")

    def __init__(self, d):
        ab = d["abilities"]
        self.id = d["id"]
        self.type = d["type"]
        self.tonnage = d["tonnage"]
        self.cost = d["cost"]
        self.epm = d["engines_per_move"]
        self.reqs = d["requirements"]
        self.bridge = d["must_have_bridge"]
        self.aux = d["can_have_aux_control"]
        self.life = d["min_life_support"]
        self.crew = d["min_crew_quarters"]
        self.uses_engines = d["uses_engines"]
        self.max_engines = d["max_engines"]
        # The data's percentages are minimums (docs/spec/03 §4.2 rule 9).
        # The least shares of the hull's space these parts must take, in percent.
        self.pct_bays = d["min_percent_fighter_bays"]
        self.pct_colony = d["min_percent_colony_modules"]
        self.pct_cargo = d["min_percent_cargo"]
        self.defense = ability_sum(ab, "Combat To Hit Defense Plus") - ability_sum(ab, "Combat To Hit Defense Minus")
        self.offense = ability_sum(ab, "Combat To Hit Offense Plus") - ability_sum(ab, "Combat To Hit Offense Minus")
        self.maint = ability_sum(ab, "Modified Maintenance Cost")


class Mount:
    __slots__ = ("id", "cost", "ton", "struct", "dmg", "supply", "shield", "range", "hit", "min", "max", "families", "wtype",
                 "vtype", "reqs")

    def __init__(self, d):
        self.id = d["id"]
        self.cost = d["cost_percent"]
        self.ton = d["tonnage_percent"]
        self.struct = d["structure_percent"]
        self.dmg = d["damage_percent"]
        self.supply = d["supply_percent"]
        self.shield = d["shield_percent"]
        self.range = d["range_modifier"]
        self.hit = d["to_hit_modifier"]
        self.min = d["minimum_hull_size"]
        self.max = d["maximum_hull_size"]
        self.families = d["families"]
        self.wtype = d["weapon_type_requirement"]
        self.vtype = d["vehicle_type"]
        self.reqs = d["requirements"]

    def offered(self, hull):
        """The designer offers it on this hull (docs/spec/03 §2.4)."""
        v = self.vtype
        if v.lower() != "any":
            names = {"ship": "Ship", "base": "Base", "fighter": "Fighter", "satellite": "Satellite", "mine": "Mine",
                     "troop": "Troop", "drone": "Drone", "weapon_platform": "Weapon Platform"}
            if names.get(hull.type, "?") not in v:
                return False
        if hull.tonnage < self.min:
            return False
        if self.max > 0 and hull.tonnage > self.max:
            return False
        return True

    def applies(self, comp):
        w = comp.weapon
        req = self.wtype.lower()
        if w is None:
            return req == "none"
        if req == "none":
            return False
        if req != "any":
            kinds = {"direct fire": "direct_fire", "seeking": "seeking", "point-defense": "point_defense", "warhead": "warhead"}
            if kinds.get(req) != w.kind:
                return False
        if self.families and comp.family not in self.families:
            return False
        return True


class Tech:
    __slots__ = ("id", "name", "max", "cost", "reqs", "racial", "unique", "group")

    def __init__(self, d):
        self.id = d["id"]
        self.name = d["name"]
        self.max = d["max_level"]
        self.cost = d["level_cost"]
        self.reqs = d["requirements"]
        self.racial = d["racial_area"]
        self.unique = d["unique_area"]
        self.group = d["group"]


class Knowledge:
    """The rules, classified, and what our technology allows now."""

    def __init__(self, rules, levels, tech_cost_option=1):
        self.facilities = [Facility(d) for d in rules["facilities"]]
        self.components = [Component(d) for d in rules["components"]]
        self.hulls = [Hull(d) for d in rules["hulls"]]
        self.mounts = [Mount(d) for d in rules["mounts"]]
        self.techs = [Tech(d) for d in rules["techs"]]
        self.planet_sizes = rules["planet_sizes"]
        self.levels = levels
        self.tech_cost_option = tech_cost_option
        self._sizes = {}
        for ps in self.planet_sizes:
            self._sizes[(ps["physical_type"], ps["stellar_size"])] = ps
            self._sizes.setdefault(("*", ps["stellar_size"]), ps)
        self.strategies = rules["strategies"]

    # ---- technology ----

    def meets(self, reqs, levels=None):
        lv = self.levels if levels is None else levels
        for q in reqs:
            a = q["area"]
            if a is not None and (a >= len(lv) or lv[a] < q["level"]):
                return False
        return True

    def missing(self, reqs):
        """[(area, level)] of the requirements not met."""
        out = []
        for q in reqs:
            a = q["area"]
            if a is not None and (a >= len(self.levels) or self.levels[a] < q["level"]):
                out.append((a, q["level"]))
        return out

    def level_cost(self, area, level):
        lc = self.techs[area].cost
        opt = self.tech_cost_option
        if opt == 0:
            c = lc * level
        elif opt == 2:
            c = lc * level * level
        else:
            c = max(lc * level, (lc * level * level) // 2)
        return c if c < 2000000000 else 2000000000

    # ---- facilities ----

    def available_facilities(self):
        return [f for f in self.facilities if self.meets(f.reqs)]

    def best_in_family(self, family):
        best = None
        for f in self.facilities:
            if f.family == family and self.meets(f.reqs) and (best is None or f.numeral > best.numeral):
                best = f
        return best

    # ---- planets ----

    def planet_size(self, physical, size):
        ps = self._sizes.get((physical, size))
        if ps is None:
            ps = self._sizes.get(("*", size))
        return ps
