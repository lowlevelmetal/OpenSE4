"""The rules: the data set as scripts read it (docs/sdk/view.md, "The rules view"), and
the rules tier of mods (docs/sdk/rules.md).

`Rules(data)` wraps the rules view the engine sends. Its tables are lists whose
positions are the indices the view and commands use: `rules.components[i]`, or
`rules.component(i)`, and by name `rules.component_named("...")`.

A rules script registers functions for the game's moments and events
(`@rules.on("colony_end_of_turn")`), for its own orders, events, intelligence projects,
victory conditions and scenario objectives; the engine calls them with `game`, the
whole game as typed objects (`Game`), and `fx`, the effects that change it (`Effects`).
"""

from __future__ import annotations

from abc import ABC, abstractmethod
from typing import Any, Callable, Dict, List, Optional, Tuple

from . import _records
from ._record import CLASSES, NO_VIEW, Record, wrap_list

# ---- The rules view -------------------------------------------------------------------------------

_TABLES = {
    # table: record type
    "components": "component",
    "facilities": "facility",
    "hulls": "hull",
    "mounts": "mount",
    "techs": "tech",
    "racial_traits": "racial_trait",
    "cultures": "culture",
    "happiness_models": "happiness_model",
    "races": "race_preset",
    "planet_sizes": "planet_size",
    "system_types": "system_type",
    "sector_types": "sector_type",
    "abilities": "ability_kind",
    "formations": "formation",
    "strategies": "default_strategy",
    "intel_projects": "intel_project",
}


class Rules(_records.RulesFields):
    """The data set: components, facilities, hulls, mounts, tech areas, races, planets,
    systems, abilities, formations, strategies and intelligence projects."""

    def __init__(self, data: Dict[str, Any]) -> None:
        _records.RulesFields.__init__(self, NO_VIEW, data)
        self._tables: Dict[str, Any] = {}
        self._names: Dict[str, Dict[str, Any]] = {}

    def _table(self, name: str) -> List[Any]:
        t = self._tables.get(name)
        if t is None:
            t = wrap_list(_TABLES[name], self, self._d[name])
            self._tables[name] = t
        return t

    def resolve(self, kind: str, id: Optional[int]) -> Any:
        return None

    def resolve_all(self, kind: str, ids: List[int]) -> List[Any]:
        return [None for _ in ids]

    def _named(self, table: str, name: str) -> Any:
        index = self._names.get(table)
        if index is None:
            index = {}
            for r in self._table(table):
                n = r._d["name"]
                if n not in index:
                    index[n] = r
            self._names[table] = index
        return index.get(name)

    @staticmethod
    def _at(table: List[Any], index: Any) -> Any:
        if index is None:
            return None
        if not isinstance(index, int) or isinstance(index, bool):
            index = index.id
        return table[index] if 0 <= index < len(table) else None

    # The tables, wrapped once.
    @property
    def components(self) -> List["Component"]:
        """Components."""
        return self._table("components")

    @property
    def facilities(self) -> List["Facility"]:
        """Facilities."""
        return self._table("facilities")

    @property
    def hulls(self) -> List["Hull"]:
        """Hulls (vehicle sizes)."""
        return self._table("hulls")

    @property
    def mounts(self) -> List[_records.Mount]:
        """Weapon mounts."""
        return self._table("mounts")

    @property
    def techs(self) -> List["Tech"]:
        """Tech areas."""
        return self._table("techs")

    @property
    def racial_traits(self) -> List[_records.RacialTrait]:
        """Racial traits."""
        return self._table("racial_traits")

    @property
    def cultures(self) -> List[_records.Culture]:
        """Cultures."""
        return self._table("cultures")

    @property
    def happiness_models(self) -> List[_records.HappinessModel]:
        """How moods change."""
        return self._table("happiness_models")

    @property
    def races(self) -> List[_records.RacePreset]:
        """The races an install offers."""
        return self._table("races")

    @property
    def planet_sizes(self) -> List[_records.PlanetSize]:
        """Planet and asteroid sizes."""
        return self._table("planet_sizes")

    @property
    def system_types(self) -> List[_records.SystemType]:
        """System types."""
        return self._table("system_types")

    @property
    def sector_types(self) -> List[_records.SectorType]:
        """The appearances of stellar objects."""
        return self._table("sector_types")

    @property
    def abilities(self) -> List[_records.AbilityKind]:
        """Every ability the engine knows, with how its values combine."""
        return self._table("abilities")

    @property
    def formations(self) -> List[_records.Formation]:
        """Fleet formations."""
        return self._table("formations")

    @property
    def strategies(self) -> List[_records.DefaultStrategy]:
        """The default combat strategies."""
        return self._table("strategies")

    @property
    def intel_projects(self) -> List[_records.IntelProject]:
        """Intelligence projects."""
        return self._table("intel_projects")

    # By index (None when out of range) and by name (None when no record has it).
    def component(self, index: Any) -> Optional["Component"]:
        return self._at(self.components, index)

    def facility(self, index: Any) -> Optional["Facility"]:
        return self._at(self.facilities, index)

    def hull(self, index: Any) -> Optional["Hull"]:
        return self._at(self.hulls, index)

    def mount(self, index: Any) -> Optional[_records.Mount]:
        return self._at(self.mounts, index)

    def tech(self, index: Any) -> Optional["Tech"]:
        return self._at(self.techs, index)

    def component_named(self, name: str) -> Optional["Component"]:
        return self._named("components", name)

    def facility_named(self, name: str) -> Optional["Facility"]:
        return self._named("facilities", name)

    def hull_named(self, name: str) -> Optional["Hull"]:
        return self._named("hulls", name)

    def tech_named(self, name: str) -> Optional["Tech"]:
        return self._named("techs", name)

    def aggregation(self, ability: str) -> Optional[str]:
        """How the entries of an ability combine (an aggregation name), or None for an unknown ability."""
        r = self._named("abilities", ability)
        return None if r is None else r._d["aggregation"]


