"""Hegemon: the computer player (see mods/hegemon/README.md for its design)."""

from opense4 import ai

from .world import World
from .knowledge import Knowledge, SURFACES
from .economy import Economy
from .construction import Construction
from .expansion import Expansion
from .research import Research
from .designs import DesignBook
from .intel import Intel
from .military import Military
from .strategy import Strategy
from .diplomacy import Diplomacy
from .explore import Explorer
from .logistics import Logistics
from .tactics import Tactics
from .invasion import Invasion
from .intel import Force, design_strength, planet_strength

DEBUG = False

# Tunable weights (mods/hegemon/README.md, "Tuning").
TUNE = {
    "research": {"military": 1.0, "share_early": 0.1, "share_late": 0.25, "horizon": 200.0, "level_score": 300.0},
}


def role_of_name(name):
    if name is None:
        return None
    for prefix in ("Scout", "Colonist", "Warship", "Guard", "Trooper", "Yard", "Sentinel", "Mine", "Troop"):
        if name.startswith(prefix + " "):
            return prefix.lower()
    return None


class Hegemon(ai.Player):
    # ---- the session's shared pieces ----

    def _setup(self, view):
        if not isinstance(self.memory, dict):
            self.memory = {}
        w = World(view)
        self.w = w
        kn = getattr(self, "_kn", None)
        if kn is None:
            kn = Knowledge(self.rules.raw, w.research_levels(), w.options["tech_cost"])
            self._kn = kn
        else:
            kn.levels = w.research_levels()
        self.kn = kn
        self.econ = Economy(w, kn)
        return w

    def my_roles(self, w):
        """Our vehicles by role: {role: [vehicle maps]}."""
        out = {}
        for v in w.my_vehicles:
            d = w.designs.get(v["design"])
            role = role_of_name(d["name"]) if d is not None else None
            if role is None:
                fig = d["figures"] if d is not None else None
                if fig is not None and v["type"] == "ship":
                    if fig["colonize"]:
                        role = "colonist"
                    elif fig["weapons"] > 0:
                        role = "warship"
            if role is None:
                role = "other"
            lst = out.get(role)
            if lst is None:
                lst = []
                out[role] = lst
            lst.append(v)
        return out

    def colonizable_surfaces(self):
        out = []
        for c in self.kn.components:
            if c.colonize and "ship" in c.types and self.kn.meets(c.reqs):
                for s in c.colonize:
                    if s not in out:
                        out.append(s)
        return out

    # ---- callbacks ----

    def politics(self, view, orders):
        w = self._setup(view)
        strategy = Strategy(w, Intel(w, self.memory), self.memory)
        dip = Diplomacy(w, self.memory, strategy)
        orders.extend(dip.plan())
        if DEBUG and dip.commands:
            self.log("T%d DIPLO %s treaties=%s" % (w.turn, [(c["kind"], c.get("message")) for c in dip.commands],
                                                    [(e, w.treaty(e)) for e in w.relations]))

    def orders(self, view, orders):
        w = self._setup(view)
        # Meeting others after a warp must not clear our orders: our plans decide.
        st = w.my["settings"]
        if st["clear_orders_on_encounter"] != "never":
            orders.add({"kind": "set_encounter_options", "clear_orders_on_encounter": "never"})
        roles = self.my_roles(w)
        mine = set()
        # Colony ships to the best targets.
        idle = []
        for v in roles.get("colonist", []):
            mine.add(v["id"])
            if not v["orders"] and v["fleet"] is None:
                idle.append(v)
        ex = Expansion(w, self.kn, self.econ, {}, self.memory)
        for v, planet in ex.assign(idle):
            orders.add({"kind": "set_orders", "vehicle": v["id"], "orders": [{"kind": "colonize", "object": planet["id"]}]})
            self.note(planet["id"], "colonize target", kind="object")
        # Scouts explore.
        logi = Logistics(w, self.kn, self.memory, view)
        orders.extend(Explorer(w, self.memory, logi).plan(roles.get("scout", [])))
        intel = Intel(w, self.memory)
        strategy = Strategy(w, intel, self.memory)
        my_force = Force()
        for v in roles.get("warship", []):
            a, h = design_strength(w.figures(v["design"]))
            my_force.add(a, h)
        good = 0
        surfaces = self.colonizable_surfaces()
        if surfaces:
            good = sum(1 for t in ex.targets(surfaces) if t[0] > 1500.0)
        strategy.update(my_force, good)
        mil = Military(w, self.kn, self.memory, intel, roles, strategy=strategy, logistics=logi)
        orders.extend(mil.plan())
        book = DesignBook(w, self.kn, self.memory)
        orders.extend(mil.launches(book))
        mil.queued_role = lambda r: 0
        mil.queued_at = {}
        orders.extend(Invasion(w, self.memory, roles, book, mil).plan(strategy.phase == "war"))
        for oid, text in mil.notes:
            self.note(oid, text, kind="object")
        if DEBUG:
            for b in w.d["battles"]:
                ms = [p for p in b["pieces"] if p["owner"] == w.me and p["kind"] == "vehicle"]
                ts = [p for p in b["pieces"] if p["owner"] not in (None, w.me) and p["kind"] == "vehicle"]
                mp = [p for p in b["pieces"] if p["owner"] == w.me and p["kind"] == "planet"]
                tp = [p for p in b["pieces"] if p["owner"] not in (None, w.me) and p["kind"] == "planet"]
                self.log("T%d SHIPFIGHT ours %d lost %d theirs %d lost %d ourplanets %d lost %d theirplanets %d lost %d" % (
                    w.turn, len(ms), sum(1 for p in ms if p["survivor"] != w.me), len(ts), sum(1 for p in ts if p["survivor"] is None),
                    len(mp), sum(1 for p in mp if p["survivor"] != w.me), len(tp), sum(1 for p in tp if p["survivor"] is None)))
                kinds = {}
                for p in b["pieces"]:
                    if p["owner"] == w.me:
                        dd = w.designs.get(p["design"]) if p["design"] is not None else None
                        role = role_of_name(dd["name"]) if dd is not None else p["kind"]
                        k = "%s:%s" % (role, "ok" if p["survivor"] == w.me else "lost")
                        kinds[k] = kinds.get(k, 0) + 1
                self.log("T%d BKIND %s" % (w.turn, sorted(kinds.items())))
                if sum(v for k, v in kinds.items() if k.endswith(":lost")) >= 5:
                    self.log("T%d BOURS %s" % (w.turn, [(p["kind"], p["name"][:14], p["count"], p["damage"], p["survivor"]) for p in b["pieces"] if p["owner"] == w.me][:40]))
                mine_n = sum(1 for p in b["pieces"] if p["owner"] == w.me)
                mine_lost = sum(1 for p in b["pieces"] if p["owner"] == w.me and p["survivor"] != w.me)
                theirs = sum(1 for p in b["pieces"] if p["owner"] != w.me)
                theirs_lost = sum(1 for p in b["pieces"] if p["owner"] != w.me and p["survivor"] is None)
                planets = [(p["owner"], p["name"], p["survivor"]) for p in b["pieces"] if p["kind"] == "planet"]
                self.log("T%d BATTLE sys=%s ours %d lost %d theirs %d lost %d planets %s" % (w.turn, b["location"]["system"], mine_n, mine_lost, theirs, theirs_lost, planets))
            th = [(s, int(f.attack), int(f.hp)) for s, f in mil.threats.items()]
            for fid, t in mil.fleet_mem.items():
                if t.get("task") == "strike":
                    c = w.colonies.get(t["planet"])
                    o = w.objects.get(t["planet"])
                    f = w.fleets.get(int(fid))
                    self.log("T%d STRIKE fleet %s at %s target %s at %s/%s pop %s owner %s orders %s" % (
                        w.turn, fid, f["location"] if f else None, t["planet"], o["system"] if o else None, o["sector"] if o else None,
                        c["total_population"] if c else None, c["owner"] if c else None, [(x["kind"], x["object"]) for x in f["orders"]] if f else None))
            for fid, t in mil.fleet_mem.items():
                f = w.fleets.get(int(fid))
                if f is not None:
                    self.log("T%d FTASK %s %s at %s orders %s" % (w.turn, fid, t, f["location"], [(x["kind"], x["object"], x["location"]["system"]) for x in f["orders"]]))
            for b in w.d["battles"]:
                self.log("T%d BPIECES %s" % (w.turn, [(p["kind"], p["owner"], p["name"][:12], p["damage"], p["survivor"]) for p in b["pieces"] if p["owner"] != w.me][:8]))
            if w.turn % 10 == 0:
                sats = sum((v["count"] or 0) for v in w.my_vehicles if v["type"] == "satellite")
                mines = sum((v["count"] or 0) for v in w.my_vehicles if v["type"] == "mine")
                plats = sum(st["count"] for c in w.my_colonies if c["cargo"] for st in c["cargo"]["units"])
                caps = [(c["cargo_capacity"], c["cargo_used"]) for c in w.my_colonies][:8]
                self.log("T%d DEFENSE sats %d mines %d cargo_units %d caps %s" % (w.turn, sats, mines, plats, caps))
                unexplored = sum(1 for x in w.systems.values() if not x["explored"])
                frontier = sum(1 for o in w.warps if o["destination_system"] is None)
                self.log("T%d INTEL foreign_colonies=%d rivals=%s unexplored=%d frontier=%d scouts=%d" % (
                    w.turn, len(w.foreign_colonies), strategy.rivals, unexplored, frontier, len(roles.get("scout", []))))
                for fid, f in w.fleets.items():
                    ms = [w.vehicles[m] for m in f["members"] if m in w.vehicles]
                    self.log("T%d FLEET %d at %s orders %s members %s depots %s" % (w.turn, fid, f["location"], [(o["kind"], o["location"]) for o in f["orders"]],
                             [(v["supply"], v["supply_capacity"], v["location"]["system"], v["max_movement"]) for v in ms][:6], logi.depots()))
                for v in roles.get("scout", []):
                    self.log("T%d SCOUT %d at %s supply %s/%s move %s orders %s" % (w.turn, v["id"], v["location"], v["supply"], v["supply_capacity"], v["max_movement"], v["orders"]))
                for o in w.warps:
                    if o["destination_system"] is None:
                        self.log("T%d FRONTIER wp %d in sys %d at %s known=%s" % (w.turn, o["id"], o["system"], o["sector"], o["link_known"]))
            self.log("T%d MIL phase=%s target=%s rally=%s warships=%d fleets=%s threats=%s cols=%s" % (
                w.turn, strategy.phase, strategy.target, mil.rally, len(roles.get("warship", [])), mil.fleet_mem, th, w.colony_systems))

    def economy(self, view, orders):
        w = self._setup(view)
        kn = self.kn
        econ = self.econ
        surfaces = self.colonizable_surfaces()
        book = DesignBook(w, kn, self.memory)
        orders.extend(book.update(surfaces))
        # Research.
        tbs = {}
        ex = Expansion(w, kn, econ, {}, self.memory)
        for sf in SURFACES:
            if sf not in surfaces:
                tbs[sf] = sum(t[0] for t in ex.targets([sf])[:8])
        rt = TUNE["research"]
        strategy = Strategy(w, Intel(w, self.memory), self.memory)
        weights = strategy.research_weights()
        r = Research(w, kn, econ, weights, {"targets_by_surface": tbs, "horizon": rt["horizon"], "level_score": rt["level_score"]})
        cmd = r.plan(self.memory)
        if cmd is not None:
            orders.add(cmd)
        if DEBUG and w.turn % 10 == 0:
            self.log("T%d RESEARCH income=%d %s" % (w.turn, w.my["research"]["income"], getattr(r, "top", None)))
        # What to build.
        wants = self.ship_wants(w, econ, book, surfaces)
        cons = Construction(w, kn, econ, wants)
        orders.extend(cons.plan())
        if DEBUG:
            q = [(c["planet"], [(i["kind"], i["facility"], i["design"]) for i in c["queue"]["items"]]) for c in w.my_colonies if c["queue"]["items"]]
            self.log("T%d col=%d stored=%s wants=%s cmds=%s queues=%s prices=%s roles=%s" % (
                w.turn, len(w.my_colonies), econ.stored, wants["ships"], [(c["target"], c["item"]) for c in cons.commands], q,
                [round(x, 2) for x in econ.prices], book.book))
            for line in book.made:
                self.log("T%d DESIGN %s" % (w.turn, line))
            if book.problems:
                self.log("design problems %s" % book.problems)

    def enter_sector(self, view, question):
        """A move would enter a sector with enemies: unarmed groups stay out; armed ones
        go in when they are a match for what is there (or were sent to fight there)."""
        w = getattr(self, "w", None)
        if w is None:
            return None
        ours = Force()
        armed = False
        for vid in question.vehicle_ids:
            v = w.vehicles.get(vid)
            if v is None:
                continue
            a, h = design_strength(w.figures(v["design"]))
            if a > 0:
                armed = True
            ours.add(a, h)
        sec = question.sector or {}
        system, x, y = sec.get("system"), sec.get("x"), sec.get("y")
        theirs = Force()
        for v in w.foreign_vehicles:
            loc = v["location"]
            if loc["system"] == system and loc["x"] == x and loc["y"] == y and w.hostile(v["owner"]):
                a, h = design_strength(w.figures(v["design"]))
                theirs.add(a * (v["count"] or 1), h * (v["count"] or 1))
        planets = 0
        for c in w.foreign_colonies:
            o = w.objects.get(c["planet"])
            if o is not None and o["system"] == system and o["sector"]["x"] == x and o["sector"]["y"] == y and w.hostile(c["owner"]):
                pa, ph = planet_strength(c, w.turn, self.memory.get("planet_seen", {}).get(str(c["planet"])))
                theirs.add(pa, ph)
                planets += 1
        if not armed:
            return theirs.attack <= 0 and planets == 0
        return ours.beats(theirs, 0.8)

    def battle_round(self, battle, orders):
        """Each phase of a space battle: our own targeting and movement (tactics.py)."""
        data = battle.raw
        view = self.view
        me = self.empire_id
        hostile_cache = getattr(self, "_hostile", None)
        if hostile_cache is None:
            hostile_cache = {}
            designs = {}
            if view is not None:
                d = view.raw
                for e in d["empires"]:
                    rel = e["relation"]
                    if e["id"] != me:
                        hostile_cache[e["id"]] = rel is None or rel["treaty"] in ("war", "non_intercourse", "none")
                for x in d["designs"]:
                    designs[x["id"]] = x
            self._hostile = hostile_cache
            self._designs = designs
        rules = getattr(self, "_rules_raw", None)
        if rules is None:
            rules = self.rules.raw
            self._rules_raw = rules
        t = Tactics(data, me, lambda e: hostile_cache.get(e, True), rules, self._designs)
        orders.extend(t.plan())
        if DEBUG:
            mine = sum(1 for p in data["pieces"] if p["owner"] == me and p["alive"])
            theirs = sum(1 for p in data["pieces"] if p["owner"] != me and p["owner"] is not None and p["alive"])
            self.log("ROUND %d mine %d theirs %d orders %d refused %s" % (data["round"], mine, theirs, len(orders), [(r["reason"]) for r in (self.refused or [])][:3]))

    def queued_designs(self, w):
        out = {}
        for c in w.my_colonies:
            for it in c["queue"]["items"]:
                if it["kind"] == "vehicle":
                    out[it["design"]] = out.get(it["design"], 0) + it["count"]
        for v in w.my_vehicles:
            q = v["queue"]
            if q is not None:
                for it in q["items"]:
                    if it["kind"] == "vehicle":
                        out[it["design"]] = out.get(it["design"], 0) + it["count"]
        return out

    def ship_wants(self, w, econ, book, surfaces):
        ships = []
        roles = self.my_roles(w)
        queued = self.queued_designs(w)

        def queued_role(prefix):
            n = 0
            for did, k in queued.items():
                d = w.designs.get(did)
                if d is not None and role_of_name(d["name"]) == prefix:
                    n += k
            return n

        # Scouts while there is unexplored space next to what we know.
        frontier = 0
        for o in w.warps:
            if o["destination_system"] is None:
                frontier += 1
        scout = book.design_id("scout")
        if scout is not None:
            want = min(2, (frontier + 2) // 3) if w.turn < 60 else min(1, frontier)
            have = len(roles.get("scout", [])) + queued_role("scout")
            if want > have:
                ships.append({"design": scout, "count": want - have, "priority": 70.0, "near": None})
        # Yards: more building queues as the empire grows.
        yard = book.design_id("yard")
        if yard is not None:
            queues = 0
            for c in w.my_colonies:
                if c["space_yard"]:
                    queues += 1
            for v in w.my_vehicles:
                if v["queue"] is not None:
                    queues += 1
            want = min(6, 2 + len(w.my_colonies) // 4)
            have = queues + queued_role("yard")
            if want > have:
                ships.append({"design": yard, "count": 1, "priority": 65.0, "near": w.home_system()})
        # Colony ships: one per good target, a few at a time.
        colony = {}
        for s in surfaces:
            did = book.design_id("colony:" + s)
            if did is not None:
                colony[s] = did
        if colony:
            in_flight = len(roles.get("colonist", [])) + queued_role("colonist")
            ex = Expansion(w, self.kn, econ, {}, self.memory)
            targets = ex.targets(list(colony.keys()))
            good = [t for t in targets if t[0] > 1500.0]
            cap = 3 + len(w.my_colonies) // 3
            want = min(len(good), cap) - in_flight
            by_surface = {}
            for t in good[:max(0, want)]:
                s = t[1]["surface"]
                by_surface[s] = by_surface.get(s, 0) + 1
            for s, n in by_surface.items():
                ships.append({"design": colony[s], "count": n, "priority": 60.0, "near": None})
        # Warships: as the military planner asks.
        intel = Intel(w, self.memory)
        strategy = Strategy(w, intel, self.memory)
        strategy.assess()
        mil = Military(w, self.kn, self.memory, intel, roles, strategy=strategy)
        mil.rally = mil.choose_rally()
        queued_at = {}
        plat = book.design_id("platform")
        for c in w.my_colonies:
            for it in c["queue"]["items"]:
                if it["kind"] == "vehicle":
                    if it["design"] == plat:
                        queued_at[c["planet"]] = queued_at.get(c["planet"], 0) + it["count"]
                    key = (c["planet"], it["design"])
                    queued_at[key] = queued_at.get(key, 0) + it["count"]
        ships.extend(mil.wants(econ, book, queued_role, queued_at))
        mil.queued_role = queued_role
        mil.queued_at = queued_at
        inv = Invasion(w, self.memory, roles, book, mil)
        inv.plan(strategy.phase == "war")
        ships.extend(inv.wants)
        if DEBUG:
            self.log("T%d MILWANT %s" % (w.turn, getattr(mil, "debug", None)))
        # Depots: the rally point, and every colony system more than a jump from one.
        depots = []
        if mil.rally is not None:
            depots.append(mil.rally)
        have = [s for s, tags in econ.sys_has.items() if "supply" in tags]
        near = w.bfs(have, 2) if have else {}
        for s in w.colony_systems:
            d = near.get(s)
            if (d is None or d > 1) and s not in depots:
                depots.append(s)
        return {"ships": ships, "yard_sites": [], "depots": depots, "defense": {}}
