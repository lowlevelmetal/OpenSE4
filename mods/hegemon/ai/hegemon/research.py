"""Hegemon's research planner: each tech area is worth what its next levels unlock for
the strategy (better facilities for the families we use, new planet types to settle,
better weapons, armour, shields, engines and hulls, and the areas they open), divided
by what those levels cost. The best areas are funded in order, never evenly, and areas
with progress are never dropped."""

from .util import RES

QUEUE_LEN = 6


class Research:
    def __init__(self, world, kn, econ, weights, tune=None):
        """`weights`: the strategy's weights for economy, expansion and military research."""
        self.w = world
        self.kn = kn
        self.econ = econ
        self.weights = weights
        self.tune = tune or {}
        self.levels = world.research_levels()
        self.state = world.my["research"]
        self.researchable = set(self.state["researchable"])

    # ---- what we have now ----

    def facility_counts(self):
        """{family: [count, best numeral now on colonies]} over our colonies."""
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
        m = {"weapon": 0.0, "armor": 0.0, "shield": 0.0, "engine": 0.0, "hull": 0, "colonize": set(), "supply": 0.0,
             "pd": 0.0, "ecm": 0, "sensor": 0}
        for c in self.kn.components:
            if not self.kn.meets(c.reqs):
                continue
            self._metrics(c, m)
        for h in self.kn.hulls:
            if h.type == "ship" and self.kn.meets(h.reqs) and h.tonnage > m["hull"]:
                m["hull"] = h.tonnage
        return m

    @staticmethod
    def weapon_score(c):
        w = c.weapon
        if w is None or c.tonnage <= 0:
            return 0.0
        dmg = w.damage
        n = 0
        total = 0
        for r in range(1, min(len(dmg), 9)):
            total += dmg[r]
            n += 1
        if n == 0:
            return 0.0
        return total / n / w.reload / c.tonnage

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
        if c.shield + c.phased > 0:
            s = (c.shield + c.phased) / ton
            if s > m["shield"]:
                m["shield"] = s
        if c.engine > 0 and "ship" in c.types:
            s = c.engine / ton
            if s > m["engine"]:
                m["engine"] = s
        for s in c.colonize:
            m["colonize"].add(s)
        if c.defense > m["ecm"]:
            m["ecm"] = c.defense

    # ---- what an area's next levels bring ----

    def area_values(self):
        kn = self.kn
        lv = self.levels
        econ = self.econ
        wt = self.weights
        counts = self.facility_counts()
        now = self.best_component_metrics()
        values = {}
        unlocks = {}

        def credit(area, gained, depth):
            k = 1.0 if depth == 1 else 0.45
            values[area] = values.get(area, 0.0) + gained * k

        def gate(reqs):
            """(area, depth) when exactly one area stands between us and the item, at most two levels short."""
            miss = None
            for q in reqs:
                a = q["area"]
                if a is None:
                    continue
                have = lv[a] if a < len(lv) else 0
                if have < q["level"]:
                    if miss is not None and miss[0] != a:
                        return None
                    depth = q["level"] - have
                    if miss is None or depth > miss[1]:
                        miss = (a, depth)
            if miss is None or miss[1] > 2:
                return None
            return miss

        p = econ.prices
        # Facilities: better levels of families we use, and new kinds.
        for f in kn.facilities:
            g = gate(f.reqs)
            if g is None:
                continue
            cur = counts.get(f.family)
            gain = 0.0
            per_gen = f.gen[0] * p[0] + f.gen[1] * p[1] + f.gen[2] * p[2] + f.research * p[3] * 0.9 + f.intel * p[4]
            if cur is not None:
                old = cur[1]
                old_gen = old.gen[0] * p[0] + old.gen[1] * p[1] + old.gen[2] * p[2] + old.research * p[3] * 0.9 + old.intel * p[4]
                if per_gen > old_gen:
                    gain = (per_gen - old_gen) * cur[0] * 0.8
            elif per_gen > 0:
                gain = per_gen * 0.5
            if f.storage[0] + f.storage[1] + f.storage[2] > 0:
                gain += 30.0
            if f.yard[0] + f.yard[1] + f.yard[2] > 0:
                gain += 150.0
            if gain > 0:
                credit(g[0], gain * wt["economy"], g[1])
        # Components: what improves our ships, and new planet types to settle.
        targets_by_surface = self.tune.get("targets_by_surface", {})
        for c in kn.components:
            g = gate(c.reqs)
            if g is None:
                continue
            gain = 0.0
            mil = 0.0
            for s in c.colonize:
                if s not in now["colonize"]:
                    # What the best planets of that surface we know would bring, over a while.
                    gain += 300.0 + targets_by_surface.get(s, 1500.0) * 0.15
            if c.weapon is not None and not c.pd and c.weapon.kind in ("direct_fire", "seeking"):
                s = self.weapon_score(c)
                if now["weapon"] > 0 and s > now["weapon"]:
                    mil += 600.0 * (s / now["weapon"] - 1.0)
                elif now["weapon"] <= 0 and s > 0:
                    mil += 600.0
            if c.armor and "ship" in c.types and c.tonnage > 0:
                s = c.structure / c.tonnage
                if s > now["armor"] > 0:
                    mil += 400.0 * (s / now["armor"] - 1.0)
                elif now["armor"] <= 0:
                    mil += 300.0
            if c.shield + c.phased > 0 and c.tonnage > 0:
                s = (c.shield + c.phased) / c.tonnage
                if s > now["shield"]:
                    mil += 350.0 * (1.0 if now["shield"] <= 0 else min(2.0, s / now["shield"] - 1.0))
            if c.engine > 0 and "ship" in c.types and c.tonnage > 0:
                s = c.engine / c.tonnage
                if s > now["engine"] > 0:
                    gain += 200.0 * (s / now["engine"] - 1.0)
                    mil += 200.0 * (s / now["engine"] - 1.0)
            if c.defense > now["ecm"]:
                mil += 3.0 * (c.defense - now["ecm"])
            if gain > 0:
                credit(g[0], gain * wt["expansion"], g[1])
            if mil > 0:
                credit(g[0], mil * wt["military"], g[1])
        for h in kn.hulls:
            if h.type != "ship":
                continue
            g = gate(h.reqs)
            if g is None or h.tonnage <= now["hull"]:
                continue
            credit(g[0], 500.0 * min(2.0, h.tonnage / max(1, now["hull"]) - 1.0) * wt["military"], g[1])
        # Areas that open other areas.
        for t in kn.techs:
            if t.id in self.researchable or lv[t.id] > 0:
                continue
            g = gate(t.reqs)
            if g is not None:
                credit(g[0], 150.0 * (wt["economy"] + wt["military"]) * 0.5, g[1])
        return values

    def plan(self):
        """The research queue we want, or None when the current one stays."""
        kn = self.kn
        lv = self.levels
        values = self.area_values()
        queue = self.state["queue"]
        progress = {}
        for e in queue:
            if e["area"] is not None:
                progress[e["area"]] = e["progress"]
        score_bonus = self.tune.get("level_score", 300.0)
        scored = []
        for a in self.researchable:
            if a >= len(kn.techs):
                continue
            t = kn.techs[a]
            if lv[a] >= t.max:
                continue
            cost = kn.level_cost(a, lv[a] + 1)
            left = max(1, cost - progress.get(a, 0))
            # Every level is also worth score and a little general strength.
            v = values.get(a, 0.0) + score_bonus
            scored.append((v * 1000.0 / left, a))
        scored.sort(key=lambda x: -x[0])
        want = []
        pool = self.state["points"] + self.state["income"]
        covered = 0
        for s, a in scored:
            if len(want) >= QUEUE_LEN and covered > pool:
                break
            if len(want) >= 12:
                break
            want.append(a)
            covered += kn.level_cost(a, lv[a] + 1) - progress.get(a, 0)
        # Never drop an area with progress.
        for a, pr in progress.items():
            if pr > 0 and a not in want and a in self.researchable and lv[a] < kn.techs[a].max:
                if len(want) < 12:
                    want.append(a)
        current = [e["area"] for e in queue]
        if current == want and not self.state["evenly"]:
            return None
        return {"kind": "set_research", "queue": [{"area": a} for a in want], "evenly": False, "repeat": False}
