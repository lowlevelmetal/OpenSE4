"""Hegemon: the computer player (see mods/hegemon/README.md for its design)."""

from opense4 import ai

from . import config
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
UNSAFE_TURNS = 25   # a system where we lost an unarmed ship is avoided this long
COLONIST_STUCK = 4  # turns a colony ship with orders may stand still before it chooses again
COLONIZE_BLOCK = 20 # turns its target is then left alone



def good_target():
    """A planet is worth settling above this score (expansion.Expansion.targets)."""
    return 400.0 if config.on("wide_expansion") else 1500.0

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
        st = self.memory.get("strategy")
        self.econ = Economy(w, kn, {"phase": st.get("phase", "expand") if isinstance(st, dict) else "expand"})
        return w

    def dangers(self, w, intel):
        """{system: weight} where colony ships had better not go: hostile armed ships are
        believed there, or one of our unarmed ships was lost there lately."""
        out = {}
        for s, f in intel.threat_by_system().items():
            if f.attack > 0:
                out[s] = f.power()
        unsafe = self.memory.get("unsafe")
        if not isinstance(unsafe, dict):
            unsafe = {}
            self.memory["unsafe"] = unsafe
        for b in w.d["battles"]:
            for p in b["pieces"]:
                if p["owner"] == w.me and p["kind"] == "vehicle" and p["survivor"] != w.me and p["design"] is not None:
                    d = w.designs.get(p["design"])
                    if d is not None and role_of_name(d["name"]) in ("colonist", "scout", "trooper"):
                        unsafe[str(b["location"]["system"])] = w.turn
        for k in list(unsafe.keys()):
            if w.turn - unsafe[k] > UNSAFE_TURNS:
                del unsafe[k]
            else:
                out[int(k)] = out.get(int(k), 0) + 1
        return out

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
        dangers = self.dangers(w, Intel(w, self.memory))
        unsafe = self.memory.get("unsafe", {})
        pos = self.memory.setdefault("colonist_pos", {})
        blocked = self.memory.setdefault("colonize_blocked", {})
        for k in list(blocked.keys()):
            if blocked[k] <= w.turn:
                del blocked[k]
        alive = set()
        for v in roles.get("colonist", []):
            mine.add(v["id"])
            if v["fleet"] is not None:
                continue
            key = str(v["id"])
            alive.add(key)
            here = v["location"]
            at = [here["system"], here["x"], here["y"]]
            p = pos.get(key)
            if p is None or p[:3] != at:
                pos[key] = at + [w.turn]
            elif v["orders"] and w.turn - p[3] >= COLONIST_STUCK:
                # Stuck (a sector it will not enter, a blockade): another target for a while.
                for o in v["orders"]:
                    if o["kind"] == "colonize" and o["object"] is not None:
                        blocked[str(o["object"])] = w.turn + COLONIZE_BLOCK
                pos[key] = at + [w.turn]
                idle.append(v)
                continue
            if not v["orders"]:
                idle.append(v)
                continue
            # On its way through a system where one of our unarmed ships was just lost:
            # choose again.
            for o in v["orders"]:
                if o["kind"] == "colonize" and o["object"] is not None:
                    s = w.system_of(o["object"])
                    p = w.path(v["location"]["system"], s) if s is not None else None
                    if p is not None and any(str(x) in unsafe for x in p[1:]):
                        idle.append(v)
                    break
        for k in list(pos.keys()):
            if k not in alive:
                del pos[k]
        ex = Expansion(w, self.kn, self.econ, dangers, self.memory)
        logi = Logistics(w, self.kn, self.memory, view)
        assigned = ex.assign(idle, logi)
        for v, planet in assigned:
            orders.add({"kind": "set_orders", "vehicle": v["id"], "orders": [{"kind": "colonize", "object": planet["id"]}]})
            self.note(planet["id"], "colonize target", kind="object")
        for v in ex.refuel:
            if (v["supply"] or 0) <= 0 or any(o["kind"] == "resupply" for o in v["orders"] or []):
                continue
            orders.add({"kind": "set_orders", "vehicle": v["id"], "orders": [{"kind": "resupply"}]})
        if DEBUG and w.turn % 5 == 0:
            self.log("T%d COLONISTS %s assigned %s dangers %s" % (
                w.turn, [(v["id"], v["location"], v["movement"], v["max_movement"], v["supply"], v["status"], [(o["kind"], o["object"]) for o in v["orders"]]) for v in roles.get("colonist", [])],
                [(v["id"], p["id"], p["system"]) for v, p in assigned], sorted(dangers.keys())))
        # Scouts explore.
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
            good = sum(1 for t in ex.targets(surfaces) if t[0] > good_target())
        strategy.update(my_force, good)
        mil = Military(w, self.kn, self.memory, intel, roles, strategy=strategy, logistics=logi)
        book = DesignBook(w, self.kn, self.memory)
        mil.troops_ready, mil.troop_fig = Invasion(w, self.memory, roles, book, mil).troops_ready()
        orders.extend(mil.plan())
        orders.extend(mil.launches(book))
        orders.extend(self.econ.conversions())
        mil.queued_role = lambda r: 0
        mil.queued_at = {}
        inv = Invasion(w, self.memory, roles, book, mil)
        orders.extend(inv.plan(strategy.phase in ("arm", "war")))
        if DEBUG and (roles.get("trooper") or inv.commands or book.design_id("troop") is not None):
            self.log("T%d INVADE troop=%s ready=%s transports=%d cmds=%s mem=%s" % (
                w.turn, book.design_id("troop"), mil.troops_ready, len(roles.get("trooper", [])),
                [(c["kind"], c.get("vehicle"), [o["kind"] for o in c.get("orders", [])]) for c in inv.commands][:6], inv.inv))
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
                by = {}
                for o in w.planets:
                    if o["colony"] is None:
                        sz = ex.capacity(o)
                        by.setdefault(o["surface"], []).append((sz[0], o["atmosphere"] == ex.atmosphere))
                self.log("T%d PLANETS free %s surfaces=%s race=%s/%s" % (w.turn, {k: (len(v), sum(1 for x in v if x[1]), sum(x[0] for x in v)) for k, v in by.items()},
                         surfaces, ex.home_surface, ex.atmosphere))
                sats = sum((v["count"] or 0) for v in w.my_vehicles if v["type"] == "satellite")
                mines = sum((v["count"] or 0) for v in w.my_vehicles if v["type"] == "mine")
                plats = sum(st["count"] for c in w.my_colonies if c["cargo"] for st in c["cargo"]["units"])
                caps = [(c["cargo_capacity"], c["cargo_used"]) for c in w.my_colonies][:8]
                self.log("T%d DEFENSE sats %d mines %d cargo_units %d caps %s" % (w.turn, sats, mines, plats, caps))
                parts = []
                for e in w.d["empires"]:
                    st = e["stats"]
                    if st is None or e["score"] is None:
                        continue
                    prod = sum(st["production"].values()) + st["research"] + st["intelligence"]
                    tons = (e["score"] - prod - 200 * st["tech_levels"]) // 10
                    parts.append((e["id"], e["score"], prod, st["research"], st["tech_levels"], tons, st["ships"], st["bases"], st["planets"]))
                self.log("T%d SCORES (id, score, production, research, techs, tonnage, ships, bases, colonies) %s" % (w.turn, parts))
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
        book = DesignBook(w, kn, self.memory, {"prices": econ.prices[:3]})
        orders.extend(book.update(surfaces))
        # Research.
        tbs = {}
        ex = Expansion(w, kn, econ, {}, self.memory)
        for sf in SURFACES:
            if sf not in surfaces:
                tl = ex.targets([sf])
                tbs[sf] = sum(t[0] for t in (tl if config.on("colonize_value") else tl[:8]))
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
        self.memory["unmet_ships"] = (self.memory.get("unmet_ships", 0) + cons.unmet) // 2
        if DEBUG:
            q = [(c["planet"], [(i["kind"], i["facility"], i["design"]) for i in c["queue"]["items"]]) for c in w.my_colonies if c["queue"]["items"]]
            self.log("T%d col=%d stored=%s wants=%s cmds=%s queues=%s prices=%s roles=%s" % (
                w.turn, len(w.my_colonies), econ.stored, wants["ships"], [(c["target"], c["item"]) for c in cons.commands], q,
                [round(x, 2) for x in econ.prices], book.book))
            self.log("T%d CONS %s" % (w.turn, getattr(cons, "debug", None)))
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

    def yard_plan(self, w, econ, book, strategy, queued_role, ships):
        # Yards: more building queues as the empire grows; space yard facilities on
        # colonies (no upkeep), and early a yard base at home.
        yard = book.design_id("yard")
        queues = 0
        for c in w.my_colonies:
            if c["space_yard"] or econ.colony_info.get(c["planet"], {}).get("yard"):
                queues += 1
        bases = 0
        for v in w.my_vehicles:
            if v["queue"] is not None:
                queues += 1
                bases += 1
        # More yards when ships we can pay for wait for a free yard (the last turns' count).
        unmet = self.memory.get("unmet_ships", 0)
        want_yards = 2
        if strategy.phase != "expand":
            want_yards = min(14, max(queues + (1 if unmet >= 2 else 0), 3))
        if yard is not None and bases + queued_role("yard") < 1 and w.turn < 40:
            ships.append({"design": yard, "count": 1, "priority": 65.0, "near": w.home_system()})
        yard_sites = []
        if queues < want_yards:
            cands = []
            for c in w.my_colonies:
                info = econ.colony_info.get(c["planet"])
                if info is None or info["yard"] or c["total_population"] < 5:
                    continue
                free = (c["facility_slots"] or 0) - len(c["facilities"] or []) - info["queued_facilities"]
                if free <= 0:
                    continue
                cands.append((c["total_population"] + 50 * free, c["planet"]))
            cands.sort(reverse=True)
            yard_sites = [p for _, p in cands[:want_yards - queues]]

        return yard_sites

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
        intel = Intel(w, self.memory)
        strategy = Strategy(w, intel, self.memory)
        strategy.assess()

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
            want = min(3, (frontier + 2) // 3) if w.turn < 80 else min(2, (frontier + 3) // 4)
            # A scout stranded without supply does not count.
            have = sum(1 for v in roles.get("scout", []) if (v["supply"] or 0) > 0 or v["unlimited_supply"]) + queued_role("scout")
            if want > have:
                ships.append({"design": scout, "count": want - have, "priority": 70.0, "near": None})
        yard_sites = []
        if config.on("yard_demand"):
            yard_sites = self.yard_plan(w, econ, book, strategy, queued_role, ships)
        else:
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
                if want > queues + queued_role("yard"):
                    ships.append({"design": yard, "count": 1, "priority": 65.0, "near": w.home_system()})
        # Colony ships: one per good target, a few at a time.
        colony = {}
        for s in surfaces:
            did = book.design_id("colony:" + s)
            if did is not None:
                colony[s] = did
        if colony:
            in_flight = len(roles.get("colonist", [])) + queued_role("colonist")
            ex = Expansion(w, self.kn, econ, self.dangers(w, intel), self.memory)
            targets = ex.targets(list(colony.keys()))
            home = w.home_system()
            good = [t for t in targets if t[0] > good_target() and (home is None or not ex.crosses_danger(home, t[2]))]
            cap = (4 + len(w.my_colonies) // 2) if config.on("more_colony_ships") else (3 + len(w.my_colonies) // 3)
            want = min(len(good), cap) - in_flight
            by_surface = {}
            for t in good[:max(0, want)]:
                s = t[1]["surface"]
                by_surface[s] = by_surface.get(s, 0) + 1
            for s, n in by_surface.items():
                ships.append({"design": colony[s], "count": n, "priority": 60.0, "near": None})
            if DEBUG and w.turn % 5 == 0:
                self.log("T%d EXPAND targets=%d good=%d in_flight=%d want=%d top=%s" % (
                    w.turn, len(targets), len(good), in_flight, want,
                    [(int(t[0]), t[1]["surface"], t[1]["size"], t[1]["atmosphere"] == ex.atmosphere, t[2]) for t in targets[:8]]))
        # Warships: as the military planner asks.
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
        inv.plan(strategy.phase in ("arm", "war"))
        ships.extend(inv.wants)
        if DEBUG:
            yq = [(c["planet"], [(i["design"], i["done_in"]) for i in c["queue"]["items"]], c["queue"]["rate"]["minerals"]) for c in w.my_colonies if c["space_yard"]]
            vq = [(v["id"], [(i["design"], i["done_in"]) for i in v["queue"]["items"]], v["queue"]["rate"]["minerals"]) for v in w.my_vehicles if v["queue"] is not None]
            self.log("T%d MILWANT %s phase=%s yards=%s %s stored=%s sites=%s conv=%s/%s avail=%s income=%s" % (w.turn, getattr(mil, "debug", None), strategy.phase, yq, vq, econ.stored, yard_sites,
                     econ.converters, econ.converter_queued, [f.id for f in self.kn.facilities if f.convert and self.kn.meets(f.reqs)], econ.report["colonies"]))
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
        return {"ships": ships, "yard_sites": yard_sites, "depots": depots, "defense": {}}