class _Abilities:
    """Ability lookups for records with an `abilities` list."""

    def has_ability(self, name: str) -> bool:
        """It has an entry of that ability."""
        for a in self._d["abilities"]:
            if a["name"] == name:
                return True
        return False

    def ability_values(self, name: str) -> List[Any]:
        """The first value of each entry of that ability, in order."""
        return [a["value1"] for a in self._d["abilities"] if a["name"] == name]

    def ability_value(self, name: str, default: Any = 0) -> Any:
        """The first value of the first entry of that ability, or `default`."""
        for a in self._d["abilities"]:
            if a["name"] == name:
                return a["value1"]
        return default


class Component(_records.ComponentFields, _Abilities):
    """A component."""

    @property
    def is_weapon(self) -> bool:
        w = self._d["weapon"]
        return w is not None and w["kind"] != "none"


class Facility(_records.FacilityFields, _Abilities):
    """A facility."""


class Hull(_records.HullFields, _Abilities):
    """A hull (vehicle size)."""


class Tech(_records.TechFields):
    """A tech area."""


for _kind, _cls in (("rules_view", Rules), ("component", Component), ("facility", Facility), ("hull", Hull),
                    ("tech", Tech)):
    CLASSES[_kind] = _cls


# ---- The rules tier (docs/sdk/rules.md) ------------------------------------------------------------

# The hooks of docs/MODDING_SDK.md, section 7.1, in turn order.
HOOKS = (
    "new_game", "generate_galaxy", "after_galaxy",
    "turn_start", "orders_applied",
    "movement_day", "vehicle_entered_sector", "before_battle", "after_battle", "vehicle_destroyed",
    "empire_end_of_turn", "colony_end_of_turn",
    "colony_founded", "vehicle_built", "tech_researched", "treaty_changed", "message_sent", "event_fired",
    "check_victory", "turn_end",
)

# The steps of an empire's end of turn that empire_end_of_turn runs before and after.
STEPS = ("intelligence", "research", "income", "maintenance", "population", "happiness", "construction", "repair",
         "supply", "ground_combat")

# What else a mod registers, by kind: its orders' effects and checks, its events, its
# intelligence projects, its victory conditions and its scenarios' objective actions.
KINDS = ("hook", "order", "order_check", "event", "intel_project", "victory", "objective")


class Registration:
    """One registered function: its mod, kind, name and (empire_end_of_turn) step and side."""

    def __init__(self, mod: str, kind: str, name: str, fn: Callable[..., Any], step: Optional[str] = None,
                 when: Optional[str] = None) -> None:
        self.mod = mod
        self.kind = kind
        self.name = name
        self.fn = fn
        self.step = step
        self.when = when

    def matches(self, mod: Optional[str], kind: str, name: str, step: Optional[str] = None,
                when: Optional[str] = None) -> bool:
        if self.kind != kind or self.name != name:
            return False
        if mod is not None and self.mod != mod:
            return False
        if step is not None and self.step is not None and self.step != step:
            return False
        if when is not None and self.when is not None and self.when != when:
            return False
        return True


_registry: List[Registration] = []
_loading: Optional[str] = None   # the mod whose scripts the engine is importing


def _register(kind: str, name: str, fn: Callable[..., Any], step: Optional[str] = None,
              when: Optional[str] = None) -> Callable[..., Any]:
    _registry.append(Registration(_loading if _loading is not None else "", kind, name, fn, step, when))
    return fn


