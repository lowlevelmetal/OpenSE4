"""The view: what one empire knows of the game (docs/sdk/view.md), as typed objects.

`View(data)` wraps the view map the engine sends without copying or converting it:
every list and index is built the first time it is used, and each object reads its
fields when asked, so wrapping even a large view costs next to nothing.

Conventions (docs/sdk/python-api.md, "The view"):

- Every documented field is a property with the docs' name: `vehicle.name`,
  `colony.population`.
- A field that holds an id gives the object it names, and the id itself under the
  same name with `_id`: `vehicle.design` is a Design, `vehicle.design_id` its id;
  `fleet.members` gives Vehicles, `fleet.member_ids` their ids. A `... ref` or
  `... index` field stays a number (a ref may name what the view does not list; an
  index is a position in the rules view).
- `record.raw` is the map as the engine sent it, `record["field"]` one field of it.
- Objects with an id (empires, systems, stellar objects, colonies, vehicles, fleets,
  designs, messages) are equal when their ids are, and can be dict keys.
"""

from __future__ import annotations

from typing import Any, Callable, Dict, Iterable, List, Optional, Tuple, Union

from . import _records
from ._record import CLASSES, NO_VIEW, Record, wrap, wrap_list
from ._values import CENTRE, id_of, location_of

# The lists of the view, with the record type of their elements and the key that is
# their id.
_LISTS: Dict[str, Tuple[str, str, str]] = {
    # kind of id: (field of the view, record type, key)
    "empire": ("empires", "empire", "id"),
    "system": ("systems", "system", "id"),
    "object": ("objects", "space_object", "id"),
    "colony": ("colonies", "colony", "planet"),
    "vehicle": ("vehicles", "vehicle", "id"),
    "fleet": ("fleets", "fleet", "id"),
    "design": ("designs", "view_design", "id"),
    "message": ("messages", "message", "id"),
}

UNIT_TYPES = ("fighter", "satellite", "mine", "troop", "drone", "weapon_platform")


