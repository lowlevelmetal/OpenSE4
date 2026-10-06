"""Hegemon's expansion: which planets are worth settling (their facility slots, their
resource values, whether our race breathes there, how far and how dangerous), which
colony ship goes where, how many more colony ships to build, and exploring with
scouts."""

from . import config
from .util import RES, chebyshev


class Expansion:
    def __init__(self, world, kn, econ, threat, mem, tune=None):
        self.w = world
        self.kn = kn
        self.econ = econ
        self.threat = threat          # system -> hostile strength seen near it
        self.mem = mem
        self.tune = tune or {}
        race = world.race
        self.atmosphere = race["atmosphere"] if race is not None else None
        self.only_breathable = bool(world.options["only_breathable"])
        self.only_home = bool(world.options["only_home_type"])
        self.home_surface = race["native_surface"] if race is not None else None
        self._targets = None

    # ---- planets ----

    def capacity(self, o):
        """(facility slots, max population) we would have there."""
        ps = self.kn.planet_size("Planet", o["size"])
        if ps is None:
            return (0, 0)
        domed = o["atmosphere"] != self.atmosphere
        if domed:
            return (ps["max_facilities_domed"], ps["max_population_domed"])
        return (ps["max_facilities"], ps["max_population"])

    def planet_worth(self, o):
        """What a colony there would bring us a turn once its slots are full, priced."""
        slots, pop = self.capacity(o)
        if slots <= 0:
            return 0.0
        econ = self.econ
        values = econ.planet_values(o)
        p = econ.prices
        apt = econ.apt
        # A typical facility yields about this much of its resource before the planet's value.
        unit = self.tune.get("facility_yield", 700.0)
        best = max(p[0] * values[0] * apt[0], p[1] * values[1] * apt[1], p[2] * values[2] * apt[2])
        research = p[3] * apt[3] * 0.8
        per = max(best, research) * unit
        w = slots * per + pop * 0.3
        for a in o["abilities"] or []:
            if a["name"] == "Ancient Ruins" or a["name"] == "Ancient Ruins Unique":
                w += 15000.0
        return w

    def targets(self, surfaces):
        """Planets we may settle with these surfaces, best first: [(score, planet, system)]."""
        w = self.w
        dist = w.territory_distance()
        taken = set()
        for v in w.my_vehicles:
            for o in v["orders"] or []:
                if o["kind"] == "colonize" and o["object"] is not None:
                    taken.add(o["object"])
        out = []
        my_sys = set(w.colony_systems)
        foreign_sys = set()
        for c in w.foreign_colonies:
            s = w.system_of(c["planet"])
            if s is not None:
                foreign_sys.add(s)
        for o in w.planets:
            if o["colony"] is not None or o["id"] in taken:
                continue
            if o["surface"] not in surfaces:
                continue
            if self.only_home and o["surface"] != self.home_surface:
                continue
            if self.only_breathable and o["atmosphere"] != self.atmosphere:
                continue
            s = o["system"]
            d = dist.get(s)
            if d is None:
                continue
            worth = self.planet_worth(o)
            if worth <= 0:
                continue
            # A new system needs a spaceport first: a slot and a few turns.
            if s not in my_sys and not self.econ.no_spaceports:
                worth *= 0.85
            danger = self.threat.get(s, 0)
            if danger > 0:
                worth *= 0.3
            if s in foreign_sys and s not in my_sys:
                worth *= 0.5
            score = worth / (1.0 + (0.25 if config.on("wide_expansion") else 0.35) * d)
            out.append((score, o, s))
        out.sort(key=lambda x: -x[0])
        return out

    # ---- colony ships ----

    def assign(self, colony_ships):
        """Orders for idle colony ships: the best target each can reach. [(vehicle, planet)]."""
        w = self.w
        out = []
        claimed = set()
        for v in colony_ships:
            fig = w.figures(v["design"])
            if fig is None:
                continue
            surfaces = fig["colonize"]
            here = v["location"]["system"]
            dist = w.bfs([here])
            best = None
            for score, o, s in self.targets(surfaces):
                if o["id"] in claimed:
                    continue
                d = dist.get(s)
                if d is None:
                    continue
                sc = score / (1.0 + 0.25 * d)
                if best is None or sc > best[0]:
                    best = (sc, o)
            if best is not None:
                claimed.add(best[1]["id"])
                out.append((v, best[1]))
        return out
