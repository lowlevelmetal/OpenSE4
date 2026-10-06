"""The world as Hegemon reads it in one planning call: the view's plain maps indexed
once (systems, objects, colonies, vehicles, designs), the warp graph of the links our
routes may use, and jump distances from our territory."""

from .util import RES


class World:
    def __init__(self, view):
        d = view.raw if hasattr(view, "raw") else view
        self.d = d
        self.me = d["empire"]
        self.my = d["my"]
        game = d["game"]
        self.turn = game["turn"]
        self.options = game["options"]
        self.simultaneous = game["turn_style"] == "simultaneous"
        self.empires = {}
        for e in d["empires"]:
            self.empires[e["id"]] = e
        self.me_empire = self.empires.get(self.me)
        self.race = self.me_empire["race"] if self.me_empire is not None else None
        self.systems = {}
        for s in d["systems"]:
            self.systems[s["id"]] = s
        self.objects = {}
        planets = []
        warps = []
        for o in d["objects"]:
            self.objects[o["id"]] = o
            k = o["kind"]
            if k == "planet":
                planets.append(o)
            elif k == "warp_point":
                warps.append(o)
        self.planets = planets
        self.warps = warps
        self.colonies = {}
        mine = []
        foreign = []
        for c in d["colonies"]:
            self.colonies[c["planet"]] = c
            if c["owner"] == self.me:
                mine.append(c)
            else:
                foreign.append(c)
        self.my_colonies = mine
        self.foreign_colonies = foreign
        self.vehicles = {}
        myv = []
        fv = []
        for v in d["vehicles"]:
            self.vehicles[v["id"]] = v
            if v["owner"] == self.me:
                myv.append(v)
            else:
                fv.append(v)
        self.my_vehicles = myv
        self.foreign_vehicles = fv
        self.designs = {}
        for x in d["designs"]:
            self.designs[x["id"]] = x
        self.fleets = {}
        for f in d["fleets"]:
            self.fleets[f["id"]] = f
        self.relations = {}
        for eid, e in self.empires.items():
            if eid != self.me and e["relation"] is not None:
                self.relations[eid] = e["relation"]
        self._graph = None
        self._dist_cache = {}
        self._colony_systems = None

    # ---- empires ----

    def treaty(self, eid):
        r = self.relations.get(eid)
        return r["treaty"] if r is not None else "none"

    def contact(self, eid):
        r = self.relations.get(eid)
        return r is not None and r["contact"]

    def hostile(self, eid):
        """Below Non-Aggression: battles happen (war, non-intercourse, none or not met)."""
        if eid is None or eid == self.me:
            return False
        e = self.empires.get(eid)
        if e is not None and not e["alive"]:
            return False
        t = self.treaty(eid)
        return t in ("war", "non_intercourse", "none")

    def at_war(self, eid):
        return self.treaty(eid) == "war"

    def living_rivals(self):
        return [e for e in self.empires.values() if e["id"] != self.me and e["alive"]]

    # ---- the warp graph ----

    @property
    def graph(self):
        g = self._graph
        if g is None:
            g = {}
            for w in self.warps:
                far = w["destination_system"]
                if far is None or not w["link_known"]:
                    continue
                s = w["system"]
                lst = g.get(s)
                if lst is None:
                    lst = []
                    g[s] = lst
                if far not in lst:
                    lst.append(far)
            self._graph = g
        return g

    def bfs(self, sources, limit=99):
        """Jumps from the nearest of `sources` to every reachable system."""
        key = (tuple(sources), limit)
        cached = self._dist_cache.get(key)
        if cached is not None:
            return cached
        g = self.graph
        dist = {}
        frontier = []
        for s in sources:
            if s not in dist:
                dist[s] = 0
                frontier.append(s)
        depth = 0
        while frontier and depth < limit:
            depth += 1
            nxt = []
            for s in frontier:
                for far in g.get(s, ()):
                    if far not in dist:
                        dist[far] = depth
                        nxt.append(far)
            frontier = nxt
        self._dist_cache[key] = dist
        return dist

    def path(self, a, b):
        """Systems from a to b by fewest jumps (both ends), or None."""
        if a == b:
            return [a]
        g = self.graph
        prev = {a: a}
        frontier = [a]
        while frontier and b not in prev:
            nxt = []
            for s in frontier:
                for far in g.get(s, ()):
                    if far not in prev:
                        prev[far] = s
                        nxt.append(far)
            frontier = nxt
        if b not in prev:
            return None
        chain = [b]
        while chain[-1] != a:
            chain.append(prev[chain[-1]])
        chain.reverse()
        return chain

    @property
    def colony_systems(self):
        cs = self._colony_systems
        if cs is None:
            cs = []
            for c in self.my_colonies:
                o = self.objects.get(c["planet"])
                if o is not None and o["system"] not in cs:
                    cs.append(o["system"])
            self._colony_systems = cs
        return cs

    def home_system(self):
        h = self.my["home_system"] if self.my is not None else None
        if h is not None:
            return h
        cs = self.colony_systems
        return cs[0] if cs else None

    def territory_distance(self):
        """Jumps from our nearest colony system to every reachable system."""
        cs = self.colony_systems
        if not cs:
            h = self.home_system()
            cs = [h] if h is not None else []
        return self.bfs(cs)

    # ---- things ----

    def system_of(self, oid):
        o = self.objects.get(oid)
        return o["system"] if o is not None else None

    def colony_planet(self, c):
        return self.objects.get(c["planet"])

    def design(self, did):
        return self.designs.get(did)

    def figures(self, did):
        x = self.designs.get(did)
        return x["figures"] if x is not None else None

    def stored(self):
        return self.my["stored"]

    def research_levels(self):
        return self.my["research"]["levels"]