class View(_records.ViewFields):
    """One empire's view of the game (docs/sdk/view.md, `view`).

    Lists: `empires`, `systems`, `objects`, `colonies`, `vehicles`, `fleets`,
    `designs`, `messages`, `log`, `battles`. Lookups by id: `empire(id)`,
    `system(id)`, `object(id)`, `colony(planet)`, `vehicle(id)`, `fleet(id)`,
    `design(id)`, `message(id)`; each takes an id or an object and gives None for
    what the view does not list. `my` is our own affairs, `me` our Empire,
    `galaxy` the warp map, `rules` the rules view.
    """

    def __init__(self, data: Dict[str, Any], rules: Any = None) -> None:
        _records.ViewFields.__init__(self, self, data)
        self._rules = rules
        self._lists: Dict[str, Any] = {}
        self._index: Dict[str, Dict[int, Any]] = {}
        self._cache: Dict[str, Any] = {}

    # ---- lists, wrapped once ----

    def _list(self, field: str, kind: str) -> List[Any]:
        items = self._lists.get(field)
        if items is None:
            items = wrap_list(kind, self, self._d[field])
            self._lists[field] = items
        return items

    @property
    def empires(self) -> List["Empire"]:
        """Every empire, as known."""
        return self._list("empires", "empire")

    @property
    def systems(self) -> List["System"]:
        """Every system; the contents of explored ones."""
        return self._list("systems", "system")

    @property
    def objects(self) -> List["SpaceObject"]:
        """The stellar objects of the systems whose contents are shown."""
        return self._list("objects", "space_object")

    @property
    def colonies(self) -> List["Colony"]:
        """Our colonies, and the foreign ones we know."""
        return self._list("colonies", "colony")

    @property
    def vehicles(self) -> List["Vehicle"]:
        """Our vehicles, and the foreign ones we see."""
        return self._list("vehicles", "vehicle")

    @property
    def fleets(self) -> List["Fleet"]:
        """Our fleets."""
        return self._list("fleets", "fleet")

    @property
    def designs(self) -> List["Design"]:
        """Our designs, the foreign ones we know, and every design a listed vehicle or cargo uses."""
        return self._list("designs", "view_design")

    @property
    def messages(self) -> List["Message"]:
        """Diplomatic messages to or from us, not yet answered or expired."""
        return self._list("messages", "message")

    @property
    def log(self) -> List[_records.LogEntry]:
        """This turn's log."""
        return self._list("log", "log_entry")

    @property
    def battles(self) -> List["Battle"]:
        """The battles we fought in the last turn processed."""
        return self._list("battles", "battle")

    # ---- lookups by id ----

    def _by_id(self, kind: str) -> Dict[int, Any]:
        index = self._index.get(kind)
        if index is None:
            field, record, key = _LISTS[kind]
            index = {}
            for item in self._list(field, record):
                index[item._d[key]] = item
            self._index[kind] = index
        return index

    def resolve(self, kind: str, id: Optional[int]) -> Any:
        """The object of the kind ("vehicle", "system", ...) with that id, or None."""
        if id is None:
            return None
        return self._by_id(kind).get(id)

    def resolve_all(self, kind: str, ids: Iterable[int]) -> List[Any]:
        """The objects with those ids (None for one the view does not list)."""
        index = self._by_id(kind)
        return [index.get(i) for i in ids]

    def empire(self, id: Any) -> Optional["Empire"]:
        """The empire with that id, or None."""
        return self.resolve("empire", id_of(id, "empire"))

    def system(self, id: Any) -> Optional["System"]:
        """The system with that id, or None."""
        return self.resolve("system", id_of(id, "system"))

    def object(self, id: Any) -> Optional["SpaceObject"]:
        """The stellar object with that id, or None."""
        return self.resolve("object", id_of(id, "object"))

    def planet(self, id: Any) -> Optional["SpaceObject"]:
        """The stellar object with that id (a planet's id names it), or None."""
        return self.resolve("object", id_of(id, "object"))

    def colony(self, planet: Any) -> Optional["Colony"]:
        """The colony on that planet (an object id or the SpaceObject), or None."""
        return self.resolve("colony", id_of(planet, "planet"))

    def vehicle(self, id: Any) -> Optional["Vehicle"]:
        """The vehicle with that id, or None."""
        return self.resolve("vehicle", id_of(id, "vehicle"))

    def fleet(self, id: Any) -> Optional["Fleet"]:
        """The fleet with that id, or None."""
        return self.resolve("fleet", id_of(id, "fleet"))

    def design(self, id: Any) -> Optional["Design"]:
        """The design with that id, or None."""
        return self.resolve("design", id_of(id, "design"))

    def message(self, id: Any) -> Optional["Message"]:
        """The message with that id, or None."""
        return self.resolve("message", id_of(id, "message"))

    # ---- what is ours, what is theirs ----

    def _cached(self, key: str, make: Callable[[], Any]) -> Any:
        value = self._cache.get(key)
        if value is None:
            value = make()
            self._cache[key] = value
        return value

    def is_mine(self, thing: Any) -> bool:
        """Whether a vehicle, fleet, colony or design (or an empire id) is ours."""
        if isinstance(thing, int) and not isinstance(thing, bool):
            return thing == self._d["empire"]
        owner = thing._d.get("owner")
        return owner is not None and owner == self._d["empire"]

    def treaty_with(self, empire: Any) -> Optional[str]:
        """Our treaty with the empire (a treaty name), or None for ourselves and empires we have not met."""
        e = self.empire(empire)
        if e is None or e._d["relation"] is None:
            return None
        return e._d["relation"]["treaty"]

    def is_enemy(self, empire: Any) -> bool:
        """Whether we are at war with the empire."""
        return self.treaty_with(empire) == "war"

    @property
    def enemies(self) -> List["Empire"]:
        """The empires we are at war with."""
        return self._cached("enemies", lambda: [e for e in self.empires if e.at_war])

    @property
    def foreign_vehicles(self) -> List["Vehicle"]:
        """The vehicles of other empires that we see."""
        me = self._d["empire"]
        return self._cached("foreign_vehicles", lambda: [v for v in self.vehicles if v._d["owner"] != me])

    @property
    def enemy_vehicles(self) -> List["Vehicle"]:
        """The vehicles we see of empires we are at war with."""
        return self._cached("enemy_vehicles", lambda: [v for v in self.foreign_vehicles if self.is_enemy(v._d["owner"])])

    @property
    def foreign_colonies(self) -> List["Colony"]:
        """The colonies of other empires that we know."""
        me = self._d["empire"]
        return self._cached("foreign_colonies", lambda: [c for c in self.colonies if c._d["owner"] != me])

    @property
    def enemy_colonies(self) -> List["Colony"]:
        """The colonies we know of empires we are at war with."""
        return self._cached("enemy_colonies", lambda: [c for c in self.foreign_colonies if self.is_enemy(c._d["owner"])])

    # ---- the galaxy ----

    @property
    def explored_systems(self) -> List["System"]:
        """The systems we have explored."""
        return self._cached("explored", lambda: [s for s in self.systems if s._d["explored"]])

    @property
    def unexplored_systems(self) -> List["System"]:
        """The systems we have not explored."""
        return self._cached("unexplored", lambda: [s for s in self.systems if not s._d["explored"]])

    @property
    def planets(self) -> List["SpaceObject"]:
        """The planets of the systems whose contents are shown."""
        return self._cached("planets", lambda: [o for o in self.objects if o._d["kind"] == "planet"])

    @property
    def warp_points(self) -> List["SpaceObject"]:
        """The warp points of the systems whose contents are shown."""
        return self._cached("warp_points", lambda: [o for o in self.objects if o._d["kind"] == "warp_point"])

    def _by_system(self, key: str, items: Callable[[], List[Any]], system_of: Callable[[Any], Optional[int]]) -> Dict[int, List[Any]]:
        def make() -> Dict[int, List[Any]]:
            out: Dict[int, List[Any]] = {}
            for x in items():
                s = system_of(x)
                if s is None:
                    continue
                group = out.get(s)
                if group is None:
                    out[s] = [x]
                else:
                    group.append(x)
            return out
        return self._cached(key, make)

    def objects_in(self, system: Any) -> List["SpaceObject"]:
        """The stellar objects of a system, in the game's order (empty when it is not shown)."""
        s = self.system(system)
        if s is None or s._d["objects"] is None:
            return []
        return [o for o in self.resolve_all("object", s._d["objects"]) if o is not None]

    def vehicles_in(self, system: Any) -> List["Vehicle"]:
        """The vehicles we see in a system."""
        by = self._by_system("vehicles_by_system", lambda: self.vehicles, lambda v: v._d["location"]["system"])
        return by.get(id_of(system, "system"), [])

    def colonies_in(self, system: Any) -> List["Colony"]:
        """The colonies we know in a system."""
        def system_of(c: Any) -> Optional[int]:
            planet = self.resolve("object", c._d["planet"])
            return None if planet is None else planet._d["system"]
        by = self._by_system("colonies_by_system", lambda: self.colonies, system_of)
        return by.get(id_of(system, "system"), [])

    def vehicles_at(self, location: Any) -> List["Vehicle"]:
        """The vehicles we see in a sector (a location, or what stands for one)."""
        loc = location_of(location)
        return [v for v in self.vehicles_in(loc["system"])
                if v._d["location"]["x"] == loc["x"] and v._d["location"]["y"] == loc["y"]]

    @property
    def galaxy(self) -> Any:
        """The warp map: jumps, routes and distances over the links we know (opense4.galaxy)."""
        g = self._cache.get("galaxy")
        if g is None:
            from .galaxy import Galaxy
            g = Galaxy(self)
            self._cache["galaxy"] = g
        return g

    @property
    def rules(self) -> Any:
        """The rules view (opense4.rules.Rules): components, hulls, techs and the rest."""
        r = self._rules
        if callable(r) and not isinstance(r, Record):
            r = r()
            self._rules = r
        if r is None:
            from . import services
            r = services.rules()
            self._rules = r
        return r