def _name(what: str, name: Any) -> str:
    if not isinstance(name, str) or not name:
        raise TypeError(what + " needs a name, as the mod's mod.toml declares it")
    return name


def on(hook: str, step: Optional[str] = None, when: Optional[str] = None) -> Callable[[Callable[..., Any]], Callable[..., Any]]:
    """Registers the decorated function for a hook (docs/sdk/rules.md, "Hooks"):

        @rules.on("colony_end_of_turn")
        def overcrowding(game, colony, fx): ...

    For empire_end_of_turn, `step` (one of STEPS) and `when` ("before" or "after") narrow
    it to one step or one side; left out, it runs around every step, before and after."""
    if hook not in HOOKS:
        raise ValueError("no hook " + repr(hook) + "; the hooks are " + ", ".join(HOOKS))
    if (step is not None or when is not None) and hook != "empire_end_of_turn":
        raise ValueError("only empire_end_of_turn takes a step and a side")
    if step is not None and step not in STEPS:
        raise ValueError("no step " + repr(step) + "; the steps are " + ", ".join(STEPS))
    if when is not None and when not in ("before", "after"):
        raise ValueError("when is \"before\" or \"after\"")

    def register(fn: Callable[..., Any]) -> Callable[..., Any]:
        return _register("hook", hook, fn, step, when)
    return register


def order(name: str) -> Callable[[Callable[..., Any]], Callable[..., Any]]:
    """Registers the effect of the mod's order `name` ([[rules.orders]] in mod.toml):
    `effect(game, order, fx)`."""
    _name("rules.order", name)
    return lambda fn: _register("order", name, fn)


def order_check(name: str) -> Callable[[Callable[..., Any]], Callable[..., Any]]:
    """Registers the check of the mod's order `name`: `check(game, order)` answers None
    (or True) when the order may be given, else the reason (text) it is refused."""
    _name("rules.order_check", name)
    return lambda fn: _register("order_check", name, fn)


def event(name: str) -> Callable[[Callable[..., Any]], Callable[..., Any]]:
    """Registers the effect of the mod's event `name` ([[rules.events]]): `effect(game, event, fx)`."""
    _name("rules.event", name)
    return lambda fn: _register("event", name, fn)


def intel_project(project_type: str) -> Callable[[Callable[..., Any]], Callable[..., Any]]:
    """Registers the effect of the mod's intelligence projects of an IntelProjects.txt Type
    ([[rules.intel_projects]]): `effect(game, project, fx)` answers True when it took effect."""
    _name("rules.intel_project", project_type)
    return lambda fn: _register("intel_project", project_type, fn)


def victory(name: str) -> Callable[[Callable[..., Any]], Callable[..., Any]]:
    """Registers the test of the mod's victory condition `name` ([[rules.victory]]):
    `test(game)` answers the winning Empire (or its id), True for an end without a winner,
    or None while the game goes on."""
    _name("rules.victory", name)
    return lambda fn: _register("victory", name, fn)


def objective(name: str) -> Callable[[Callable[..., Any]], Callable[..., Any]]:
    """Registers a scenario objective's action (`action = "name"` in the scenario):
    `action(game, objective, fx)`, when the objective is met."""
    _name("rules.objective", name)
    return lambda fn: _register("objective", name, fn)


def handlers(hook: str, mod: Optional[str] = None) -> List[Callable[..., Any]]:
    """The functions registered for a hook (by one mod, or all), in the order they were registered."""
    if hook not in HOOKS:
        raise ValueError("no hook " + repr(hook))
    return [r.fn for r in _registry if r.kind == "hook" and r.name == hook and (mod is None or r.mod == mod)]


def registrations(mod: Optional[str] = None) -> List[Registration]:
    """Everything registered (by one mod, or all), in order."""
    return [r for r in _registry if mod is None or r.mod == mod]


def clear() -> None:
    """Forgets every registration (for tests)."""
    del _registry[:]


_log: List[str] = []


def log(text: str) -> None:
    """Writes a line to the game's log file (opense4.log on the host; not an empire's Log)."""
    _log.append(str(text))


# ---- Reading the game ----------------------------------------------------------------------------------

from ._values import id_of as _id_of, location_of as _location_of, plain as _plain  # noqa: E402
from .view import View as _View, _LISTS  # noqa: E402

_TARGET_KINDS = {"empire": "empire", "my_empire": "empire", "colony": "colony", "vehicle": "vehicle",
                 "system": "system", "space_object": "object", "fleet": "fleet", "view_design": "design"}


