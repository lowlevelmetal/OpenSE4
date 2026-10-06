"""Hegemon's strategic layer: the phase of the game it is in, the rival it means to
beat, and the weights every planner takes from them.

Phases:
- expand: settle every good planet while nobody threatens us; little military, cheap
  zero-upkeep platforms where colonies are exposed;
- arm: expansion is slowing or a rival is dangerous; research turns to weapons, hulls
  and armour and the fleet grows to an upkeep share of income;
- war: the fleet is strong enough to take a rival's colonies; it strikes, and the rest of
  the economy feeds it.

Opponent model: for each rival, what we have seen of it (colonies, their population,
its armed ships), how far it is, how it treats us (treaty, battles it fought against us),
kept in memory and refreshed whenever we see more. The target is the rival whose
reachable colonies are worth most for the force it takes, nearer ones first, and the
one already fighting us before one at peace."""

from .intel import Force


class Strategy:
    def __init__(self, world, intel, mem, tune=None):
        self.w = world
        self.intel = intel
        self.mem = mem
        self.tune = tune or {}
        st = mem.get("strategy")
        if not isinstance(st, dict):
            st = {"phase": "expand", "since": 0, "target": None}
            mem["strategy"] = st
        self.st = st
        rivals = mem.get("rivals")
        if not isinstance(rivals, dict):
            rivals = {}
            mem["rivals"] = rivals
        self.rivals = rivals

    @property
    def phase(self):
        return self.st["phase"]

    @property
    def target(self):
        return self.st.get("target")

    # ---- the opponent model ----

    def update_rivals(self):
        w = self.w
        turn = w.turn
        my_sys = w.colony_systems
        dist = w.bfs(my_sys) if my_sys else {}
        cols = {}
        for c in w.foreign_colonies:
            cols.setdefault(c["owner"], []).append(c)
        force = {}
        for k, e in self.intel.seen.items():
            owner, system, a, h, t = e
            if turn - t > 10:
                continue
            f = force.get(owner)
            if f is None:
                f = [0, 0]
                force[owner] = f
            f[0] += a
            f[1] += h
        fought = {}
        for b in w.d["battles"]:
            if w.me not in b["participants"]:
                continue
            for p in b["participants"]:
                if p != w.me:
                    fought[p] = fought.get(p, 0) + 1
        for e in w.living_rivals():
            eid = e["id"]
            key = str(eid)
            r = self.rivals.get(key)
            if r is None:
                r = {"colonies": 0, "pop": 0, "force": [0, 0], "dist": 99, "battles": 0, "seen": -1}
                self.rivals[key] = r
            cs = cols.get(eid, [])
            if cs:
                r["colonies"] = len(cs)
                r["pop"] = sum(c["total_population"] or 0 for c in cs)
                d = 99
                for c in cs:
                    s = w.system_of(c["planet"])
                    if s is not None and s in dist and dist[s] < d:
                        d = dist[s]
                r["dist"] = d
                r["seen"] = turn
            f = force.get(eid)
            if f is not None:
                r["force"] = [max(f[0], int(r["force"][0] * 0.9)), max(f[1], int(r["force"][1] * 0.9))]
            r["battles"] = r["battles"] + fought.get(eid, 0)
        for key in list(self.rivals.keys()):
            e = w.empires.get(int(key))
            if e is None or not e["alive"]:
                del self.rivals[key]

    def pick_target(self):
        """The rival to fight: worth for the force it takes, near first."""
        w = self.w
        best = None
        for key, r in self.rivals.items():
            eid = int(key)
            e = w.empires.get(eid)
            if e is None or e["neutral"] or not e["alive"]:
                continue
            if r["colonies"] <= 0 or r["dist"] > 6:
                continue
            treaty = w.treaty(eid)
            worth = r["colonies"] * 1000.0 + r["pop"] * 1.5
            cost = 1.0 + (r["force"][0] * r["force"][1]) / 2.0e5
            score = worth / cost / (1.0 + 0.5 * r["dist"])
            if treaty == "war":
                score *= 1.5
            elif not w.hostile(eid):
                score *= 0.3
            if r["battles"] > 0:
                score *= 1.2
            if best is None or score > best[0]:
                best = (score, eid)
        return best[1] if best is not None else None

    # ---- the phase ----

    def update(self, my_force, good_targets):
        """Moves the phase on: `my_force` our warships' Force, `good_targets` how many
        good planets remain to settle."""
        self.update_rivals()
        w = self.w
        st = self.st
        turn = w.turn
        threat = Force()
        for s, f in self.intel.threat_by_system().items():
            threat.add(f.attack, f.hp, 0)
        target = self.pick_target()
        st["target"] = target
        phase = st["phase"]
        tf = Force()
        if target is not None:
            r = self.rivals[str(target)]
            tf.add(r["force"][0], r["force"][1], 0)
        if phase == "expand":
            if turn >= self.tune.get("arm_turn", 45) or (turn >= 25 and good_targets < 2) or threat.power() > 1.5 * my_force.power() and turn > 15:
                phase = "arm"
        elif phase == "arm":
            if target is not None and my_force.count >= self.tune.get("war_ships", 8) and my_force.beats(tf, 1.5):
                phase = "war"
        elif phase == "war":
            if target is None:
                phase = "arm"
            elif my_force.count < 3 or not my_force.beats(tf, 0.7):
                phase = "arm"
        if phase != st["phase"]:
            st["phase"] = phase
            st["since"] = turn
        self.threat = threat
        return phase

    # ---- what the phase asks of the planners ----

    def upkeep_share(self):
        p = self.phase
        base = {"expand": 0.10, "arm": 0.35, "war": 0.45}.get(p, 0.2)
        return base + self.tune.get("upkeep_bonus", 0.0)

    def research_weights(self):
        p = self.phase
        if p == "expand":
            return {"economy": 1.0, "expansion": 1.0, "military": 0.8, "military_share": 0.1}
        if p == "arm":
            return {"economy": 1.0, "expansion": 0.8, "military": 1.3, "military_share": 0.4}
        return {"economy": 0.9, "expansion": 0.6, "military": 1.5, "military_share": 0.5}