class Game(_records.GameFields):
    """The game: turn, date, options and victory conditions."""

    @property
    def simultaneous(self) -> bool:
        """Every player gives orders, then one turn processing carries them out."""
        return self._d["turn_style"] == "simultaneous"


class MyEmpire(_records.MyEmpireFields):
    """Our own affairs (docs/sdk/view.md, `my_empire`), with our vehicles, fleets,
    colonies and designs gathered from the view."""

    @property
    def empire(self) -> Optional["Empire"]:
        """Our empire as the empires list shows it."""
        return self._v.empire(self._d["id"])

    def _mine(self, key: str, items: Callable[[], List[Any]]) -> List[Any]:
        me = self._d["id"]
        return self._v._cached("my_" + key, lambda: [x for x in items() if x._d["owner"] == me])

    @property
    def vehicles(self) -> List["Vehicle"]:
        """Our vehicles: ships, bases and unit groups."""
        return self._mine("vehicles", lambda: self._v.vehicles)

    @property
    def fleets(self) -> List["Fleet"]:
        """Our fleets."""
        return self._mine("fleets", lambda: self._v.fleets)

    @property
    def colonies(self) -> List["Colony"]:
        """Our colonies."""
        return self._mine("colonies", lambda: self._v.colonies)

    @property
    def designs(self) -> List["Design"]:
        """Our designs."""
        return self._mine("designs", lambda: self._v.designs)

    def vehicles_of_type(self, *types: str) -> List["Vehicle"]:
        """Our vehicles of these vehicle types ("ship", "base", "fighter"...)."""
        return [v for v in self.vehicles if v._d["type"] in types]

    @property
    def ships(self) -> List["Vehicle"]:
        """Our ships."""
        return self._v._cached("my_ships", lambda: self.vehicles_of_type("ship"))

    @property
    def bases(self) -> List["Vehicle"]:
        """Our bases."""
        return self._v._cached("my_bases", lambda: self.vehicles_of_type("base"))

    @property
    def units(self) -> List["Vehicle"]:
        """Our unit groups in space: fighters, satellites, mines, troops, drones and weapon platforms."""
        return self._v._cached("my_units", lambda: self.vehicles_of_type(*UNIT_TYPES))

    @property
    def idle_vehicles(self) -> List["Vehicle"]:
        """Our vehicles outside fleets with no orders that can move: ready for something to do."""
        return self._v._cached("my_idle_vehicles", lambda: [v for v in self.vehicles if v.idle])

    @property
    def idle_fleets(self) -> List["Fleet"]:
        """Our fleets with no orders."""
        return self._v._cached("my_idle_fleets", lambda: [f for f in self.fleets if not f._d["orders"]])

    def colonies_of_type(self, *colony_types: str) -> List["Colony"]:
        """Our colonies of these colony types ("Mining", "Research"...)."""
        return [c for c in self.colonies if c._d["colony_type"] in colony_types]

    def designs_of_type(self, *design_types: str) -> List["Design"]:
        """Our designs of these design types ("Attack Ship", "Scout"...), obsolete ones left out."""
        return [d for d in self.designs if d._d["design_type"] in design_types and not d._d["obsolete"]]

    def design_named(self, name: str) -> Optional["Design"]:
        """Our design with that name, or None."""
        for d in self.designs:
            if d._d["name"] == name:
                return d
        return None


