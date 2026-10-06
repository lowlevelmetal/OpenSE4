"""Hegemon's picture of its enemies: the armed ships and colonies it sees, what it
remembers of those it no longer sees, how strong each is by its design, and the
threat each system of ours faces. Beliefs fade: a fleet not seen for a while counts
less each turn, until it is forgotten.

Strength is kept as two numbers, as Lanchester's square law has it: attack (damage a
combat turn) and hit points. Two forces compare by attack x hit points."""

from .util import res_total

FORGET_TURNS = 12


def design_strength(fig, tonnage_hint=0):
    """(attack, hit points) of one vehicle of a design from its figures."""
    if fig is None:
        # A design we have not seen: a guess from its size.
        t = tonnage_hint or 150
        return (t * 0.25, t * 1.6)
    a = fig["weapon_damage"] * 0.55
    h = fig["structure"] + (fig["shields"] + fig["phased_shields"]) * 1.5
    return (a, h)


class Force:
    __slots__ = ("attack", "hp", "count")

    def __init__(self, attack=0.0, hp=0.0, count=0):
        self.attack = attack
        self.hp = hp
        self.count = count

    def add(self, a, h, n=1):
        self.attack += a * n
        self.hp += h * n
        self.count += n

    def power(self):
        return self.attack * self.hp

    def beats(self, other, margin=1.0):
        return self.attack * self.hp > margin * other.attack * other.hp


def planet_strength(colony, turn, known=None):
    """A guess at a foreign colony's defences: its population fights as hit points, and
    planets of an older empire carry more guns. `known`: what a battle showed."""
    pop = colony["total_population"] or 0
    h = pop * 10.0 + 300.0 + turn * 15.0
    a = 30.0 + turn * 4.0
    if known is not None:
        a = max(a, known[0])
        h = max(h, known[1])
    return (a, h)


class Intel:
    def __init__(self, world, mem):
        self.w = world
        self.mem = mem
        seen = mem.get("seen")
        if not isinstance(seen, dict):
            seen = {}
            mem["seen"] = seen
        self.seen = seen             # vehicle id (text) -> [owner, system, attack, hp, turn]
        self.enemy_ships = []        # (vehicle map, attack, hp)
        self.update()

    def hostile(self, owner):
        return self.w.hostile(owner)

    def update(self):
        w = self.w
        turn = w.turn
        seen = self.seen
        for v in w.foreign_vehicles:
            if v["type"] not in ("ship", "base") and v["type"] not in ("fighter", "drone", "satellite"):
                continue
            d = w.designs.get(v["design"])
            fig = d["figures"] if d is not None else None
            hull_t = 0
            a, h = design_strength(fig, hull_t)
            if a <= 0 and v["type"] not in ("base",):
                # Unarmed: colony ships, scouts, transports.
                if fig is not None:
                    continue
            n = v["count"] or 1
            if v["type"] in ("fighter", "satellite", "drone"):
                a *= n
                h *= n
            self.enemy_ships.append((v, a, h))
            seen[str(v["id"])] = [v["owner"], v["location"]["system"], int(a), int(h), turn]
        # Forget what is old or gone.
        visible_systems = set()
        for s in w.systems.values():
            if s["present"]:
                visible_systems.add(s["id"])
        now_ids = set(str(v["id"]) for v in w.foreign_vehicles)
        for k in list(seen.keys()):
            e = seen[k]
            if turn - e[4] > FORGET_TURNS:
                del seen[k]
            elif k not in now_ids and e[1] in visible_systems:
                # We see that system and it is not there: it moved or died.
                del seen[k]

    def threat_by_system(self):
        """{system: Force} of hostile armed vehicles believed there (fading with age)."""
        out = {}
        turn = self.w.turn
        for k, e in self.seen.items():
            owner, system, a, h, t = e
            if not self.hostile(owner):
                continue
            age = turn - t
            k2 = 1.0 if age <= 1 else max(0.2, 1.0 - age / float(FORGET_TURNS))
            f = out.get(system)
            if f is None:
                f = Force()
                out[system] = f
            f.add(a * k2, h * k2)
        return out

    def threat_near(self, threats, radius=1):
        """{system: Force} within `radius` jumps of each of our colony systems."""
        w = self.w
        out = {}
        for s in w.colony_systems:
            dist = w.bfs([s], radius)
            f = Force()
            for t, force in threats.items():
                d = dist.get(t)
                if d is not None:
                    k = 1.0 if d == 0 else 0.6
                    f.add(force.attack * k, force.hp * k)
            if f.count > 0:
                out[s] = f
        return out
