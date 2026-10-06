"""Hegemon's battle tactics (`battle_round`): every phase of our side, each piece gets
its own orders.

- Targets: enemies are ranked by the damage they deal per hit point it takes to kill
  them; our pieces, strongest first, are assigned to them greedily, each target taking
  only as much fire as kills it (no overkill, the classic AI's weakness), the rest moving
  on to the next.
- Range: each mobile piece finds the distance at which its weapons do most for what the
  target can answer at that distance, moves there, fires, and with any movement left
  backs off out of the target's reach (shoot and scoot: our move orders do not end a
  piece's turn, while the computer moves its pieces once a phase).
- Point-defense is switched off for hand fire at the start (it still reacts by itself).
- Troop carriers land beside enemy colonies once the planet's guns are down.
- Pieces it has no plan for (fighters, drones, carriers launching) play their strategy.

Damage tables come from the rules (the weapon's component and, for designs we know,
its mount); hit chances from the battle's rule of 10 % less per square."""

MAP_W = 72
MAP_H = 63


def dist(ax, ay, bx, by):
    dx = ax - bx
    if dx < 0:
        dx = -dx
    dy = ay - by
    if dy < 0:
        dy = -dy
    return dx if dx > dy else dy


def piece_dist(a, b):
    """Range distance between two pieces, to the nearest square of each footprint."""
    ax, ay = a["position"]["x"], a["position"]["y"]
    bx, by = b["position"]["x"], b["position"]["y"]
    sa = a["size"] or 1
    sb = b["size"] or 1
    if sa <= 1 and sb <= 1:
        return dist(ax, ay, bx, by)
    # Distance between two squares of side sa and sb (top-left given).
    dx = 0
    if bx > ax + sa - 1:
        dx = bx - (ax + sa - 1)
    elif ax > bx + sb - 1:
        dx = ax - (bx + sb - 1)
    dy = 0
    if by > ay + sa - 1:
        dy = by - (ay + sa - 1)
    elif ay > by + sb - 1:
        dy = ay - (by + sb - 1)
    return dx if dx > dy else dy


