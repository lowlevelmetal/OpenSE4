"""Stand-ins for the engine's queries in Hegemon's tests: a design's figures worked out
from the rules view (tonnage, cost, movement, weapons, a validity test on space and
technology), enough for the planners; the real figures come from the engine in a game."""


def _sum(abilities, name, which="value1"):
    t = 0
    for a in abilities:
        if a["name"] == name and isinstance(a[which], int):
            t += a[which]
    return t


def design_figures(rules, levels):
    """A function answering the design_figures query for proposals {hull, entries}."""
    comps = rules["components"]
    hulls = rules["hulls"]

    def meets(reqs):
        for q in reqs:
            a = q["area"]
            if a is not None and (a >= len(levels) or levels[a] < q["level"]):
                return False
        return True

    def answer(args):
        hull = hulls[args["hull"]]
        cost = dict(hull["cost"])
        ton = 0
        structure = 0
        engines = 0
        supply = 0
        cargo = 0
        shields = 0
        weapons = 0
        damage = 0
        rng = 0
        colonize = []
        problems = []
        if not meets(hull["requirements"]):
            problems.append("hull beyond our technology")
        for e in args.get("entries") or []:
            c = comps[e["component"]]
            if not meets(c["requirements"]):
                problems.append("component beyond our technology")
            ton += c["tonnage"]
            structure += c["structure"]
            for r in ("minerals", "organics", "radioactives"):
                cost[r] += c["cost"][r]
            engines += _sum(c["abilities"], "Standard Ship Movement")
            supply += _sum(c["abilities"], "Supply Storage")
            cargo += _sum(c["abilities"], "Cargo Storage")
            shields += _sum(c["abilities"], "Shield Generation")
            w = c["weapon"]
            if w is not None and w["kind"] != "none":
                weapons += 1
                d = w["damage_at_range"]
                damage += d[1] if len(d) > 1 else 0
                rng = max(rng, w["max_range"])
            for a in c["abilities"]:
                if a["name"].startswith("Colonize Planet - "):
                    s = a["name"][len("Colonize Planet - "):]
                    s = "Gas Giant" if s == "Gas" else s
                    if s not in colonize:
                        colonize.append(s)
        if ton > hull["tonnage"]:
            problems.append("too much tonnage")
        epm = hull["engines_per_move"]
        return {"vehicle_type": hull["type"], "tonnage_used": ton, "tonnage_max": hull["tonnage"], "cost": cost,
                "maintenance": None, "structure": structure, "movement": engines // epm if epm > 0 else 0, "supply": supply,
                "cargo": cargo, "shields": shields, "phased_shields": 0, "engines": 0, "weapons": weapons,
                "max_weapon_range": rng, "weapon_damage": damage, "offense_bonus": 0, "defense_bonus": 0,
                "space_yard": False, "colonize": colonize, "valid": not problems, "problems": problems}

    return answer


def movement(args):
    return {"vehicle": args.get("vehicle"), "movement": 6, "max_movement": 6, "moves_per_turn": 6, "supply": 1000,
            "supply_capacity": 1000, "unlimited_supply": False, "uses_supply": True, "supply_per_move": 10,
            "moves_on_supply": 100, "turns_on_supply": 16}