def target_of(thing: Any, default_kind: str = "") -> Optional[Dict[str, Any]]:
    """{kind, id} for an object of the game (an Empire, Colony, Vehicle, System,
    SpaceObject, Fleet or Design), a (kind, id) pair, or None."""
    if thing is None:
        return None
    if isinstance(thing, Record):
        kind = _TARGET_KINDS.get(thing._kind)
        if kind is None:
            raise TypeError("expected an empire, colony, vehicle, system, object, fleet or design, got " + type(thing).__name__)
        return {"kind": kind, "id": thing.id}
    if isinstance(thing, (tuple, list)) and len(thing) == 2:
        return {"kind": thing[0], "id": thing[1]}
    if isinstance(thing, dict) and "kind" in thing:
        return {"kind": thing["kind"], "id": thing.get("id")}
    if isinstance(thing, int) and not isinstance(thing, bool) and default_kind:
        return {"kind": default_kind, "id": thing}
    raise TypeError("expected an object of the game or (kind, id), got " + repr(thing))


class _Backend:
    """The engine's native functions for rules scripts (module _opense4_rules)."""

    def __init__(self, native: Any = None) -> None:
        self._native = native

    def call(self, name: str, arg: Dict[str, Any]) -> Any:
        if self._native is None:
            import _opense4_rules  # the engine's, in the game only
            self._native = _opense4_rules
        return getattr(self._native, name)(arg)


class _Data:
    """The whole game's view map (docs/sdk/view.md), each part read from the engine when
    first used."""

    def __init__(self, backend: _Backend) -> None:
        self._backend = backend
        self._parts: Dict[str, Any] = {"api": 1, "empire": None, "whole": True}

    def __getitem__(self, key: str) -> Any:
        parts = self._parts
        if key not in parts:
            parts[key] = self._backend.call("read", {"what": key})
        return parts[key]

    def __contains__(self, key: str) -> bool:
        return key in ("api", "empire", "whole", "game", "my", "empires", "systems", "objects", "colonies", "vehicles",
                       "fleets", "designs", "messages", "log", "battles")

    def get(self, key: str, default: Any = None) -> Any:
        return self[key] if key in self else default

    def loaded(self, key: str) -> bool:
        return key in self._parts


class Random:
    """The game's own random numbers (GameState::rng): each draw takes the engine's next
    number, so a game played again draws the same."""

    def __init__(self, backend: _Backend) -> None:
        self._backend = backend

    def below(self, n: int) -> int:
        """A whole number from 0 to n - 1."""
        return self._backend.call("random", {"op": "below", "n": n})

    def range(self, a: int, b: int) -> int:
        """A whole number from a to b, both included."""
        return self._backend.call("random", {"op": "range", "a": a, "b": b})

    def chance(self, percent: int) -> bool:
        """True `percent` times in a hundred."""
        return self._backend.call("random", {"op": "chance", "percent": percent})

    def choice(self, items: List[Any]) -> Any:
        """One of the items."""
        if not items:
            raise IndexError("choice from an empty list")
        return items[self.below(len(items))]

    def shuffle(self, items: List[Any]) -> None:
        """Shuffles the list in place."""
        for i in range(len(items) - 1, 0, -1):
            j = self.below(i + 1)
            items[i], items[j] = items[j], items[i]


