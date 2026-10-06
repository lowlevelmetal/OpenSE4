"""Hegemon's military operations.

Doctrine: concentrate, then strike where we are much stronger. Warships gather at a
rally point (our colony system nearest the enemy, with a supply depot) and form one
or a few large fleets. Each turn every fleet gets a task:

- defend: a colony system with hostile armed ships in it, if we can beat them there;
- strike: an enemy colony (or fleet) we beat by a wide margin, the most valuable for
  the risk and the distance;
- refit: resupply or repair when low;
- hold: wait at the rally point.

Colonies defend themselves with weapon platforms (units pay no maintenance) where
threats are seen. A strike is only launched when Lanchester's square law says we win
with a margin, counting the planet's defences as we estimate them."""

from . import config
from .intel import Force, design_strength, planet_strength
from .invasion import troops_needed
from .util import loc

STRIKE_MARGIN = 2.0
DEFEND_MARGIN = 1.3
SHARE_MAX = 0.75   # at most this share of a resource's income for the fleet's upkeep
MAINT_MAX = 0.85   # and all maintenance together at most this share


class Military:
    def __init__(self, world, kn, mem, intel, roles, tune=None, strategy=None, logistics=None):
        self.strategy = strategy
        self.logistics = logistics
        self.w = world
        self.kn = kn
        self.mem = mem
        self.intel = intel
        self.roles = roles
        self.tune = tune or {}
        self.commands = []
        self.notes = []
        fm = mem.get("fleets")
        if not isinstance(fm, dict):
            fm = {}
            mem["fleets"] = fm
        self.fleet_mem = fm
        self.threats = intel.threat_by_system()
        self.rally = None
        self.target = None

    # ---- strength ----

    def vehicle_force(self, v):
        fig = self.w.figures(v["design"])
        a, h = design_strength(fig)
        dmg = v["damage"] or 0
        full = v["structure"] or (fig["structure"] if fig is not None else 0)
        if full and dmg:
            k = max(0.1, 1.0 - dmg / float(full + 1))
            a *= k
            h *= k
        return a, h

    def fleet_force(self, members):
        f = Force()
        for v in members:
            a, h = self.vehicle_force(v)
            f.add(a, h)
        return f

    # ---- places ----

    def colony_sector(self, system):
        """The sector of our best colony in a system (where depots and yards are)."""
        best = None
        for c in self.w.my_colonies:
            o = self.w.objects.get(c["planet"])
            if o is not None and o["system"] == system:
                if best is None or c["total_population"] > best[0]:
                    best = (c["total_population"], o)
        if best is None:
            return loc(system)
        o = best[1]
        return loc(system, o["sector"]["x"], o["sector"]["y"])

    def enemy_colony_systems(self):
        out = {}
        for c in self.w.foreign_colonies:
            if not self.w.hostile(c["owner"]):
                continue
            s = self.w.system_of(c["planet"])
            if s is not None:
                out.setdefault(s, []).append(c)
        return out

    def choose_rally(self):
        w = self.w
        home = w.home_system()
        enemy = self.enemy_colony_systems()
        if not enemy or not w.colony_systems:
            return home
        dist = w.bfs(list(enemy.keys()))
        best = None
        for s in w.colony_systems:
            d = dist.get(s)
            if d is None:
                continue
            # Close to the enemy, but not where the enemy already is.
            key = (d if d > 0 else 2, 0 if s == home else 1)
            if best is None or key < best[0]:
                best = (key, s)
        return best[1] if best is not None else home

    # ---- targets ----

    def strike_targets(self):
        """Enemy colonies worth hitting, a sector at a time (a battle is fought in one
        sector): the target rival's and those of rivals at war with us, within three
        jumps of our space. [(score, colonies, system, needed force)]."""
        w = self.w
        enemy = self.enemy_colony_systems()
        if not enemy:
            return []
        dist = w.bfs([self.rally]) if self.rally is not None else {}
        near = w.territory_distance()
        known = self.mem.get("planet_seen", {})
        target = self.strategy.target if self.strategy is not None else None
        groups = []
        for s, cols in enemy.items():
            d = dist.get(s)
            if d is None or near.get(s, 99) > 3:
                continue
            by_sector = {}
            for c in cols:
                if c["owner"] != target and w.treaty(c["owner"]) != "war":
                    continue
                o = w.objects.get(c["planet"])
                if o is not None:
                    by_sector.setdefault((o["sector"]["x"], o["sector"]["y"]), []).append(c)
            for sector_cols in by_sector.values():
                groups.append((s, d, sector_cols))
        out = []
        for s, d, cols in groups:
            need = Force()
            value = 0.0
            invade = False
            troops = getattr(self, "troops_ready", 0)
            tfig = getattr(self, "troop_fig", None)
            for c in cols:
                a, h = planet_strength(c, w.turn, known.get(str(c["planet"])))
                n = troops_needed(c, tfig) if tfig is not None else None
                if n is not None and troops >= n and config.on("raids"):
                    # Troops take it: only its guns need silencing, not its people.
                    invade = True
                    h = max(300.0, h - (c["total_population"] or 0) * 10.0)
                need.add(a, h)
                value += 1000.0 + (c["total_population"] or 0) * 2.0
            t = self.threats.get(s)
            if t is not None:
                need.add(t.attack, t.hp, 0)
            score = value / (1.0 + need.power() / 1.0e6) / (1.0 + 0.4 * d)
            if invade:
                score *= 4.0
            if target is not None and any(c["owner"] == target for c in cols):
                score *= 3.0
            out.append((score, cols, s, need))
        out.sort(key=lambda x: -x[0])
        return out

    # ---- the plan ----

    @staticmethod
    def same_place(a, b):
        return a is not None and b is not None and a["system"] == b["system"] and a["x"] == b["x"] and a["y"] == b["y"]

    def plan(self):
        w = self.w
        self.rally = self.choose_rally()
        rally_loc = self.colony_sector(self.rally) if self.rally is not None else None
        warships = [v for v in self.roles.get("warship", []) if v["status"] != "mothballed"]
        by_fleet = {}
        loose = []
        for v in warships:
            if v["fleet"] is not None:
                by_fleet.setdefault(v["fleet"], []).append(v)
            else:
                loose.append(v)
        for k in list(self.fleet_mem.keys()):
            if int(k) not in w.fleets:
                del self.fleet_mem[k]
        # Cripples slow a whole fleet down: they leave it and go for repairs alone.
        repair = self.mem.setdefault("repairing", {})
        for fid in list(by_fleet.keys()):
            members = by_fleet[fid]
            speeds = [v["max_movement"] or 0 for v in members]
            top = max(speeds) if speeds else 0
            keep = []
            for v in members:
                mv = v["max_movement"] or 0
                if len(members) > 1 and mv < top - 1 and mv <= 2:
                    self.commands.append({"kind": "leave_fleet", "vehicle": v["id"]})
                    self.commands.append({"kind": "set_orders", "vehicle": v["id"], "orders": [{"kind": "repair"}]})
                    repair[str(v["id"])] = w.turn
                else:
                    keep.append(v)
            by_fleet[fid] = keep
        still = []
        for v in loose:
            key = str(v["id"])
            if key in repair:
                full = v["structure"] or 1
                if (v["damage"] or 0) <= 0 and (v["max_movement"] or 0) > 2 or w.turn - repair[key] > 25:
                    del repair[key]
                    still.append(v)
                elif not v["orders"] and w.turn - repair[key] > 3:
                    # Nowhere to repair it: it rejoins as it is.
                    del repair[key]
                    still.append(v)
                continue
            still.append(v)
        loose = still
        for key in list(repair.keys()):
            if int(key) not in w.vehicles:
                del repair[key]
        fleets = []
        for fid, members in by_fleet.items():
            f = w.fleets.get(fid)
            if f is not None:
                fleets.append([fid, f, members, self.fleet_force(members)])
        fleets.sort(key=lambda x: -x[3].power())
        # Fleets that meet merge into the strongest one there.
        merged = set()
        for i in range(len(fleets)):
            fid, f, members, force = fleets[i]
            if fid in merged:
                continue
            for j in range(i + 1, len(fleets)):
                gid, g, gm, gforce = fleets[j]
                if gid in merged or not self.same_place(g["location"], f["location"]):
                    continue
                for v in gm:
                    self.commands.append({"kind": "leave_fleet", "vehicle": v["id"]})
                    self.commands.append({"kind": "join_fleet", "fleet": fid, "vehicle": v["id"]})
                merged.add(gid)
                force.add(gforce.attack, gforce.hp, gforce.count)
                members.extend(gm)
        fleets = [x for x in fleets if x[0] not in merged]
        main = fleets[0] if fleets else None
        main_loc = main[1]["location"] if main is not None else None
        # Loose warships gather: at the rally they join a fleet there or form one.
        at_rally = []
        for v in loose:
            here = v["location"]
            if self.same_place(here, rally_loc):
                at_rally.append(v)
            elif not v["orders"] and rally_loc is not None:
                self.commands.append({"kind": "set_orders", "vehicle": v["id"], "orders": [{"kind": "move_to", "location": rally_loc}]})
        if at_rally:
            host = None
            for fid, f, members, force in fleets:
                if self.same_place(f["location"], rally_loc):
                    host = fid
                    break
            if host is not None:
                for v in at_rally:
                    self.commands.append({"kind": "join_fleet", "fleet": host, "vehicle": v["id"]})
            elif len(at_rally) >= 2:
                n = self.mem.get("fleet_seq", 0) + 1
                self.mem["fleet_seq"] = n
                self.commands.append({"kind": "create_fleet", "name": "Hegemon Host %d" % n, "members": [v["id"] for v in at_rally]})
        defend = self.defense_needs()
        targets = self.strike_targets()
        at_war = self.strategy is None or self.strategy.phase == "war"
        used_targets = set()
        defended = set()
        for idx in range(len(fleets)):
            fid, f, members, force = fleets[idx]
            is_main = idx == 0
            task = self.fleet_mem.get(str(fid), {})
            orders = f["orders"]
            if self.refit(fid, f, members, task, orders):
                continue
            # A strike under way that went badly: the planet is stronger than we thought.
            if task.get("task") == "strike":
                start = task.get("power", 0)
                if start > 0 and force.power() < 0.45 * start:
                    known = self.mem.setdefault("planet_seen", {})
                    key = str(task.get("planet"))
                    need = task.get("need", [0, 0])
                    known[key] = [int(max(need[0], 1) * 2.5), int(max(need[1], 1) * 2.5)]
                    self.give_fleet(fid, [{"kind": "repair"}], {"task": "repair", "since": w.turn})
                    continue
            if is_main and at_war and not self.home_in_danger(force) and \
                    self.try_strike(fid, task, orders, force, targets, used_targets, STRIKE_MARGIN):
                continue
            # Defend a colony system under attack, nearest first.
            job = None
            dist = w.bfs([f["location"]["system"]])
            for s, threat in defend:
                if s in defended:
                    continue
                if force.beats(threat, DEFEND_MARGIN):
                    d = dist.get(s)
                    if d is not None and d <= (3 if is_main and at_war else 5) and (job is None or d < job[0]):
                        job = (d, s, threat)
            if job is not None:
                s = job[1]
                defended.add(s)
                if task.get("task") != "defend" or task.get("system") != s or not orders:
                    target = self.biggest_enemy_in(s)
                    if target is not None:
                        o = [{"kind": "attack", "vehicle": target["id"]}]
                    else:
                        o = [{"kind": "move_to", "location": self.colony_sector(s)}, {"kind": "sentry"}]
                    self.give_fleet(fid, o, {"task": "defend", "system": s, "since": w.turn})
                continue
            # Strike when the odds are overwhelming, whatever the phase.
            if self.try_strike(fid, task, orders, force, targets, used_targets, STRIKE_MARGIN * 2.5):
                continue
            # Otherwise: in war, join the main fleet; else hold at the rally.
            dest = rally_loc
            if at_war and not is_main and main_loc is not None:
                dest = main_loc
            if dest is not None:
                here = f["location"]
                if not self.same_place(here, dest):
                    if task.get("task") != "hold" or not orders or task.get("to") != [dest["system"], dest["x"], dest["y"]]:
                        self.give_fleet(fid, [{"kind": "move_to", "location": dest}], {"task": "hold", "since": w.turn,
                                                                                      "to": [dest["system"], dest["x"], dest["y"]]})
                elif not orders:
                    self.give_fleet(fid, [{"kind": "sentry"}], {"task": "hold", "since": w.turn})
        return self.commands

    def home_in_danger(self, force):
        """Whether hostile ships in our colony systems are strong enough that the main fleet must stay."""
        total = Force()
        for s, f in self.defense_needs():
            total.add(f.attack, f.hp, 0)
        return total.power() > 0.25 * force.power()

    def try_strike(self, fid, task, orders, force, targets, used_targets, margin):
        """Sends a fleet against the best enemy colony system it beats by `margin`."""
        if force.count < 2:
            return False
        pick = None
        # Keep at the target we went for while it stands and we still beat it.
        if task.get("task") == "strike":
            for score, cols, s, need in targets:
                if s == task.get("system") and s not in used_targets and force.beats(need, margin * 0.6):
                    pick = (s, cols, need)
                    break
        for score, cols, s, need in targets:
            if pick is not None:
                break
            if s in used_targets:
                continue
            if force.beats(need, margin):
                pick = (s, cols, need)
                break
        if pick is None:
            return False
        s, cols, need = pick
        used_targets.add(s)
        w = self.w
        if task.get("task") != "strike" or task.get("system") != s or not self.w.fleets[fid]["orders"]:
            troops = getattr(self, "troops_ready", 0)
            tfig = getattr(self, "troop_fig", None)
            takeable = [c for c in cols if tfig is not None and (troops_needed(c, tfig) or 10 ** 9) <= troops]
            col = max(takeable or cols, key=lambda c: c["total_population"] or 0)
            power = task.get("power") if task.get("task") == "strike" and task.get("system") == s else None
            self.give_fleet(fid, [{"kind": "attack", "object": col["planet"]}],
                            {"task": "strike", "system": s, "planet": col["planet"], "since": w.turn,
                             "power": int(power if power else force.power()), "need": [int(need.attack), int(need.hp)]})
            self.notes.append((col["planet"], "Hegemon strike target"))
        return True

    def refit(self, fid, f, members, task, orders):
        """Sends a fleet to repair or resupply when it needs it; True when it is busy so."""
        w = self.w
        low_supply = False
        damaged = 0
        for v in members:
            cap = v["supply_capacity"] or 0
            if self.logistics is not None:
                if self.logistics.must_refuel(v, margin=1.5, extra=15):
                    low_supply = True
            elif cap > 0 and not v["unlimited_supply"] and (v["supply"] or 0) < cap * 0.3:
                low_supply = True
            full = v["structure"] or 1
            if (v["damage"] or 0) > full * 0.35:
                damaged += 1
        if damaged * 2 > len(members):
            if task.get("task") != "repair":
                self.give_fleet(fid, [{"kind": "repair"}], {"task": "repair", "since": w.turn})
                return True
            if orders:
                return True
            return False
        if task.get("task") == "repair" and damaged > 0 and orders:
            return True
        if low_supply:
            if task.get("task") != "resupply":
                self.give_fleet(fid, [{"kind": "resupply"}], {"task": "resupply", "since": w.turn})
                return True
            if orders:
                return True
            return False
        if task.get("task") == "resupply" and orders:
            return True
        return False

    def give_fleet(self, fid, orders, task):
        self.commands.append({"kind": "set_orders", "fleet": fid, "orders": orders})
        self.fleet_mem[str(fid)] = task

    def biggest_enemy_in(self, system):
        best = None
        for v, a, h in self.intel.enemy_ships:
            if v["location"]["system"] != system or not self.w.hostile(v["owner"]):
                continue
            if best is None or a * h > best[0]:
                best = (a * h, v)
        return best[1] if best is not None else None

    def defense_needs(self):
        """Our colony systems with hostile armed ships in them now: [(system, Force)], worst first."""
        out = []
        mine = set(self.w.colony_systems)
        for s, f in self.threats.items():
            if s in mine and f.attack > 0:
                out.append((s, f))
        out.sort(key=lambda x: -x[1].power())
        return out

    # ---- what to build ----

    def exposure(self):
        """How exposed each of our colony systems is: 0 (deep inside) to 3 (enemies here)."""
        w = self.w
        enemy = self.enemy_colony_systems()
        dist_enemy = w.bfs(list(enemy.keys())) if enemy else {}
        threat_sys = [s for s, f in self.threats.items() if f.attack > 0]
        dist_threat = w.bfs(threat_sys) if threat_sys else {}
        out = {}
        home = w.home_system()
        for s in w.colony_systems:
            e = 0
            dt = dist_threat.get(s)
            de = dist_enemy.get(s)
            if dt is not None:
                e = max(e, 3 - min(dt, 3))
            if de is not None:
                e = max(e, 2 - min(de, 2) + (1 if de <= 1 else 0))
            if s == home:
                e = max(e, 1)
            out[s] = e
        return out

    def wants(self, econ, book, queued_role, queued_at):
        """What military construction to ask for: warships (paying upkeep) within an
        upkeep budget, and weapon platforms (no upkeep) at exposed colonies."""
        w = self.w
        out = []
        rep = econ.report
        res3 = ("minerals", "organics", "radioactives")
        income = sum(rep["colonies"][r] for r in res3) + 1
        warships = self.roles.get("warship", [])
        mine = Force()
        upkeep = 0.0
        upkeep_r = {r: 0.0 for r in res3}
        pct = econ_maint_pct(w) / 100.0
        for v in warships:
            a, h = self.vehicle_force(v)
            mine.add(a, h)
            fig = w.figures(v["design"])
            if fig is not None:
                upkeep += sum(fig["cost"].values()) * pct
                for r in res3:
                    upkeep_r[r] += fig["cost"][r] * pct
        threat = Force()
        for s, f in self.threats.items():
            threat.add(f.attack, f.hp, 0)
        share = self.strategy.upkeep_share() if self.strategy is not None else 0.2
        if threat.power() > 0.5 * mine.power():
            share += 0.12
        war = book.design_id("warship")
        if war is not None:
            fig = w.figures(war)
            each = sum(fig["cost"].values()) * pct if fig is not None else 1.0
            # Maintenance is paid resource by resource: the fleet's share of each income,
            # and never more than MAINT_MAX of it for all maintenance together.
            share = min(SHARE_MAX, share)
            room = None
            hard = None
            if fig is not None:
                for r in res3:
                    u = fig["cost"][r] * pct
                    if u <= 0:
                        continue
                    inc = rep["colonies"][r]
                    h = (MAINT_MAX * inc - rep["maintenance"][r]) / u
                    k = min((share * inc - upkeep_r[r]) / u, h)
                    room = k if room is None else min(room, k)
                    hard = h if hard is None else min(hard, h)
            room = int(room if room is not None else (share * income - upkeep) / max(1.0, each)) - queued_role("warship")
            # Stores that overflow pay for more, whatever the share.
            if fig is not None:
                extra = None
                for r in ("minerals", "organics", "radioactives"):
                    u = fig["cost"][r]
                    if u <= 0:
                        continue
                    spare = max(0, econ.stored[r] - 0.6 * econ.cap[r])
                    k = int(spare / u)
                    extra = k if extra is None else min(extra, k)
                if extra and config.on("overflow_ships"):
                    room = max(room, min(extra, int(hard) if hard is not None else extra) - queued_role("warship"))
            n = max(0, min(3, room))
            self.debug = (round(share, 2), income, int(upkeep), int(each), room, n)
            if n > 0:
                protect = self.strategy is not None and self.strategy.phase in ("arm", "war") and config.on("protect")
                out.append({"design": war, "count": n, "priority": 45.0 if threat.power() > mine.power() else 30.0, "near": self.rally,
                            "protect": protect})
        # Satellites and mines around exposed colonies: no upkeep, launched from the planet.
        out.extend(self.fortify(book, queued_at))
        # Weapon platforms where colonies are exposed.
        plat = book.design_id("platform")
        pfig = w.figures(plat) if plat is not None else None
        if pfig is not None:
            size = pfig["tonnage_max"] or pfig["tonnage_used"] or 1
            exposure = self.exposure()
            for c in w.my_colonies:
                s = w.system_of(c["planet"])
                e = exposure.get(s, 0)
                if c["total_population"] <= 0:
                    continue
                # Every colony gets a gun once it has started building, more where enemies are near.
                if e <= 0 and (len(c["facilities"] or []) < 2 or not config.on("platform_all")):
                    continue
                cap = c["cargo_capacity"] or 0
                used = c["cargo_used"] or 0
                free = cap - used - queued_at.get(c["planet"], 0) * size
                room = free // size
                if room <= 0:
                    continue
                have = queued_at.get(c["planet"], 0)
                for st in (c["cargo"]["units"] if c["cargo"] is not None else []):
                    d = w.designs.get(st["design"])
                    if d is not None and d["figures"] is not None and d["figures"]["vehicle_type"] == "weapon_platform":
                        have += st["count"]
                limit = ({0: 2, 1: 3, 2: 5, 3: 99} if config.on("platform_two") else {0: 1, 1: 2, 2: 4, 3: 99}).get(e, 0)
                n = min(room, max(0, limit - have), 3)
                if n > 0:
                    out.append({"design": plat, "count": n, "priority": 50.0 + 5 * e, "at": c["planet"], "units": True})
        return out


    def units_in_sector(self, kind, system, x, y):
        n = 0
        for v in self.w.my_vehicles:
            loc = v["location"]
            if v["type"] == kind and loc["system"] == system and loc["x"] == x and loc["y"] == y:
                n += v["count"] or 0
        return n

    def fortify(self, book, queued_at):
        """Satellite and mine batches for exposed colonies, up to what their cargo holds."""
        w = self.w
        out = []
        exposure = self.exposure()
        late = w.turn >= 30
        sat_want = {3: 30, 2: 10 if late else 0}
        mine_want = {3: 50, 2: 20 if late else 0}
        done_sectors = set()
        for role, want in (("satellite", sat_want), ("mine", mine_want)):
            did = book.design_id(role)
            fig = w.figures(did) if did is not None else None
            if fig is None:
                continue
            size = fig["tonnage_max"] or fig["tonnage_used"] or 1
            for c in w.my_colonies:
                if c["total_population"] <= 0:
                    continue
                o = w.objects.get(c["planet"])
                if o is None:
                    continue
                s = o["system"]
                e = exposure.get(s, 0)
                target = want.get(e, 0)
                if target <= 0:
                    continue
                key = (role, s, o["sector"]["x"], o["sector"]["y"])
                if key in done_sectors:
                    continue
                done_sectors.add(key)
                have = self.units_in_sector(role, s, o["sector"]["x"], o["sector"]["y"])
                for st in (c["cargo"]["units"] if c["cargo"] is not None else []):
                    if st["design"] == did:
                        have += st["count"]
                have += queued_at.get((c["planet"], did), 0)
                need = min(100, target) - have
                if need <= 0:
                    continue
                free = (c["cargo_capacity"] or 0) - (c["cargo_used"] or 0)
                n = min(need, free // size, 10)
                if n <= 0:
                    continue
                out.append({"design": did, "count": n, "priority": 48.0 + 4 * e, "at": c["planet"], "units": True})
        return out

    def launches(self, book):
        """Colonies with satellites or mines in their cargo launch them into their sector."""
        cmds = []
        roles = {}
        for role in ("satellite", "mine"):
            did = book.design_id(role)
            if did is not None:
                roles[did] = role
        for c in self.w.my_colonies:
            if c["cargo"] is None:
                continue
            orders = []
            for st in c["cargo"]["units"]:
                d = self.w.designs.get(st["design"])
                if d is None or st["count"] <= 0:
                    continue
                fig = d["figures"]
                if fig is not None and fig["vehicle_type"] in ("satellite", "mine"):
                    orders.append({"kind": "launch_units", "design": st["design"], "amount": st["count"]})
            if orders and not c["orders"]:
                cmds.append({"kind": "set_orders", "planet": c["planet"], "orders": orders})
        return cmds


def econ_maint_pct(w):
    return w.my["maintenance_percent"] or 25
