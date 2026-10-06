"""Small helpers shared by Hegemon's planners. Everything works on the plain maps of
the view and the rules view (`record.raw`), which costs far fewer bytecodes in the
game's runtime than the wrapped objects."""

RES = ("minerals", "organics", "radioactives")


def ability_entries(abilities, name):
    """The entries of one ability in an ability list (dicts with name, value1, value2)."""
    return [a for a in abilities if a["name"] == name]


def ability_sum(abilities, name, which="value1"):
    """The sum of an ability's numeric values in an ability list (text values count 0)."""
    total = 0
    for a in abilities:
        if a["name"] == name:
            v = a[which]
            if isinstance(v, int):
                total += v
    return total


def ability_max(abilities, name, which="value1"):
    best = None
    for a in abilities:
        if a["name"] == name:
            v = a[which]
            if isinstance(v, int) and (best is None or v > best):
                best = v
    return best


def has_ability(abilities, name):
    for a in abilities:
        if a["name"] == name:
            return True
    return False


def res_total(r):
    return r["minerals"] + r["organics"] + r["radioactives"]


def res_add(a, b, k=1):
    return {"minerals": a["minerals"] + b["minerals"] * k, "organics": a["organics"] + b["organics"] * k,
            "radioactives": a["radioactives"] + b["radioactives"] * k}


def res_zero():
    return {"minerals": 0, "organics": 0, "radioactives": 0}


def res_scale(a, k):
    return {"minerals": int(a["minerals"] * k), "organics": int(a["organics"] * k), "radioactives": int(a["radioactives"] * k)}


def res_covers(have, need):
    return have["minerals"] >= need["minerals"] and have["organics"] >= need["organics"] and have["radioactives"] >= need["radioactives"]


def chebyshev(ax, ay, bx, by):
    dx = ax - bx
    if dx < 0:
        dx = -dx
    dy = ay - by
    if dy < 0:
        dy = -dy
    return dx if dx > dy else dy


def clamp(v, lo, hi):
    return lo if v < lo else (hi if v > hi else v)


def isqrt(n):
    if n <= 0:
        return 0
    x = int(n ** 0.5)
    while x * x > n:
        x -= 1
    while (x + 1) * (x + 1) <= n:
        x += 1
    return x


def top(items, key, n):
    """The n items with the largest key, best first (stable)."""
    s = sorted(items, key=key, reverse=True)
    return s[:n]


def loc(system, x=6, y=6):
    return {"system": system, "x": x, "y": y}