class Empire(_records.EmpireFields):
    """An empire of the game, ours included."""

    @property
    def treaty(self) -> Optional[str]:
        """Our treaty with it, or None for ourselves and empires we have not met."""
        r = self._d["relation"]
        return None if r is None else r["treaty"]

    @property
    def at_war(self) -> bool:
        """We are at war with it."""
        return self.treaty == "war"

    @property
    def vehicles(self) -> List["Vehicle"]:
        """Its vehicles that we see."""
        me = self._d["id"]
        return [v for v in self._v.vehicles if v._d["owner"] == me]

    @property
    def colonies(self) -> List["Colony"]:
        """Its colonies that we know."""
        me = self._d["id"]
        return [c for c in self._v.colonies if c._d["owner"] == me]


class System(_records.SystemFields):
    """A star system. An unexplored one shows only where it is."""

    @property
    def centre(self) -> Dict[str, Any]:
        """The location of its centre sector."""
        return {"system": self._d["id"], "x": CENTRE, "y": CENTRE}

    @property
    def planets(self) -> List["SpaceObject"]:
        """Its planets (empty when its contents are not shown)."""
        return [o for o in self._v.objects_in(self) if o._d["kind"] == "planet"]

    @property
    def warp_points(self) -> List["SpaceObject"]:
        """Its warp points (empty when its contents are not shown)."""
        return [o for o in self._v.objects_in(self) if o._d["kind"] == "warp_point"]

    @property
    def colonies(self) -> List["Colony"]:
        """The colonies we know in it."""
        return self._v.colonies_in(self)

    @property
    def vehicles(self) -> List["Vehicle"]:
        """The vehicles we see in it."""
        return self._v.vehicles_in(self)


class SpaceObject(_records.SpaceObjectFields):
    """A star, planet, asteroid field, storm, warp point or comet."""

    @property
    def colony(self) -> Optional["Colony"]:
        """The colony on it that we know, or None (`colony_owner` is its owner)."""
        return self._v.colony(self._d["id"])

    @property
    def location(self) -> Dict[str, Any]:
        """Its sector, as a location."""
        return {"system": self._d["system"], "x": self._d["sector"]["x"], "y": self._d["sector"]["y"]}

    @property
    def is_planet(self) -> bool:
        return self._d["kind"] == "planet"

    @property
    def is_warp_point(self) -> bool:
        return self._d["kind"] == "warp_point"


