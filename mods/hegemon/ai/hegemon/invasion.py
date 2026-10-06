"""Hegemon's invasions: take enemy colonies with their facilities instead of only
burning them (the classic AI never invades, and rebuilds burnt colonies at once).

- Troop transports wait at a staging colony in the rally system, where troop units are
  built (into the colony's cargo, then the transports' in the same sector) and loaded.
- When the main fleet is striking a colony and the transports hold enough troops for its
  militia (one militia unit for every 20 million people, 10 attack and 30 hit points
  each, as ground combat has it), they go to the colony's sector; in the battle there
  the tactics land them beside the planet once its guns are silent.
- Empty transports go back to the staging colony to reload."""

from .intel import design_strength

MILITIA_ATTACK = 10
MILITIA_HP = 30
MILITIA_PER = 20


def troop_power(fig):
    """(attack, hit points) of one troop unit: its weapons' damage and its structure."""
    if fig is None:
        return (0, 0)
    return (fig["weapon_damage"], fig["structure"] + 2 * (fig["shields"] or 0))


def troops_needed(colony, fig, margin=2.0):
    """How many troop units of a design beat a colony's militia (Lanchester), or None."""
    pop = colony["total_population"] or 0
    militia = pop // MILITIA_PER
    a, h = troop_power(fig)
    if a <= 0 or h <= 0:
        return None
    if militia <= 0:
        return 2
    need = (margin * militia * militia * MILITIA_ATTACK * MILITIA_HP / float(a * h)) ** 0.5
    return int(need) + 2


class Invasion:
    def __init__(self, world, mem, roles, book, military):
        self.w = world
        self.mem = mem
        self.roles = roles
        self.book = book
        self.mil = military
        inv = mem.get("invasion")
        if not isinstance(inv, dict):
            inv = {}
            mem["invasion"] = inv
        self.inv = inv
        self.commands = []
        self.wants = []

    def troops_aboard(self, v, troop_ids):
        n = 0
        cargo = v["cargo"]
        if cargo is not None:
            for st in cargo["units"]:
                if st["design"] in troop_ids:
                    n += st["count"]
        return n

    def staging(self):
        """Our colony in the rally system that can build (the most populous)."""
        w = self.w
        rally = self.mil.rally
        best = None
        for c in w.my_colonies:
            o = w.objects.get(c["planet"])
            if o is None or o["system"] != rally or c["total_population"] <= 0:
                continue
            if best is None or c["total_population"] > best[0]["total_population"]:
                best = (c, o)
        return best

    def troops_ready(self):
        """(troop units aboard our transports, the troop design's figures)."""
        w = self.w
        troop = self.book.design_id("troop")
        tfig = w.figures(troop) if troop is not None else None
        ids = set()
        for d in w.d["designs"]:
            if d["owner"] == w.me and d["figures"] is not None and d["figures"]["vehicle_type"] == "troop":
                ids.add(d["id"])
        n = 0
        for v in self.roles.get("trooper", []):
            n += self.troops_aboard(v, ids)
        return n, tfig

    def plan(self, at_war):
        w = self.w
        troop = self.book.design_id("troop")
        trooper = self.book.design_id("trooper")
        tfig = w.figures(troop) if troop is not None else None
        if tfig is None or trooper is None:
            return self.commands
        troop_ids = set()
        for d in w.d["designs"]:
            if d["owner"] == w.me and d["figures"] is not None and d["figures"]["vehicle_type"] == "troop":
                troop_ids.add(d["id"])
        transports = [v for v in self.roles.get("trooper", []) if v["fleet"] is None]
        stage = self.staging()
        # The colony the main fleet is striking.
        target = None
        fleet_at = None
        for fid, t in self.mil.fleet_mem.items():
            if t.get("task") == "strike":
                c = w.colonies.get(t.get("planet"))
                f = w.fleets.get(int(fid))
                if c is not None and c["owner"] != w.me and f is not None:
                    target = c
                    fleet_at = f["location"]
                    break
        need = troops_needed(target, tfig) if target is not None else None
        aboard = 0
        for v in transports:
            aboard += self.troops_aboard(v, troop_ids)
        # Build transports and troops while at war.
        cap_each = (w.figures(trooper) or {}).get("cargo", 0)
        size = tfig["tonnage_max"] or tfig["tonnage_used"] or 1
        per_ship = cap_each // size if size > 0 else 0
        if at_war and per_ship > 0:   # (arm or war)
            want_units = max(need or 0, self.mem.get("troop_goal", 12))
            want_ships = min(4, (want_units + per_ship - 1) // per_ship)
            if len(self.roles.get("trooper", [])) + self.mil.queued_role("trooper") < want_ships:
                self.wants.append({"design": trooper, "count": 1, "priority": 44.0, "near": self.mil.rally})
            if stage is not None:
                c, o = stage
                in_colony = 0
                for st in (c["cargo"]["units"] if c["cargo"] is not None else []):
                    if st["design"] in troop_ids:
                        in_colony += st["count"]
                room = len(transports) * per_ship
                short = min(want_units, room) - aboard - in_colony - self.mil.queued_at.get((c["planet"], troop), 0)
                free = (c["cargo_capacity"] or 0) - (c["cargo_used"] or 0)
                free_ships = room * size - aboard * size
                n = min(short, (free + free_ships) // size, 10)
                if n > 0:
                    self.wants.append({"design": troop, "count": n, "priority": 46.0, "at": c["planet"], "units": True})
        # Each transport: load at the staging colony, go in when the strike is on, come back empty.
        for v in transports:
            here = v["location"]
            n = self.troops_aboard(v, troop_ids)
            key = str(v["id"])
            job = self.inv.get(key)
            if v["orders"]:
                continue
            if stage is not None:
                c, o = stage
                at_stage = here["system"] == o["system"] and here["x"] == o["sector"]["x"] and here["y"] == o["sector"]["y"]
            else:
                at_stage = False
            if n > 0 and target is not None and fleet_at is not None and fleet_at["system"] == w.system_of(target["planet"]) \
                    and aboard >= (need or 999):
                to = w.objects.get(target["planet"])
                if to is not None:
                    loc = {"system": to["system"], "x": to["sector"]["x"], "y": to["sector"]["y"]}
                    self.commands.append({"kind": "set_orders", "vehicle": v["id"], "orders": [{"kind": "move_to", "location": loc}]})
                    self.inv[key] = {"job": "invade", "planet": target["planet"], "turn": w.turn}
                    continue
            if stage is None:
                continue
            c, o = stage
            loc = {"system": o["system"], "x": o["sector"]["x"], "y": o["sector"]["y"]}
            if not at_stage:
                if job is None or job.get("job") != "invade" or w.turn - job.get("turn", 0) > 3 or n == 0:
                    self.commands.append({"kind": "set_orders", "vehicle": v["id"], "orders": [{"kind": "move_to", "location": loc}]})
                    self.inv[key] = {"job": "stage", "turn": w.turn}
                continue
            # At the staging colony: load what troops it holds.
            for st in (c["cargo"]["units"] if c["cargo"] is not None else []):
                if st["design"] in troop_ids and st["count"] > 0:
                    self.commands.append({"kind": "set_orders", "vehicle": v["id"],
                                          "orders": [{"kind": "load_cargo", "design": st["design"], "amount": -1, "location": loc}]})
                    break
        for k in list(self.inv.keys()):
            if int(k) not in w.vehicles:
                del self.inv[k]
        return self.commands