class Tactics:
    def __init__(self, battle, me, hostile, rules=None, designs=None, mem=None):
        """`battle` the battle map (args.battle), `hostile(empire)` whether we fight that
        empire, `rules` the rules view map, `designs` {design id: view design map}."""
        self.b = battle
        self.me = me
        self.hostile = hostile
        self.rules = rules
        self.designs = designs or {}
        self.mem = mem if mem is not None else {}
        self.pieces = battle.get("pieces") or []
        self.orders = []
        self._tables = {}

    # ---- weapons ----

    def weapon_table(self, piece, wi, w):
        """Damage at each range 0..20 of one weapon of a piece."""
        comp = w["component"]
        d = piece.get("design")
        key = (d, wi, comp)
        t = self._tables.get(key)
        if t is not None:
            return t
        base = None
        if self.rules is not None and comp is not None and 0 <= comp < len(self.rules["components"]):
            cw = self.rules["components"][comp]["weapon"]
            if cw is not None:
                base = cw["damage_at_range"]
        if base is None:
            r = w["range"] or 1
            base = [0] + [20] * r
        mount = None
        des = self.designs.get(d) if d is not None else None
        if des is not None and des["entries"] is not None and self.rules is not None:
            n = 0
            for e in des["entries"]:
                c = e["component"]
                if 0 <= c < len(self.rules["components"]) and self.rules["components"][c]["weapon"] is not None:
                    if n == wi:
                        if e["mount"] is not None and e["mount"] >= 0 and e["mount"] < len(self.rules["mounts"]):
                            mount = self.rules["mounts"][e["mount"]]
                        break
                    n += 1
        t = []
        for r in range(21):
            if mount is not None and mount["damage_percent"] != 100 or (mount is not None and mount["range_modifier"]):
                i = r - mount["range_modifier"]
                if i < 1:
                    i = 1
                v = base[i] if i < len(base) else 0
                v = v * mount["damage_percent"] // 100
            else:
                v = base[r] if r < len(base) else 0
            t.append(v)
        self._tables[key] = t
        return t

    def damage_profile(self, p, ready_only=True):
        """Expected damage this piece's weapons deal at each distance 0..20 (and its reach)."""
        prof = [0.0] * 21
        reach = 0
        planet_target = False
        for wi, w in enumerate(p["weapons"] or []):
            if w["kind"] in ("point_defense", "none", "warhead"):
                continue
            if not w["enabled"]:
                continue
            ready = w["ready"] if ready_only else w["instances"]
            if ready <= 0:
                continue
            t = self.weapon_table(p, wi, w)
            seek = w["kind"] == "seeking"
            for r in range(1, 21):
                v = t[r]
                if v <= 0:
                    continue
                hit = 0.8 if seek else max(0.05, 1.0 - 0.1 * r)
                prof[r] += v * hit * ready
                if r > reach:
                    reach = r
        return prof, reach

    # ---- the plan ----

    def plan(self):
        pieces = self.pieces
        me = self.me
        mine = []
        enemies = []
        for i, p in enumerate(pieces):
            if not p["alive"] or p["captured"]:
                continue
            owner = p["owner"]
            if owner == me:
                if p["kind"] in ("seeker", "obstacle"):
                    continue
                mine.append((i, p))
            elif owner is not None and self.hostile(owner) and p["kind"] in ("vehicle", "planet", "unit_group"):
                enemies.append((i, p))
        if not mine:
            return self.orders
        rnd = self.b.get("round", 1)
        # Every piece moves on its own: combat groups would drag members after their leader.
        if any(p["group"] >= 0 or p["is_leader"] for i, p in mine):
            self.orders.append({"kind": "clear_all_groups"})
        # Point-defense: never by hand.
        if rnd <= 1:
            for i, p in mine:
                for wi, w in enumerate(p["weapons"] or []):
                    if w["kind"] == "point_defense" and w["enabled"]:
                        self.orders.append({"kind": "toggle_weapon", "piece": i, "weapon": wi, "on": False})
        if not enemies:
            for i, p in mine:
                self.orders.append({"kind": "auto", "piece": i})
            return self.orders
        # Enemies: what they deal, what they take to kill.
        einfo = {}
        for i, p in enemies:
            prof, reach = self.damage_profile(p, ready_only=False)
            hp = (p["hit_points"] or 0) + (p["shields"] or 0)
            threat = max(prof) if reach > 0 else 0.0
            einfo[i] = {"p": p, "prof": prof, "reach": reach, "hp": float(max(1, hp)), "left": float(max(1, hp)), "threat": threat,
                        "speed": p["speed"] or 0}
        # Our pieces, strongest first.
        ours = []
        for i, p in mine:
            prof, reach = self.damage_profile(p)
            ours.append((max(prof) if reach else 0.0, i, p, prof, reach))
        ours.sort(key=lambda x: -x[0])
        for power, i, p, prof, reach in ours:
            if p["troops"]:
                self.troop_carrier(i, p, enemies, einfo)
                continue
            if p["kind"] == "unit_group" and p["type"] in ("fighter", "drone"):
                self.orders.append({"kind": "auto", "piece": i})
                continue
            if reach <= 0:
                if p["type"] in ("ship",) and (p["speed"] or 0) > 0:
                    self.evade(i, p, einfo)
                else:
                    self.orders.append({"kind": "auto", "piece": i})
                continue
            mobile = p["kind"] == "vehicle" and (p["movement"] or 0) > 0
            if mobile:
                self.fight_mobile(i, p, prof, reach, einfo)
            else:
                self.fight_fixed(i, p, prof, reach, einfo)
        self.orders.append({"kind": "end_phase"})
        return self.orders

    def priority(self, e, my_dmg):
        """How much we want to fire at an enemy: its threat per hit point left, and
        whether our shot finishes it."""
        left = e["left"]
        if left <= 0:
            return -1.0
        kill = min(1.0, my_dmg / left)
        value = (e["threat"] + 5.0) / e["hp"]
        if e["p"]["kind"] == "planet":
            value *= 0.6
        return value * (0.5 + kill)

    def fight_fixed(self, i, p, prof, reach, einfo):
        """A piece that does not move: fire at the best targets in range, a weapon at a
        time for planets (which may engage many targets), all at once otherwise."""
        cands = []
        for j, e in einfo.items():
            d = piece_dist(p, e["p"])
            if d < 1 or d > reach:
                continue
            dmg = prof[d] if d < len(prof) else 0.0
            if dmg <= 0:
                continue
            cands.append((self.priority(e, dmg), j, d, dmg))
        if not cands:
            return
        cands.sort(key=lambda x: -x[0])
        budget = max(1, (p["budget"] or 1) - (p["engaged"] or 0))
        if p["kind"] == "planet" or budget > 1:
            # Spread the weapons over the targets, each taking what kills it.
            weapons = []
            for wi, w in enumerate(p["weapons"] or []):
                if w["kind"] in ("point_defense", "none", "warhead") or not w["enabled"] or w["ready"] <= 0:
                    continue
                weapons.append((wi, w))
            used = 0
            for prio, j, d, dmg in cands:
                if used >= budget or not weapons:
                    break
                e = einfo[j]
                given = False
                while weapons and e["left"] > 0:
                    wi, w = weapons.pop(0)
                    t = self.weapon_table(p, wi, w)
                    v = (t[d] if d < len(t) else 0) * max(0.05, 1.0 - 0.1 * d) * w["ready"]
                    if v <= 0:
                        continue
                    self.orders.append({"kind": "fire", "piece": i, "target": j, "weapon": wi})
                    e["left"] -= v
                    given = True
                if given:
                    used += 1
            return
        prio, j, d, dmg = cands[0]
        self.orders.append({"kind": "fire", "piece": i, "target": j, "weapon": -1})
        einfo[j]["left"] -= dmg

    def fight_mobile(self, i, p, prof, reach, einfo):
        px, py = p["position"]["x"], p["position"]["y"]
        mv = p["movement"] or 0
        best = None
        for j, e in einfo.items():
            ep = e["p"]
            d0 = piece_dist(p, ep)
            # The distance we would fight at: our best, beyond its reach if we can.
            want = self.best_range(prof, reach, e)
            if d0 - mv > want:
                reachable = d0 - mv
            else:
                reachable = want
            dmg = prof[reachable] if 0 < reachable < len(prof) else 0.0
            if dmg <= 0:
                continue
            score = self.priority(e, dmg) / (1.0 + 0.05 * max(0, d0 - mv - want))
            if best is None or score > best[0]:
                best = (score, j, want, dmg)
        if best is None:
            # Nothing reachable this phase: close on the nearest enemy.
            near = None
            for j, e in einfo.items():
                d0 = piece_dist(p, e["p"])
                if near is None or d0 < near[0]:
                    near = (d0, j)
            if near is not None:
                t = einfo[near[1]]["p"]
                self.move_toward(i, p, t["position"]["x"], t["position"]["y"], max(1, reach))
            return
        score, j, want, dmg = best
        e = einfo[j]
        tp = e["p"]
        tx, ty = tp["position"]["x"], tp["position"]["y"]
        d0 = piece_dist(p, tp)
        steps = 0
        if d0 != want:
            steps = self.move_toward(i, p, tx, ty, want)
        self.orders.append({"kind": "fire", "piece": i, "target": j, "weapon": -1})
        e["left"] -= dmg
        # Any other weapons ready and in range of another target: a second target.
        budget = (p["budget"] or 1) - (p["engaged"] or 0)
        if budget > 1:
            for k, e2 in einfo.items():
                if k == j or e2["left"] <= 0:
                    continue
                if piece_dist(p, e2["p"]) <= reach:
                    self.orders.append({"kind": "fire", "piece": i, "target": k, "weapon": -1})
                    break
        # Back off out of its reach with what movement is left.
        left = mv - steps
        if left > 0 and e["reach"] > 0:
            safe = e["reach"] + max(1, (e["speed"] + 1) // 2)
            if safe <= reach + left and want < safe:
                self.move_away(i, p, tx, ty, min(left, safe - want))

    def best_range(self, prof, reach, e):
        """The distance to fight an enemy at: where our damage less what it deals back is best."""
        best = None
        eprof = e["prof"]
        for d in range(1, reach + 1):
            mine = prof[d]
            if mine <= 0:
                continue
            theirs = eprof[d] if d < len(eprof) else 0.0
            v = mine - 0.6 * theirs
            if best is None or v > best[0] or (v == best[0] and d > best[1]):
                best = (v, d)
        return best[1] if best is not None else 1

    def move_toward(self, i, p, tx, ty, want):
        """Moves a piece along the line to (tx, ty) until it stands `want` squares away; the steps taken."""
        px, py = p["position"]["x"], p["position"]["y"]
        mv = p["movement"] or 0
        d = dist(px, py, tx, ty)
        steps = min(mv, abs(d - want))
        if steps <= 0:
            return 0
        sign = 1 if d > want else -1
        x, y = px, py
        for _ in range(steps):
            dx = 0 if tx == x else (1 if tx > x else -1)
            dy = 0 if ty == y else (1 if ty > y else -1)
            x += dx * sign
            y += dy * sign
        x = max(0, min(MAP_W - 1, x))
        y = max(0, min(MAP_H - 1, y))
        self.orders.append({"kind": "move", "piece": i, "x": x, "y": y})
        p["position"] = {"x": x, "y": y}
        return steps

    def move_away(self, i, p, tx, ty, steps):
        px, py = p["position"]["x"], p["position"]["y"]
        x, y = px, py
        for _ in range(steps):
            dx = 0 if tx == x else (1 if tx > x else -1)
            dy = 0 if ty == y else (1 if ty > y else -1)
            nx = x - dx
            ny = y - dy
            if dx == 0 and dy == 0:
                nx = x + 1
            x = max(0, min(MAP_W - 1, nx))
            y = max(0, min(MAP_H - 1, ny))
        if (x, y) != (px, py):
            self.orders.append({"kind": "move", "piece": i, "x": x, "y": y})
            p["position"] = {"x": x, "y": y}

    def evade(self, i, p, einfo):
        """An unarmed ship keeps away from the nearest armed enemy."""
        near = None
        for j, e in einfo.items():
            if e["reach"] <= 0:
                continue
            d0 = piece_dist(p, e["p"])
            if near is None or d0 < near[0]:
                near = (d0, e["p"])
        if near is None:
            return
        t = near[1]
        self.move_away(i, p, t["position"]["x"], t["position"]["y"], p["movement"] or 0)

    def troop_carrier(self, i, p, enemies, einfo):
        """Lands its troops on an adjacent enemy colony; otherwise closes in once the
        planet's guns are silent, or keeps away while they are not."""
        planets = [(j, e) for j, e in einfo.items() if e["p"]["kind"] == "planet"]
        if not planets:
            self.evade(i, p, einfo)
            return
        best = None
        for j, e in planets:
            d0 = piece_dist(p, e["p"])
            if best is None or d0 < best[0]:
                best = (d0, j, e)
        d0, j, e = best
        if d0 <= 1:
            self.orders.append({"kind": "drop_troops", "piece": i})
            return
        armed = 0.0
        for k, e2 in einfo.items():
            if e2["reach"] > 0:
                armed += e2["threat"]
        if armed <= 0 or self.b.get("round", 1) >= 6:
            tp = e["p"]
            steps = self.move_toward(i, p, tp["position"]["x"] + 1, tp["position"]["y"] + 1, 1)
            if piece_dist(p, tp) <= 1:
                self.orders.append({"kind": "drop_troops", "piece": i})
        else:
            self.evade(i, p, einfo)