class Colony(_records.ColonyFields):
    """A colony. Its `id` is its planet's: commands name colonies by their planet."""

    @property
    def id(self) -> int:
        """Its planet's id (the object id that commands name the colony by)."""
        return self._d["planet"]

    @property
    def system(self) -> Optional["System"]:
        """Its planet's system."""
        p = self.planet
        return None if p is None else p.system

    @property
    def location(self) -> Optional[Dict[str, Any]]:
        """Its planet's sector, as a location."""
        p = self.planet
        return None if p is None else p.location

    @property
    def mine(self) -> bool:
        """It is ours."""
        return self._d["owner"] == self._v._d["empire"] if isinstance(self._v, View) else False


class Vehicle(_records.VehicleFields):
    """A ship, base or unit group."""

    @property
    def system(self) -> Optional["System"]:
        """The system it is in."""
        return self._v.resolve("system", self._d["location"]["system"])

    @property
    def mine(self) -> bool:
        """It is ours."""
        return self._d["owner"] == self._v._d["empire"] if isinstance(self._v, View) else False

    @property
    def is_unit(self) -> bool:
        """A unit group: fighters, satellites, mines, troops, drones or weapon platforms."""
        return self._d["type"] in UNIT_TYPES

    @property
    def idle(self) -> bool:
        """Ours, outside a fleet, in service, able to move, with no orders."""
        d = self._d
        return (d["orders"] is not None and not d["orders"] and d["fleet"] is None and d["status"] != "mothballed"
                and (d["max_movement"] or 0) > 0)


class Fleet(_records.FleetFields):
    """One of our fleets."""

    @property
    def vehicles(self) -> List["Vehicle"]:
        """Its members (as `members`)."""
        return [v for v in self.members if v is not None]

    @property
    def system(self) -> Optional["System"]:
        """The system it is in."""
        return self._v.resolve("system", self._d["location"]["system"])

    @property
    def idle(self) -> bool:
        """It has no orders."""
        return not self._d["orders"]


class Design(_records.DesignFields):
    """A design: ours, or a foreign one we know."""

    @property
    def mine(self) -> bool:
        """It is ours."""
        return self._d["owner"] == self._v._d["empire"] if isinstance(self._v, View) else False

    @property
    def vehicle_type(self) -> Optional[str]:
        """What its hull builds (from its figures), when known."""
        f = self._d["figures"]
        return None if f is None else f["vehicle_type"]


class Message(_records.MessageFields):
    """A diplomatic message."""


class Location(_records.LocationFields):
    """A sector of a system. Equal to another location of the same sector, and usable as a dict key."""

    def __hash__(self) -> int:
        d = self._d
        return hash((d["system"], d["x"], d["y"]))

    @property
    def sector(self) -> Tuple[int, int]:
        """(x, y)."""
        return (self._d["x"], self._d["y"])

    def distance_to(self, other: Any) -> Optional[int]:
        """Sectors between here and another place in the same system (moves, diagonals
        counting one), or None in another system."""
        o = location_of(other)
        if o["system"] != self._d["system"]:
            return None
        return max(abs(o["x"] - self._d["x"]), abs(o["y"] - self._d["y"]))


class Resources(_records.ResourcesFields):
    """Minerals, organics and radioactives."""

    @property
    def total(self) -> int:
        d = self._d
        return d["minerals"] + d["organics"] + d["radioactives"]

    def as_tuple(self) -> Tuple[int, int, int]:
        d = self._d
        return (d["minerals"], d["organics"], d["radioactives"])

    def covers(self, cost: Any) -> bool:
        """Every resource is at least the cost's (a Resources, map or tuple)."""
        c = cost._d if isinstance(cost, Record) else cost
        if isinstance(c, (tuple, list)):
            c = {"minerals": c[0], "organics": c[1], "radioactives": c[2]}
        d = self._d
        return d["minerals"] >= c["minerals"] and d["organics"] >= c["organics"] and d["radioactives"] >= c["radioactives"]


class Battle(_records.BattleFields):
    """A battle we fought in the last turn processed."""


for _kind, _cls in (("view", View), ("game", Game), ("my_empire", MyEmpire), ("empire", Empire), ("system", System),
                    ("space_object", SpaceObject), ("colony", Colony), ("vehicle", Vehicle), ("fleet", Fleet),
                    ("view_design", Design), ("message", Message), ("location", Location), ("resources", Resources),
                    ("battle", Battle)):
    CLASSES[_kind] = _cls


def record(kind: str, data: Optional[Dict[str, Any]], view: Any = None) -> Any:
    """A record of the documented type `kind` ("path_result", "location"...) over `data`,
    resolving its ids in `view` (or nowhere)."""
    return wrap(kind, NO_VIEW if view is None else view, data)