class Game(_View):
    """The whole game as a rules hook reads it: the view of docs/sdk/view.md with every
    empire's affairs (no fog of war), read from the engine a part at a time. Lookups by id
    (`game.colony(planet)`, `game.vehicle(id)`...) read only that object.

    Beyond the view: `turn`, `options` (the game's options), `option(name)` (the mod's
    own), `mod_data` (the mod's data on the game), `rng` (the game's random numbers),
    `ability(thing, name)`, `query(name, **args)` and `rules`."""

    def __init__(self, backend: Optional[_Backend] = None) -> None:
        b = backend if backend is not None else _Backend()
        self._backend = b
        _View.__init__(self, _Data(b), rules=lambda: Rules(b.call("rules", {})))
        self._mod_data: Dict[Tuple[str, int], Any] = {}
        self._mod_data_before: Dict[Tuple[str, int], Any] = {}
        self.rng = Random(b)

    def _invalidate(self) -> None:
        """Forgets what was read: the game changed (an effect)."""
        self._d = _Data(self._backend)
        self._wrapped = {}
        self._full = {}
        self._index = {}
        self._found = {}
        self._cache = {}

    def resolve(self, kind: str, id: Optional[int]) -> Any:
        if id is None:
            return None
        found = self._found.get(kind)
        if found is None:
            found = {}
            self._found[kind] = found
        r = found.get(id)
        if r is None:
            field, record, key = _LISTS[kind]
            if self._d.loaded(field):
                r = _View.resolve(self, kind, id)
            else:
                raw = self._backend.call("read", {"what": kind, "id": id})
                r = None if raw is None else CLASSES[record](self, raw)
            if r is not None:
                found[id] = r
        return r

    def raw_of(self, kind: str, id: Optional[int]) -> Optional[Dict[str, Any]]:
        r = self.resolve(kind, id)
        return None if r is None else r._d

    @property
    def turn(self) -> int:
        """The game turn (the one being processed)."""
        return self.game.turn

    @property
    def options(self) -> Any:
        """The game's options (docs/sdk/view.md, `game_options`)."""
        return self.game.options

    @property
    def simultaneous(self) -> bool:
        return self.game.simultaneous

    def option(self, name: str) -> int:
        """The value of one of the mod's own options ([[rules.options]]); a switch is 0 or 1."""
        return self._backend.call("option", {"name": name})

    def ability(self, thing: Any, name: str, kind: Optional[str] = None) -> Optional[int]:
        """The value of an ability (a declared one or the game's own) on a vehicle, design,
        colony, system, component, facility or hull, combined as the ability combines;
        None when it does not carry it."""
        t = target_of(thing, kind or "")
        if t is None:
            raise TypeError("ability: no thing")
        if kind is not None:
            t["kind"] = kind
        return self._backend.call("ability", {"kind": t["kind"], "id": t["id"], "name": name})

    def query(self, name: str, **args: Any) -> Any:
        """A query of docs/sdk/view.md ("Queries") on the whole game, as plain values."""
        return self._backend.call("query", {"name": name, "args": {k: _plain(v) for k, v in args.items()}})

    # ---- mod data ----

    def mod_data_of(self, kind: str, id: Optional[int]) -> Any:
        """The mod's data on a thing ("game", "empire", "colony", "vehicle"): a dict to read
        and change; what it holds when the function returns is kept (docs/sdk/rules.md)."""
        key = (kind, -1 if id is None else id)
        if key not in self._mod_data:
            value = self._backend.call("read", {"what": "mod_data", "kind": kind, "id": key[1]})
            if value is None:
                return None
            self._mod_data[key] = value
            self._mod_data_before[key] = _deep_copy(value)
        return self._mod_data[key]

    @property
    def mod_data(self) -> Any:
        """The mod's data on the game."""
        return self.mod_data_of("game", None)

    def _set_mod_data(self, kind: str, id: Optional[int], value: Any) -> None:
        key = (kind, -1 if id is None else id)
        self._mod_data[key] = value
        self._mod_data_before[key] = _deep_copy(value)

    def _changed_mod_data(self) -> List[Dict[str, Any]]:
        out = []
        for key, value in self._mod_data.items():
            if value != self._mod_data_before.get(key):
                out.append({"kind": key[0], "id": key[1], "value": value})
        return out


def _deep_copy(v: Any) -> Any:
    if isinstance(v, dict):
        return {k: _deep_copy(x) for k, x in v.items()}
    if isinstance(v, list):
        return [_deep_copy(x) for x in v]
    return v


class _Info(Record):
    """A small record of a hook's argument: its fields as attributes, ids resolved in the game."""

    _kind = "info"
    _ids: Dict[str, str] = {}

    def __getattr__(self, name: str) -> Any:
        if name.startswith("_"):
            raise AttributeError(name)
        d = self._d
        if name in d:
            kind = self._ids.get(name)
            if kind is not None:
                return self._v.resolve(kind, d[name]) if kind != "location" else (
                    None if d[name] is None else CLASSES["location"](self._v, d[name]))
            return d[name]
        if name.endswith("_id") and name[:-3] in d:
            return d[name[:-3]]
        raise AttributeError(type(self).__name__ + " has no field " + repr(name))


class Setup(_Info):
    """new_game's setup: `seed` and `empires` (each with name, kind, preset, controller)."""


class Built(_Info):
    """vehicle_built's argument: `vehicle` (None when units went into cargo), `design`,
    `count`, `colony` (where it was built, or None), `location`, `empire`."""
    _ids = {"vehicle": "vehicle", "design": "design", "colony": "colony", "empire": "empire", "location": "location"}


class EventInfo(_Info):
    """An event (event_fired, a mod's event): `name`, `mod` (None for a classic event),
    `empire`, `colony`, `vehicle`, `location`, and for a mod's event `target`, `label`."""
    _ids = {"empire": "empire", "colony": "colony", "vehicle": "vehicle", "location": "location"}

    @property
    def target_object(self) -> Any:
        """A mod's event: the object it strikes (an Empire, Colony, Vehicle or System), or None."""
        t = self._d.get("target")
        if t is None:
            return None
        return self._v.resolve(t["kind"], t["id"])


