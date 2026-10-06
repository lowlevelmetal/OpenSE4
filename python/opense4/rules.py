"""The rules: the data set as scripts read it (docs/sdk/view.md, "The rules view"), and
the hooks of rules scripts.

`Rules(data)` wraps the rules view the engine sends. Its tables are lists whose
positions are the indices the view and commands use: `rules.components[i]`, or
`rules.component(i)`, and by name `rules.component_named("...")`.

Rules hooks (`@rules.on("colony_end_of_turn")`) and the effects API (`Effects`) are the
interface of the rules tier (docs/MODDING_SDK.md, section 7). They arrive in a later
step of the SDK: today a script can register hooks, and nothing calls them yet.
"""

from __future__ import annotations

from abc import ABC, abstractmethod
from typing import Any, Callable, Dict, List, Optional

from . import _records
from ._record import CLASSES, NO_VIEW, wrap_list

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


# ---- Hooks (the rules tier; a later step of the SDK) ---------------------------------------------

# The hooks of docs/MODDING_SDK.md, section 7.1, in turn order.
HOOKS = (
    "new_game", "generate_galaxy", "after_galaxy",
    "turn_start", "orders_applied",
    "movement_day", "vehicle_entered_sector", "before_battle", "after_battle", "vehicle_destroyed",
    "empire_end_of_turn", "colony_end_of_turn",
    "colony_founded", "vehicle_built", "tech_researched", "treaty_changed", "message_sent", "event_fired",
    "check_victory", "turn_end",
)

_handlers: Dict[str, List[Callable[..., Any]]] = {}


def on(hook: str) -> Callable[[Callable[..., Any]], Callable[..., Any]]:
    """Registers the decorated function for a hook:

        @rules.on("colony_end_of_turn")
        def overcrowding(game, colony, fx): ...

    The rules tier arrives in a later step of the SDK: nothing calls hooks yet."""
    if hook not in HOOKS:
        raise ValueError("no hook " + repr(hook) + "; the hooks are " + ", ".join(HOOKS))

    def register(fn: Callable[..., Any]) -> Callable[..., Any]:
        _handlers.setdefault(hook, []).append(fn)
        return fn
    return register


def handlers(hook: str) -> List[Callable[..., Any]]:
    """The functions registered for a hook, in the order they were registered."""
    if hook not in HOOKS:
        raise ValueError("no hook " + repr(hook))
    return list(_handlers.get(hook, ()))


def clear() -> None:
    """Forgets every registered hook (for tests)."""
    _handlers.clear()


class Effects(ABC):
    """`fx`: how a rules hook changes the game. Every change goes through the engine,
    which keeps the game valid and refuses fractions (docs/MODDING_SDK.md, section 7.2).

    The interface of a later step of the SDK: the engine provides the implementation.
    (CPython refuses to make an Effects that leaves a method out; the game's runtime
    does not check, and the method left out raises NotImplementedError.)"""

    # Resources and population
    @abstractmethod
    def add_resources(self, empire: Any, minerals: int = 0, organics: int = 0, radioactives: int = 0) -> None:
        """Adds to (or, with negative amounts, takes from) an empire's treasury."""
        raise NotImplementedError("fx.add_resources arrives with the rules tier")

    @abstractmethod
    def change_population(self, colony: Any, race: Any, millions: int) -> None:
        """Adds people of a race to a colony (negative: removes them)."""
        raise NotImplementedError("fx.change_population arrives with the rules tier")

    @abstractmethod
    def change_happiness(self, colony: Any, change: int) -> None:
        """Changes a colony's mood (positive: happier)."""
        raise NotImplementedError("fx.change_happiness arrives with the rules tier")

    # Damage, repair and supply
    @abstractmethod
    def damage(self, vehicle: Any, amount: int) -> None:
        """Damages a vehicle."""
        raise NotImplementedError("fx.damage arrives with the rules tier")

    @abstractmethod
    def repair(self, vehicle: Any, amount: int) -> None:
        """Repairs a vehicle."""
        raise NotImplementedError("fx.repair arrives with the rules tier")

    @abstractmethod
    def change_supply(self, vehicle: Any, amount: int) -> None:
        """Adds supply to a vehicle (negative: uses it)."""
        raise NotImplementedError("fx.change_supply arrives with the rules tier")

    # Vehicles and facilities
    @abstractmethod
    def create_vehicle(self, empire: Any, design: Any, location: Any, count: int = 1) -> Any:
        """Creates vehicles of a design; returns the new vehicle's id."""
        raise NotImplementedError("fx.create_vehicle arrives with the rules tier")

    @abstractmethod
    def remove_vehicle(self, vehicle: Any) -> None:
        """Removes a vehicle from the game."""
        raise NotImplementedError("fx.remove_vehicle arrives with the rules tier")

    @abstractmethod
    def add_facility(self, colony: Any, facility: Any) -> None:
        """Adds a facility to a colony."""
        raise NotImplementedError("fx.add_facility arrives with the rules tier")

    @abstractmethod
    def remove_facility(self, colony: Any, slot: int) -> None:
        """Removes the facility at a position of a colony's list."""
        raise NotImplementedError("fx.remove_facility arrives with the rules tier")

    # Treaties, the log and events
    @abstractmethod
    def set_treaty(self, empire: Any, other: Any, treaty: str) -> None:
        """Sets the treaty between two empires."""
        raise NotImplementedError("fx.set_treaty arrives with the rules tier")

    @abstractmethod
    def log(self, empire: Any, text: str, title: str = "", category: str = "misc") -> None:
        """Adds an entry to an empire's log."""
        raise NotImplementedError("fx.log arrives with the rules tier")

    @abstractmethod
    def fire_event(self, name: str, empire: Any = None, target: Any = None) -> None:
        """Fires an event."""
        raise NotImplementedError("fx.fire_event arrives with the rules tier")
