"""Hegemon's economy: what each resource is worth to us now (shadow prices), what each
colony should build next (a facility, an upgrade, or a ship at a yard), and the
spaceports, yards and depots the empire needs.

The model: a facility's output is its base value times the planet's value for that
resource, the race's aptitude and the planet's modifiers; it reaches the treasury only
from a system with a spaceport (the home system gets a quarter without one). Each
colony builds one item at a time at its own rate, so a colony's queue is worth most
when it holds the item that adds the most value per turn of building it."""

from .util import RES, res_total, clamp

MOOD_PCT = {"jubilant": 120, "happy": 110, "indifferent": 100, "unhappy": 90, "angry": 80, "rioting": 0}

# What one point of each kind is worth before the empire's needs adjust it:
# minerals, organics, radioactives, research, intelligence.
BASE_PRICES = [1.0, 0.7, 0.7, 1.3, 0.15]


class Economy:
    def __init__(self, world, kn, tune=None):
        self.w = world
        self.kn = kn
        self.tune = tune or {}
        my = world.my
        self.report = my["economy"]
        self.stored = my["stored"]
        self.cap = self.report["storage_capacity"]
        race = world.race
        ch = race["characteristics"] if race is not None else None
        if ch is not None:
            self.apt = [ch["mining_aptitude"] / 100.0, ch["farming_aptitude"] / 100.0, ch["refining_aptitude"] / 100.0,
                        ch["intelligence"] / 100.0, ch["cunning"] / 100.0]
        else:
            self.apt = [1.0, 1.0, 1.0, 1.0, 1.0]
        self.no_spaceports = False
        self.home = world.home_system()
        self.finite = bool(world.options["finite_resources"])
        # Facilities on our colonies, by system: which system-wide ones exist or are queued.
        self.sys_has = {}       # system -> set of tags ("spaceport", "supply", "happy", ...)
        self.colony_info = {}   # planet -> ColonyInfo
        self.yards = 0
        self._prices = None
        for c in world.my_colonies:
            self._scan_colony(c)

    # ---- what we have ----

    def _tag(self, system, tag):
        s = self.sys_has.get(system)
        if s is None:
            s = set()
            self.sys_has[system] = s
        s.add(tag)

    def _scan_colony(self, c):
        kn = self.kn
        o = self.w.objects.get(c["planet"])
        if o is None:
            return
        system = o["system"]
        facs = c["facilities"] or []
        queued = []
        q = c["queue"]
        if q is not None:
            for it in q["items"]:
                if it["kind"] == "facility":
                    queued.append(it["facility"])
        tags = []
        for fid in facs + queued:
            if fid < 0 or fid >= len(kn.facilities):
                continue
            f = kn.facilities[fid]
            if f.spaceport:
                self._tag(system, "spaceport")
            if f.supply:
                self._tag(system, "supply")
            if f.happy > 0:
                self._tag(system, "happy")
            if f.repro > 0:
                self._tag(system, "repro")
            if f.yard[0] > 0 or f.yard[1] > 0 or f.yard[2] > 0:
                tags.append("yard")
            for r in range(3):
                if f.smod[r] > 0:
                    self._tag(system, "smod%d" % r)
            if f.sres > 0:
                self._tag(system, "sres")
        if c["space_yard"] or "yard" in tags:
            self.yards += 1
        self.colony_info[c["planet"]] = {"system": system, "planet": o, "queued_facilities": len(queued), "yard": bool(c["space_yard"]) or "yard" in tags}

    def delivered(self, system):
        """The share of a system's output that reaches us."""
        if self.no_spaceports:
            return 1.0
        s = self.sys_has.get(system)
        if s is not None and "spaceport" in s:
            return 1.0
        if system == self.home:
            return 0.25
        return 0.0

    # ---- prices ----

    @property
    def prices(self):
        p = self._prices
        if p is None:
            p = self._make_prices()
            self._prices = p
        return p

    def _make_prices(self):
        rep = self.report
        base = list(self.tune.get("prices", BASE_PRICES))
        out = []
        for i in range(3):
            r = RES[i]
            income = rep["colonies"][r] + rep["remote_mining"][r] + rep["other_income"][r] + rep["trade"][r]
            spend = rep["maintenance"][r] + rep["construction"][r]
            stock = self.stored[r]
            cap = self.cap[r]
            k = 1.0
            # Short of it: stock covers few turns of spending.
            if spend > income and stock < 5 * (spend - income):
                k *= 1.6
            elif stock < 2 * spend:
                k *= 1.3
            # Plenty: the store is filling up.
            if cap > 0 and stock > 0.8 * cap and income >= spend:
                k *= 0.4
            elif cap > 0 and stock > 0.5 * cap and income > spend:
                k *= 0.75
            out.append(base[i] * k)
        out.append(base[3])
        out.append(base[4])
        return out

    # ---- facility values ----

    def planet_values(self, o):
        """The planet's value of each resource as a fraction (finite games: what the stock lasts)."""
        v = o["value"]
        if v is None:
            return [1.0, 1.0, 1.0]
        if self.finite:
            return [clamp(v[r] / 200000.0, 0.0, 1.0) for r in RES]
        return [v[r] / 100.0 for r in RES]

    def colony_mods(self, c):
        """The best planet modifiers already on a colony: [m, o, r, research]."""
        kn = self.kn
        best = [0, 0, 0, 0]
        for fid in c["facilities"] or []:
            if 0 <= fid < len(kn.facilities):
                f = kn.facilities[fid]
                for r in range(3):
                    if f.pmod[r] > best[r]:
                        best[r] = f.pmod[r]
                if f.pres > best[3]:
                    best[3] = f.pres
        return best

    def output_value(self, f, values, mods, system):
        """What facility f yields us a turn, priced, on a colony with those planet values and modifiers."""
        p = self.prices
        apt = self.apt
        v = 0.0
        for r in range(3):
            g = f.gen[r]
            if g > 0:
                v += p[r] * g * values[r] * apt[r] * (1.0 + mods[r] / 100.0)
            s = f.solar[r]
            if s > 0:
                v += p[r] * s * self.stars(system)
        if f.research > 0:
            v += p[3] * f.research * apt[3] * (1.0 + mods[3] / 100.0)
        if f.intel > 0:
            v += p[4] * f.intel * apt[4]
        return v * self.delivered(system)

    def stars(self, system):
        n = 0
        s = self.w.systems.get(system)
        if s is None or s["objects"] is None:
            return 1
        for oid in s["objects"]:
            o = self.w.objects.get(oid)
            if o is not None and o["kind"] in ("star", "destroyed_star"):
                n += 1
        return n

    def build_turns(self, cost, rate):
        t = 1
        for r in RES:
            rr = rate[r]
            c = cost[r]
            if c > 0:
                if rr <= 0:
                    return 99
                n = (c + rr - 1) // rr
                if n > t:
                    t = n
        return t

    def cost_value(self, cost):
        p = self.prices
        return cost["minerals"] * p[0] + cost["organics"] * p[1] + cost["radioactives"] * p[2]
