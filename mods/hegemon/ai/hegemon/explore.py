"""Hegemon's scouts: each idle scout goes through the nearest frontier warp point (a
warp point of an explored system whose far side we have not seen) that no other scout
is heading for. A scout sets out only with the supply for the trip there and back to a
depot (else it refuels first); once on its way it turns back only when its supply no
longer covers the way home, so it does not give up at the warp point. A scout that has
not moved for a few turns gives up its warp point for a while (a hazard, a pull toward
a system's centre, a blockade) and tries another."""

STUCK_TURNS = 4
BLOCK_TURNS = 15


class Explorer:
    def __init__(self, world, mem, logistics=None):
        self.logistics = logistics
        self.w = world
        self.mem = mem
        ex = mem.get("explore")
        if not isinstance(ex, dict):
            ex = {"pos": {}, "blocked": {}}
            mem["explore"] = ex
        self.ex = ex
        self.commands = []

    def frontier(self):
        w = self.w
        blocked = self.ex["blocked"]
        out = []
        for o in w.warps:
            if o["destination_system"] is not None:
                continue
            if w.turn < blocked.get(str(o["id"]), -1):
                continue
            out.append(o)
        return out

    def committed(self, v):
        """Whether the scout is on its way through a warp point."""
        for o in v["orders"] or []:
            if o["kind"] == "warp" and o["object"] is not None:
                return True
        return False

    def fuel_for(self, v, o):
        """Whether the scout's supply takes it to warp point `o`, through it and back to a depot."""
        lg = self.logistics
        if lg is None or v["unlimited_supply"] or not v["supply_capacity"]:
            return True
        per = lg.per_move(v)
        if per <= 0:
            return True
        loc = {"system": o["system"], "x": o["sector"]["x"], "y": o["sector"]["y"]}
        g = lg.galaxy
        there = g.route_length(v["location"], loc) if g is not None else None
        back = lg.route_to_depot(loc)
        if there is None or back is None:
            return True
        return (v["supply"] or 0) >= (there + back + 6) * per * 1.1

    def plan(self, scouts):
        w = self.w
        pos = self.ex["pos"]
        blocked = self.ex["blocked"]
        for k in list(blocked.keys()):
            if blocked[k] <= w.turn:
                del blocked[k]
        frontier = self.frontier()
        taken = set()
        for v in scouts:
            for o in v["orders"] or []:
                if o["kind"] == "warp" and o["object"] is not None:
                    taken.add(o["object"])
        alive = set()
        for v in scouts:
            vid = str(v["id"])
            alive.add(vid)
            here = v["location"]
            key = [here["system"], here["x"], here["y"]]
            p = pos.get(vid)
            if p is None or p[0] != key:
                pos[vid] = [key, w.turn]
            elif v["orders"] and w.turn - p[1] >= STUCK_TURNS:
                # Stuck: give up the warp point it was heading for.
                for o in v["orders"]:
                    if o["kind"] == "warp" and o["object"] is not None:
                        blocked[str(o["object"])] = w.turn + BLOCK_TURNS
                        taken.discard(o["object"])
                self.commands.append({"kind": "set_orders", "vehicle": v["id"], "orders": []})
                pos[vid] = [key, w.turn]
                v["orders"] = []
            cap = v["supply_capacity"] or 0
            supply = v["supply"] or 0
            refuel = self.ex.setdefault("refuel", {})
            r = refuel.get(vid)
            if r is not None:
                if supply >= cap * 0.9 or not v["orders"]:
                    # Refuelled, or no depot to go to: back to exploring.
                    del refuel[vid]
                    if supply < cap * 0.35:
                        refuel[vid + "!"] = w.turn
                else:
                    continue
            elif cap > 0 and not v["unlimited_supply"] and w.turn - refuel.get(vid + "!", -99) > 10 and (
                    (self.logistics.must_refuel(v, margin=1.15, extra=5) if self.committed(v) else self.logistics.must_refuel(v))
                    if self.logistics is not None else supply < cap * 0.5):
                refuel[vid] = w.turn
                self.commands.append({"kind": "set_orders", "vehicle": v["id"], "orders": [{"kind": "resupply"}]})
                continue
            if v["orders"]:
                continue
            best = None
            dist = w.bfs([here["system"]])
            for o in frontier:
                if o["id"] in taken:
                    continue
                d = dist.get(o["system"])
                if d is None:
                    continue
                step = max(abs(o["sector"]["x"] - here["x"]), abs(o["sector"]["y"] - here["y"])) if o["system"] == here["system"] else 13
                key2 = (d, step)
                if best is None or key2 < best[0]:
                    best = (key2, o)
            if best is not None and supply < cap * 0.9 and not self.fuel_for(v, best[1]) and \
                    self.logistics is not None and self.logistics.route_to_depot(here) is not None:
                # Not enough for the trip: fill up first.
                refuel[vid] = w.turn
                self.commands.append({"kind": "set_orders", "vehicle": v["id"], "orders": [{"kind": "resupply"}]})
                continue
            if best is not None:
                o = best[1]
                taken.add(o["id"])
                loc = {"system": o["system"], "x": o["sector"]["x"], "y": o["sector"]["y"]}
                self.commands.append({"kind": "set_orders", "vehicle": v["id"],
                                      "orders": [{"kind": "move_to", "location": loc}, {"kind": "warp", "object": o["id"]}]})
        for k in list(pos.keys()):
            if k not in alive:
                del pos[k]
        refuel = self.ex.get("refuel", {})
        for k in list(refuel.keys()):
            if k.rstrip("!") not in alive:
                del refuel[k]
        return self.commands
