"""Hegemon's research planner.

Each tech area is worth what its next levels unlock for the strategy:
- civil: better facilities for the families we use and new kinds (their extra output,
  priced), new planet surfaces to settle (the planets we know of that surface), score;
- military: better weapons, armour, shields, engines, to-hit parts and bigger hulls,
  measured against the best we have now.
An area whose requirements we lack passes part of its value to the areas it needs.
What pays off sooner is worth more: values are discounted by the turns the levels
take at our research income.

Research points are shared between the civil and the military side by a share the
strategy sets (more military in war); the side furthest behind its share gets the
head of the queue, which is funded in order (never evenly), and an area with progress
is never dropped."""

from . import config

QUEUE_LEN = 6


class Research:
    def __init__(self, world, kn, econ, weights, tune=None):
        """`weights`: {"economy", "expansion", "military"} multipliers, and
        "military_share": the share of points the military side should get."""
        self.w = world
        self.kn = kn
        self.econ = econ
        self.weights = weights
        self.tune = tune or {}
        self.levels = world.research_levels()
        self.state = world.my["research"]
        self.researchable = set(self.state["researchable"])
        self.top = None
        self.has_troops = False
        for h in kn.hulls:
            if h.type == "troop" and kn.meets(h.reqs):
                self.has_troops = True
        # A troop needs a weapon that troops can mount, as well as its hull.
        self.has_troop_weapon = False
        for c in kn.components:
            if c.weapon is not None and "troop" in c.types and kn.meets(c.reqs):
                self.has_troop_weapon = True

    # ---- what we have now ----

    def facility_counts(self):
        """{family: [count, a facility of it now on our colonies]}."""
        kn = self.kn
        out = {}
        for c in self.w.my_colonies:
            for fid in c["facilities"] or []:
                if 0 <= fid < len(kn.facilities):
                    f = kn.facilities[fid]
                    e = out.get(f.family)
                    if e is None:
                        out[f.family] = [1, f]
                    else:
                        e[0] += 1
        return out

    def best_component_metrics(self):
        """The best of each role among the components we may use now."""
        m = {"weapon": 0.0, "armor": 0.0, "shield": 0.0, "engine": 0.0, "hull": 0, "colonize": set(), "pd": 0.0, "ecm": 0,
             "platform": 0}
        for c in self.kn.components:
            if not self.kn.meets(c.reqs):
                continue
            self._metrics(c, m)
        for h in self.kn.hulls:
            if not self.kn.meets(h.reqs):
                continue
            if h.type == "ship" and h.pct_colony <= 0 and h.pct_cargo <= 0 and h.pct_bays <= 0 and h.tonnage > m["hull"]:
                m["hull"] = h.tonnage
            if h.type == "weapon_platform" and h.tonnage > m["platform"]:
                m["platform"] = h.tonnage
        return m

    @staticmethod
    def weapon_score(c):
        w = c.weapon
        if w is None or c.tonnage <= 0:
            return 0.0
        dmg = w.damage
        total = 0.0
        for r in range(1, 9):
            d = dmg[r] if r < len(dmg) else 0
            total += d * max(0.05, (100 + w.modifier - 10 * r) / 100.0)
        return total / 8.0 / w.reload / c.tonnage

    def _metrics(self, c, m):
        ton = c.tonnage if c.tonnage > 0 else 1
        if c.weapon is not None:
            s = self.weapon_score(c)
            if c.pd:
                if s > m["pd"]:
                    m["pd"] = s
            elif c.weapon.kind in ("direct_fire", "seeking") and s > m["weapon"]:
                m["weapon"] = s
        if c.armor and "ship" in c.types:
            s = c.structure / ton
            if s > m["armor"]:
                m["armor"] = s
        if c.shield + c.phased > 0 and c.weapon is None:
            s = (c.shield + c.phased) / ton
            if s > m["shield"]:
                m["shield"] = s
        if c.engine > 0 and "ship" in c.types:
            s = c.engine / ton
            if s > m["engine"]:
                m["engine"] = s
        for s in c.colonize:
            m["colonize"].add(s)
        if c.defense > m["ecm"] and c.weapon is None:
            m["ecm"] = c.defense

    # ---- what an area's next levels bring ----

    def area_values(self):
        """{area: [civil value, military value]} a turn, before discounting."""
        kn = self.kn
        lv = self.levels
        econ = self.econ
        wt = self.weights
        counts = self.facility_counts()
        now = self.best_component_metrics()
        values = {}

        def credit(misses, civ, mil):
            # Shared among the areas still missing; a level further away counts less.
            n = len(misses)
            for a, depth in misses:
                k = (1.0 if depth == 1 else (0.5 if depth == 2 else 0.25)) / n
                e = values.get(a)
                if e is None:
                    e = [0.0, 0.0]
                    values[a] = e
                e[0] += civ * k
                e[1] += mil * k

        def gate(reqs):
            """The areas (and how many levels short) between us and an item, or None
            when we have it or it is too far."""
            out = []
            total = 0
            for q in reqs:
                a = q["area"]
                if a is None:
                    continue
                have = lv[a] if a < len(lv) else 0
                if have < q["level"]:
                    d = q["level"] - have
                    out.append((a, d))
                    total += d
            if not out or total > 3:
                return None
            return out

        p = econ.prices
        # Facilities: better levels of families we use, and new kinds.
        for f in kn.facilities:
            g = gate(f.reqs)
            if g is None:
                continue
            cur = counts.get(f.family)
            gain = 0.0
            per = f.gen[0] * p[0] + f.gen[1] * p[1] + f.gen[2] * p[2] + f.research * p[3] + f.intel * p[4]
            if cur is not None:
                old = cur[1]
                old_per = old.gen[0] * p[0] + old.gen[1] * p[1] + old.gen[2] * p[2] + old.research * p[3] + old.intel * p[4]
                if per > old_per:
                    gain = (per - old_per) * cur[0] * 0.8
            elif per > 0:
                gain = per * 0.4
            if f.yard[0] + f.yard[1] + f.yard[2] > 0:
                gain += 100.0
            if gain > 0:
                credit(g, gain * wt["economy"], 0.0)
        # Components: what improves our ships, and new planet types to settle.
        targets_by_surface = self.tune.get("targets_by_surface", {})
        troop_weapon = None     # the nearest troop weapon, while we have none
        for c in kn.components:
            g = gate(c.reqs)
            if g is None:
                continue
            if self.has_troops and not self.has_troop_weapon and c.weapon is not None and "troop" in c.types and \
                    config.on("troop_research") and (troop_weapon is None or len(g) < len(troop_weapon)):
                troop_weapon = g
            civ = 0.0
            mil = 0.0
            for s in c.colonize:
                if s not in now["colonize"]:
                    # The planets of that surface we know, once settled and built up:
                    # their output a turn, a share of it within the planning horizon.
                    civ += 300.0 + targets_by_surface.get(s, 1500.0) * (0.3 if config.on("colonize_value") else 0.15)
            if c.weapon is not None and not c.pd and c.weapon.kind in ("direct_fire", "seeking"):
                s = self.weapon_score(c)
                if now["weapon"] <= 0:
                    mil += 500.0
                elif s > now["weapon"]:
                    mil += 500.0 * min(2.0, s / now["weapon"] - 1.0) + 60.0
            if c.armor and "ship" in c.types and c.tonnage > 0:
                s = c.structure / c.tonnage
                if now["armor"] <= 0:
                    mil += 500.0
                elif s > now["armor"]:
                    mil += 400.0 * min(2.0, s / now["armor"] - 1.0) + 40.0
            if c.shield + c.phased > 0 and c.tonnage > 0 and c.weapon is None:
                s = (c.shield + c.phased) / c.tonnage
                if now["shield"] <= 0:
                    mil += 400.0
                elif s > now["shield"]:
                    mil += 300.0 * min(2.0, s / now["shield"] - 1.0) + 30.0
            if c.engine > 0 and "ship" in c.types and c.tonnage > 0:
                s = c.engine / c.tonnage
                if now["engine"] > 0 and s > now["engine"]:
                    r = min(2.0, s / now["engine"] - 1.0)
                    civ += 100.0 * r
                    mil += 200.0 * r
            if c.defense > now["ecm"] and c.weapon is None:
                mil += 4.0 * (c.defense - now["ecm"])
            if c.pd and now["pd"] <= 0:
                mil += 80.0
            if civ > 0 or mil > 0:
                credit(g, civ * wt["expansion"], mil * wt["military"])
        if troop_weapon is not None:
            credit(troop_weapon, 0.0, wt.get("troops", 0.0))
        for h in kn.hulls:
            g = gate(h.reqs)
            if g is None:
                continue
            if h.type == "ship" and h.pct_colony <= 0 and h.pct_cargo <= 0 and h.pct_bays <= 0 and h.tonnage > now["hull"]:
                r = min(2.0, h.tonnage / float(max(1, now["hull"])) - 1.0)
                mil = 600.0 * r + (300.0 if h.tonnage >= 400 > now["hull"] else 0.0)
                credit(g, 80.0 * r * wt["expansion"], mil * wt["military"])
            elif h.type == "weapon_platform" and h.tonnage > now["platform"]:
                credit(g, 0.0, 250.0 * wt["military"])
            elif h.type == "troop" and not self.has_troops and config.on("troop_research"):
                # Troops take colonies with their facilities: worth much in war.
                credit(g, 0.0, wt.get("troops", 0.0))
        # An area we cannot research yet passes some of its worth to those it needs.
        for t in kn.techs:
            a = t.id
            if a in self.researchable or lv[a] >= t.max:
                continue
            e = values.get(a)
            if e is None:
                continue
            g = gate(t.reqs)
            if g is None:
                continue
            credit(g, e[0] * 0.6, e[1] * 0.6)
        return values

    @staticmethod
    def category(v):
        return "mil" if v[1] > v[0] else "civ"

    def plan(self, mem):
        """The research queue we want, or None when the current one stays."""
        kn = self.kn
        lv = self.levels
        values = self.area_values()
        queue = self.state["queue"]
        progress = {}
        for e in queue:
            if e["area"] is not None:
                progress[e["area"]] = e["progress"]
        # Book what was spent since the last call on the side of the queue's head.
        spent = mem.get("rp_spent")
        if not isinstance(spent, dict):
            spent = {"civ": 0, "mil": 0}
            mem["rp_spent"] = spent
        head = mem.get("rp_head")
        if head in ("civ", "mil"):
            spent[head] = spent[head] + self.state["points"]
        score_bonus = self.tune.get("level_score", 250.0)
        income = max(1, self.state["income"])
        horizon = self.tune.get("horizon", 20.0)
        scored = {"civ": [], "mil": []}
        for a in self.researchable:
            if a >= len(kn.techs):
                continue
            t = kn.techs[a]
            if lv[a] >= t.max:
                continue
            cost = kn.level_cost(a, lv[a] + 1)
            left = max(1, cost - progress.get(a, 0))
            v = values.get(a, [0.0, 0.0])
            total = v[0] + v[1] + score_bonus
            turns = left / float(income)
            score = total * 1000.0 / left / (1.0 + turns / horizon)
            scored[self.category(v)].append((score, a))
        for k in scored:
            scored[k].sort(key=lambda x: -x[0])
        share = self.weights.get("military_share", 0.35)
        total_spent = spent["civ"] + spent["mil"] + 1
        mil_behind = spent["mil"] < share * total_spent
        first, second = ("mil", "civ") if mil_behind else ("civ", "mil")
        if not scored[first]:
            first, second = second, first
        # Interleave: the side behind first, then alternate, best areas of each side.
        want = []
        lists = [list(scored[first]), list(scored[second])]
        i = 0
        pool = self.state["points"] + self.state["income"]
        covered = 0
        while len(want) < 12 and (lists[0] or lists[1]):
            lst = lists[i % 2] if lists[i % 2] else lists[(i + 1) % 2]
            s, a = lst.pop(0)
            want.append(a)
            covered += kn.level_cost(a, lv[a] + 1) - progress.get(a, 0)
            i += 1
            if len(want) >= QUEUE_LEN and covered > pool * 2:
                break
        for a, pr in progress.items():
            if pr > 0 and a not in want and a in self.researchable and lv[a] < kn.techs[a].max and len(want) < 12:
                want.append(a)
        mem["rp_head"] = self.category(values.get(want[0], [0.0, 0.0])) if want else None
        self.top = [(kn.techs[a].name, self.category(values.get(a, [0.0, 0.0])), [int(x) for x in values.get(a, [0.0, 0.0])]) for a in want[:5]]
        current = [e["area"] for e in queue]
        if current == want and not self.state["evenly"]:
            return None
        return {"kind": "set_research", "queue": [{"area": a} for a in want], "evenly": False, "repeat": False}