class BattleSite(_Info):
    """before_battle's argument: `location` and `empires` (those with objects there)."""
    _ids = {"location": "location"}

    @property
    def empires(self) -> List[Any]:
        return [self._v.empire(e) for e in self._d["empires"]]


class OrderInfo(_Info):
    """A mod order being given: `mod`, `name`, `empire` (who gives it), `target` (the
    vehicle, fleet, colony or empire it is given to, or None for the empire itself) and
    `args` (a dict of its arguments, defaults filled in)."""
    _ids = {"empire": "empire", "vehicle": "vehicle", "fleet": "fleet", "colony": "colony", "target_empire": "empire"}

    @property
    def target(self) -> Any:
        for key in ("vehicle", "fleet", "colony", "target_empire"):
            if self._d.get(key) is not None:
                return getattr(self, key)
        return None


class ProjectInfo(_Info):
    """A mod's intelligence project taking effect: `type`, `name`, `project` (its index),
    `empire` (whose), `target` (the empire it is aimed at), `target_planet`,
    `target_vehicle`, `third_empire`, `target_tech` and `amount` (its Effect Amount)."""
    _ids = {"empire": "empire", "target": "empire", "target_planet": "object", "target_vehicle": "vehicle",
            "third_empire": "empire"}


class ObjectiveInfo(_Info):
    """A scenario objective just met: `name`, `text`, `action`, `empire`, `scenario`."""
    _ids = {"empire": "empire"}


# ---- Changing the game -----------------------------------------------------------------------------------

