"""Hegemon's design book: which of our designs plays each role (scout, colony ship per
surface, warship, base, troop transport, troops, weapon platform, satellite, yard base),
kept in memory by name, and redesigned by the designer when our technology changes."""

from .designer import Designer
from .util import res_total


class DesignBook:
    def __init__(self, world, kn, mem, tune=None):
        self.w = world
        self.kn = kn
        self.mem = mem
        self.tune = tune or {}
        book = mem.get("roles")
        if not isinstance(book, dict):
            book = {}
            mem["roles"] = book
        self.book = book
        values = mem.get("role_values")
        if not isinstance(values, dict):
            values = {}
            mem["role_values"] = values
        self.values = values
        self.by_name = {}
        for d in world.d["designs"]:
            if d["owner"] == world.me:
                self.by_name[d["name"]] = d
        # Roles whose design was refused or has gone.
        for role in list(book.keys()):
            if book[role] not in self.by_name:
                pending = mem.get("pending_roles", {})
                if role not in pending:
                    del book[role]
                    values.pop(role, None)
        self.commands = []
        self.problems = []
        self.made = []

    def design(self, role):
        name = self.book.get(role)
        if name is None:
            return None
        return self.by_name.get(name)

    def design_id(self, role):
        d = self.design(role)
        return d["id"] if d is not None else None

    def roles_of(self, did):
        out = []
        for role, name in self.book.items():
            d = self.by_name.get(name)
            if d is not None and d["id"] == did:
                out.append(role)
        return out

    def update(self, surfaces_wanted, force=False):
        """Redesigns the roles when our technology changed (or a role has no design yet)."""
        lv = self.w.research_levels()
        tech = sum(lv)
        mem = self.mem
        mem["pending_roles"] = {}
        changed = force or mem.get("design_tech") != tech
        missing = [r for r in self.wanted_roles(surfaces_wanted) if r not in self.book]
        if not changed and not missing:
            return self.commands
        mem["design_tech"] = tech
        dz = Designer(self.w, self.kn, mem, self.tune)
        for role in self.wanted_roles(surfaces_wanted):
            if not changed and role not in missing:
                continue
            p = None
            if role == "scout":
                p = dz.scout()
            elif role.startswith("colony:"):
                p = dz.colony_ship(role[7:])
            elif role == "warship":
                p = dz.warship()
            elif role == "yard":
                p = dz.yard_base()
            elif role == "platform":
                p = dz.warship(vehicle_type="weapon_platform", role="Guard")
            if p is None:
                self.problems.append((role, getattr(dz, "why", "?")))
                continue
            value = self.role_value(role, p)
            old = self.values.get(role)
            if role in self.book and old is not None and value <= old * 1.08:
                continue
            self.commands.append({"kind": "create_design", "design": {"name": p["name"], "design_type": p["design_type"], "hull": p["hull"],
                                                                     "entries": p["entries"], "strategy": p.get("strategy", 0)}})
            prev = self.design(role)
            if prev is not None and len(self.roles_of(prev["id"])) <= 1:
                self.commands.append({"kind": "set_design_obsolete", "design": prev["id"], "obsolete": True})
            f = p["figures"]
            self.made.append("%s %s hull=%d ton=%d/%d cost=%d move=%d struct=%d wpn=%d dmg=%d rng=%d shields=%d value=%s" % (
                role, p["name"], p["hull"], f["tonnage_used"], f["tonnage_max"], res_total(f["cost"]), f["movement"], f["structure"],
                f["weapons"], f["weapon_damage"], f["max_weapon_range"], f["shields"], int(value)))
            self.book[role] = p["name"]
            self.values[role] = int(value)
            mem["pending_roles"][role] = True
        return self.commands

    def wanted_roles(self, surfaces):
        roles = ["scout", "warship", "yard", "platform"]
        for s in surfaces:
            roles.append("colony:" + s)
        return roles

    @staticmethod
    def role_value(role, p):
        """A whole number: how good a design is for its role (higher is better)."""
        f = p["figures"]
        cost = res_total(f["cost"]) or 1
        if role == "scout":
            v = f["movement"] * 1.0e7 / cost
        elif role == "yard":
            v = 1.0e9 / cost
        elif role.startswith("colony:"):
            v = (1.0 + 0.15 * f["movement"]) * 1.0e9 / cost
        else:
            v = p.get("value", 0.0) * 1.0e12
        return int(v)
