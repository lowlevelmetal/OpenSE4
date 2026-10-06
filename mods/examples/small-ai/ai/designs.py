"""Designs: Pioneer's ships, put together from the best parts it has.

Three roles: a scout (small and fast), a colony ship for each surface it can settle, and
a warship (the largest hull, half engines, then weapons and armour). Each design is
composed here as a list of components, checked by the engine with the `design_figures`
query (the designer's own checks), and created only when it differs from the role's
current design: when research brings a better part, the role gets a new design and the
old one is marked obsolete.
"""

from opense4 import cmd

import parts as p


def crew(hull, parts):
    """What a ship hull needs before anything else: a bridge, life support and quarters."""
    needed = []
    if hull.must_have_bridge:
        needed.append(parts.best_with(p.BRIDGE))
    needed += [parts.best_with(p.LIFE_SUPPORT)] * max(hull.min_life_support, 0)
    needed += [parts.best_with(p.CREW_QUARTERS)] * max(hull.min_crew_quarters, 0)
    return needed


def max_engines(hull):
    """How many engines a hull may carry (0 in the data means no limit: we stop at 8)."""
    if not hull.uses_engines:
        return 0
    return hull.max_engines if hull.max_engines > 0 else 8


def fits(components, hull):
    """The components fit in the hull, each family within its limit per vehicle."""
    if sum(c.tonnage for c in components) > hull.tonnage:
        return False
    per_family = {}
    for c in components:
        per_family[c.family] = per_family.get(c.family, 0) + 1
    return all(c.max_per_vehicle == 0 or per_family[c.family] <= c.max_per_vehicle for c in components)


def compose(role, parts, surface=None):
    """The hull and components for a role, or None when we cannot make one yet."""
    engine = parts.best_with(p.ENGINE)
    if engine is None:
        return None
    if role == "colony":
        module = parts.best_with(p.COLONIZE[surface])
        if module is None:
            return None
        # A hull made for colony modules if we have one (it asks for a share of its
        # space to be modules), else the smallest ship hull it fits in.
        hulls = sorted(parts.hulls(), key=lambda h: (h.max_percent_colony_modules == 0, h.tonnage))
        for hull in hulls:
            base = crew(hull, parts)
            if None in base:
                continue
            if module.tonnage * 100 < hull.tonnage * hull.max_percent_colony_modules:
                continue      # not enough module for this hull's rule
            chosen = base + [module]
            for _ in range(min(max_engines(hull), 4)):
                if fits(chosen + [engine], hull):
                    chosen.append(engine)
            if fits(chosen, hull) and engine in chosen:
                return hull, chosen
        return None
    # Scouts and warships: hulls without a rule for colony modules, fighter bays or cargo.
    plain = [h for h in parts.hulls() if h.max_percent_colony_modules == 0 and h.max_percent_fighter_bays == 0
             and h.max_percent_cargo == 0]
    if not plain:
        return None
    if role == "scout":
        hull = min(plain, key=lambda h: h.tonnage)
        chosen = crew(hull, parts)
        if None in chosen:
            return None
        for _ in range(max_engines(hull)):
            if fits(chosen + [engine], hull):
                chosen.append(engine)
        supply = parts.best_with(p.SUPPLY, without=p.ENGINE)
        while supply is not None and fits(chosen + [supply], hull):
            chosen.append(supply)          # range: scouts go far from any depot
        return (hull, chosen) if engine in chosen else None
    # role == "warship": the largest hull, half its engines, then weapons and armour in turn.
    hull = max(plain, key=lambda h: h.tonnage)
    chosen = crew(hull, parts)
    weapon = parts.best_weapon()
    if None in chosen or weapon is None:
        return None
    for _ in range(max(1, max_engines(hull) // 2)):
        if fits(chosen + [engine], hull):
            chosen.append(engine)
    armor = parts.best_with(p.ARMOR)
    fill = [weapon, armor] if armor is not None else [weapon]
    added = True
    while added:
        added = False
        for c in fill:
            if fits(chosen + [c], hull):
                chosen.append(c)
                added = True
    return (hull, chosen) if weapon in chosen else None


def design_type(view, role, surface=None):
    """A design type of ours for the role. Design types are the data set's own names
    (my.design_types); they are labels for players and the classic ministers, so we pick
    one whose name says the role, else the first."""
    types = view.my.design_types
    words = {"scout": ("Scout", "Explor", "Attack"), "warship": ("Attack",), "colony": ("Colony",)}[role]
    for word in words:
        for t in types:
            if word in t and (role != "colony" or surface is None or surface.split()[0] in t):
                return t
    for t in types:
        if words[0] in t:
            return t
    return types[0] if types else ""


class Designer:
    """Keeps each role's design current. `memory` is the player's: {"designs": {role:
    design id}, "marks": {role: number}}."""

    def __init__(self, player, view, parts):
        self.player = player
        self.view = view
        self.parts = parts
        self.memory = player.memory

    def current(self, role):
        """Our design for a role (a Design of the view), or None. A design made earlier in
        this call is not in the view yet: update() returns its id."""
        design_id = self.memory.setdefault("designs", {}).get(role)
        return self.view.design(design_id) if design_id is not None else None

    def update(self, role, surface=None):
        """Makes a new design for the role when the best parts changed; returns the id of
        the role's design, new or old (None when there is none)."""
        made = compose(role.split(":")[0], self.parts, surface)
        old = self.current(role)
        old_id = None if old is None else old.id
        if made is None:
            return old_id
        hull, components = made
        ids = [c.id for c in components]
        if old is not None and old.hull == hull.id and [e.component for e in old.entries] == ids:
            return old_id
        # Ask the engine whether the designer accepts it: the same checks as for a human.
        entries = [cmd.design_entry(component=i) for i in ids]
        figures = self.player.query("design_figures", hull=hull.id, entries=entries)
        if not figures.valid:
            self.player.log("design for {} refused by the designer: {}".format(role, "; ".join(figures.problems)))
            return old_id
        marks = self.memory.setdefault("marks", {})
        marks[role] = marks.get(role, 0) + 1
        label = role.replace("colony:", "Colony ").replace("scout", "Scout").replace("warship", "Warship")
        name = "{} {} {}".format(self.view.me.name.split()[0], label, marks[role])
        design = cmd.design(name=name, design_type=design_type(self.view, role.split(":")[0], surface),
                            hull=hull.id, entries=entries)
        # apply: the design is made now, so it can go into a queue in this same call.
        result = self.player.apply(cmd.create_design(design))
        if not result.ok:
            self.player.log("create_design {} refused: {}".format(name, result.reason))
            return old_id
        # The answer lists what changed in the view: our new design is among the designs.
        # (The view we were given stays as it was; the next turn's view will have it.)
        made_now = [d for d in (result.changed or {}).get("designs", []) if d["name"] == name]
        if not made_now:
            return old_id
        new_id = made_now[0]["id"]
        self.memory["designs"][role] = new_id
        if old is not None:
            self.player.apply(cmd.set_design_obsolete(design=old.id, obsolete=True))
        self.player.note(self.view.me, "new design " + name)
        return new_id