class Effects(ABC):
    """`fx`: how a rules function changes the game. Every change goes through the engine,
    which keeps the game valid and refuses what it cannot do (an exception in the script)
    and fractions (docs/sdk/rules.md, "Effects"). Objects may be given as the game's
    records or as ids.

    In the game the engine provides it; `NativeEffects` is that implementation. (CPython
    refuses to make an Effects that leaves a method out; the game's runtime does not
    check, and the method left out raises NotImplementedError.)"""

    # Resources, research and population
    @abstractmethod
    def add_resources(self, empire: Any, minerals: int = 0, organics: int = 0, radioactives: int = 0) -> Any:
        """Adds to (negative: takes from) an empire's stores, never below 0; returns what changed."""
        raise NotImplementedError("fx.add_resources")

    @abstractmethod
    def add_research(self, empire: Any, points: int) -> int:
        """Adds research points to the pool the empire's research spends this turn."""
        raise NotImplementedError("fx.add_research")

    @abstractmethod
    def add_intelligence(self, empire: Any, points: int) -> int:
        """Adds intelligence points to the pool its projects spend this turn."""
        raise NotImplementedError("fx.add_intelligence")

    @abstractmethod
    def grant_tech(self, empire: Any, area: Any, levels: int = 1) -> int:
        """Gives an empire tech levels in an area (a Tech or its index); returns the new level."""
        raise NotImplementedError("fx.grant_tech")

    @abstractmethod
    def change_population(self, colony: Any, millions: int, race: Any = None) -> int:
        """Adds people of a race (an empire; the owner's by default) to a colony, up to its
        room, or takes them (negative); returns the change made."""
        raise NotImplementedError("fx.change_population")

    @abstractmethod
    def change_happiness(self, colony: Any, change: int) -> int:
        """Changes a colony's mood (positive: happier, less anger); returns its anger."""
        raise NotImplementedError("fx.change_happiness")

    @abstractmethod
    def set_colony_type(self, colony: Any, colony_type: str) -> None:
        """Sets a colony's type (one of its owner's colony types)."""
        raise NotImplementedError("fx.set_colony_type")

    @abstractmethod
    def set_plague(self, colony: Any, level: int) -> None:
        """Sets a colony's plague level (0: none)."""
        raise NotImplementedError("fx.set_plague")

    # Damage, repair and supply
    @abstractmethod
    def damage(self, vehicle: Any, amount: int, cause: str = "Damaged.") -> bool:
        """Damages a vehicle as a hazard does; True when it is destroyed."""
        raise NotImplementedError("fx.damage")

    @abstractmethod
    def repair(self, vehicle: Any, components: Optional[int] = None) -> int:
        """Repairs damaged components (all, or that many), in design order; returns how many."""
        raise NotImplementedError("fx.repair")

    @abstractmethod
    def change_supply(self, vehicle: Any, amount: int) -> int:
        """Adds supply (negative: uses it), within 0 and its capacity; returns its supply."""
        raise NotImplementedError("fx.change_supply")

    # Vehicles and facilities
    @abstractmethod
    def create_vehicle(self, empire: Any, design: Any, location: Any, count: int = 1) -> Any:
        """Creates a vehicle of one of the empire's designs (units: a group of `count`);
        returns the new Vehicle."""
        raise NotImplementedError("fx.create_vehicle")

    @abstractmethod
    def remove_vehicle(self, vehicle: Any) -> None:
        """Removes a vehicle from the game (not a loss in battle: nothing is counted)."""
        raise NotImplementedError("fx.remove_vehicle")

    @abstractmethod
    def add_facility(self, colony: Any, facility: Any) -> int:
        """Adds a facility (a Facility or its index) to a colony with a free slot; returns its position."""
        raise NotImplementedError("fx.add_facility")

    @abstractmethod
    def remove_facility(self, colony: Any, index: int) -> int:
        """Removes the facility at a position of a colony's list; returns which facility it was."""
        raise NotImplementedError("fx.remove_facility")

    # Treaties, the log and events
    @abstractmethod
    def set_treaty(self, empire: Any, other: Any, treaty: str) -> None:
        """Sets the treaty between two empires that have met (a treaty name: "war", "alliance"...)."""
        raise NotImplementedError("fx.set_treaty")

    @abstractmethod
    def log(self, empire: Any, text: str, title: str = "", category: str = "misc", location: Any = None) -> None:
        """Adds an entry to an empire's Log."""
        raise NotImplementedError("fx.log")

    @abstractmethod
    def fire_event(self, name: str, target: Any = None) -> None:
        """Fires one of the mod's events ([[rules.events]]) at a target, after this function."""
        raise NotImplementedError("fx.fire_event")

    # The galaxy
    @abstractmethod
    def set_planet(self, planet: Any, minerals: Optional[int] = None, organics: Optional[int] = None,
                   radioactives: Optional[int] = None, conditions: Optional[int] = None, size: Optional[str] = None,
                   surface: Optional[str] = None, atmosphere: Optional[str] = None) -> None:
        """Changes a planet or asteroid field: its resource values, conditions (in
        hundredths, 0 to 150), size, surface or atmosphere."""
        raise NotImplementedError("fx.set_planet")

    @abstractmethod
    def rename(self, thing: Any, name: str) -> None:
        """Renames a system, stellar object, vehicle or fleet."""
        raise NotImplementedError("fx.rename")

    @abstractmethod
    def link_systems(self, a: Any, b: Any) -> Any:
        """Links two systems with a new pair of warp points; returns their ids."""
        raise NotImplementedError("fx.link_systems")

    @abstractmethod
    def replace_galaxy(self, map_text: str) -> List[str]:
        """generate_galaxy only: replaces the quadrant with a map in OpenSE4's map format
        (docs/MAPS.md); returns its warnings."""
        raise NotImplementedError("fx.replace_galaxy")

    # The game
    @abstractmethod
    def set_option(self, name: str, value: Any) -> None:
        """new_game only: sets one of the mod's options, or one of the classic options a mod may set."""
        raise NotImplementedError("fx.set_option")

    @abstractmethod
    def victory(self, empire: Any = None, reason: str = "") -> None:
        """Ends the game, with a winner (or None) and the reason the end screens show."""
        raise NotImplementedError("fx.victory")

    @abstractmethod
    def set_mod_data(self, thing: Any, value: Any) -> None:
        """Keeps the mod's data on a thing (None: the game), as `thing.mod_data` gives it."""
        raise NotImplementedError("fx.set_mod_data")


