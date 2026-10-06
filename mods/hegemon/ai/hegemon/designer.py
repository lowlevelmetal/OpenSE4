"""Hegemon's ship designer. It builds designs from the rules view by role (scout,
colony ship, warship, base, troop transport, weapon platform, satellite), checks each
candidate with the engine's `design_figures` query, and picks warship loadouts by a
combat model: a fleet of equal cost fights as its firepower times its staying power
(Lanchester's square law), so a design is worth (offense x defense) / cost^2.

Offense counts each weapon's damage per combat turn over the ranges battles are fought
at, its chance to hit and how often it reloads; defense counts structure (armour first),
shields and the to-hit defense bonuses. Against designs we have seen it weighs the
ranges and the shield-skipping damage types that beat them."""

from opense4 import services

from . import config

from .util import RES, res_total


def _query(name, args):
    return services.current().services.query(name, args)


class Designer:
    def __init__(self, world, kn, mem, tune=None):
        self.w = world
        self.kn = kn
        self.mem = mem
        self.tune = tune or {}
        self.types = world.my["design_types"]
        self.queries = 0
        p = self.tune.get("prices") if config.on("price_designs") else None
        if p is None:
            p = [1.0, 1.0, 1.0]
        m = (p[0] + p[1] + p[2]) / 3.0 or 1.0
        self.prices = [p[0] / m, p[1] / m, p[2] / m]

    def price(self, cost):
        """A cost as one number, each resource weighed by how scarce it is for us now."""
        p = self.prices
        return cost["minerals"] * p[0] + cost["organics"] * p[1] + cost["radioactives"] * p[2]

    # ---- names and types ----

    def design_type(self, *words):
        for word in words:
            lw = word.lower()
            for t in self.types:
                if lw in t.lower():
                    return t
        return self.types[0] if self.types else ""

    def new_name(self, role):
        n = self.mem.get("design_seq", 0) + 1
        self.mem["design_seq"] = n
        return "%s %s-%d" % (role, chr(65 + (self.w.me % 26)), n)

    # ---- parts ----

    def available(self, vehicle_type):
        kn = self.kn
        return [c for c in kn.components if vehicle_type in c.types and kn.meets(c.reqs)]

    def hulls(self, vehicle_type, plain=True):
        """The hulls we may build; `plain`: none that must carry colony modules, cargo or fighter bays."""
        kn = self.kn
        return [h for h in kn.hulls if h.type == vehicle_type and kn.meets(h.reqs) and
                (not plain or (h.pct_colony <= 0 and h.pct_cargo <= 0 and h.pct_bays <= 0))]

    @staticmethod
    def smallest(parts, test):
        best = None
        for c in parts:
            if test(c) and (best is None or (c.tonnage, res_total(c.cost)) < (best.tonnage, res_total(best.cost))):
                best = c
        return best

    def control(self, hull, parts):
        """The control parts a hull needs: [(component, count)], or None when we lack them."""
        out = []
        need_bridge = hull.bridge
        if need_bridge:
            b = self.smallest(parts, lambda c: c.bridge and not c.master)
            if b is None:
                m = self.smallest(parts, lambda c: c.master)
                if m is None:
                    return None
                return [(m, 1)]
            out.append((b, 1))
        if hull.life > 0:
            l = self.smallest(parts, lambda c: c.life and not c.bridge)
            if l is None:
                return None
            out.append((l, hull.life))
        if hull.crew > 0:
            q = self.smallest(parts, lambda c: c.crew and not c.bridge)
            if q is None:
                return None
            out.append((q, hull.crew))
        return out

    def best_engine(self, parts):
        best = None
        for c in parts:
            if c.engine > 0 and c.tonnage > 0:
                key = (c.engine / c.tonnage, -self.price(c.cost))
                if best is None or key > best[0]:
                    best = (key, c)
        return best[1] if best is not None else None

    def engines_for(self, hull, engine, speed):
        """How many engines give `speed` movement on this hull (capped by the hull)."""
        if engine is None or not hull.uses_engines or hull.epm <= 0:
            return 0
        need = speed * hull.epm
        n = (need + engine.engine - 1) // engine.engine
        if hull.max_engines > 0 and n > hull.max_engines:
            n = hull.max_engines
        return n

    @staticmethod
    def tonnage(entries):
        t = 0
        for c, n in entries:
            t += c.tonnage * n
        return t

    # ---- checking ----

    def figures(self, hull, entries):
        """The engine's figures for a proposed design: (figures, entries as the protocol writes them)."""
        flat = []
        for c, n, mount in entries:
            for _ in range(n):
                flat.append({"component": c.id, "mount": mount})
        self.queries += 1
        try:
            f = _query("design_figures", {"hull": hull.id, "entries": flat})
        except Exception as e:
            self.why = "query: %s" % e
            return None, flat
        return f, flat

    def proposal(self, role, design_type, hull, entries, strategy=0):
        f, flat = self.figures(hull, entries)
        if f is None:
            return None
        if not f["valid"]:
            self.why = "invalid hull %d: %s" % (hull.id, f["problems"])
            return None
        return {"name": self.new_name(role), "design_type": design_type, "hull": hull.id, "entries": flat, "strategy": strategy,
                "figures": f}

    # ---- roles ----

    def scout(self):
        """The cheapest ship that moves at a decent speed."""
        parts = self.available("ship")
        engine = self.best_engine(parts)
        if engine is None:
            self.why = "no engine among %d parts" % len(parts)
            return None
        best = None
        for hull in self.hulls("ship"):
            ctl = self.control(hull, parts)
            if ctl is None:
                self.why = "no control for hull %d" % hull.id
                continue
            tank = self.smallest(parts, lambda c: c.supply > 0 and c.engine <= 0 and c.weapon is None)
            for speed in (7, 6, 5, 4, 3):
                n = self.engines_for(hull, engine, speed)
                entries = [(c, k) for c, k in ctl] + ([(engine, n)] if n > 0 else [])
                if self.tonnage(entries) > hull.tonnage:
                    continue
                # Fuel for a long trip: supply parts in the room left.
                if tank is not None and tank.tonnage > 0:
                    room = hull.tonnage - self.tonnage(entries)
                    k = min(3, room // tank.tonnage)
                    if k > 0:
                        entries.append((tank, k))
                cost = self.price(hull.cost) + sum(self.price(c.cost) * k for c, k in entries)
                key = (-speed // 2, cost)
                if best is None or key < best[0]:
                    best = (key, hull, entries)
                break
        if best is None:
            return None
        _, hull, entries = best
        return self.proposal("Scout", self.design_type("scout", "explor", "attack"), hull, [(c, k, -1) for c, k in entries])

    def colony_ship(self, surface):
        parts = self.available("ship")
        modules = [c for c in parts if surface in c.colonize]
        if not modules:
            return None
        engine = self.best_engine(parts)
        best = None
        for hull in self.hulls("ship", plain=False):
            if hull.pct_cargo > 0 or hull.pct_bays > 0:
                continue
            ctl = self.control(hull, parts)
            if ctl is None:
                continue
            for module in modules:
                need = (hull.tonnage * hull.pct_colony) // 100
                k = max(1, (need + module.tonnage - 1) // module.tonnage) if module.tonnage > 0 else 1
                for speed in (5, 4, 3, 2):
                    n = self.engines_for(hull, engine, speed)
                    entries = [(c, j) for c, j in ctl] + [(module, k)] + ([(engine, n)] if n > 0 else [])
                    if self.tonnage(entries) > hull.tonnage:
                        continue
                    cost = self.price(hull.cost) + sum(self.price(c.cost) * j for c, j in entries)
                    key = (cost / (1.0 + 0.15 * speed),)
                    if best is None or key < best[0]:
                        best = (key, hull, entries)
                    break
        if best is None:
            return None
        _, hull, entries = best
        return self.proposal("Colonist", self.design_type("colony (" + surface.split(" ")[0].lower(), "colony"), hull,
                             [(c, k, -1) for c, k in entries])

    # ---- warships ----

    def weapon_options(self, hull, parts, against_planets=False):
        """Each weapon we may fit on this hull, unmounted and with each mount that applies:
        (component, mount id, tonnage, cost, offense per combat turn, range)."""
        out = []
        mounts = [m for m in self.kn.mounts if self.kn.meets(m.reqs) and m.offered(hull)]
        for c in parts:
            w = c.weapon
            if w is None or w.kind not in ("direct_fire", "seeking") or c.tonnage <= 0:
                continue
            if not w.hits("ship"):
                continue
            if against_planets and not w.hits("planet"):
                continue
            opts = [(None, c.tonnage, c.cost, w.damage, w.modifier)]
            for m in mounts:
                if not m.applies(c):
                    continue
                dmg = []
                for r in range(len(w.damage)):
                    i = r - m.range
                    if i < 1:
                        i = 1
                    if i >= len(w.damage):
                        dmg.append(0)
                    else:
                        dmg.append((w.damage[i] * m.dmg + 50) // 100)
                cost = {r: (c.cost[r] * m.cost + 50) // 100 for r in RES}
                opts.append((m, (c.tonnage * m.ton + 50) // 100, cost, dmg, w.modifier + m.hit))
            for m, ton, cost, dmg, mod in opts:
                if ton <= 0:
                    continue
                off = self.offense_of(dmg, mod, w.reload, w.kind == "seeking")
                rng = 0
                for r in range(1, len(dmg)):
                    if dmg[r] > 0:
                        rng = r
                out.append((c, m.id if m is not None else -1, ton, cost, off, rng))
        return out

    @staticmethod
    def offense_of(dmg, modifier, reload, seeking):
        """Expected damage per combat turn over the ranges battles are fought at."""
        total = 0.0
        n = 0
        for r in range(1, 9):
            d = dmg[r] if r < len(dmg) else 0
            hit = 1.0 if seeking else max(0.05, min(0.99, (100 + modifier - 10 * r) / 100.0))
            total += d * hit
            n += 1
        return total / n / (reload if reload > 0 else 1)

    def warship(self, speed=None, vehicle_type="ship", role="Warship", max_hulls=None, planets=None):
        """The warship (or base) design worth most per cost squared, or None."""
        parts = self.available(vehicle_type)
        engine = self.best_engine(parts) if vehicle_type == "ship" else None
        armor = None
        for c in parts:
            if c.armor and c.tonnage > 0:
                key = c.structure / (c.tonnage + self.price(c.cost) / 40.0)
                if armor is None or key > armor[0]:
                    armor = (key, c)
        armor = armor[1] if armor is not None else None
        shield = None
        for c in parts:
            if (c.shield + c.phased) > 0 and c.tonnage > 0 and c.weapon is None:
                key = (c.shield + c.phased) / (c.tonnage + self.price(c.cost) / 40.0)
                if shield is None or key > shield[0]:
                    shield = (key, c)
        shield = shield[1] if shield is not None else None
        ecm = None
        for c in parts:
            if c.defense > 0 and c.weapon is None and not c.armor and c.tonnage > 0:
                if ecm is None or c.defense > ecm.defense or (c.defense == ecm.defense and c.tonnage < ecm.tonnage):
                    ecm = c
        best = None
        hulls = sorted(self.hulls(vehicle_type), key=lambda h: -h.tonnage)
        if max_hulls is not None:
            hulls = hulls[:max_hulls]
        target_speed = speed if speed is not None else self.tune.get("speed", 6)
        for hull in hulls:
            ctl = self.control(hull, parts)
            if ctl is None:
                continue
            base = [(c, k, -1) for c, k in ctl]
            if vehicle_type == "ship":
                n = self.engines_for(hull, engine, target_speed)
                if n > 0:
                    base.append((engine, n, -1))
            used = sum(c.tonnage * k for c, k, m in base)
            room = hull.tonnage - used
            if room <= 0:
                continue
            base_cost = self.price(hull.cost) + sum(self.price(c.cost) * k for c, k, m in base)
            base_hp = sum(c.structure * k for c, k, m in base) * 0.6
            options = self.weapon_options(hull, parts, against_planets=True) if (planets if planets is not None else vehicle_type == "ship") else []
            if not options:
                options = self.weapon_options(hull, parts)
            if not options:
                continue
            options.sort(key=lambda o: -(o[4] / (o[2] + self.price(o[3]) / 40.0)))
            for wopt in options[:4]:
                wc, wm, wton, wcost, woff, wrng = wopt
                max_w = room // wton
                if wc.max_per > 0:
                    max_w = min(max_w, wc.max_per)
                for nw in range(1, max_w + 1):
                    left = room - nw * wton
                    for ns in (0, 1, 2):
                        if ns > 0 and (shield is None or (shield.max_per > 0 and ns > shield.max_per)):
                            break
                        left2 = left - (shield.tonnage * ns if ns else 0)
                        if left2 < 0:
                            break
                        for ne in (0, 1):
                            if ne and ecm is None:
                                break
                            left3 = left2 - (ecm.tonnage if ne else 0)
                            if left3 < 0:
                                break
                            na = left3 // armor.tonnage if armor is not None and armor.tonnage > 0 else 0
                            if armor is not None and armor.max_per > 0 and na > armor.max_per:
                                na = armor.max_per
                            cost = base_cost + nw * self.price(wcost) + (ns * self.price(shield.cost) if ns else 0) + \
                                (self.price(ecm.cost) if ne else 0) + (na * self.price(armor.cost) if na else 0)
                            off = nw * woff
                            hp = base_hp + nw * wc.structure * 0.6 + (na * armor.structure if na else 0) + \
                                (ns * (shield.shield + shield.phased) * 1.5 + ns * shield.structure * 0.6 if ns else 0)
                            dfn = hull.defense + (ecm.defense if ne else 0)
                            hp *= 0.65 / max(0.15, 0.65 - dfn / 100.0)
                            value = off * hp / float(cost * cost)
                            if best is None or value > best[0]:
                                entries = list(base) + [(wc, nw, wm)]
                                if ns:
                                    entries.append((shield, ns, -1))
                                if ne:
                                    entries.append((ecm, 1, -1))
                                if na:
                                    entries.append((armor, na, -1))
                                best = (value, hull, entries, off, hp, cost, wrng)
        if best is None:
            return None
        value, hull, entries, off, hp, cost, rng = best
        if vehicle_type == "ship":
            design_type = self.design_type("attack", "warship")
        elif vehicle_type == "weapon_platform":
            design_type = self.design_type("weapon platform", "platform")
        elif vehicle_type == "satellite":
            design_type = self.design_type("satellite")
        else:
            design_type = self.design_type("defense base", "base")
        p = self.proposal(role, design_type, hull, entries)
        if p is None:
            return None
        p["value"] = value
        p["offense"] = off
        p["hp"] = hp
        return p

    def yard_base(self):
        """A base with a space yard: a second building queue where it stands."""
        parts = self.available("base")
        yards = [c for c in parts if c.yard]
        if not yards:
            self.why = "no yard component for bases"
            return None
        yard = None
        for c in yards:
            if yard is None or self.kn_yard_rate(c) > self.kn_yard_rate(yard):
                yard = c
        best = None
        for hull in self.hulls("base"):
            ctl = self.control(hull, parts)
            if ctl is None:
                continue
            entries = [(c, k) for c, k in ctl] + [(yard, 1)]
            if self.tonnage(entries) > hull.tonnage:
                continue
            cost = self.price(hull.cost) + sum(self.price(c.cost) * k for c, k in entries)
            if best is None or cost < best[0]:
                best = (cost, hull, entries)
        if best is None:
            self.why = "no base hull holds a yard"
            return None
        _, hull, entries = best
        return self.proposal("Yard", self.design_type("space yard", "base"), hull, [(c, k, -1) for c, k in entries])

    @staticmethod
    def kn_yard_rate(c):
        return c.yard_rate

    def mine(self):
        """A mine: the most warhead damage per cost on a mine hull."""
        parts = self.available("mine")
        heads = [c for c in parts if c.weapon is not None and c.weapon.kind == "warhead" and c.tonnage > 0]
        if not heads:
            self.why = "no warhead for mines"
            return None
        best = None
        for hull in self.hulls("mine"):
            ctl = self.control(hull, parts)
            if ctl is None:
                continue
            base = [(c, k) for c, k in ctl]
            room = hull.tonnage - self.tonnage(base)
            for h in heads:
                n = room // h.tonnage
                if h.max_per > 0:
                    n = min(n, h.max_per)
                if n <= 0:
                    continue
                dmg = max(h.weapon.damage) * n
                cost = self.price(hull.cost) + sum(self.price(c.cost) * k for c, k in base) + self.price(h.cost) * n
                v = dmg / float(max(1, cost))
                if best is None or v > best[0]:
                    best = (v, hull, base + [(h, n)])
        if best is None:
            self.why = "no mine hull holds a warhead"
            return None
        v, hull, entries = best
        p = self.proposal("Mine", self.design_type("mine"), hull, [(c, k, -1) for c, k in entries])
        if p is not None:
            p["value"] = v
        return p

    def troop(self):
        """A troop unit: ground attack (each weapon's best damage) times hit points, per cost squared."""
        parts = self.available("troop")
        weapons = [c for c in parts if c.weapon is not None and c.weapon.kind in ("direct_fire", "seeking") and c.tonnage > 0]
        armor = None
        for c in parts:
            if c.armor and c.tonnage > 0:
                key = c.structure / (c.tonnage + self.price(c.cost) / 40.0)
                if armor is None or key > armor[0]:
                    armor = (key, c)
        armor = armor[1] if armor is not None else None
        best = None
        for hull in self.hulls("troop"):
            ctl = self.control(hull, parts)
            if ctl is None:
                continue
            base = [(c, k) for c, k in ctl]
            room = hull.tonnage - self.tonnage(base)
            base_cost = self.price(hull.cost) + sum(self.price(c.cost) * k for c, k in base)
            base_hp = sum(c.structure * k for c, k in base)
            for wpn in weapons + [None]:
                wmax = room // wpn.tonnage if wpn is not None else 0
                if wpn is not None and wpn.max_per > 0:
                    wmax = min(wmax, wpn.max_per)
                for nw in range(0 if wpn is None else 1, wmax + 1):
                    left = room - (nw * wpn.tonnage if wpn is not None else 0)
                    na = left // armor.tonnage if armor is not None and armor.tonnage > 0 else 0
                    if armor is not None and armor.max_per > 0:
                        na = min(na, armor.max_per)
                    att = nw * max(wpn.weapon.damage) if wpn is not None else 0
                    hp = base_hp + (nw * wpn.structure if wpn is not None else 0) + (na * armor.structure if na else 0)
                    cost = base_cost + (nw * self.price(wpn.cost) if wpn is not None else 0) + (na * self.price(armor.cost) if na else 0)
                    if att <= 0:
                        continue
                    v = att * hp / float(cost * cost)
                    if best is None or v > best[0]:
                        entries = list(base)
                        if nw:
                            entries.append((wpn, nw))
                        if na:
                            entries.append((armor, na))
                        best = (v, hull, entries)
        if best is None:
            self.why = "no troop hull with a weapon"
            return None
        v, hull, entries = best
        p = self.proposal("Troop", self.design_type("troop"), hull, [(c, k, -1) for c, k in entries])
        if p is not None:
            p["value"] = v
        return p

    def transport(self, speed=None):
        """A troop transport: the most cargo space per cost at a fleet's speed."""
        parts = self.available("ship")
        holds = [c for c in parts if c.cargo > 0 and c.weapon is None and c.engine <= 0 and not c.colonize and c.tonnage > 0]
        if not holds:
            self.why = "no cargo component"
            return None
        hold = max(holds, key=lambda c: c.cargo / (c.tonnage + self.price(c.cost) / 40.0))
        engine = self.best_engine(parts)
        target = speed if speed is not None else self.tune.get("speed", 6)
        best = None
        for hull in self.hulls("ship", plain=False):
            if hull.pct_colony > 0 or hull.pct_bays > 0:
                continue
            ctl = self.control(hull, parts)
            if ctl is None:
                continue
            for sp in (target, target - 1, target - 2):
                if sp < 2:
                    break
                n = self.engines_for(hull, engine, sp)
                entries = [(c, k) for c, k in ctl] + ([(engine, n)] if n > 0 else [])
                room = hull.tonnage - self.tonnage(entries)
                k = room // hold.tonnage
                if hold.max_per > 0:
                    k = min(k, hold.max_per)
                if k <= 0:
                    continue
                if hull.pct_cargo > 0 and k * hold.tonnage < hull.tonnage * hull.pct_cargo // 100:
                    continue
                entries.append((hold, k))
                cargo = k * hold.cargo
                cost = self.price(hull.cost) + sum(self.price(c.cost) * j for c, j in entries)
                v = cargo / float(cost) * (1.0 + 0.1 * sp)
                if best is None or v > best[0]:
                    best = (v, hull, entries)
                break
        if best is None:
            self.why = "no hull for a transport"
            return None
        v, hull, entries = best
        p = self.proposal("Trooper", self.design_type("troop transport", "transport"), hull, [(c, k, -1) for c, k in entries])
        if p is not None:
            p["value"] = v
        return p
