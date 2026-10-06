"""Hegemon's logistics: where our supply depots are, how far a ship is from the nearest,
and whether it must turn back now. A ship at zero supply moves one sector a turn at
best, fires nothing and drops its shields, so every ship heads for a depot while its
supply still covers the trip there with a margin."""

from opense4 import services
from opense4.galaxy import Galaxy


def _query(name, args):
    return services.current().services.query(name, args)


class Logistics:
    def __init__(self, world, kn, mem, view=None):
        self.w = world
        self.kn = kn
        self.mem = mem
        self.view = view
        self._galaxy = None
        self._depots = None
        cache = mem.get("supply_per_move")
        if not isinstance(cache, dict):
            cache = {}
            mem["supply_per_move"] = cache
        self.cache = cache

    @property
    def galaxy(self):
        if self._galaxy is None and self.view is not None:
            self._galaxy = Galaxy(self.view)
        return self._galaxy

    def depots(self):
        """Locations of our colonies with a supply depot."""
        d = self._depots
        if d is None:
            d = []
            facs = self.kn.facilities
            for c in self.w.my_colonies:
                has = False
                for fid in c["facilities"] or []:
                    if 0 <= fid < len(facs) and facs[fid].supply:
                        has = True
                        break
                if has:
                    o = self.w.objects.get(c["planet"])
                    if o is not None:
                        d.append({"system": o["system"], "x": o["sector"]["x"], "y": o["sector"]["y"]})
            self._depots = d
        return d

    def route_to_depot(self, here):
        """Movement points from `here` to the nearest depot, or None."""
        g = self.galaxy
        best = None
        dist = self.w.bfs([here["system"]], 6)
        for dep in self.depots():
            j = dist.get(dep["system"])
            if j is None:
                continue
            if best is not None and j * 6 > best:
                continue
            n = g.route_length(here, dep) if g is not None else j * 12
            if n is not None and (best is None or n < best):
                best = n
        return best

    def per_move(self, v):
        """Supply one step costs this vehicle (asked once per design and kept)."""
        key = str(v["design"])
        c = self.cache.get(key)
        if c is None:
            try:
                r = _query("movement", {"vehicle": v["id"]})
                c = r["supply_per_move"] if not r["unlimited_supply"] else 0
            except Exception:
                c = 50
            self.cache[key] = c
        return c

    def must_refuel(self, v, margin=1.3, extra=8):
        """Whether the vehicle's supply only just covers the trip to the nearest depot."""
        cap = v["supply_capacity"] or 0
        if cap <= 0 or v["unlimited_supply"]:
            return False
        supply = v["supply"] or 0
        if supply >= cap * 0.9:
            return False
        per = self.per_move(v)
        if per <= 0:
            return False
        route = self.route_to_depot(v["location"])
        if route is None:
            return supply < cap * 0.4
        need = (route + extra) * per * margin
        return supply < need
