"""Hegemon's construction: every turn each queue that is about to run dry gets the item
worth most to it, within what the treasury can pay this turn.

- Colonies without a yard build facilities and upgrades, chosen by value per turn of
  building (economy.Economy prices them).
- Yards (colonies and space yard ships or bases) build the ships the strategy asks for,
  in its order of priority, and facilities when no ship is wanted.
- One spaceport per system comes first; yards and depots go where the strategy wants
  them."""

from . import config
from .util import RES, res_total

UPGRADE_PERCENT = 50
RESERVE_MAINT = 0.3     # of a turn's maintenance, kept in store
RESERVE_INCOME = 0.05   # of a turn's income, kept in store
FACILITY_SHARE = 0.4    # of each income, kept for the facilities of idle colonies before ships
URGENT = 62             # ship requests of this priority or more come before that


class Construction:
    def __init__(self, world, kn, econ, wants, tune=None):
        """`wants`: what the strategy asks of construction: {"ships": [requests], "yards": n,
        "depots": [systems], "defense": {system: weight}}. A ship request is a dict
        {"design": id, "count": n, "priority": p, "near": system or None, "tag": text}."""
        self.w = world
        self.kn = kn
        self.econ = econ
        self.wants = wants
        self.tune = tune or {}
        self.commands = []
        self.notes = []
        self.unmet = 0
        self.blocked = {"minerals": 0, "organics": 0, "radioactives": 0}
        rep = econ.report
        # What the treasury holds when construction runs: the store, plus income less
        # maintenance, less a reserve: income lost in this turn's battles must not leave
        # maintenance unpaid (the game then abandons ships).
        self.budget = {}
        self.reserve = {}
        for r in RES:
            income = rep["colonies"][r] + rep["remote_mining"][r] + rep["other_income"][r] + rep["trade"][r] + rep["tariffs_in"][r] - rep["tariffs_out"][r]
            self.reserve[r] = int(RESERVE_MAINT * rep["maintenance"][r] + RESERVE_INCOME * max(0, income))
            self.budget[r] = econ.stored[r] + income - rep["maintenance"][r] - self.reserve[r]
        self._best = None

    # ---- the facilities we may build ----

    def best_facilities(self):
        b = self._best
        if b is None:
            fam = {}
            for f in self.kn.facilities:
                if not self.kn.meets(f.reqs):
                    continue
                cur = fam.get(f.family)
                if cur is None or f.numeral > cur.numeral:
                    fam[f.family] = f
            b = list(fam.values())
            self._best = b
        return b

    # ---- spending ----

    def _first_spend(self, cost, rate):
        return {r: min(rate[r], cost[r]) for r in RES}

    def _commit_existing(self, queue):
        """Takes what a queue's top item will spend this turn off the budget."""
        items = queue["items"] if queue is not None else []
        if not items:
            return
        top = items[0]
        rem = top["remaining"]
        rate = queue["rate"]
        for r in RES:
            self.budget[r] -= min(rate[r], rem[r])

    def _affordable(self, cost, rate, reserve=0.0):
        for r in RES:
            need = min(rate[r], cost[r])
            if need > 0 and self.budget[r] - need < reserve * need:
                return False
        return True

    def _spend(self, cost, rate):
        for r in RES:
            self.budget[r] -= min(rate[r], cost[r])

    # ---- facility worth ----

    def facility_worth(self, f, c, info, values, mods):
        econ = self.econ
        system = info["system"]
        has = econ.sys_has.get(system, set())
        p = econ.prices
        v = econ.output_value(f, values, mods, system)
        if f.spaceport:
            if "spaceport" in has or econ.no_spaceports:
                return 0.0
            return 1.0e6
        if f.yard[0] > 0 or f.yard[1] > 0 or f.yard[2] > 0:
            if info["yard"] or c["planet"] not in self.wants.get("yard_sites", ()):
                return 0.0
            v += 5.0e5
        if f.supply:
            if "supply" in has:
                v += 0.0
            elif system in self.wants.get("depots", ()):
                v += 2500.0
        if f.convert and config.on("convert"):
            if not econ.converters and not econ.converter_queued and econ.imbalance() is not None:
                v += 3000.0
        if f.storage[0] or f.storage[1] or f.storage[2]:
            # Room to hoard is worth little: only a scarce resource that overflows anyway.
            for r in range(3):
                name = RES[r]
                if config.on("storage_low"):
                    if f.storage[r] > 0 and p[r] >= 0.9 and econ.report["lost_to_storage"][name] > 0:
                        v += p[r] * f.storage[r] / 60.0
                elif f.storage[r] > 0 and (econ.report["lost_to_storage"][name] > 0 or econ.stored[name] > 0.85 * econ.cap[name]):
                    v += p[r] * f.storage[r] / 15.0
        sys_out = None
        if f.happy > 0 and "happy" not in has:
            bad = 0
            for c2 in self.w.my_colonies:
                i2 = econ.colony_info.get(c2["planet"])
                if i2 is not None and i2["system"] == system and c2["mood"] in ("unhappy", "angry", "rioting"):
                    bad += 1
            if bad:
                sys_out = self.system_output(system)
                v += 0.25 * sys_out * bad
        for r in range(3):
            if f.smod[r] > 0 and ("smod%d" % r) not in has:
                v += p[r] * f.smod[r] / 100.0 * self.system_output_r(system, r) * econ.delivered(system)
            if f.pmod[r] > mods[r]:
                out = c["output"]["production"][RES[r]] if c["output"] is not None else 0
                v += p[r] * (f.pmod[r] - mods[r]) / 100.0 * out * econ.delivered(system)
        if f.pres > mods[3]:
            out = c["output"]["research"] if c["output"] is not None else 0
            v += p[3] * (f.pres - mods[3]) / 100.0 * out * econ.delivered(system)
        if f.sres > 0 and "sres" not in has:
            v += p[3] * f.sres / 100.0 * self.system_output_r(system, 3) * econ.delivered(system)
        if f.repro > 0 and "repro" not in has:
            room = 0
            for c2 in self.w.my_colonies:
                i2 = econ.colony_info.get(c2["planet"])
                if i2 is not None and i2["system"] == system and c2["max_population"]:
                    room += max(0, c2["max_population"] - c2["total_population"])
            v += min(400.0, room / 20.0)
        d = self.wants.get("defense", {}).get(system, 0)
        if d > 0:
            if f.shield > 0:
                v += d * f.shield / 10.0
            if f.combat > 0:
                v += d * f.combat * 5.0
        return v

    def system_output(self, system):
        t = 0
        for c in self.w.my_colonies:
            i = self.econ.colony_info.get(c["planet"])
            if i is not None and i["system"] == system and c["output"] is not None:
                t += res_total(c["output"]["production"]) + c["output"]["research"]
        return t

    def system_output_r(self, system, r):
        t = 0
        for c in self.w.my_colonies:
            i = self.econ.colony_info.get(c["planet"])
            if i is not None and i["system"] == system and c["output"] is not None:
                t += c["output"]["research"] if r == 3 else c["output"]["production"][RES[r]]
        return t

    # ---- choosing for one colony ----

    def colony_choice(self, c, info):
        """The best facility or upgrade for a colony: (score, item, cost) or None."""
        kn = self.kn
        econ = self.econ
        o = info["planet"]
        values = econ.planet_values(o)
        mods = econ.colony_mods(c)
        rate = c["queue"]["rate"]
        facs = c["facilities"] or []
        free = (c["facility_slots"] or 0) - len(facs) - info["queued_facilities"]
        best = None
        if free > 0:
            for f in self.best_facilities():
                val = self.facility_worth(f, c, info, values, mods)
                if val <= 0:
                    continue
                turns = econ.build_turns(f.cost, rate)
                score = val / turns
                if best is None or score > best[0]:
                    best = (score, {"kind": "facility", "facility": f.id}, f.cost, f)
        # Upgrades: each family on the colony whose best researched level is higher.
        counts = {}
        for fid in facs:
            if 0 <= fid < len(kn.facilities):
                f = kn.facilities[fid]
                e = counts.get(f.family)
                if e is None:
                    counts[f.family] = [f, 1]
                else:
                    e[1] += 1
                    if f.numeral < e[0].numeral:
                        e[0] = f
        queued_upgrades = []
        for it in c["queue"]["items"]:
            if it["kind"] == "upgrade":
                queued_upgrades.append(it["facility"])
        for family, (cur, n) in counts.items():
            target = kn.best_in_family(family)
            if target is None or target.numeral <= cur.numeral or target.id in queued_upgrades:
                continue
            gain = self.facility_worth(target, c, info, values, mods) - self.facility_worth(cur, c, info, values, mods)
            if cur.spaceport or cur.yard[0] or cur.yard[1] or cur.yard[2]:
                gain = 0.0
            if gain <= 0:
                continue
            gain *= n
            cost = {r: (target.cost[r] * UPGRADE_PERCENT // 100) * n for r in RES}
            turns = econ.build_turns(cost, rate)
            score = gain / turns
            if best is None or score > best[0]:
                best = (score, {"kind": "upgrade", "facility": target.id}, cost, target)
        return best

    # ---- the plan ----

    def plan(self):
        w = self.w
        econ = self.econ
        # Queues: colonies and our vehicles with a yard.
        idle_colonies = []
        yards = []
        for c in w.my_colonies:
            q = c["queue"]
            if q is None:
                continue
            self._commit_existing(q)
            if c["total_population"] <= 0:
                continue
            info = econ.colony_info.get(c["planet"])
            if info is None:
                continue
            if info["yard"]:
                yards.append(("planet", c, info))
            if not q["items"] and not q["on_hold"]:
                idle_colonies.append((c, info))
        for v in w.my_vehicles:
            q = v["queue"]
            if q is None:
                continue
            self._commit_existing(q)
            if v["status"] == "normal":
                yards.append(("vehicle", v, None))
        # What the best facilities of idle colonies will take this turn, up to a share of
        # income, is kept from ships of lower priority: new colonies without facilities
        # are worth little, so expansion must not starve their development.
        choices = []
        for c, info in idle_colonies:
            ch = self.colony_choice(c, info)
            if ch is not None:
                choices.append((ch[0], c, info, ch))
        choices.sort(key=lambda x: -x[0])
        held = {r: 0 for r in RES}
        rep = econ.report
        for score, c, info, ch in choices:
            cost, rate = ch[2], c["queue"]["rate"]
            if any(held[r] + min(rate[r], cost[r]) > FACILITY_SHARE * max(0, rep["colonies"][r]) for r in RES if cost[r] > 0):
                continue
            for r in RES:
                held[r] += min(rate[r], cost[r])
        # Ships first, in the strategy's order, at idle yards.
        requests = sorted(self.wants.get("ships", []), key=lambda x: -x["priority"])
        busy = {}
        for kind, holder, info in yards:
            q = holder["queue"]
            busy[(kind, holder["planet"] if kind == "planet" else holder["id"])] = len(q["items"]) > 0 or q["on_hold"]
        colonies = {}
        for c in w.my_colonies:
            colonies[c["planet"]] = c
        holding = False
        for req in requests:
            if req["priority"] < URGENT and not holding:
                # Urgent ships (scouts, yards, guns where enemies are) come before the hold.
                holding = True
                for r in RES:
                    self.budget[r] -= held[r]
            n = req["count"]
            fig = w.figures(req["design"])
            if fig is None:
                continue
            if req.get("units"):
                # Units built into a colony's own cargo, in its own queue.
                c = colonies.get(req.get("at"))
                if c is None or c["total_population"] <= 0:
                    continue
                key = ("planet", c["planet"])
                q = c["queue"]
                if busy.get(key) or q["items"] or q["on_hold"]:
                    continue
                cost = {r: fig["cost"][r] * n for r in RES}
                if not self._affordable(cost, q["rate"], 0.0):
                    continue
                busy[key] = True
                self.commands.append({"kind": "queue_add", "target": {"planet": c["planet"]}, "item": {"kind": "vehicle", "design": req["design"], "count": n}})
                self._spend(cost, q["rate"])
                continue
            while n > 0:
                spot = self.pick_yard(yards, busy, req)
                if spot is None:
                    # No yard free for it: the strategy may want more yards.
                    if fig["vehicle_type"] in ("ship", "base"):
                        self.unmet += n
                    break
                kind, holder, info = spot
                rate = holder["queue"]["rate"]
                if not self._affordable(fig["cost"], rate, self.tune.get("reserve", 0.3) if req["priority"] < 50 else 0.0):
                    # Facilities must not take what this ship waits for.
                    if req.get("protect"):
                        for r in RES:
                            self.blocked[r] += min(rate[r], fig["cost"][r])
                    break
                key = (kind, holder["planet"] if kind == "planet" else holder["id"])
                busy[key] = True
                target = {"planet": holder["planet"]} if kind == "planet" else {"vehicle": holder["id"]}
                self.commands.append({"kind": "queue_add", "target": target, "item": {"kind": "vehicle", "design": req["design"], "count": 1}})
                self._spend(fig["cost"], rate)
                n -= 1
        # Facilities on every other idle colony, the most valuable first.
        if holding:
            for r in RES:
                self.budget[r] += held[r]
        choices = [x for x in choices if not busy.get(("planet", x[1]["planet"]))]
        self.debug = {"budget": {r: int(v) for r, v in self.budget.items()}, "reserve": self.reserve, "idle": len(idle_colonies),
                      "colonies": [(c["planet"], len(c["queue"]["items"]), c["queue"]["on_hold"], c["total_population"]) for c in w.my_colonies][:12],
                      "choices": [(int(x[0]), x[1]["planet"], x[3][1]) for x in choices[:6]]}
        for score, c, info, ch in choices:
            rate = c["queue"]["rate"]
            item, cost, f = ch[1], ch[2], ch[3]
            if score < 1.0e5:
                if not self._affordable(cost, rate, 0.0):
                    continue
                short = False
                for r in RES:
                    need = min(rate[r], cost[r])
                    if need > 0 and self.blocked[r] > 0 and self.budget[r] - need < self.blocked[r]:
                        short = True
                if short:
                    continue
            # A facility one per system: claim it so a second colony does not queue it too.
            if item["kind"] == "facility":
                if f.spaceport:
                    if "spaceport" in econ.sys_has.get(info["system"], set()):
                        continue
                    econ._tag(info["system"], "spaceport")
                if f.supply:
                    econ._tag(info["system"], "supply")
                if f.happy > 0:
                    econ._tag(info["system"], "happy")
                for r in range(3):
                    if f.smod[r] > 0:
                        econ._tag(info["system"], "smod%d" % r)
                if f.yard[0] or f.yard[1] or f.yard[2]:
                    info["yard"] = True
                if f.convert:
                    econ.converter_queued = True
            self.commands.append({"kind": "queue_add", "target": {"planet": c["planet"]}, "item": item})
            self._spend(cost, rate)
        return self.commands

    def pick_yard(self, yards, busy, req):
        near = req.get("near")
        best = None
        dist = self.w.bfs([near]) if near is not None else None
        for spot in yards:
            kind, holder, info = spot
            key = (kind, holder["planet"] if kind == "planet" else holder["id"])
            if busy.get(key):
                continue
            if kind == "vehicle":
                system = holder["location"]["system"]
            else:
                system = info["system"]
            # Units go into cargo: only a colony's queue builds them here.
            if req.get("units") and kind != "planet":
                continue
            rate = holder["queue"]["rate"]
            score = res_total(rate)
            if dist is not None:
                d = dist.get(system)
                score -= 1000 * (d if d is not None else 20)
            if req.get("at") is not None and req["at"] != (holder["planet"] if kind == "planet" else None):
                continue
            if best is None or score > best[0]:
                best = (score, spot)
        return best[1] if best is not None else None