class NativeEffects(Effects):
    """The engine's effects: each call is one engine function, and the game's objects read
    before it are read again after it."""

    def __init__(self, game: Game) -> None:
        self._game = game

    def _do(self, _effect: str, **args: Any) -> Any:
        try:
            return self._game._backend.call("effect", {"name": _effect, "args": args})
        finally:
            self._game._invalidate()

    def add_resources(self, empire: Any, minerals: int = 0, organics: int = 0, radioactives: int = 0) -> Any:
        return self._do("add_resources", empire=_id_of(empire, "empire", "empire"), minerals=minerals, organics=organics,
                        radioactives=radioactives)

    def add_research(self, empire: Any, points: int) -> int:
        return self._do("add_research", empire=_id_of(empire, "empire", "empire"), points=points)

    def add_intelligence(self, empire: Any, points: int) -> int:
        return self._do("add_intelligence", empire=_id_of(empire, "empire", "empire"), points=points)

    def grant_tech(self, empire: Any, area: Any, levels: int = 1) -> int:
        a = area if isinstance(area, int) and not isinstance(area, bool) else area.id
        return self._do("grant_tech", empire=_id_of(empire, "empire", "empire"), area=a, levels=levels)

    def change_population(self, colony: Any, millions: int, race: Any = None) -> int:
        args: Dict[str, Any] = {"colony": _id_of(colony, "planet", "colony"), "millions": millions}
        if race is not None:
            args["race"] = _id_of(race, "empire", "race")
        return self._do("change_population", **args)

    def change_happiness(self, colony: Any, change: int) -> int:
        return self._do("change_happiness", colony=_id_of(colony, "planet", "colony"), change=change)

    def set_colony_type(self, colony: Any, colony_type: str) -> None:
        self._do("set_colony_type", colony=_id_of(colony, "planet", "colony"), type=colony_type)

    def set_plague(self, colony: Any, level: int) -> None:
        self._do("set_plague", colony=_id_of(colony, "planet", "colony"), level=level)

    def damage(self, vehicle: Any, amount: int, cause: str = "Damaged.") -> bool:
        return self._do("damage", vehicle=_id_of(vehicle, "vehicle", "vehicle"), amount=amount, cause=cause)

    def repair(self, vehicle: Any, components: Optional[int] = None) -> int:
        return self._do("repair", vehicle=_id_of(vehicle, "vehicle", "vehicle"),
                        components=-1 if components is None else components)

    def change_supply(self, vehicle: Any, amount: int) -> int:
        return self._do("change_supply", vehicle=_id_of(vehicle, "vehicle", "vehicle"), amount=amount)

    def create_vehicle(self, empire: Any, design: Any, location: Any, count: int = 1) -> Any:
        new = self._do("create_vehicle", empire=_id_of(empire, "empire", "empire"), design=_id_of(design, "design", "design"),
                       location=_location_of(location), count=count)
        return self._game.vehicle(new)

    def remove_vehicle(self, vehicle: Any) -> None:
        self._do("remove_vehicle", vehicle=_id_of(vehicle, "vehicle", "vehicle"))

    def add_facility(self, colony: Any, facility: Any) -> int:
        f = facility if isinstance(facility, int) and not isinstance(facility, bool) else facility.id
        return self._do("add_facility", colony=_id_of(colony, "planet", "colony"), facility=f)

    def remove_facility(self, colony: Any, index: int) -> int:
        return self._do("remove_facility", colony=_id_of(colony, "planet", "colony"), index=index)

    def set_treaty(self, empire: Any, other: Any, treaty: str) -> None:
        self._do("set_treaty", empire=_id_of(empire, "empire", "empire"), other=_id_of(other, "empire", "other"), treaty=treaty)

    def log(self, empire: Any, text: str, title: str = "", category: str = "misc", location: Any = None) -> None:
        args: Dict[str, Any] = {"empire": _id_of(empire, "empire", "empire"), "text": text, "title": title, "category": category}
        if location is not None:
            args["location"] = _location_of(location)
        self._do("log", **args)

    def fire_event(self, name: str, target: Any = None) -> None:
        self._do("fire_event", name=name, target=target_of(target))

    def set_planet(self, planet: Any, minerals: Optional[int] = None, organics: Optional[int] = None,
                   radioactives: Optional[int] = None, conditions: Optional[int] = None, size: Optional[str] = None,
                   surface: Optional[str] = None, atmosphere: Optional[str] = None) -> None:
        self._do("set_planet", planet=_id_of(planet, "planet", "planet"), minerals=minerals, organics=organics,
                 radioactives=radioactives, conditions=conditions, size=size, surface=surface, atmosphere=atmosphere)

    def rename(self, thing: Any, name: str) -> None:
        t = target_of(thing)
        if t is None:
            raise TypeError("rename: no thing")
        self._do("rename", kind=t["kind"], id=t["id"], name=name)

    def link_systems(self, a: Any, b: Any) -> Any:
        return self._do("link_systems", a=_id_of(a, "system", "a"), b=_id_of(b, "system", "b"))

    def replace_galaxy(self, map_text: str) -> List[str]:
        return self._do("replace_galaxy", map=map_text)

    def set_option(self, name: str, value: Any) -> None:
        self._do("set_option", name=name, value=value)

    def victory(self, empire: Any = None, reason: str = "") -> None:
        args: Dict[str, Any] = {"empire": _id_of(empire, "empire", "empire")}
        if reason:
            args["reason"] = reason
        self._do("victory", **args)

    def set_mod_data(self, thing: Any, value: Any) -> None:
        t = target_of(thing)
        self._do("set_mod_data", target=t, value=value)
        if t is None:
            self._game._set_mod_data("game", None, value)
        else:
            self._game._set_mod_data(t["kind"], t["id"], value)
